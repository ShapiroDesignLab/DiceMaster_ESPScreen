# ChatGPT Output
import sys
from PIL import Image
import os

def convert_image_to_rgb666(input_image_path):
    # Check if the file exists
    if not os.path.isfile(input_image_path):
        print("File does not exist.")
        return
    
    # Check if the file format is supported
    supported_formats = ['png', 'jpeg', 'jpg']
    file_format = input_image_path.split('.')[-1].lower()
    if file_format not in supported_formats:
        print("Unsupported file format.")
        return
    
    # Load and resize image
    img = Image.open(input_image_path).resize((480, 480))
    img_rgb = img.convert('RGB')
    
    # Convert to RGB666
    data_rgb666 = []
    for y in range(img_rgb.height):
        for x in range(img_rgb.width):
            r, g, b = img_rgb.getpixel((x, y))
            rgb666 = ((r >> 2) << 12) | ((g >> 2) << 6) | (b >> 2)
            data_rgb666.append(rgb666)
    
    # Determine array name from file name
    filename_no_ext = os.path.splitext(os.path.basename(input_image_path))[0]
    output_filename = f"rgb666_{filename_no_ext}.h"
    array_name = filename_no_ext.replace('-', '_').replace(' ', '_')
    
    # Generate C header file
    with open(output_filename, 'w') as f:
        f.write(f'const uint16_t {array_name}[230400] = {{\n')
        for i in range(0, len(data_rgb666), 16):
            line = ', '.join(str(data_rgb666[j]) for j in range(i, min(i + 16, len(data_rgb666))))
            if i + 16 < len(data_rgb666):
                f.write(f'    {line},\n')
            else:
                f.write(f'    {line}\n')
        f.write('};\n')
    
    print(f"File converted and saved as {output_filename}")

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python script.py <image_path>")
    else:
        convert_image_to_rgb666(sys.argv[1])
