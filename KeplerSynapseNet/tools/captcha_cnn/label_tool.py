#!/usr/bin/env python3
import argparse
import csv
import os
import sys

try:
    from PIL import Image
except ImportError:
    Image = None


def show_image_terminal(path):
    try:
        from PIL import Image as PILImage
        img = PILImage.open(path)
        w, h = img.size
        print(f"  [{w}x{h}] {os.path.basename(path)}")
    except Exception:
        print(f"  {os.path.basename(path)}")

    if sys.platform == "darwin":
        os.system(f'open -g "{path}" 2>/dev/null &')
    elif sys.platform == "linux":
        os.system(f'xdg-open "{path}" 2>/dev/null &')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", default="captcha_dataset/unlabeled")
    parser.add_argument("--output", default="captcha_dataset/labels.csv")
    parser.add_argument("--resume", action="store_true")
    args = parser.parse_args()

    existing = set()
    if args.resume and os.path.exists(args.output):
        with open(args.output) as f:
            reader = csv.reader(f)
            next(reader, None)
            for row in reader:
                if row:
                    existing.add(row[0])

    images = sorted([
        f for f in os.listdir(args.input)
        if f.lower().endswith((".png", ".jpg", ".jpeg", ".gif"))
        and f not in existing
    ])

    if not images:
        print("No unlabeled images found.")
        return

    os.makedirs(os.path.dirname(args.output) or ".", exist_ok=True)
    mode = "a" if args.resume and os.path.exists(args.output) else "w"
    csvfile = open(args.output, mode, newline="")
    writer = csv.writer(csvfile)
    if mode == "w":
        writer.writerow(["filename", "label"])

    print(f"Labeling {len(images)} images. Type the text you see, 's' to skip, 'q' to quit.\n")

    labeled = 0
    for i, fname in enumerate(images):
        fpath = os.path.join(args.input, fname)
        print(f"[{i+1}/{len(images)}]")
        show_image_terminal(fpath)

        answer = input("  Label: ").strip()
        if answer.lower() == "q":
            break
        if answer.lower() == "s" or not answer:
            continue

        writer.writerow([fname, answer])
        csvfile.flush()
        labeled += 1

    csvfile.close()
    print(f"\nLabeled {labeled} images. Saved to {args.output}")


if __name__ == "__main__":
    main()
