import torch
import torch.nn as nn


CHARSET = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"
NUM_CLASSES = len(CHARSET) + 1
MAX_LEN = 6
IMG_H = 64
IMG_W = 200


class CaptchaCNN(nn.Module):
    def __init__(self):
        super().__init__()
        self.features = nn.Sequential(
            nn.Conv2d(1, 32, 3, padding=1),
            nn.BatchNorm2d(32),
            nn.ReLU(inplace=True),
            nn.MaxPool2d(2, 2),

            nn.Conv2d(32, 64, 3, padding=1),
            nn.BatchNorm2d(64),
            nn.ReLU(inplace=True),
            nn.MaxPool2d(2, 2),

            nn.Conv2d(64, 128, 3, padding=1),
            nn.BatchNorm2d(128),
            nn.ReLU(inplace=True),
            nn.MaxPool2d(2, 2),

            nn.Conv2d(128, 256, 3, padding=1),
            nn.BatchNorm2d(256),
            nn.ReLU(inplace=True),
            nn.AdaptiveAvgPool2d((1, MAX_LEN)),
        )
        self.classifier = nn.Sequential(
            nn.Dropout(0.3),
            nn.Linear(256, NUM_CLASSES),
        )

    def forward(self, x):
        b = x.size(0)
        f = self.features(x)
        f = f.squeeze(2)
        f = f.permute(0, 2, 1)
        out = self.classifier(f)
        return out


def decode_prediction(output):
    _, preds = output.max(2)
    result = []
    for seq in preds:
        chars = []
        prev = -1
        for idx in seq:
            idx = idx.item()
            if idx != 0 and idx != prev:
                chars.append(CHARSET[idx - 1])
            prev = idx
        result.append("".join(chars))
    return result


def encode_label(text):
    encoded = []
    for ch in text:
        idx = CHARSET.find(ch)
        if idx >= 0:
            encoded.append(idx + 1)
    while len(encoded) < MAX_LEN:
        encoded.append(0)
    return encoded[:MAX_LEN]
