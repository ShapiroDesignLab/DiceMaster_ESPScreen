import argparse
import os
import sys

def parse_arguments():
    parser = argparse.ArgumentParser(
        description='Convert a PNG file to a C header file with a uint8_t array of its byte values.'
    )
    parser.add_argument('input_png', help='Path to the input PNG file.')
    parser.add_argument(
        '-o', '--output',
        help='Path to the output .h file. If not specified, uses the input file name with .h extension.'
    )
    parser.add_argument(
        '-n', '--array_name',
        help='Name of the array in the header file. Defaults to the input file name without extension.',
        default=None
    )
    parser.add_argument(
        '-g', '--guard',
        help='Custom include guard name. If not specified, it is derived from the array name.',
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
    lines = []
    lines.append(f'#ifndef {guard_name}\n')
    lines.append(f'#define {guard_name}\n\n')
    lines.append(f'const uint8_t {array_name}[] = {{\n')
    
    # Format the array elements, 12 per line for readability
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

def main():
    args = parse_arguments()
    
    input_png = args.input_png
    output_h = args.output
    array_name = args.array_name
    guard_name = args.guard
    
    if not output_h:
        base_name = os.path.splitext(os.path.basename(input_png))[0]
        output_h = f"{base_name}.h"
    
    if not array_name:
        base_name = os.path.splitext(os.path.basename(input_png))[0]
        array_name = sanitize_identifier(base_name)
    else:
        array_name = sanitize_identifier(array_name)
    
    if not guard_name:
        # Create a guard name based on the output file name
        guard_base = os.path.splitext(os.path.basename(output_h))[0].upper()
        guard_name = f"{guard_base}_H"
    
    byte_data = read_png_bytes(input_png)
    byte_list = list(byte_data)
    header_content = generate_header_content(array_name, byte_list, guard_name)
    write_header_file(output_h, header_content)

if __name__ == "__main__":
    main()
