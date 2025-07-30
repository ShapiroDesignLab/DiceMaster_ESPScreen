#!/usr/bin/env python3
import os
import sys
import argparse
from PIL import Image
import imageio

def crop_center_square(img: Image.Image) -> Image.Image:
    """
    Crop the largest possible square from the center of the image.
    """
    w, h = img.size
    side = min(w, h)
    left = (w - side) // 2
    top = (h - side) // 2
    return img.crop((left, top, left + side, top + side))

def process_image(input_path: str):
    """
    Crop center square and resize to 480×480, then save as JPEG with _480 suffix in same folder.
    """
    img = Image.open(input_path)
    img = crop_center_square(img)
    img = img.resize((480, 480), Image.LANCZOS)

    dirpath = os.path.dirname(input_path)
    base = os.path.splitext(os.path.basename(input_path))[0]
    out_name = f"{base}_480.jpg"
    out_path = os.path.join(dirpath, out_name)

    img.save(out_path, "JPEG", quality=80)
    print(f"[+] Saved cropped & resized image to {out_path}")

def process_sequence(input_path: str):
    """
    Extract frames from GIF/video, crop center square, resize to 240×240, compress, and save in a new folder named after the file.
    """
    base = os.path.splitext(os.path.basename(input_path))[0]
    output_folder = os.path.join(os.getcwd(), base)
    os.makedirs(output_folder, exist_ok=True)

    reader = imageio.get_reader(input_path)
    for idx, frame in enumerate(reader):
        img = Image.fromarray(frame)
        img = crop_center_square(img)
        img = img.resize((240, 240), Image.LANCZOS)
        out_path = os.path.join(output_folder, f"{idx}.jpg")
        img.save(out_path, "JPEG", quality=30)
        if (idx + 1) % 50 == 0:
            print(f"  ↳ Processed {idx+1} frames…")
    print(f"[+] Done: {idx+1} frames saved in {output_folder}/")

def main():
    parser = argparse.ArgumentParser(
        description=(
            "Crop from center and resize a single image to 480×480 "
            "or decode a GIF/video into compressed 240×240 JPEG frames."
        )
    )
    parser.add_argument(
        "input",
        help="Path to input file (JPG, PNG, GIF, MP4, etc.)"
    )
    args = parser.parse_args()
    input_path = args.input
    if not os.path.isfile(input_path):
        print(f"Error: file not found: {input_path}", file=sys.stderr)
        sys.exit(1)

    ext = os.path.splitext(input_path)[1].lower()
    if ext in (".jpg", ".jpeg", ".png"):
        process_image(input_path)
    else:
        process_sequence(input_path)

if __name__ == "__main__":
    main()
