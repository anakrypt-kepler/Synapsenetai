import torch
import torch.nn as nn


CHARSET = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"
NUM_CLASSES = len(CHARSET) + 1
MAX_LEN = 8
IMG_H = 64
IMG_W = 200


class CaptchaCRNN(nn.Module):
    def __init__(self, hidden_size=128, num_layers=1):
        super().__init__()
        self.cnn = nn.Sequential(
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
            nn.MaxPool2d((2, 1), (2, 1)),

            nn.Conv2d(128, 256, 3, padding=1),
            nn.BatchNorm2d(256),
            nn.ReLU(inplace=True),
            nn.Dropout2d(0.2),
            nn.AdaptiveAvgPool2d((1, None)),
        )

        self.rnn = nn.GRU(
            input_size=256,
            hidden_size=hidden_size,
            num_layers=num_layers,
            bidirectional=True,
            batch_first=True,
        )

        self.fc = nn.Linear(hidden_size * 2, NUM_CLASSES)

    def forward(self, x):
        conv = self.cnn(x)
        conv = conv.squeeze(2)
        conv = conv.permute(0, 2, 1)
        rnn_out, _ = self.rnn(conv)
        out = self.fc(rnn_out)
        return out


CaptchaCNN = CaptchaCRNN


def decode_prediction(output):
    _, preds = output.max(2)
    result = []
    for seq in preds:
        chars = []
        prev = -1
        for idx in seq:
            idx = idx.item()
            if idx != prev:
                if idx != 0:
                    if idx - 1 < len(CHARSET):
                        chars.append(CHARSET[idx - 1])
            prev = idx
        result.append("".join(chars))
    return result


def decode_prediction_beam(output, beam_width=10):
    log_probs = torch.nn.functional.log_softmax(output, dim=2)
    B, T, C = log_probs.shape
    results = []
    for b in range(B):
        beams = [(0.0, [], -1)]
        for t in range(T):
            new_beams = []
            for score, prefix, last in beams:
                for c in range(C):
                    lp = log_probs[b, t, c].item()
                    new_score = score + lp
                    if c == 0:
                        new_beams.append((new_score, prefix[:], c))
                    elif c == last:
                        new_beams.append((new_score, prefix[:], c))
                    else:
                        new_beams.append((new_score, prefix + [c], c))
            new_beams.sort(key=lambda x: x[0], reverse=True)
            merged = {}
            for sc, pf, lc in new_beams:
                key = (tuple(pf), lc)
                if key not in merged:
                    merged[key] = (sc, pf, lc)
            beams = list(merged.values())[:beam_width]
        best = max(beams, key=lambda x: x[0])
        chars = [CHARSET[i - 1] for i in best[1] if 0 < i <= len(CHARSET)]
        results.append("".join(chars))
    return results


def encode_label(text):
    encoded = []
    for ch in text:
        idx = CHARSET.find(ch)
        if idx >= 0:
            encoded.append(idx + 1)
    return encoded


def encode_label_padded(text, max_len=MAX_LEN):
    encoded = encode_label(text)
    while len(encoded) < max_len:
        encoded.append(0)
    return encoded[:max_len]
