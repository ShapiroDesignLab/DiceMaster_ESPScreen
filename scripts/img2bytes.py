#!/usr/bin/env python
import argparse
import os
import sys

def parse_arguments():
    # In the parse_arguments() function, update the description (if desired):
    parser = argparse.ArgumentParser(
        description='Convert a PNG/JPG file to a C header file with a uint8_t array of its byte values.'
    )
    parser.add_argument('input_path', help='Path to the input PNG file or directory containing PNG files.')
    parser.add_argument(
        '-o', '--output',
        help='Path to the output .h file. If not specified, the input file or directory name (with .h extension) is used.'
    )
    parser.add_argument(
        '-n', '--array_name',
        help='Base name of the array(s) in the header file. For a directory input, each array gets a numeric suffix. '
             'Defaults to the input file name (without extension) or directory name.',
        default=None
    )
    parser.add_argument(
        '-g', '--guard',
        help='Custom include guard name. If not specified, it is derived from the output file name.',
        default=None
    )
    return parser.parse_args()

def read_png_bytes(file_path):
    try:
        with open(file_path, 'rb') as f:
            return f.read()
    except IOError as e:
        print(f"Error reading file {file_path}: {e}")
        sys.exit(1)

def generate_header_content(array_name, byte_list, guard_name):
    """Generate header content for a single PNG file."""
    lines = []
    lines.append(f'#ifndef {guard_name}\n')
    lines.append(f'#define {guard_name}\n\n')
    lines.append(f'const uint8_t {array_name}[] = {{\n')
    
    # Format the array elements, 12 per line for readability.
    elements_per_line = 12
    for i in range(0, len(byte_list), elements_per_line):
        line_bytes = byte_list[i:i+elements_per_line]
        line = '    ' + ', '.join(f'0x{byte:02X}' for byte in line_bytes)
        if i + elements_per_line < len(byte_list):
            line += ',\n'
        else:
            line += '\n'
        lines.append(line)
    
    lines.append('};\n\n')
    lines.append(f'const size_t {array_name}_SIZE = sizeof({array_name}) / sizeof({array_name}[0]);\n\n')
    lines.append(f'#endif // {guard_name}\n')
    return ''.join(lines)

def write_header_file(output_path, content):
    try:
        with open(output_path, 'w') as f:
            f.write(content)
        print(f"Header file successfully written to {output_path}")
    except IOError as e:
        print(f"Error writing to file {output_path}: {e}")
        sys.exit(1)

def sanitize_identifier(name):
    # Replace invalid characters with underscores
    return ''.join(c if c.isalnum() or c == '_' else '_' for c in name)

def process_file_mode(input_file, output_h, array_name, guard_name):
    byte_data = read_png_bytes(input_file)
    byte_list = list(byte_data)
    header_content = generate_header_content(array_name, byte_list, guard_name)
    write_header_file(output_h, header_content)

def process_directory_mode(directory_path, output_h, base_array_name, guard_name):
    # Find all .png files (case-insensitive) in the given directory (non-recursive)
    files = [f for f in os.listdir(directory_path)
         if os.path.isfile(os.path.join(directory_path, f)) and f.lower().endswith(('.png', '.jpg', '.jpeg'))]
    files.sort()  # Sort alphabetically

    if not files:
        print("No PNG files found in the directory.")
        sys.exit(1)

    header_lines = []
    header_lines.append(f'#ifndef {guard_name}\n')
    header_lines.append(f'#define {guard_name}\n\n')

    array_sizes = []
    array_names = []

    # Process each PNG file and output its byte array with a numeric suffix.
    for idx, file in enumerate(files):
        full_path = os.path.join(directory_path, file)
        byte_data = read_png_bytes(full_path)
        byte_list = list(byte_data)
        current_array_name = f"{base_array_name}_{idx:02d}"
        array_names.append(current_array_name)
        array_sizes.append(len(byte_list))
        
        header_lines.append(f'const uint8_t {current_array_name}[] = {{\n')
        elements_per_line = 12
        for i in range(0, len(byte_list), elements_per_line):
            line_bytes = byte_list[i:i+elements_per_line]
            line = '    ' + ', '.join(f'0x{b:02X}' for b in line_bytes)
            if i + elements_per_line < len(byte_list):
                line += ',\n'
            else:
                line += '\n'
            header_lines.append(line)
        header_lines.append('};\n\n')

    # Write an array of pointers to all byte arrays.
    header_lines.append(f'const uint8_t* {base_array_name}_array[{len(array_names)}] = {{\n')
    for idx, name in enumerate(array_names):
        if idx < len(array_names) - 1:
            header_lines.append(f'    {name},\n')
        else:
            header_lines.append(f'    {name}\n')
    header_lines.append('};\n\n')

    # Write an array of sizes (number of elements) for each byte array.
    header_lines.append(f'const size_t {base_array_name}_sizes[{len(array_names)}] = {{\n')
    for idx, size in enumerate(array_sizes):
        if idx < len(array_sizes) - 1:
            header_lines.append(f'    {size},\n')
        else:
            header_lines.append(f'    {size}\n')
    header_lines.append('};\n\n')

    header_lines.append(f'#endif // {guard_name}\n')
    header_content = ''.join(header_lines)
    write_header_file(output_h, header_content)

def main():
    args = parse_arguments()
    input_path = args.input_path
    output_h = args.output
    array_name = args.array_name
    guard_name = args.guard

    # For directory input: if output file is not provided, default to the directory name with .h extension.
    if os.path.isdir(input_path):
        if not output_h:
            base_name = os.path.basename(os.path.normpath(input_path))
            output_h = f"{base_name}.h"
        if not array_name:
            base_name = os.path.basename(os.path.normpath(input_path))
            array_name = sanitize_identifier(base_name)
        else:
            array_name = sanitize_identifier(array_name)
        if not guard_name:
            guard_base = os.path.splitext(os.path.basename(output_h))[0].upper()
            guard_name = f"{guard_base}_H"
        process_directory_mode(input_path, output_h, array_name, guard_name)
    elif os.path.isfile(input_path):
        if not output_h:
            base_name = os.path.splitext(os.path.basename(input_path))[0]
            output_h = f"{base_name}.h"
        if not array_name:
            base_name = os.path.splitext(os.path.basename(input_path))[0]
            array_name = sanitize_identifier(base_name)
        else:
            array_name = sanitize_identifier(array_name)
        if not guard_name:
            guard_base = os.path.splitext(os.path.basename(output_h))[0].upper()
            guard_name = f"{guard_base}_H"
        process_file_mode(input_path, output_h, array_name, guard_name)
    else:
        print("Provided path is neither a valid file nor directory.")
        sys.exit(1)

if __name__ == "__main__":
    main()
    