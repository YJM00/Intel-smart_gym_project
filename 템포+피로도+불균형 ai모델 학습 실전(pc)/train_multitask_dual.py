#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
train_multitask_dual.py — 좌/우 EMG 윈도 기반 피로도(FI) + 불균형(BI) 멀티태스크 학습
입력: window_features_L.tsv + window_features_R.tsv (frame_id 기준 매칭)
출력: models/mt_fi_balance.pt, mt_scaler.joblib, mt_feature_order.json
"""

import os, json, joblib
import numpy as np
import pandas as pd
import torch, torch.nn as nn
from sklearn.preprocessing import StandardScaler
from sklearn.model_selection import GroupKFold

os.makedirs("models", exist_ok=True)

# ────────────────────────────────────────────────
# 1️⃣ 데이터 경로
LEFT_PATH  = "data/logs/window_features_L.tsv"
RIGHT_PATH = "data/logs/window_features_R.tsv"
OUT_MODEL  = "models/mt_fi_balance.pt"
OUT_SCALER = "models/mt_scaler.joblib"
OUT_META   = "models/mt_feature_order.json"

# ────────────────────────────────────────────────
# 2️⃣ 입력 피처 선택
BASIC_FEATS = ["rms_norm","diemg","dmdf","dsampen","dmsesen"]

# ────────────────────────────────────────────────
# 3️⃣ 유틸 함수
def load_pair():
    """좌/우 윈도우 파일 로드 및 frame_id 기준으로 조인"""
    l = pd.read_csv(LEFT_PATH, sep="\t")
    r = pd.read_csv(RIGHT_PATH, sep="\t")
    if "frame_id" not in l.columns: l["frame_id"] = np.arange(len(l))
    if "frame_id" not in r.columns: r["frame_id"] = np.arange(len(r))
    df = pd.merge(l, r, on="frame_id", suffixes=("_L","_R"))

    # 필요한 컬럼만 추출
    use_cols = []
    for f in BASIC_FEATS:
        use_cols += [f"{f}_L", f"{f}_R"]

    # 비대칭 피처 생성
    for f in BASIC_FEATS:
        df[f"diff_{f}"] = df[f"{f}_L"] - df[f"{f}_R"]
        df[f"ratio_{f}"] = (df[f"{f}_L"] + 1e-6) / (df[f"{f}_R"] + 1e-6)

    if "tempo_cv" in l.columns:
        df["tempo_cv"] = l["tempo_cv"]
    else:
        df["tempo_cv"] = 0.0

    # 타깃 (FI_L, FI_R, BI)
    df["FI_L"] = df.get("FIh_ema_L", df.get("FI_L", 0))
    df["FI_R"] = df.get("FIh_ema_R", df.get("FI_R", 0))
    df["AIF"]  = np.abs(df["FI_L"] - df["FI_R"])
    df["AI_RMS"]  = np.abs(df["rms_norm_L"] - df["rms_norm_R"])
    df["AI_iEMG"] = np.abs(df["diemg_L"] - df["diemg_R"])
    df["BI"] = 0.4*df["AI_RMS"] + 0.4*df["AI_iEMG"] + 0.2*df["AIF"]

    return df

# ────────────────────────────────────────────────
# 4️⃣ 모델 구조
class MTHead(nn.Module):
    """Dual Input MLP — 피로도(FI_L, FI_R) + 불균형(BI) 동시 예측"""
    def __init__(self, in_dim, hidden=64):
        super().__init__()
        self.shared = nn.Sequential(
            nn.Linear(in_dim, hidden), nn.ReLU(),
            nn.Linear(hidden, hidden), nn.ReLU(),
        )
        self.fi_head = nn.Linear(hidden, 2)  # [FI_L, FI_R]
        self.bi_head = nn.Linear(hidden, 1)  # [BI]

    def forward(self, x):
        h = self.shared(x)
        fi = torch.sigmoid(self.fi_head(h))
        bi = torch.sigmoid(self.bi_head(h))
        return fi, bi

# ────────────────────────────────────────────────
# 5️⃣ 학습 함수
def main():
    torch.set_default_dtype(torch.float32)

    df = load_pair()
    user_id = df.get("user_id_L", "user01")
    groups = pd.Series(user_id)

    # 입력 구성
    feat_cols = []
    for f in BASIC_FEATS:
        feat_cols += [f"{f}_L", f"{f}_R", f"diff_{f}", f"ratio_{f}"]
    feat_cols.append("tempo_cv")

    X = df[feat_cols].astype(np.float32).values
    Y_fi = df[["FI_L","FI_R"]].clip(0,1).astype(np.float32).values
    Y_bi = df[["BI"]].clip(0,1).astype(np.float32).values

    scaler = StandardScaler()
    Xs = scaler.fit_transform(X).astype(np.float32)
    joblib.dump(scaler, OUT_SCALER)
    print(f"[OK] scaler → {OUT_SCALER}")

    model = MTHead(Xs.shape[1], hidden=64).float()
    opt = torch.optim.Adam(model.parameters(), lr=1e-3)
    mae = nn.L1Loss()

    X_t = torch.tensor(Xs, dtype=torch.float32)
    y_f = torch.tensor(Y_fi, dtype=torch.float32)
    y_b = torch.tensor(Y_bi, dtype=torch.float32)

    for ep in range(200):
        model.train(); opt.zero_grad()
        fi, bi = model(X_t)
        loss_fi = mae(fi, y_f)
        loss_bi = mae(bi, y_b)
        loss = 0.7*loss_fi + 0.3*loss_bi
        loss.backward(); opt.step()
        if (ep+1) % 40 == 0:
            print(f"[train] ep={ep+1:03d} loss={loss.item():.4f} fi={loss_fi.item():.4f} bi={loss_bi.item():.4f}")

    torch.save(model.state_dict(), OUT_MODEL)
    print(f"[OK] model saved → {OUT_MODEL}")

    # 메타정보 저장
    meta = {
        "input_features": feat_cols,
        "target_labels": ["FI_L","FI_R","BI"],
        "scaler": OUT_SCALER,
        "model_path": OUT_MODEL
    }
    with open(OUT_META, "w", encoding="utf-8") as f:
        json.dump(meta, f, ensure_ascii=False, indent=2)
    print(f"[OK] feature meta → {OUT_META}")

    # ─ 평가 (GroupKFold)
    gkf = GroupKFold(n_splits=min(5, len(np.unique(groups))))
    maes_f, maes_b = [], []
    for i, (tr, te) in enumerate(gkf.split(Xs, Y_bi, groups)):
        m = MTHead(Xs.shape[1]).float()
        opt = torch.optim.Adam(m.parameters(), lr=1e-3)
        Xt, Yf, Yb = map(torch.tensor, (Xs[tr], Y_fi[tr], Y_bi[tr]))
        Xv, Vf, Vb = map(torch.tensor, (Xs[te], Y_fi[te], Y_bi[te]))
        for _ in range(80):
            m.train(); opt.zero_grad()
            pf, pb = m(Xt.float())
            lf, lb = mae(pf, Yf.float()), mae(pb, Yb.float())
            l = 0.7*lf + 0.3*lb
            l.backward(); opt.step()
        m.eval()
        with torch.no_grad():
            pfv, pbv = m(Xv.float())
            maes_f.append(mae(pfv, Vf.float()).item())
            maes_b.append(mae(pbv, Vb.float()).item())
    print(f"[Eval] FI MAE={np.mean(maes_f):.4f}, BI MAE={np.mean(maes_b):.4f}")

if __name__ == "__main__":
    main()
