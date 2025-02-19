#!/usr/bin/env python3
import argparse
import math
from PIL import Image


NUM_CIRCLE_IMAGES = 24

def process_image(input_path, output_path, circle):
    # Open the input image
    img = Image.open(input_path)
    
    # Optionally, you might want to verify the image is 480x480
    if img.size != (480, 480):
        print("Warning: Input image is not 480x480. Continuing anyway.")
    
    # Resize the image to 240x240
    resized = img.resize((240, 240), Image.LANCZOS)

    if not circle:
        # Save the single 240x240 image
        resized.save(output_path, format="JPEG")
    else:
        # For a clock-face, use 24 positions, one every 15 degrees.
        # We'll treat 0° as 12 o'clock (i.e. upward) and increase clockwise.
        navy = (0, 0, 128)
        for i in range(24):
            # Compute the angle in degrees (0°, 15°, 30°, …)
            angle_deg = i * 360 // NUM_CIRCLE_IMAGES
            # Compute the offset vector.
            # For a clock face, at 0° the offset should be (0, -60) (i.e. 60 pixels upward).
            # Use: dx = 60*sin(angle) and dy = -60*cos(angle)
            rad = math.radians(angle_deg)
            dx = int(round(60 * math.sin(rad)))
            dy = int(round(-60 * math.cos(rad)))

            # Create a navy blue canvas
            canvas = Image.new("RGB", (240, 240), navy)

            # When pasting the 240x240 image with top-left at (dx, dy),
            # parts may lie outside the canvas.
            # Compute the overlapping region between the canvas and the shifted image.
            # Destination coordinates on the canvas:
            dst_x0 = max(0, dx)
            dst_y0 = max(0, dy)
            dst_x1 = min(240, dx + 240)
            dst_y1 = min(240, dy + 240)
            # Corresponding source coordinates on the resized image:
            src_x0 = max(0, -dx)
            src_y0 = max(0, -dy)
            src_x1 = src_x0 + (dst_x1 - dst_x0)
            src_y1 = src_y0 + (dst_y1 - dst_y0)

            # Crop the appropriate region from the resized image
            cropped = resized.crop((src_x0, src_y0, src_x1, src_y1))
            # Paste it onto the canvas at the computed destination offset
            canvas.paste(cropped, (dst_x0, dst_y0))

            # Save the result. The filename is built from the given output_path.
            # For example, if output_path is "output.jpg", images will be saved as "output_00.jpg", "output_01.jpg", etc.
            base, ext = output_path.rsplit(".", 1)
            output_filename = f"{base}_{i:02d}.{ext}"
            canvas.save(output_filename, format="JPEG")
            print(f"Saved: {output_filename}")

def main():
    parser = argparse.ArgumentParser(
        description="Resize a 480x480 JPEG to 240x240. If --circle is given, produce 24 images with a circular offset."
    )
    parser.add_argument("input", help="Path to the input 480x480 JPEG image")
    parser.add_argument("output", help="Path for the output JPEG image (or base name if using --circle)")
    parser.add_argument("--circle", action="store_true", help=f"Generate {NUM_CIRCLE_IMAGES} images with circular offsets")
    
    args = parser.parse_args()
    process_image(args.input, args.output, args.circle)

if __name__ == "__main__":
    main()
