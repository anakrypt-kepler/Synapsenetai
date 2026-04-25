#!/usr/bin/env python3
import argparse
import os
import sys

import cv2
import numpy as np
import torch

from model import IMG_H, IMG_W, CaptchaCRNN, decode_prediction


def load_model(model_path):
    checkpoint = torch.load(model_path, map_location="cpu", weights_only=False)
    model = CaptchaCRNN()
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


def infer_single(model, image_path):
    tensor = preprocess(image_path)
    if tensor is None:
        return ""
    with torch.no_grad():
        output = model(tensor)
    results = decode_prediction(output)
    return results[0] if results else ""


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("image")
    parser.add_argument("--model", default="captcha_crnn_v6.pt")
    args = parser.parse_args()

    if not os.path.exists(args.model):
        print("", end="")
        sys.exit(0)

    model = load_model(args.model)
    result = infer_single(model, args.image)
    print(result, end="")


if __name__ == "__main__":
    main()
