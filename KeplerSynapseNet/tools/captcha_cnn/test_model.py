#!/usr/bin/env python3
import os
import sys
import tempfile

import cv2
import numpy as np
import torch

from model import (
    CHARSET,
    IMG_H,
    IMG_W,
    MAX_LEN,
    NUM_CLASSES,
    CaptchaCNN,
    decode_prediction,
    encode_label,
)
from train import SyntheticCaptchaDataset


def test_model_architecture():
    model = CaptchaCNN()
    dummy = torch.randn(2, 1, IMG_H, IMG_W)
    out = model(dummy)
    assert out.shape == (2, MAX_LEN, NUM_CLASSES), f"Expected (2, {MAX_LEN}, {NUM_CLASSES}), got {out.shape}"
    print("PASS: model architecture")


def test_encode_decode():
    text = "Ab3X"
    encoded = encode_label(text)
    assert len(encoded) == MAX_LEN
    assert encoded[0] == CHARSET.index("A") + 1
    assert encoded[1] == CHARSET.index("b") + 1
    assert encoded[2] == CHARSET.index("3") + 1
    assert encoded[3] == CHARSET.index("X") + 1
    for i in range(4, MAX_LEN):
        assert encoded[i] == 0
    print("PASS: encode/decode")


def test_synthetic_dataset():
    ds = SyntheticCaptchaDataset(size=10, hard_ratio=0.5)
    assert len(ds) == 10
    tensor, target, label = ds[0]
    assert tensor.shape == (1, IMG_H, IMG_W)
    assert target.shape == (MAX_LEN,)
    assert len(label) >= 4
    print("PASS: synthetic dataset")


def test_train_one_epoch():
    from torch.utils.data import DataLoader

    ds = SyntheticCaptchaDataset(size=32, hard_ratio=0.3)
    loader = DataLoader(ds, batch_size=8, shuffle=True)
    model = CaptchaCNN()
    criterion = torch.nn.CrossEntropyLoss(ignore_index=0)
    optimizer = torch.optim.Adam(model.parameters(), lr=1e-3)

    model.train()
    for images, targets, labels in loader:
        output = model(images)
        loss = 0
        for pos in range(MAX_LEN):
            loss = loss + criterion(output[:, pos, :], targets[:, pos])
        loss = loss / MAX_LEN
        optimizer.zero_grad()
        loss.backward()
        optimizer.step()
        break

    print("PASS: train one batch")


def test_save_load_infer():
    model = CaptchaCNN()

    with tempfile.NamedTemporaryFile(suffix=".pt", delete=False) as f:
        model_path = f.name
    torch.save({
        "model_state_dict": model.state_dict(),
        "epoch": 0,
        "accuracy": 0.0,
        "charset": CHARSET,
        "max_len": MAX_LEN,
        "img_h": IMG_H,
        "img_w": IMG_W,
    }, model_path)

    checkpoint = torch.load(model_path, map_location="cpu", weights_only=False)
    loaded = CaptchaCNN()
    loaded.load_state_dict(checkpoint["model_state_dict"])
    loaded.eval()

    dummy = torch.randn(1, 1, IMG_H, IMG_W)
    with torch.no_grad():
        out = loaded(dummy)
    results = decode_prediction(out)
    assert len(results) == 1
    assert isinstance(results[0], str)

    os.unlink(model_path)
    print("PASS: save/load/infer")


def test_infer_script():
    import subprocess

    model = CaptchaCNN()
    with tempfile.NamedTemporaryFile(suffix=".pt", delete=False) as f:
        model_path = f.name
    torch.save({
        "model_state_dict": model.state_dict(),
        "epoch": 0,
        "accuracy": 0.0,
        "charset": CHARSET,
        "max_len": MAX_LEN,
        "img_h": IMG_H,
        "img_w": IMG_W,
    }, model_path)

    img = np.ones((50, 150), dtype=np.uint8) * 200
    cv2.putText(img, "AB12", (10, 35), cv2.FONT_HERSHEY_SIMPLEX, 1.0, 0, 2)
    with tempfile.NamedTemporaryFile(suffix=".png", delete=False) as f:
        img_path = f.name
    cv2.imwrite(img_path, img)

    script_dir = os.path.dirname(os.path.abspath(__file__))
    result = subprocess.run(
        ["python3", os.path.join(script_dir, "infer.py"),
         "--model", model_path, img_path],
        capture_output=True, text=True, timeout=30,
    )
    assert result.returncode == 0
    print(f"PASS: infer script returned '{result.stdout.strip()}'")

    os.unlink(model_path)
    os.unlink(img_path)


if __name__ == "__main__":
    test_model_architecture()
    test_encode_decode()
    test_synthetic_dataset()
    test_train_one_epoch()
    test_save_load_infer()
    test_infer_script()
    print("\nAll captcha CNN tests passed.")
