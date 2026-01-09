# 🏋️‍♂️ **AI Smart Gym Project**



## 📘 **프로젝트 개요 (Overview)**

**AI Smart Gym**은 포즈 랜드마크 기반 운동 분류 + 실시간 분석 앱이며,  
EMG·IMU 센서 융합 파워리프팅 스쿼트 분석까지 지원하는 파이썬 중심 프로젝트입니다.  

애플리케이션은 **Raspberry Pi**에서 구동되며,  
카메라·센서 스트림을 받아 운동을 인식하고 자세·가동범위(ROM)·템포·피로도·하체불균형 등 핵심 지표를 실시간으로 제공하고 피드백 해줍니다.

---

## 🎯 **프로젝트 목표**

- 운동 동작의 **정확한 분류 및 실시간 분석**  
- IMU, EMG 등 센서를 통한 **정량적 운동 데이터 수집**  
- AI 기반 **운동 수행 평가 알고리즘 및 피드백 제공**  
- **Raspberry Pi / Hailo-8 경량화 및 실시간 처리**


---

## ⚙️ **주요 기능**

<img width="925" height="372" alt="image" src="https://github.com/user-attachments/assets/efa01696-fd79-4d37-a17b-1f43dc46d913" />

### 🔹 운동 분류 모델 (AI)
- **TCN 기반 동작 분류 모델**
- 포즈 추정 + 센서 데이터 결합
- ONNX / HEF 기반 경량 추론 및 시각화

### 🔹 운동 분석 알고리즘
- 속도·가속도·파워 등 **운동 성능 지표 계산**
- 센서 기반 **운동 품질 평가 로직 설계**

### 🔹 센서 하드웨어 수집
- IMU, EMG 등 **실시간 수집**
- BLE/UART 통신, ADC,노이즈 필터링·캘리브레이션

### 🔹 통합 어플리케이션
- AI + 알고리즘 + 센서 모듈 통합
- **PySide6 UI 시각화 + BLE/Wi-Fi 데이터 연동**

---

## 🖥️ **시스템 구성도**

<img width="839" height="430" alt="image" src="https://github.com/user-attachments/assets/97e1d978-0dcd-4460-b81d-8fe5502806a3" />
<img width="861" height="485" alt="image" src="https://github.com/user-attachments/assets/eeaca896-b994-43b9-892b-fadff9362e0d" />


---

## 🧠 **기술 스택**

| 분야 | 기술 |
| --- | --- |
| **AI / ML** | PyTorch, ONNX, TCN |
| **임베디드** | Arduino, Raspberry Pi, Hailo-8 |
| **센서** | IMU, EMG |
| **프론트엔드 / 앱** | PySide6, BLE 통신, Python |
| **기타** | YAML Config, CSV/JSON Logging, Autodesk Fusion 360 |

<p align="center">
  <img src="https://cdn.jsdelivr.net/gh/devicons/devicon/icons/python/python-original.svg" width="48" />
  <img src="https://cdn.jsdelivr.net/gh/devicons/devicon/icons/pytorch/pytorch-original.svg" width="48" />
  <img src="https://cdn.jsdelivr.net/gh/devicons/devicon/icons/raspberrypi/raspberrypi-original.svg" width="48" />
  <img src="https://cdn.jsdelivr.net/gh/devicons/devicon/icons/arduino/arduino-original.svg" width="48" />
  <img src="https://cdn.jsdelivr.net/gh/devicons/devicon/icons/opencv/opencv-original.svg" width="48" />
  <img src="https://cdn.jsdelivr.net/gh/devicons/devicon/icons/linux/linux-original.svg" width="48" />
  <img src="https://cdn.jsdelivr.net/npm/simple-icons@v11/icons/qt.svg" width="48" title="PySide6 (Qt for Python)"/>
  <img src="https://cdn.jsdelivr.net/npm/simple-icons@v11/icons/onnx.svg" width="48" title="ONNX Runtime"/>
  <img src="https://cdn.jsdelivr.net/npm/simple-icons@v11/icons/autodesk.svg" width="48" title="Autodesk Fusion 360"/>
  <a href="https://hailo.ai/" title="Hailo">
    <img src="https://img.shields.io/badge/Hailo-000?style=for-the-badge" height="24"/>
  </a>
</p>

---

## 🎬 **시연 예시**


https://github.com/user-attachments/assets/f4ce4fb4-64c6-46c4-88ab-7b38399b903d


---

## 🧩 **운동 분류 시스템**

운동 분류 시스템은 **포즈 랜드마크 추출 → 시퀀스 분류 → (상세 분석) → (후속 처리)** 순서로 동작합니다.


### **1. 영상 입력·전처리 (Raspberry Pi 5 + Hailo-8)**

- 프레임 리사이즈  
- 색공간 변환  

### **2. 포즈 키포인트 추출**

- **YOLOv8s-Pose @ Hailo-8**  
- 매 프레임 키포인트 추출  
- 키포인트 정규화: 스케일·중심  

### **3. 시퀀스 버퍼링**

- 윈도우: **60 프레임**  
- 실시간 **stride = 1**  
- 슬라이딩 업데이트  

### **4. 운동 분류 (ONNX TCN)**

- 입력: 정규화 키포인트 시퀀스 (60 프레임)  
- 분류 클래스: **idle / shoulder_press / 덤벨로우 / 점핑잭 / 스쿼트 / 푸쉬업 / 레그레이즈 / 버피 / 사이드 레터럴 레이즈**  
- (옵션) 히스테리시스·스무딩  

#### **후속 처리 (운동 채점)**

- 각도 기반 공통 채점: 각 운동 자세 채점에 필요한 관절만 사용  
- 가중치·진행률: 관절별 가중치, 목표 각도 범위 대비 progress(0~1) 계산  
- 점수 산출: 한 동작의 인식 가동 범위 내에서 최고 혹은 최저 각으로 산출  

---
## 🧠 근전도·IMU 기반 운동 상세 분석 시스템

운동 분석 시스템은 **센서 데이터 수집 → 전처리 → 전송 → 신호 분석 → AI 추론** 단계로 동작합니다.  
본 시스템은 **좌·우 허벅지의 근전도(EMG)** 와 **IMU 센서 데이터**를 융합하여  
운동 중 **피로도(Fatigue Index, FI)** 와 **불균형도(Balance Index, BI)**,  
그리고 **템포 일관성(Tempo Consistency)** 을 분석합니다.


### 1. 데이터 수집
- **Arduino Nano 33 IoT (2대)** 사용  
- 좌/우 허벅지 근전도(EMG) 센서 각 1개 연결  
- IMU(가속도계/자이로)로 하강·상승 동작 구분 및 템포 계산  
- 샘플링 속도: EMG 500Hz, IMU 10Hz


### 2. 전처리
- Arduino 단에서 **EMG 필터링 및 정규화(DC offset 제거)**  
- IMU의 기울기값을 이용한 **하강/상승 구간 분리하고 yaw값을 이용해 각 랩당 운동 템포 추출**  
- 3초간 MVC(Maximum Voluntary Contraction) 측정을 통한 **근수축 기준 정규화**


### 3. 데이터 전송
- BLE(Bluetooth Low Energy)를 이용해 라즈베리파이로 전송  
- 좌측(NANO33_L), 우측(NANO33_R) 장치로 구분  
- 전송 데이터:  
  - `EMG_preprocessed`, `IMU_pitch`, `tempo`, `rep_id`


### 4. 신호 분석
<img width="871" height="497" alt="image" src="https://github.com/user-attachments/assets/8ab70e79-a1ab-404b-bbba-56d50bbcb4b7" />


- Raspberry Pi에서 실시간 분석 수행  
- FFT 기반 주파수 도메인 특징 및 시간 도메인 특징 추출  

| 특징값 | 의미 | 피로 시 변화 |
|:---|:---|:---|
| **RMS_norm** | 근육 수축 세기(정규화 RMS) | ⬇️ 감소 |
| **MDF** | 스펙트럼 중심 주파수 | ⬇️ 저주파 쪽 이동 |
| **SampEn** | 신호의 불규칙성(복잡도) | ⬇️ 더 규칙적 |
| **MSESEn** | 주파수 분포의 무질서 | ⬇️ 장·단기 패턴 단순화 |
| **iEMG_norm** | EMG 적분값(활동량) | ⬇️ 활성 근섬유 감소 |
| **tempo_cv** | 스쿼트 템포의 변동성(속도 일관성 지표) | ⬆️ 증가 시 불균일한 리듬 |


### 5. AI 추론
#### 🧩 멀티태스크 AI 모델 (Multi-Task MLP)
- 입력: `[RMS_norm, MDF, SampEn, MSESEn, iEMG_norm, tempo_cv]`
- 출력:
  - **FI_pred**: 피로도 (Fatigue Index, 0~1)
  - **BI_pred**: 불균형도 (Balance Index, 0~1)
- 구조:
  - Dense(32) → ReLU → Dense(16) → 공유층  
  - Head1(FI): Dense(8) → Sigmoid  
  - Head2(BI): Dense(8) → Sigmoid  
- 손실 함수:  
  `Loss = α·MSE(FI_pred, FI_label) + β·MSE(BI_pred, BI_label)`

#### ⚖️ 보조 계산 지표
| 항목 | 계산식 | 의미 |
|:---|:---|:---|
| **AIF** | \|FI_L - FI_R\| | 피로 누적 불균형 |
| **AI_RMS** | \|RMS_L - RMS_R\| | 좌·우 근수축 세기 차이 |
| **AI_iEMG** | \|iEMG_L - iEMG_R\| | 좌·우 근육 활성 차이 |
| **BI (최종)** | 0.4×AI_RMS + 0.4×AI_iEMG + 0.2×AIF | 종합 불균형 점수 |


### 6. 출력 및 시각화
- **FI, BI, tempo_cv** 값을 실시간으로 시각화  
- PyQt 대시보드에서 게이지/그래프 형태로 표시  
- 결과는 `.tsv` 형식(`window_features.tsv`, `reps_pred_dual.tsv`)으로 자동 저장

### 7. 웨어러블 케이스 모델링
<img width="817" height="688" alt="image" src="https://github.com/user-attachments/assets/db870a0e-abe2-4cee-9077-f69e6831b411" />


### 🧠 요약
- **FI (Fatigue Index)** → 근육 피로 누적 정도  
- **BI (Balance Index)** → 좌우 근육 사용의 불균형 정도  
- **tempo_cv** → 스쿼트 속도의 일관성 (리듬 안정성)
- 세 지표를 통해 운동자의 **피로도, 균형, 리듬**을 동시에 평가합니다.



### **기대 효과**

- 운동 수행 정확도 향상 및 부상 예방  
- 개인 맞춤형 피드백을 통한 훈련 효율 극대화  
- AI + 센서 융합을 통한 스마트 피트니스 솔루션 실현  
- 실시간 채점 및 시각화로 재미 향상  

---

## 🧩 **Clone Code**
git clone https://github.com/Biomedical-Signal-Processing-Lab/smart_gym_project.git


## ⚙️ **Steps to Build**

```
# 0) 기본 설정
sudo apt update
sudo apt install -y git curl wget build-essential pkg-config
python -m venv .sgym_venv
source .sgym_venv/bin/activate
cd smart_gym_project/app
pip install -r requirements.txt

# 1) Hailo (공식 APT 레포 추가 후 설치)
# ⚠️ 반드시 벤더 문서 절차에 따라 레포를 먼저 등록해야 합니다.
sudo apt install -y hailo-all

# 2) GStreamer 런타임 + 플러그인 묶음
sudo apt install -y \
  gstreamer1.0-tools gstreamer1.0-plugins-base gstreamer1.0-plugins-good \
  gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-libav \
  gstreamer1.0-gl gstreamer1.0-alsa

# 3) GI(PyGObject) 바인딩 (Python에서 GStreamer를 사용하는 경우)
sudo apt install -y \
  python3-gi python3-gi-cairo gobject-introspection \
  gir1.2-gstreamer-1.0 gir1.2-gst-plugins-base-1.0 libgirepository1.0-dev

# 4) 카메라 유틸리티 설치
sudo apt install -y v4l-utils libcamera-apps

```
## ▶️ **Step to Run**
```
# 1) 가상환경 활성화
source .sgym_venv/bin/activate

# 2) 프로젝트 실행
python main.py


---

> 💡 **Tip:**  
> 첫 실행 시 `.venv` 환경을 다시 활성화해야 합니다:  
> ```bash
> source .sgym_venv/bin/activate
> 
> 실행 후 UI 창이 뜨면, 센서 연결 상태와 카메라 입력이 정상 동작하는지 로그를 확인하세요.




```

---

## ⚙️ **트러블슈팅 및 배운점**

| **No.** | **문제 상황 (Troubleshooting)** | **해결 및 개선 과정 (Solution)** | **배운점 (Lessons Learned)** |
| :---: | --- | --- | --- |
| **1** | 좌우 하체에 부착된 **두 아두이노(EMG 센서)** 간 데이터 동기화가 어려움.<br>BLE 통신으로 전송된 근전도 신호가 실시간으로 약간의 지연(레이지) 차이를 보였고,<br>랩(rep) 단위로 데이터를 정확히 구분해 분석해야 했음. | BLE 전송 주기와 아두이노의 타임스탬프를 기준으로<br>**랩 단위 버퍼링 및 타임라인 정렬 알고리즘**을 구현.<br>각 랩 시작/종료 신호를 IMU 기반 이벤트로 감지해 동기화精度를 높임. | 센서 2대 이상을 사용할 경우 **시간 기준 동기화(타임스탬프 기반)** 가 핵심임을 깨달음.<br>BLE의 비동기성에 대비해 **랩 단위 버퍼 구조 설계**가 필요함. |
| **2** | **Arduino Nano 33 IoT의 연산속도와 BLE 전송속도 한계**로 인해,<br>EMG 특징값(RMS, MDF, SampEn 등)을 어디까지 아두이노에서 계산하고<br>어디서부터 Raspberry Pi로 넘길지 결정이 어려웠음.<br>파이에서는 3개 이상의 AI 모델이 동시에 구동되어 CPU 부하도 컸음. | **계산 분담 구조 설계**:  
  - 아두이노: 신호 필터링, RMS 등 단순 통계  
  - 라즈베리파이: FFT, SampEn, MSESEn 등 고비용 분석  
  이렇게 역할을 분리하고 BLE 패킷 크기 최적화. | 임베디드 환경에서는 **연산 분산 설계(Edge–Server 분리)** 가 필수.<br>“모든 연산을 한쪽에 몰지 말고” **프로세서 역할 분담**이 시스템 안정성을 높임. |
| **3** | **근전도 센서 배송 지연**으로 사용자별 데이터셋 확보가 어려워<br>AI 모델 훈련에 충분한 raw 데이터 확보가 불가능했음. | 임시로 **특징 기반 머신러닝 하이브리드 방식** 적용:  
  - 센서 신호 → 특징 추출(RMS, MDF 등) → MLP 입력  
  향후 개선안으로 CNN + LSTM + Attention 구조 계획:  
  - CNN: 센서 시계열의 로컬 패턴 자동추출  
  - LSTM: 피로 누적 시계열 패턴 학습  
  - Attention: 주요 구간 강조 | 데이터 수집이 제한적일 땐 **엔지니어링 기반 특징추출 + 간단한 MLP** 로 빠른 검증이 유효.<br>추후 데이터가 쌓이면 **엔드투엔드 CNN-LSTM 모델**로 확장 가능. |

### 💡 **종합 교훈**

- **하드웨어 제약(BLE, 연산속도)** 을 먼저 파악하고 시스템 설계를 해야 한다.  
- 센서가 여러 개일수록 **타이밍·동기화 관리**가 프로젝트의 성패를 좌우한다.  
- 데이터가 부족한 상황에서는 **특징기반 접근(Feature Engineering)** 이 실용적이며,  
  장기적으로는 **딥러닝 전이(Feature Learning)** 으로 확장할 수 있다.


---


## 👥 **Team: 자세어때**

| 이름 | 역할 | 주요 담당 |
| --- | --- | --- |
| **서민솔** | 팀장 | 프로젝트 총괄, 운동 분류 모델 설계 |
| **이동현** | 부팀장 | 통합 어플리케이션 개발 |
| **유종민** | 센서 | 센서 신호처리 AI 개발, 3D 모델링 |
| **윤찬민** | AI 개발 | 운동 분류 모델 구현 |
| **임정민** | 운동 분석 | 운동 분석 알고리즘 개발,Yocto 개발 |


---

## 📎 **Appendix**

[피로도 분석 논문1.pdf](https://github.com/user-attachments/files/23041197/default.pdf)
[피로도 분석 논문2.pdf](https://github.com/user-attachments/files/23042535/default.pdf)
[피로도 분석 논문3.pdf](https://github.com/user-attachments/files/23042548/s41598-019-41860-4.pdf)


