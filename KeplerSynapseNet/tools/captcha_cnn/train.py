#!/usr/bin/env python3
import argparse
import csv
import os
import random

import cv2
import numpy as np
import torch
import torch.nn as nn
from torch.utils.data import DataLoader, Dataset

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


class CaptchaDataset(Dataset):
    def __init__(self, image_dir, labels_csv, augment=False):
        self.image_dir = image_dir
        self.augment = augment
        self.samples = []
        with open(labels_csv) as f:
            reader = csv.reader(f)
            next(reader, None)
            for row in reader:
                if len(row) >= 2 and row[1].strip():
                    fpath = os.path.join(image_dir, row[0])
                    if os.path.exists(fpath):
                        self.samples.append((fpath, row[1].strip()))

    def __len__(self):
        return len(self.samples)

    def __getitem__(self, idx):
        path, label = self.samples[idx]
        img = cv2.imread(path, cv2.IMREAD_GRAYSCALE)
        if img is None:
            img = np.zeros((IMG_H, IMG_W), dtype=np.uint8)

        if self.augment:
            img = self._augment(img)

        img = cv2.resize(img, (IMG_W, IMG_H), interpolation=cv2.INTER_LANCZOS4)
        img = img.astype(np.float32) / 255.0
        tensor = torch.from_numpy(img).unsqueeze(0)
        target = torch.tensor(encode_label(label), dtype=torch.long)
        return tensor, target, label

    def _augment(self, img):
        h, w = img.shape[:2]

        if random.random() < 0.3:
            angle = random.uniform(-5, 5)
            M = cv2.getRotationMatrix2D((w / 2, h / 2), angle, 1.0)
            img = cv2.warpAffine(img, M, (w, h), borderValue=255)

        if random.random() < 0.3:
            noise = np.random.normal(0, 15, img.shape).astype(np.float32)
            img = np.clip(img.astype(np.float32) + noise, 0, 255).astype(np.uint8)

        if random.random() < 0.3:
            k = random.choice([3, 5])
            img = cv2.GaussianBlur(img, (k, k), 0)

        if random.random() < 0.3:
            alpha = random.uniform(0.7, 1.3)
            beta = random.randint(-30, 30)
            img = np.clip(alpha * img.astype(np.float32) + beta, 0, 255).astype(np.uint8)

        if random.random() < 0.2:
            for _ in range(random.randint(1, 3)):
                x1 = random.randint(0, w - 1)
                y1 = random.randint(0, h - 1)
                x2 = random.randint(0, w - 1)
                y2 = random.randint(0, h - 1)
                color = random.randint(0, 128)
                cv2.line(img, (x1, y1), (x2, y2), color, 1)

        if random.random() < 0.2:
            for _ in range(random.randint(5, 20)):
                x = random.randint(0, w - 1)
                y = random.randint(0, h - 1)
                img[y, x] = random.randint(0, 255)

        return img


class SyntheticCaptchaDataset(Dataset):
    def __init__(self, size=10000, hard_ratio=0.3):
        self.size = size
        self.hard_ratio = hard_ratio
        self.fonts = self._find_fonts()

    def _find_fonts(self):
        candidates = [
            "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
            "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
            "/usr/share/fonts/truetype/ubuntu/UbuntuMono-R.ttf",
            "/System/Library/Fonts/Courier.dfont",
            "/System/Library/Fonts/Monaco.dfont",
            "/System/Library/Fonts/Menlo.ttc",
        ]
        found = [f for f in candidates if os.path.exists(f)]
        return found if found else []

    def __len__(self):
        return self.size

    def __getitem__(self, idx):
        length = random.randint(4, 6)
        text = "".join(random.choices(CHARSET, k=length))

        is_hard = random.random() < self.hard_ratio
        if is_hard:
            img = self._render_hard(text)
        else:
            img = self._render_simple(text)

        img = cv2.resize(img, (IMG_W, IMG_H), interpolation=cv2.INTER_LANCZOS4)
        img = img.astype(np.float32) / 255.0
        tensor = torch.from_numpy(img).unsqueeze(0)
        target = torch.tensor(encode_label(text), dtype=torch.long)
        return tensor, target, text

    def _render_simple(self, text):
        img = np.ones((60, 180), dtype=np.uint8) * 255

        for _ in range(random.randint(2, 5)):
            x1 = random.randint(0, 179)
            y1 = random.randint(0, 59)
            x2 = random.randint(0, 179)
            y2 = random.randint(0, 59)
            color = random.randint(150, 230)
            cv2.line(img, (x1, y1), (x2, y2), color, 1)

        x_offset = 10
        for ch in text:
            font = random.choice([
                cv2.FONT_HERSHEY_SIMPLEX,
                cv2.FONT_HERSHEY_DUPLEX,
                cv2.FONT_HERSHEY_COMPLEX,
            ])
            scale = random.uniform(0.8, 1.2)
            thickness = random.choice([1, 2])
            color = random.randint(0, 60)
            y = random.randint(30, 45)
            cv2.putText(img, ch, (x_offset, y), font, scale, color, thickness)
            x_offset += random.randint(22, 32)

        for _ in range(random.randint(20, 80)):
            x = random.randint(0, 179)
            y = random.randint(0, 59)
            img[y, x] = random.randint(0, 200)

        return img

    def _render_hard(self, text):
        h, w = 100, 400
        img = np.random.randint(40, 220, (h, w), dtype=np.uint8)

        for _ in range(random.randint(3, 8)):
            x1 = random.randint(0, w - 1)
            y1 = random.randint(0, h - 1)
            x2 = random.randint(0, w - 1)
            y2 = random.randint(0, h - 1)
            color = random.randint(0, 255)
            cv2.line(img, (x1, y1), (x2, y2), color, random.randint(1, 3))

        x_offset = 20
        for ch in text:
            font = random.choice([
                cv2.FONT_HERSHEY_SIMPLEX,
                cv2.FONT_HERSHEY_DUPLEX,
                cv2.FONT_HERSHEY_TRIPLEX,
            ])
            scale = random.uniform(1.5, 2.5)
            thickness = random.choice([2, 3])
            color = random.randint(0, 40)
            y = random.randint(50, 75)
            cv2.putText(img, ch, (x_offset, y), font, scale, color, thickness)
            x_offset += random.randint(45, 65)

        for _ in range(random.randint(5, 15)):
            x1 = random.randint(0, w - 1)
            y1 = random.randint(0, h - 1)
            x2 = random.randint(0, w - 1)
            y2 = random.randint(0, h - 1)
            cv2.line(img, (x1, y1), (x2, y2), random.randint(50, 200), 1)

        return img


def train(args):
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    print(f"Device: {device}")

    model = CaptchaCNN().to(device)

    if args.labels and os.path.exists(args.labels):
        real_ds = CaptchaDataset(args.image_dir, args.labels, augment=True)
        print(f"Real samples: {len(real_ds)}")
    else:
        real_ds = None

    synth_ds = SyntheticCaptchaDataset(size=args.synthetic_count, hard_ratio=0.4)
    print(f"Synthetic samples: {len(synth_ds)}")

    if real_ds and len(real_ds) > 0:
        from torch.utils.data import ConcatDataset
        combined = ConcatDataset([real_ds, synth_ds])
    else:
        combined = synth_ds

    loader = DataLoader(combined, batch_size=args.batch_size, shuffle=True, num_workers=0)

    criterion = nn.CrossEntropyLoss(ignore_index=0)
    optimizer = torch.optim.AdamW(model.parameters(), lr=args.lr, weight_decay=1e-4)
    scheduler = torch.optim.lr_scheduler.CosineAnnealingLR(optimizer, T_max=args.epochs)

    best_acc = 0.0
    os.makedirs(os.path.dirname(args.output) or ".", exist_ok=True)

    for epoch in range(args.epochs):
        model.train()
        total_loss = 0
        correct = 0
        total = 0

        for batch_idx, (images, targets, labels) in enumerate(loader):
            images = images.to(device)
            targets = targets.to(device)

            output = model(images)
            loss = 0
            for pos in range(MAX_LEN):
                loss = loss + criterion(output[:, pos, :], targets[:, pos])
            loss = loss / MAX_LEN

            optimizer.zero_grad()
            loss.backward()
            torch.nn.utils.clip_grad_norm_(model.parameters(), 5.0)
            optimizer.step()

            total_loss += loss.item()

            preds = decode_prediction(output)
            for pred, gt in zip(preds, labels):
                total += 1
                if pred == gt:
                    correct += 1

        scheduler.step()
        acc = correct / max(total, 1) * 100
        avg_loss = total_loss / max(len(loader), 1)
        print(f"Epoch {epoch+1}/{args.epochs}  loss={avg_loss:.4f}  acc={acc:.1f}%  lr={scheduler.get_last_lr()[0]:.6f}")

        if acc > best_acc:
            best_acc = acc
            torch.save({
                "model_state_dict": model.state_dict(),
                "epoch": epoch,
                "accuracy": acc,
                "charset": CHARSET,
                "max_len": MAX_LEN,
                "img_h": IMG_H,
                "img_w": IMG_W,
            }, args.output)
            print(f"  Saved best model ({acc:.1f}%)")

    print(f"\nTraining complete. Best accuracy: {best_acc:.1f}%")
    print(f"Model saved to: {args.output}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--image-dir", default="captcha_dataset/unlabeled")
    parser.add_argument("--labels", default="captcha_dataset/labels.csv")
    parser.add_argument("--output", default="captcha_cnn_model.pt")
    parser.add_argument("--epochs", type=int, default=50)
    parser.add_argument("--batch-size", type=int, default=32)
    parser.add_argument("--lr", type=float, default=1e-3)
    parser.add_argument("--synthetic-count", type=int, default=20000)
    args = parser.parse_args()
    train(args)


if __name__ == "__main__":
    main()
