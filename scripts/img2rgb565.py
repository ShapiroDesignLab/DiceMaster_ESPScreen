#!/usr/bin/env python
import sys
import os
from PIL import Image

def convert_image_to_rgb565_data(input_image_path):
    """
    Open an image file, resize it to 480x480, convert to RGB mode,
    and compute the RGB565 value for each pixel.
    Returns a list of strings with each value formatted as '0xXXXX'.
    """
    if not os.path.isfile(input_image_path):
        print(f"File {input_image_path} does not exist.")
        return None

    # Accept jpg, jpeg, and png (you can add more if needed)
    supported_formats = ['png', 'jpeg', 'jpg']
    ext = input_image_path.split('.')[-1].lower()
    if ext not in supported_formats:
        print(f"Unsupported file format for {input_image_path}")
        return None

    # Load image, resize and convert to RGB
    img = Image.open(input_image_path).resize((480, 480))
    img_rgb = img.convert('RGB')

    data_rgb565 = []
    for y in range(img_rgb.height):
        for x in range(img_rgb.width):
            r, g, b = img_rgb.getpixel((x, y))
            # Compute 16-bit RGB565 value
            rgb565 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
            data_rgb565.append(f'0x{rgb565:04X}')
    return data_rgb565

def write_array_to_file(f, array_name, data):
    """
    Write a C array declaration given the variable name and data list.
    Formats the array with 16 items per line.
    """
    array_length = len(data)
    f.write(f'const uint16_t {array_name}[{array_length}] = {{\n')
    for i in range(0, array_length, 16):
        # Create a line with at most 16 elements
        line = ', '.join(data[i:i+16])
        if i + 16 < array_length:
            f.write(f'    {line},\n')
        else:
            f.write(f'    {line}\n')
    f.write('};\n\n')

def process_file_mode(image_path):
    """
    Process a single image file.
    """
    data = convert_image_to_rgb565_data(image_path)
    if data is None:
        return

    filename_no_ext = os.path.splitext(os.path.basename(image_path))[0]
    # Replace any dashes or spaces in the variable name
    array_name = filename_no_ext.replace('-', '_').replace(' ', '_')
    output_filename = f"rgb565_{filename_no_ext}.h"
    with open(output_filename, 'w') as f:
        write_array_to_file(f, array_name, data)
    print(f"File converted and saved as {output_filename}")

def process_directory_mode(directory_path):
    """
    Process all jpg/jpeg files in the given directory (non-recursive), sort them
    in alphabetical order and write each converted image as a separate C array.
    Also, write out an array of pointers and an array of array sizes.
    """
    # Find all .jpg and .jpeg files (case-insensitive) in the directory
    files = [f for f in os.listdir(directory_path)
             if os.path.isfile(os.path.join(directory_path, f)) and f.lower().endswith(('.jpg', '.jpeg'))]
    files.sort()  # Sort files alphabetically

    if not files:
        print("No JPG files found in the directory.")
        return

    # Derive a base name from the directory name, cleaning any dash or space characters.
    base_name = os.path.basename(os.path.normpath(directory_path)).replace('-', '_').replace(' ', '_')
    output_filename = f"rgb565_{base_name}.h"

    # Store sizes of converted data arrays for later use in a separate size array.
    array_sizes = []
    num_images = 0

    with open(output_filename, 'w') as f:
        # Process each file and output its array definition.
        for idx, file in enumerate(files):
            full_path = os.path.join(directory_path, file)
            data = convert_image_to_rgb565_data(full_path)
            if data is None:
                continue
            array_var_name = f"{base_name}_{idx:02d}"
            write_array_to_file(f, array_var_name, data)
            array_sizes.append(len(data))
            num_images += 1

        # Write an array of pointers to all image arrays.
        f.write(f'const uint16_t* {base_name}_array[{num_images}] = {{\n')
        pointer_lines = []
        for idx in range(num_images):
            pointer_lines.append(f'    {base_name}_{idx:02d}')
        f.write(',\n'.join(pointer_lines))
        f.write('\n};\n\n')

        # Write an array of sizes (number of elements) for each image array.
        f.write(f'const size_t {base_name}_sizes[{num_images}] = {{\n')
        size_lines = []
        for size in array_sizes:
            size_lines.append(f'    {size}')
        f.write(',\n'.join(size_lines))
        f.write('\n};\n')
    print(f"Processed {num_images} images. Output saved to {output_filename}")

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python script.py <image_path_or_directory>")
    else:
        input_path = sys.argv[1]
        if os.path.isdir(input_path):
            process_directory_mode(input_path)
        elif os.path.isfile(input_path):
            process_file_mode(input_path)
        else:
            print("Provided path is neither a valid file nor directory.")

            
# # ChatGPT Output
# import sys
# from PIL import Image
# import os

# def convert_image_to_rgb565(input_image_path):
#     # Check if the file exists
#     if not os.path.isfile(input_image_path):
#         print("File does not exist.")
#         return
    
#     # Check if the file format is supported
#     supported_formats = ['png', 'jpeg', 'jpg']
#     file_format = input_image_path.split('.')[-1].lower()
#     if file_format not in supported_formats:
#         print("Unsupported file format.")
#         return
    
#     # Load and resize image
#     img = Image.open(input_image_path).resize((480, 480))
#     img_rgb = img.convert('RGB')
    
#     # Convert to RGB565
#     data_rgb565 = []
#     for y in range(img_rgb.height):
#         for x in range(img_rgb.width):
#             r, g, b = img_rgb.getpixel((x, y))
#             rgb565 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
#             data_rgb565.append(f'0x{rgb565:04X}')
    
#     # Determine array name from file name
#     filename_no_ext = os.path.splitext(os.path.basename(input_image_path))[0]
#     output_filename = f"rgb565_{filename_no_ext}.h"
#     array_name = filename_no_ext.replace('-', '_').replace(' ', '_')
    
#     # Generate C header file
#     with open(output_filename, 'w') as f:
#         f.write(f'const uint16_t {array_name}[230400] = {{\n')
#         for i in range(0, len(data_rgb565), 16):
#             line = ', '.join(str(data_rgb565[j]) for j in range(i, min(i + 16, len(data_rgb565))))
#             if i + 16 < len(data_rgb565):
#                 f.write(f'    {line},\n')
#             else:
#                 f.write(f'    {line}\n')
#         f.write('};\n')
    
#     print(f"File converted and saved as {output_filename}")

# if __name__ == "__main__":
#     if len(sys.argv) != 2:
#         print("Usage: python script.py <image_path>")
#     else:
#         convert_image_to_rgb565(sys.argv[1])
