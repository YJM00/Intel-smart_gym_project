/*
  Nano33_Gateway_L.ino — v2.6.1-L (IMU ON)
  ------------------------------------------------------------
  - BLE 연결/해제 시마다 세션 리셋
  - EMG 500 Hz, 100 ms마다 RAW 'E' 패킷 전송
  - IMU 10 Hz, 'I' 패킷 전송 (왼쪽만)
  - 광고/디바이스 이름: NANO33_L
  ------------------------------------------------------------
*/

#include <Arduino.h>
#include <ArduinoBLE.h>
#include <Arduino_LSM6DS3.h>
#include <string.h>
#include <math.h>

// ======================= 사용자 설정 =======================
#define EMG_PIN            A0
#define FS_HZ              500u
#define EMG_BATCH_N        50u

#define USE_IMU            1          // ← 왼쪽: IMU 사용
#define IMU_REPORT_HZ      10u

#define USE_HPF            1
#define HPF_ALPHA          0.995f
#define USE_ENV            0
#define ENV_ALPHA          0.10f

#define GYRO_BIAS_MS       3000u
#define USE_COMP_FILTER    1
#define COMP_TAU_S         0.60f

// ========= 판별/필터 파라미터 ============================
#define INVERT_DIR         1
#define MOVE_THR_DPS       10.0f
#define MIN_DESC_MS        120u
#define MIN_RISE_MS        120u
#define REP_WINDOW_N       3
#define VEL_LPF_CUTOFF_HZ  15.0f
#define COUNT_ON_EDGE      1

// 세션 리셋 시 자이로 바이어스/필터까지 초기화할지
#define RESET_GYRO_ON_SESSION 1

// ======================= 기본 상수 =========================
#define ADC_BITS           12
#define ADC_MID            (1 << (ADC_BITS-1))
#define RBUF_SIZE          1024

// BLE 이름(왼쪽)
static const char* BLE_LOCAL_NAME  = "NANO33_L";
static const char* BLE_DEVICE_NAME = "NANO33_L";

static const char* UUID_SVC = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
static const char* UUID_RX  = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";
static const char* UUID_TX  = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";

BLEService nusService(UUID_SVC);
BLECharacteristic rxChar(UUID_RX, BLEWriteWithoutResponse | BLEWrite, 128);
BLECharacteristic txChar(UUID_TX, BLENotify, 200);

// ======================= 패킷 정의 =========================
#pragma pack(push,1)
typedef struct {
  uint8_t  tag;  uint8_t seq;  uint32_t ts_ms;  uint16_t fs;  uint16_t n;
  int16_t  s[EMG_BATCH_N];
} pkt_emg_t;

typedef struct {
  uint8_t  tag;  uint32_t ts_ms;
  float    pitch_deg;  float pitch_vel_dps;
  int8_t   state;      uint16_t rep_id;
  uint16_t desc_ms;    uint16_t rise_ms;
  float    tempo_cv;
} pkt_imu_t;
#pragma pack(pop)

static_assert(sizeof(pkt_emg_t) == (1+1+4+2+2 + 2*EMG_BATCH_N), "pkt_emg_t size mismatch");

// ======================= 전역 상태 =========================
volatile int16_t ringBuf[RBUF_SIZE];
volatile uint16_t rHead=0, rTail=0;

static const uint32_t SAMPLE_DT_US = 1000000UL / FS_HZ;
uint32_t nextSampleUs = 0;

uint8_t  seqE = 0;
uint32_t lastImuMs = 0;

float emg_mean=0.f, env_val=0.f;

// IMU/필터
float gyroBiasY=0.f; bool gyroBiasLocked=false;
uint32_t biasStartMs=0, biasCount=0;
float pitch_est_deg=0.f; uint32_t lastImuUs=0;
float vel_f = 0.f; bool vel_f_init=false;

// 템포/상태
enum Phase { DESC=-1, HOLD=0, RISE=1 };
struct TempoState {
  Phase    phase = HOLD, last_phase = HOLD;
  uint32_t seg_t0 = 0;
  uint16_t desc_ms = 0, rise_ms = 0, rep_id = 0;
  float    recent_ms[REP_WINDOW_N]; uint8_t recent_n = 0;
  float    tempo_cv = 0.0f;
  float    pitch = 0.0f, pitch_vel = 0.0f;
} tempo;

// 연결상태 edge 감지
bool wasConnected = false;

// ======================= 유틸 함수 =========================
static inline bool ring_isEmpty() { return rHead==rTail; }
static inline void ring_push(int16_t v){
  uint16_t next = (uint16_t)(rHead+1) % RBUF_SIZE;
  if (next==rTail) { rTail = (uint16_t)(rTail+1) % RBUF_SIZE; }
  ringBuf[rHead]=v; rHead=next;
}
static inline bool ring_pop(int16_t& out){
  if (ring_isEmpty()) return false;
  out = ringBuf[rTail]; rTail=(uint16_t)(rTail+1)%RBUF_SIZE; return true;
}

static inline int16_t hpf_emg(int16_t s){
#if USE_HPF
  emg_mean = HPF_ALPHA*emg_mean + (1.f-HPF_ALPHA)*(float)s;
  float y = (float)s - emg_mean;
  if (y>32767.f) y=32767.f; else if (y<-32768.f) y=-32768.f;
  return (int16_t)y;
#else
  return s;
#endif
}
static inline void step_env_abs(int16_t x){
#if USE_ENV
  float a = (x>=0)? (float)x : (float)(-x);
  env_val = ENV_ALPHA*a + (1.f-ENV_ALPHA)*env_val;
#else
  (void)x;
#endif
}

// ====== 세션 리셋(중요) ======
void resetSession() {
  tempo.phase = HOLD; tempo.last_phase = HOLD;
  tempo.seg_t0 = 0;
  tempo.desc_ms = tempo.rise_ms = 0;
  tempo.rep_id = 0;
  tempo.recent_n = 0; tempo.tempo_cv = 0.0f;
  for (uint8_t i=0;i<REP_WINDOW_N;++i) tempo.recent_ms[i]=0.f;

  seqE = 0; lastImuMs = 0;

  rHead = rTail = 0;
  emg_mean = env_val = 0.f;

#if RESET_GYRO_ON_SESSION
  gyroBiasLocked=false; gyroBiasY=0.f; biasCount=0; biasStartMs=millis();
  pitch_est_deg=0.f; lastImuUs=0; vel_f=0.f; vel_f_init=false;
#endif

  nextSampleUs = micros();

  Serial.println("[SESSION] reset: rep_id -> 0");
}

// IMU + 보정필터 + 각속도 LPF
static inline void readIMU(float& pitchDeg, float& pitchVelDps){
#if USE_IMU
  float ax=0, ay=0, az=0, gx=0, gy=0, gz=0;
  bool gotA = IMU.accelerationAvailable(); if (gotA) IMU.readAcceleration(ax,ay,az);
  bool gotG = IMU.gyroscopeAvailable();    if (gotG) IMU.readGyroscope(gx,gy,gz);

  uint32_t nowMs = millis();
  if (!gyroBiasLocked && gotG){
    if (biasCount==0) biasStartMs=nowMs;
    gyroBiasY += gy; biasCount++;
    if ((nowMs - biasStartMs) >= GYRO_BIAS_MS){
      gyroBiasY /= (float)biasCount;
      gyroBiasLocked = true;
    }
  }
  float gy_corr = gotG ? (gy - (gyroBiasLocked?gyroBiasY:0.f)) : 0.f;

  float accPitchDeg = 0.f;
  if (gotA){
    float denom = sqrtf(ay*ay + az*az) + 1e-6f;
    accPitchDeg = atan2f(-ax, denom) * 57.2957795f;
  }

#if USE_COMP_FILTER
  uint32_t nowUs = micros();
  float dt = (lastImuUs==0)? 0.01f : (nowUs - lastImuUs) * 1e-6f;
  lastImuUs = nowUs;
  float dt_eff = (dt<1e-3f)? 1e-3f : dt;
  float alpha = COMP_TAU_S / (COMP_TAU_S + dt_eff);
  pitch_est_deg = alpha * (pitch_est_deg + gy_corr*dt) + (1.f-alpha) * accPitchDeg;
  pitchDeg = pitch_est_deg;
#else
  pitchDeg = accPitchDeg;
#endif

  float vel_raw = INVERT_DIR ? -gy_corr : gy_corr;

  float fc = VEL_LPF_CUTOFF_HZ;
  float dt_for_lpf = (lastImuUs==0)? 0.01f : ((micros() - lastImuUs) * 1e-6f);
  if (dt_for_lpf < 1e-4f) dt_for_lpf = 1e-4f;
  float tau = 1.0f / (6.2831853f * fc);
  float a = tau / (tau + dt_for_lpf);
  if (!vel_f_init){ vel_f = vel_raw; vel_f_init = true; }
  else            { vel_f = a * vel_f + (1.f - a) * vel_raw; }

  pitchVelDps = vel_f;
#else
  pitchDeg=0.f; pitchVelDps=0.f;
#endif
}

// ===== rep 확정 =====
static inline void commit_rep_immediately() {
  uint32_t rep_ms = (uint32_t)tempo.desc_ms + (uint32_t)tempo.rise_ms;
  if (rep_ms > 50) {
    if (tempo.recent_n < REP_WINDOW_N) tempo.recent_ms[tempo.recent_n++] = (float)rep_ms;
    else { for (uint8_t i=1;i<REP_WINDOW_N;++i) tempo.recent_ms[i-1]=tempo.recent_ms[i];
           tempo.recent_ms[REP_WINDOW_N-1] = (float)rep_ms; }
    if (tempo.recent_n>=2){
      float sum=0, sum2=0; uint8_t n=tempo.recent_n;
      for (uint8_t i=0;i<n;++i){ float v=tempo.recent_ms[i]; sum+=v; sum2+=v*v; }
      float mean = sum/n; float var = (sum2/n) - mean*mean; if (var<0) var=0;
      float sd = sqrtf(var);
      tempo.tempo_cv = (mean>1e-6f)? (sd/mean) : 0.f;
    } else tempo.tempo_cv = 0.f;

    tempo.rep_id++;
  }
}

// 템포/상태 (단일 임계 + 엣지 트리거)
static inline void tempo_update(uint32_t ts_ms, float pitch_deg, float pitch_vel_dps){
  tempo.pitch = pitch_deg; tempo.pitch_vel = pitch_vel_dps;

  Phase phase_now;
  if      (pitch_vel_dps <= -MOVE_THR_DPS) phase_now = DESC;
  else if (pitch_vel_dps >=  MOVE_THR_DPS) phase_now = RISE;
  else                                     phase_now = HOLD;

  if (tempo.seg_t0 == 0){
    tempo.seg_t0 = ts_ms;
    tempo.phase = tempo.last_phase = phase_now;
    tempo.desc_ms = 0; tempo.rise_ms = 0;
    return;
  }

  if (phase_now != tempo.phase){
    uint32_t now = ts_ms;
    uint32_t seg_ms = (now>tempo.seg_t0)? (now - tempo.seg_t0) : 0;

    if (tempo.phase == DESC) tempo.desc_ms = (seg_ms >= MIN_DESC_MS) ? (uint16_t)min(seg_ms,(uint32_t)65535) : 0;
    if (tempo.phase == RISE) tempo.rise_ms = (seg_ms >= MIN_RISE_MS) ? (uint16_t)min(seg_ms,(uint32_t)65535) : 0;

    if (phase_now == HOLD) {
      if (COUNT_ON_EDGE && tempo.phase == RISE) commit_rep_immediately();
    } else if (phase_now == DESC) {
      tempo.desc_ms = 0; tempo.rise_ms = 0;
    } else { // RISE
      tempo.rise_ms = 0;
    }

    tempo.last_phase = tempo.phase;
    tempo.phase = phase_now;
    tempo.seg_t0 = now;
  }
}

// ======================= BLE 수명주기 ======================
void setupBLE(){
  if (!BLE.begin()) { while(1){ delay(1000);} }
  BLE.setLocalName(BLE_LOCAL_NAME);
  BLE.setDeviceName(BLE_DEVICE_NAME);
  BLE.setAdvertisedService(nusService);
  nusService.addCharacteristic(txChar);
  nusService.addCharacteristic(rxChar);
  BLE.addService(nusService);
  uint8_t dummy[1]={0}; txChar.setValue(dummy,1);
  BLE.advertise();
}

// ======================= 셋업 ==============================
void setup(){
  Serial.begin(115200);
  //while(!Serial){ ; }

  analogReadResolution(ADC_BITS);
  pinMode(EMG_PIN, INPUT);

#if USE_IMU
  IMU.begin();
  gyroBiasLocked=false; gyroBiasY=0.f; biasCount=0; biasStartMs=millis();
  pitch_est_deg=0.f; lastImuUs=0; vel_f=0.f; vel_f_init=false;
#endif

  setupBLE();
  nextSampleUs = micros();

  Serial.println("[BOOT] v2.6.1-L");
  Serial.print("[CFG] INVERT_DIR="); Serial.println(INVERT_DIR);
  Serial.print("[CFG] MOVE_THR_DPS="); Serial.println(MOVE_THR_DPS);
  Serial.print("[CFG] VEL_LPF_CUTOFF_HZ="); Serial.println(VEL_LPF_CUTOFF_HZ);
  Serial.print("[CFG] COUNT_ON_EDGE="); Serial.println(COUNT_ON_EDGE);
}

// ======================= 메인 루프 =========================
void loop(){
  bool isConnected = BLE.connected();
  static bool wasConnected = false;

  if (isConnected && !wasConnected) {
    resetSession();
    wasConnected = true;
    Serial.println("[BLE] connected → session reset");
  } else if (!isConnected && wasConnected) {
    resetSession();
    BLE.advertise();
    wasConnected = false;
    Serial.println("[BLE] disconnected → session reset & advertise");
  }

  // 1) 500 Hz 샘플링
  uint32_t nowUs = micros();
  if ((int32_t)(nowUs - nextSampleUs) >= 0){
    nextSampleUs += SAMPLE_DT_US;

    int raw = analogRead(EMG_PIN);
    int centered = raw - ADC_MID;
    int16_t s = (int16_t)(((int32_t)centered) << 4);

    int16_t s_proc = hpf_emg(s);
    step_env_abs(s_proc);

    ring_push(s);
  }

#if USE_IMU
  // 1.5) IMU 갱신 및 템포 업데이트
  bool any = IMU.accelerationAvailable() || IMU.gyroscopeAvailable();
  if (any){
    float pitch, vel; readIMU(pitch, vel);
    tempo_update(millis(), pitch, vel);
  }
#endif

  // 2) EMG 패킷 전송(100ms)
  static int16_t batch[EMG_BATCH_N]; static uint16_t have=0;
  while (have < EMG_BATCH_N) { int16_t v; if(!ring_pop(v)) break; batch[have++]=v; }
  if (have >= EMG_BATCH_N){
    have=0;
    pkt_emg_t pkt; pkt.tag='E'; pkt.seq=seqE++; pkt.ts_ms=millis(); pkt.fs=FS_HZ; pkt.n=EMG_BATCH_N;
    memcpy(pkt.s, batch, sizeof(int16_t)*EMG_BATCH_N);
    if (isConnected) txChar.writeValue((uint8_t*)&pkt, sizeof(pkt));
  }

  // 3) IMU 패킷 전송(10Hz)
#if USE_IMU
  if (IMU_REPORT_HZ>0 && isConnected){
    uint32_t nowMs = millis();
    if (nowMs - lastImuMs >= (1000u/IMU_REPORT_HZ)){
      lastImuMs = nowMs;

      pkt_imu_t ipkt;
      ipkt.tag='I'; ipkt.ts_ms=nowMs;
      ipkt.pitch_deg=tempo.pitch; ipkt.pitch_vel_dps=tempo.pitch_vel;
      ipkt.state=(int8_t)tempo.phase; ipkt.rep_id=tempo.rep_id;
      ipkt.desc_ms=tempo.desc_ms; ipkt.rise_ms=tempo.rise_ms; ipkt.tempo_cv=tempo.tempo_cv;

      txChar.writeValue((uint8_t*)&ipkt, sizeof(ipkt));
    }
  }
#endif

  BLE.poll();
}
