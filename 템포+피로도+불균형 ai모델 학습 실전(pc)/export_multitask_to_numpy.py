#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
export_multitask_to_numpy.py
- PyTorch 멀티태스크 모델(공유 백본 + [FI(2출력), BI(1출력)])을
  NumPy 가중치로 내보낸다.
- train_multitask_dual.py가 만든 state_dict 네이밍
  (shared.0, shared.2, fi_head, bi_head)을 기본 지원.
- 과거 스크립트(b1/b2 or backbone.*) 네이밍도 자동 탐색.
- 저장 키:
    Wb1, bb1, Wb2, bb2  # 백본 2층
    W_fi, b_fi          # FI 헤드 (출력 2차원: [FI_L, FI_R])
    W_bi, b_bi          # BI 헤드 (출력 1차원)
"""

import re
import numpy as np
import torch

PT_PATH = "models/mt_fi_balance.pt"
NPZ_PATH = "models/mt_model_numpy.npz"

def _find_first_two_backbone(sd):
    """
    다양한 네이밍을 지원하며,
    백본의 연속된 두 Linear layer 가중치(weight/bias)를 찾아 반환.
    반환: [(W1,b1),(W2,b2)]
    """
    # (name, idx, weight_key, bias_key)
    candidates = []

    # 패턴 1: shared.N.weight / shared.N.bias
    for k in sd.keys():
        m = re.match(r"^(shared)\.(\d+)\.weight$", k)
        if m:
            name, idx = m.group(1), int(m.group(2))
            bkey = f"{name}.{idx}.bias"
            if bkey in sd:
                candidates.append((f"{name}.{idx}", idx, k, bkey))

    # 패턴 2: backbone.N.weight
    for k in sd.keys():
        m = re.match(r"^(backbone)\.(\d+)\.weight$", k)
        if m:
            name, idx = m.group(1), int(m.group(2))
            bkey = f"{name}.{idx}.bias"
            if bkey in sd:
                candidates.append((f"{name}.{idx}", idx, k, bkey))

    # 패턴 3: b1.weight / b2.weight
    for base in ["b1", "b2"]:
        wkey = f"{base}.weight"
        bkey = f"{base}.bias"
        if wkey in sd and bkey in sd:
            idx = 0 if base == "b1" else 2
            candidates.append((base, idx, wkey, bkey))

    if not candidates:
        raise RuntimeError("backbone layers not found in state_dict")

    # idx 기준 정렬 후, 앞에서 두 층 선택
    candidates.sort(key=lambda x: x[1])
    unique = []
    seen = set()
    for name, idx, wk, bk in candidates:
        if wk not in seen:
            unique.append((wk, bk))
            seen.add(wk)
        if len(unique) == 2:
            break

    if len(unique) < 2:
        raise RuntimeError("cannot locate two backbone layers")

    return unique  # [(w1,b1),(w2,b2)]

def _find_head(sd, names):
    """
    names 리스트 내 후보 중 첫 번째로 발견되는 (weight,bias) 키를 반환
    """
    for base in names:
        wkey, bkey = f"{base}.weight", f"{base}.bias"
        if wkey in sd and bkey in sd:
            return wkey, bkey
    raise RuntimeError(f"head not found for candidates: {names}")

def main():
    sd = torch.load(PT_PATH, map_location="cpu")
    # 1) 백본 두 층
    (w1k, b1k), (w2k, b2k) = _find_first_two_backbone(sd)

    # 2) 헤드들
    # FI 헤드: 기본 'fi_head' (out=2), 레거시: 'head_fi' 등도 시도
    w_fi_k, b_fi_k = _find_head(sd, ["fi_head", "head_fi", "h_fi", "head_fiL"])  # 마지막 항목은 혹시 모를 레거시
    # BI 헤드: 기본 'bi_head'
    w_bi_k, b_bi_k = _find_head(sd, ["bi_head", "head_bi", "h_bi"])

    # 3) 텐서를 NumPy로 변환 (T 전치: [in, out] 형태로 저장)
    Wb1 = sd[w1k].detach().cpu().numpy().T
    bb1 = sd[b1k].detach().cpu().numpy()
    Wb2 = sd[w2k].detach().cpu().numpy().T
    bb2 = sd[b2k].detach().cpu().numpy()

    W_fi = sd[w_fi_k].detach().cpu().numpy().T   # shape: [hidden, 2]
    b_fi = sd[b_fi_k].detach().cpu().numpy()     # shape: [2]

    W_bi = sd[w_bi_k].detach().cpu().numpy().T   # shape: [hidden, 1]
    b_bi = sd[b_bi_k].detach().cpu().numpy()     # shape: [1]

    np.savez(
        NPZ_PATH,
        Wb1=Wb1, bb1=bb1, Wb2=Wb2, bb2=bb2,
        W_fi=W_fi, b_fi=b_fi,
        W_bi=W_bi, b_bi=b_bi,
    )
    print(f"[OK] exported numpy weights → {NPZ_PATH}")
    print(f"  backbone: {Wb1.shape=} {Wb2.shape=}")
    print(f"  heads   : {W_fi.shape=} {W_bi.shape=}")

if __name__ == "__main__":
    main()
