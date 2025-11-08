/*
  Nano33_Gateway_R.ino — v2.6.1-R (IMU OFF)
  ------------------------------------------------------------
  - BLE 연결/해제 시마다 세션 리셋
  - EMG 500 Hz, 100 ms마다 RAW 'E' 패킷 전송
  - IMU 패킷 전송 없음(오른쪽은 IMU 비활성)
  - 광고/디바이스 이름: NANO33_R
  ------------------------------------------------------------
*/

#include <Arduino.h>
#include <ArduinoBLE.h>
#include <Arduino_LSM6DS3.h>   // 포함해도 USE_IMU=0이면 사용 안 함
#include <string.h>
#include <math.h>

// ======================= 사용자 설정 =======================
#define EMG_PIN            A0
#define FS_HZ              500u
#define EMG_BATCH_N        50u

#define USE_IMU            0          // ← 오른쪽: IMU 사용 안 함
#define IMU_REPORT_HZ      10u        // (미사용)

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

// BLE 이름(오른쪽)
static const char* BLE_LOCAL_NAME  = "NANO33_R";
static const char* BLE_DEVICE_NAME = "NANO33_R";

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

// IMU/필터 (IMU 미사용이지만 빌드 오류 방지용 변수는 둠)
float gyroBiasY=0.f; bool gyroBiasLocked=false;
uint32_t biasStartMs=0, biasCount=0;
float pitch_est_deg=0.f; uint32_t lastImuUs=0;
float vel_f = 0.f; bool vel_f_init=false;

// 템포/상태 (오른쪽은 사용하지 않지만 구조 유지)
enum Phase { DESC=-1, HOLD=0, RISE=1 };
struct TempoState {
  Phase    phase = HOLD, last_phase = HOLD;
  uint32_t seg_t0 = 0;
  uint16_t desc_ms = 0, rise_ms = 0, rep_id = 0;
  float    recent_ms[REP_WINDOW_N]; uint8_t recent_n = 0;
  float    tempo_cv = 0.0f;
  float    pitch = 0.0f, pitch_vel = 0.0f;
} tempo;

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

// ====== 세션 리셋 ======
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

// (IMU 미사용: 더미 구현)
static inline void readIMU(float& pitchDeg, float& pitchVelDps){
#if USE_IMU
  // 왼쪽 코드와 동일하지만, 오른쪽은 USE_IMU=0이므로 실행되지 않음
#else
  pitchDeg = 0.f; pitchVelDps = 0.f;
#endif
}

// ======================= BLE ======================
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

// ======================= 셋업 ======================
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

  Serial.println("[BOOT] v2.6.1-R");
  Serial.print("[CFG] USE_IMU="); Serial.println(USE_IMU);
  Serial.print("[CFG] MOVE_THR_DPS="); Serial.println(MOVE_THR_DPS);
  Serial.print("[CFG] COUNT_ON_EDGE="); Serial.println(COUNT_ON_EDGE);
}

// ======================= 메인 루프 =================
void loop(){
  bool isConnected = BLE.connected();
  static bool wasConnectedLocal = false;

  if (isConnected && !wasConnectedLocal) {
    resetSession();
    wasConnectedLocal = true;
    Serial.println("[BLE] connected → session reset");
  } else if (!isConnected && wasConnectedLocal) {
    resetSession();
    BLE.advertise();
    wasConnectedLocal = false;
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

  // 2) EMG 패킷 전송(100ms)
  static int16_t batch[EMG_BATCH_N]; static uint16_t have=0;
  while (have < EMG_BATCH_N) { int16_t v; if(!ring_pop(v)) break; batch[have++]=v; }
  if (have >= EMG_BATCH_N){
    have=0;
    pkt_emg_t pkt; pkt.tag='E'; pkt.seq=seqE++; pkt.ts_ms=millis(); pkt.fs=FS_HZ; pkt.n=EMG_BATCH_N;
    memcpy(pkt.s, batch, sizeof(int16_t)*EMG_BATCH_N);
    if (isConnected) txChar.writeValue((uint8_t*)&pkt, sizeof(pkt));
  }

  // 3) (IMU 패킷 없음)
  BLE.poll();
}
