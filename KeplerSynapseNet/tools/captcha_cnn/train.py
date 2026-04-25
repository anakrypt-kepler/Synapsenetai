#!/usr/bin/env python3
import argparse
import csv
import os
import random

import cv2
import numpy as np
import torch
import torch.nn as nn
from torch.utils.data import DataLoader, Dataset, ConcatDataset

from model import (
    CHARSET,
    IMG_H,
    IMG_W,
    MAX_LEN,
    NUM_CLASSES,
    CaptchaCRNN,
    decode_prediction,
    encode_label,
)


class RealCaptchaDataset(Dataset):
    def __init__(self, image_dir, labels_csv, oversample=100):
        self.samples = []
        with open(labels_csv) as f:
            reader = csv.reader(f)
            next(reader, None)
            for row in reader:
                if len(row) >= 2 and row[1].strip():
                    fpath = os.path.join(image_dir, row[0])
                    if os.path.exists(fpath):
                        self.samples.append((fpath, row[1].strip()))
        self.base_len = len(self.samples)
        self.oversample = oversample

    def __len__(self):
        return self.base_len * self.oversample

    def __getitem__(self, idx):
        path, label = self.samples[idx % self.base_len]
        img = cv2.imread(path, cv2.IMREAD_GRAYSCALE)
        if img is None:
            img = np.zeros((IMG_H, IMG_W), dtype=np.uint8)
        img = self._augment(img)
        img = cv2.resize(img, (IMG_W, IMG_H), interpolation=cv2.INTER_LANCZOS4)
        img = img.astype(np.float32) / 255.0
        tensor = torch.from_numpy(img).unsqueeze(0)
        encoded = encode_label(label)
        target = torch.tensor(encoded, dtype=torch.long)
        target_len = torch.tensor(len(encoded), dtype=torch.long)
        return tensor, target, target_len, label

    def _augment(self, img):
        h, w = img.shape[:2]

        if random.random() < 0.5:
            angle = random.uniform(-8, 8)
            sc = random.uniform(0.9, 1.1)
            M = cv2.getRotationMatrix2D((w / 2, h / 2), angle, sc)
            img = cv2.warpAffine(img, M, (w, h), borderValue=128)

        if random.random() < 0.5:
            noise = np.random.normal(0, random.uniform(5, 25), img.shape).astype(np.float32)
            img = np.clip(img.astype(np.float32) + noise, 0, 255).astype(np.uint8)

        if random.random() < 0.4:
            k = random.choice([3, 5, 7])
            img = cv2.GaussianBlur(img, (k, k), 0)

        if random.random() < 0.5:
            alpha = random.uniform(0.6, 1.4)
            beta = random.randint(-40, 40)
            img = np.clip(alpha * img.astype(np.float32) + beta, 0, 255).astype(np.uint8)

        if random.random() < 0.3:
            for _ in range(random.randint(1, 4)):
                x1, y1 = random.randint(0, w - 1), random.randint(0, h - 1)
                x2, y2 = random.randint(0, w - 1), random.randint(0, h - 1)
                cv2.line(img, (x1, y1), (x2, y2), random.randint(0, 200), 1)

        if random.random() < 0.3:
            px = random.randint(0, 15)
            py = random.randint(0, 10)
            img = img[py:h-py if py > 0 else h, px:w-px if px > 0 else w]

        if random.random() < 0.3:
            img = cv2.erode(img, np.ones((2, 2), np.uint8), iterations=1)
        elif random.random() < 0.3:
            img = cv2.dilate(img, np.ones((2, 2), np.uint8), iterations=1)

        return img


class SyntheticCaptchaDataset(Dataset):
    def __init__(self, size=20000, hard_ratio=0.8):
        self.size = size
        self.hard_ratio = hard_ratio

    def __len__(self):
        return self.size

    def __getitem__(self, idx):
        length = random.randint(3, 6)
        text = "".join(random.choices("ABCDEFGHJKLMNPQRSTUVWXYZ23456789", k=length))

        if random.random() < self.hard_ratio:
            img = self._render_hard(text)
        else:
            img = self._render_simple(text)

        img = cv2.resize(img, (IMG_W, IMG_H), interpolation=cv2.INTER_LANCZOS4)
        img = img.astype(np.float32) / 255.0
        tensor = torch.from_numpy(img).unsqueeze(0)
        encoded = encode_label(text)
        target = torch.tensor(encoded, dtype=torch.long)
        target_len = torch.tensor(len(encoded), dtype=torch.long)
        return tensor, target, target_len, text

    def _render_simple(self, text):
        img = np.ones((60, 180), dtype=np.uint8) * 255
        for _ in range(random.randint(2, 5)):
            x1, y1 = random.randint(0, 179), random.randint(0, 59)
            x2, y2 = random.randint(0, 179), random.randint(0, 59)
            cv2.line(img, (x1, y1), (x2, y2), random.randint(150, 230), 1)
        x_offset = 10
        for ch in text:
            font = random.choice([cv2.FONT_HERSHEY_SIMPLEX, cv2.FONT_HERSHEY_DUPLEX, cv2.FONT_HERSHEY_COMPLEX])
            scale = random.uniform(0.8, 1.2)
            cv2.putText(img, ch, (x_offset, random.randint(30, 45)), font, scale, random.randint(0, 60), random.choice([1, 2]))
            x_offset += random.randint(22, 32)
        for _ in range(random.randint(20, 80)):
            x, y = random.randint(0, 179), random.randint(0, 59)
            img[y, x] = random.randint(0, 200)
        return img

    def _render_hard(self, text):
        h, w = 100, 400
        base_hue = random.randint(0, 180)
        yy, xx = np.mgrid[0:h, 0:w]
        hue_map = (base_hue + xx // 3 + yy // 2) % 180
        noise = np.random.randint(-15, 16, (h, w))
        hue_map = ((hue_map + noise) % 180).astype(np.uint8)
        sat_map = np.random.randint(120, 256, (h, w), dtype=np.uint8)
        val_map = np.random.randint(80, 201, (h, w), dtype=np.uint8)
        hsv_img = np.stack([hue_map, sat_map, val_map], axis=-1).astype(np.uint8)
        img = cv2.cvtColor(hsv_img, cv2.COLOR_HSV2BGR)
        img = cv2.GaussianBlur(img, (5, 5), 2)

        btc_font = random.choice([cv2.FONT_HERSHEY_COMPLEX, cv2.FONT_HERSHEY_TRIPLEX])
        for _ in range(random.randint(8, 20)):
            bx, by = random.randint(-20, w - 10), random.randint(15, h + 10)
            bscale = random.uniform(0.8, 2.5)
            b_hue = random.randint(15, 35)
            b_block = np.array([[[b_hue, random.randint(150, 255), random.randint(140, 220)]]], dtype=np.uint8)
            b_bgr = cv2.cvtColor(b_block, cv2.COLOR_HSV2BGR)[0, 0]
            b_color = (int(b_bgr[0]), int(b_bgr[1]), int(b_bgr[2]))
            cv2.putText(img, random.choice(["B", "8"]), (bx, by), btc_font, bscale, b_color, random.choice([2, 3]))

        for _ in range(random.randint(3, 8)):
            x1, y1 = random.randint(0, w - 1), random.randint(0, h - 1)
            x2, y2 = random.randint(0, w - 1), random.randint(0, h - 1)
            lc = (random.randint(50, 200), random.randint(50, 200), random.randint(50, 200))
            cv2.line(img, (x1, y1), (x2, y2), lc, 1)

        text_colors = [
            lambda: (random.randint(120, 200), random.randint(30, 80), random.randint(30, 80)),
            lambda: (random.randint(30, 80), random.randint(120, 200), random.randint(30, 80)),
            lambda: (random.randint(30, 80), random.randint(30, 80), random.randint(120, 200)),
            lambda: (random.randint(0, 40), random.randint(0, 40), random.randint(0, 40)),
            lambda: (random.randint(0, 60), random.randint(80, 140), random.randint(80, 140)),
        ]

        x_offset = random.randint(10, 30)
        for ch in text:
            font = random.choice([cv2.FONT_HERSHEY_SIMPLEX, cv2.FONT_HERSHEY_DUPLEX,
                                  cv2.FONT_HERSHEY_TRIPLEX, cv2.FONT_HERSHEY_COMPLEX])
            scale = random.uniform(1.8, 3.0)
            thickness = random.choice([2, 3, 4])
            color = random.choice(text_colors)()
            y_pos = random.randint(55, 85)
            outline_color = (0, 0, 0) if sum(color) > 200 else (180, 180, 180)
            cv2.putText(img, ch, (x_offset, y_pos), font, scale, outline_color, thickness + 2)
            cv2.putText(img, ch, (x_offset, y_pos), font, scale, color, thickness)
            x_offset += random.randint(55, 80)

        return cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)


def ctc_collate(batch):
    images, targets, target_lens, labels = zip(*batch)
    images = torch.stack(images, 0)
    max_tlen = max(t.size(0) for t in targets)
    padded = torch.zeros(len(targets), max_tlen, dtype=torch.long)
    for i, t in enumerate(targets):
        padded[i, :t.size(0)] = t
    target_lens = torch.stack(target_lens, 0)
    return images, padded, target_lens, labels


def char_accuracy(pred, gt):
    correct = 0
    total = max(len(gt), 1)
    for i, c in enumerate(gt):
        if i < len(pred) and pred[i] == c:
            correct += 1
    return correct, total


def train(args):
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    print(f"Device: {device}")

    model = CaptchaCRNN().to(device)

    datasets = []
    if args.labels and os.path.exists(args.labels):
        real_ds = RealCaptchaDataset(args.image_dir, args.labels, oversample=args.oversample)
        print(f"Real base: {real_ds.base_len}, oversampled: {len(real_ds)}")
        datasets.append(real_ds)

    synth_ds = SyntheticCaptchaDataset(size=args.synthetic_count, hard_ratio=0.8)
    print(f"Synthetic: {len(synth_ds)}")
    datasets.append(synth_ds)

    combined = ConcatDataset(datasets) if len(datasets) > 1 else datasets[0]
    loader = DataLoader(combined, batch_size=args.batch_size, shuffle=True,
                        num_workers=0, collate_fn=ctc_collate)

    ctc_loss = nn.CTCLoss(blank=0, zero_infinity=True)
    optimizer = torch.optim.AdamW(model.parameters(), lr=args.lr, weight_decay=1e-4)
    scheduler = torch.optim.lr_scheduler.CosineAnnealingLR(optimizer, T_max=args.epochs)

    best_acc = 0.0
    os.makedirs(os.path.dirname(args.output) or ".", exist_ok=True)

    for epoch in range(args.epochs):
        model.train()
        total_loss = 0
        exact_correct = 0
        char_correct = 0
        char_total = 0
        n_samples = 0

        for images, targets, target_lens, labels in loader:
            images = images.to(device)
            output = model(images)
            log_probs = output.permute(1, 0, 2).log_softmax(2)
            T = log_probs.size(0)
            B = log_probs.size(1)
            input_lens = torch.full((B,), T, dtype=torch.long)

            flat_targets = []
            for i in range(B):
                flat_targets.extend(targets[i, :target_lens[i]].tolist())
            flat_targets = torch.tensor(flat_targets, dtype=torch.long)

            loss = ctc_loss(log_probs, flat_targets, input_lens, target_lens)
            if torch.isnan(loss) or torch.isinf(loss):
                continue

            optimizer.zero_grad()
            loss.backward()
            torch.nn.utils.clip_grad_norm_(model.parameters(), 5.0)
            optimizer.step()

            total_loss += loss.item()
            preds = decode_prediction(output)
            for pred, gt in zip(preds, labels):
                n_samples += 1
                if pred == gt:
                    exact_correct += 1
                cc, ct = char_accuracy(pred, gt)
                char_correct += cc
                char_total += ct

        scheduler.step()
        exact_acc = exact_correct / max(n_samples, 1) * 100
        ch_acc = char_correct / max(char_total, 1) * 100
        avg_loss = total_loss / max(len(loader), 1)
        lr = scheduler.get_last_lr()[0]
        print(f"Epoch {epoch+1}/{args.epochs}  loss={avg_loss:.4f}  exact={exact_acc:.1f}%  char={ch_acc:.1f}%  lr={lr:.6f}")

        if exact_acc > best_acc:
            best_acc = exact_acc
            torch.save({
                "model_state_dict": model.state_dict(),
                "epoch": epoch,
                "accuracy": exact_acc,
                "char_accuracy": ch_acc,
                "charset": CHARSET,
                "max_len": MAX_LEN,
                "img_h": IMG_H,
                "img_w": IMG_W,
            }, args.output)
            print(f"  Saved best model (exact={exact_acc:.1f}% char={ch_acc:.1f}%)")

    print(f"\nDone. Best exact accuracy: {best_acc:.1f}%")
    print(f"Model: {args.output}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--image-dir", default="captcha_dataset/unlabeled")
    parser.add_argument("--labels", default="captcha_dataset/labels.csv")
    parser.add_argument("--output", default="captcha_cnn_model.pt")
    parser.add_argument("--epochs", type=int, default=100)
    parser.add_argument("--batch-size", type=int, default=32)
    parser.add_argument("--lr", type=float, default=3e-4)
    parser.add_argument("--synthetic-count", type=int, default=20000)
    parser.add_argument("--oversample", type=int, default=100)
    args = parser.parse_args()
    train(args)


if __name__ == "__main__":
    main()
