<h1 align="center">SynapseNet 0.1.0-alphaV5.1</h1>

<p align="center"><strong>CNN CAPTCHA Solver for Darknet Fonts</strong></p>

<p align="center">
  <img src="https://img.shields.io/badge/Version-0.1.0--alphaV5.1-000000?style=for-the-badge&labelColor=000000" alt="Version" />
  <img src="https://img.shields.io/badge/Status-In_Progress-000000?style=for-the-badge&labelColor=000000" alt="Status" />
  <img src="https://img.shields.io/badge/Model-PyTorch_CNN-000000?style=for-the-badge&labelColor=000000" alt="Model" />
  <img src="https://img.shields.io/badge/Target-95%25_Accuracy-000000?style=for-the-badge&labelColor=000000" alt="Target" />
</p>

<p align="center">
  <a href="https://github.com/anakrypt-kepler"><img src="https://img.shields.io/badge/Kepler-000000?style=for-the-badge&logo=github&logoColor=white" alt="Profile" /></a>
  <a href="https://github.com/anakrypt-kepler/Synapsenetai"><img src="https://img.shields.io/badge/Source_Code-000000?style=for-the-badge&logo=github&logoColor=white" alt="Source" /></a>
  <a href="https://github.com/anakrypt-kepler/Synapsenetai/tree/main/RELEASES/0.1.0-alphaV5"><img src="https://img.shields.io/badge/←_0.1.0--alphaV5-000000?style=for-the-badge&logo=rocket&logoColor=white" alt="V5" /></a>
  <a href="https://github.com/anakrypt-kepler/Synapsenetai/tree/main/RELEASES"><img src="https://img.shields.io/badge/All_Releases-000000?style=for-the-badge&logo=github&logoColor=white" alt="All Releases" /></a>
</p>

---

> V5.1 adds a dedicated CNN model trained on darknet CAPTCHA fonts. The V5 ML pipeline relied on generic OCR (EasyOCR + Tesseract + TrOCR) which works well on clean CAPTCHAs but only reaches ~80% on hard backgrounds like BTC VPS-style challenges. V5.1 trains a purpose-built 4-layer CNN that learns the specific fonts, noise patterns, and color schemes used by darknet services. The model runs as the primary solver — if it produces a confident result, OCR is skipped entirely. If not, the full V5 pipeline runs as fallback.

---

## What's New

### CNN Architecture

```
Input: 1x64x200 grayscale
  │
  ├─ Conv2d(1, 32, 3) + BN + ReLU + MaxPool(2)
  ├─ Conv2d(32, 64, 3) + BN + ReLU + MaxPool(2)
  ├─ Conv2d(64, 128, 3) + BN + ReLU + MaxPool(2)
  ├─ Conv2d(128, 256, 3) + BN + ReLU + AdaptiveAvgPool(1, 6)
  │
  ├─ Dropout(0.3)
  └─ Linear(256, 63) × 6 positions
      │
      └─ Output: 6 × 63 (62 alphanumeric chars + blank)
```

- 4 conv layers with batch normalization
- Adaptive pooling splits feature map into 6 character positions
- Per-position classification (no CTC, no RNN)
- Total params: ~750K (lightweight, runs on CPU in <50ms)

### Training Pipeline

```
tools/captcha_cnn/
  ├─ model.py       — CaptchaCNN architecture, charset, encode/decode
  ├─ harvest.py     — Tor-based CAPTCHA image harvester from live onion sites
  ├─ label_tool.py  — Terminal labeling tool for harvested images
  ├─ train.py       — Training with synthetic + real data, augmentation
  ├─ infer.py       — Single-image inference (called from C++ engine)
  ├─ benchmark.py   — Accuracy comparison: CNN vs EasyOCR vs Tesseract
  └─ test_model.py  — Unit tests for architecture, encode/decode, train, infer
```

### Harvest Sources

Images are collected via Tor from real darknet CAPTCHA endpoints:

| Source | Type | Description |
|--------|------|-------------|
| Bizzle Forum | Direct PNG | `generate_captcha.php`, 150x50, black text + noise lines |
| DeepMa/TorMart | Inline base64 | JPEG, 150x46, red handwritten text on grey |
| BTC VPS | Page img tag | 400x100, hard text on busy Bitcoin background |
| Ahmia Discovery | Search | Crawls Ahmia for new onion sites with CAPTCHA forms |
| Torch Discovery | Search | Crawls Torch for additional CAPTCHA sources |

### Data Augmentation

- Random rotation (+-5 degrees)
- Gaussian noise injection
- Gaussian blur (k=3 or 5)
- Brightness/contrast jitter
- Random line overlay (mimics noise lines)
- Salt-and-pepper noise
- Hard-background synthetic generation (K-means style busy backgrounds)

### Integration

The CNN model file (`captcha_cnn_model.pt`) is checked first in `solveTextCaptcha()`. If the model exists and returns a 3-8 character alphanumeric result, it is used immediately. Otherwise, the full V5 EasyOCR/Tesseract/TrOCR pipeline runs as fallback.

```
solveTextCaptcha(imgUrl)
  │
  ├─ Download/decode image
  │
  ├─ CNN model check
  │   ├─ Model exists? → python3 infer.py --model captcha_cnn_model.pt image.png
  │   ├─ Result 3-8 chars? → return immediately
  │   └─ Otherwise → fall through to V5 pipeline
  │
  └─ V5 ML Pipeline (EasyOCR + Tesseract + TrOCR + scoring)
```

---

## Usage

### 1. Harvest CAPTCHA images from darknet

```bash
cd KeplerSynapseNet

python3 tools/captcha_cnn/harvest.py \
  --output captcha_dataset/unlabeled \
  --count 200 \
  --discover
```

### 2. Label the images

```bash
python3 tools/captcha_cnn/label_tool.py \
  --input captcha_dataset/unlabeled \
  --output captcha_dataset/labels.csv
```

### 3. Train the model

```bash
python3 tools/captcha_cnn/train.py \
  --image-dir captcha_dataset/unlabeled \
  --labels captcha_dataset/labels.csv \
  --output data/captcha_cnn_model.pt \
  --epochs 50 \
  --synthetic-count 20000
```

### 4. Benchmark against OCR baselines

```bash
python3 tools/captcha_cnn/benchmark.py \
  --image-dir captcha_dataset/unlabeled \
  --labels captcha_dataset/labels.csv \
  --model data/captcha_cnn_model.pt \
  --compare
```

### 5. Deploy

Copy `captcha_cnn_model.pt` to the node data directory. The engine picks it up automatically on next CAPTCHA encounter.

---

## Dependencies

### Required (from V5)

- `torch` >= 2.0
- `opencv-python` (cv2)

### Optional

- `easyocr` (fallback OCR)
- `tesseract` (fallback OCR)

---

## Files Changed

| File | Lines Changed | What |
|------|--------------|------|
| `KeplerSynapseNet/tools/captcha_cnn/model.py` | +78 | CNN architecture, charset, encode/decode |
| `KeplerSynapseNet/tools/captcha_cnn/harvest.py` | +240 | Tor CAPTCHA image harvester with Ahmia/Torch discovery |
| `KeplerSynapseNet/tools/captcha_cnn/label_tool.py` | +85 | Terminal labeling tool |
| `KeplerSynapseNet/tools/captcha_cnn/train.py` | +230 | Training with synthetic data + augmentation |
| `KeplerSynapseNet/tools/captcha_cnn/infer.py` | +55 | Single-image inference script |
| `KeplerSynapseNet/tools/captcha_cnn/benchmark.py` | +115 | CNN vs EasyOCR vs Tesseract benchmark |
| `KeplerSynapseNet/tools/captcha_cnn/test_model.py` | +120 | Unit tests |
| `KeplerSynapseNet/src/ide/synapsed_engine.cpp` | +20 | CNN solver integration in solveTextCaptcha() |

---

## What's Next

- V5.2 — EndGame V3 hashcash-style Proof-of-Work bypass
- V6 — Full NAAN agent integration testing across 50+ darknet services with automated success rate reporting

---

<p align="center">
  <a href="https://github.com/anakrypt-kepler"><img src="https://img.shields.io/badge/Built_by_Kepler-000000?style=for-the-badge&logo=github&logoColor=white" alt="Kepler" /></a>
  <a href="https://github.com/anakrypt-kepler/Synapsenetai"><img src="https://img.shields.io/badge/Source_Code-000000?style=for-the-badge&logo=github&logoColor=white" alt="Source Code" /></a>
</p>

<p align="center">
  If you find this project worth watching — even if you can't contribute code — you can help keep it going.<br>
  Donations go directly toward VPS hosting for seed nodes, build infrastructure, and development time.
</p>

<p align="center">
  <a href="https://www.blockchain.com/btc/address/bc1q5pkemq7q84ld4rf5kwtafp7jfl9dlf3pc4z9d4"><img src="https://img.shields.io/badge/bc1q5pkemq7q84ld4rf5kwtafp7jfl9dlf3pc4z9d4-000000?style=for-the-badge&logo=bitcoin&logoColor=white" alt="BTC" /></a>
</p>
