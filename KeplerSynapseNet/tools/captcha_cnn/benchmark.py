#!/usr/bin/env python3
import argparse
import csv
import os
import time

import cv2
import numpy as np
import torch

from model import IMG_H, IMG_W, CaptchaCNN, decode_prediction


def load_model(model_path):
    checkpoint = torch.load(model_path, map_location="cpu", weights_only=False)
    model = CaptchaCNN()
    model.load_state_dict(checkpoint["model_state_dict"])
    model.eval()
    return model


def preprocess(image_path):
    img = cv2.imread(image_path, cv2.IMREAD_GRAYSCALE)
    if img is None:
        return None
    img = cv2.resize(img, (IMG_W, IMG_H), interpolation=cv2.INTER_LANCZOS4)
    img = img.astype(np.float32) / 255.0
    return torch.from_numpy(img).unsqueeze(0).unsqueeze(0)


def run_easyocr(image_path):
    try:
        import easyocr
        reader = easyocr.Reader(["en"], gpu=False, verbose=False)
        results = reader.readtext(image_path, detail=1,
                                  allowlist="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789")
        if results:
            best = max(results, key=lambda x: x[2])
            return best[1], best[2]
    except Exception:
        pass
    return "", 0.0


def run_tesseract(image_path):
    try:
        import subprocess
        r = subprocess.run(
            ["tesseract", image_path, "stdout", "--psm", "7",
             "-c", "tessedit_char_whitelist=ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"],
            capture_output=True, text=True, timeout=10,
        )
        return r.stdout.strip(), 0.5
    except Exception:
        return "", 0.0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--image-dir", required=True)
    parser.add_argument("--labels", required=True)
    parser.add_argument("--model", default="captcha_cnn_model.pt")
    parser.add_argument("--compare", action="store_true")
    args = parser.parse_args()

    model = load_model(args.model)

    samples = []
    with open(args.labels) as f:
        reader = csv.reader(f)
        next(reader, None)
        for row in reader:
            if len(row) >= 2:
                fpath = os.path.join(args.image_dir, row[0])
                if os.path.exists(fpath):
                    samples.append((fpath, row[1].strip()))

    if not samples:
        print("No labeled samples found.")
        return

    cnn_correct = 0
    easy_correct = 0
    tess_correct = 0
    total = len(samples)
    cnn_time = 0.0

    print(f"Benchmarking on {total} samples...\n")
    print(f"{'Image':<30} {'Ground Truth':<12} {'CNN':<12} {'Match':<6}", end="")
    if args.compare:
        print(f" {'EasyOCR':<12} {'Tesseract':<12}", end="")
    print()
    print("-" * (80 if args.compare else 60))

    for fpath, gt in samples:
        fname = os.path.basename(fpath)

        t0 = time.time()
        tensor = preprocess(fpath)
        if tensor is not None:
            with torch.no_grad():
                output = model(tensor)
            cnn_pred = decode_prediction(output)[0]
        else:
            cnn_pred = ""
        cnn_time += time.time() - t0

        cnn_match = cnn_pred.lower() == gt.lower()
        if cnn_match:
            cnn_correct += 1

        print(f"{fname:<30} {gt:<12} {cnn_pred:<12} {'OK' if cnn_match else 'FAIL':<6}", end="")

        if args.compare:
            easy_pred, _ = run_easyocr(fpath)
            tess_pred, _ = run_tesseract(fpath)
            if easy_pred.lower() == gt.lower():
                easy_correct += 1
            if tess_pred.lower() == gt.lower():
                tess_correct += 1
            print(f" {easy_pred:<12} {tess_pred:<12}", end="")

        print()

    print(f"\n{'='*60}")
    print(f"CNN:       {cnn_correct}/{total} ({cnn_correct/total*100:.1f}%)  avg {cnn_time/total*1000:.1f}ms/image")
    if args.compare:
        print(f"EasyOCR:   {easy_correct}/{total} ({easy_correct/total*100:.1f}%)")
        print(f"Tesseract: {tess_correct}/{total} ({tess_correct/total*100:.1f}%)")


if __name__ == "__main__":
    main()
