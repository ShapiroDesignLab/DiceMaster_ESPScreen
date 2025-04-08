#!/usr/bin/env python3
import argparse
import glob
import os

def combine_h_files(input_dir, output_file):
    # Use glob to get all .h files in the input directory
    h_files = glob.glob(os.path.join(input_dir, "*.h"))
    # Exclude the output file if it exists in the list
    output_abs = os.path.abspath(output_file)
    h_files = [f for f in h_files if os.path.abspath(f) != output_abs]
    h_files.sort()  # optional: sort files alphabetically for consistent output

    with open(output_file, "w") as outfile:
        for h_file in h_files:
            # Write a comment marking the start of a file's content
            outfile.write(f"// Begin content from: {os.path.basename(h_file)}\n")
            with open(h_file, "r") as infile:
                outfile.write(infile.read())
            # Write a comment marking the end of a file's content
            outfile.write(f"\n// End content from: {os.path.basename(h_file)}\n\n")

def main():
    parser = argparse.ArgumentParser(description="Combine all .h files into a single .h file.")
    parser.add_argument("input_dir", nargs="?", default=".",
                        help="Directory to search for .h files (default: current directory)")
    parser.add_argument("-o", "--output", default="combined.h",
                        help="Output file name (default: combined.h)")
    args = parser.parse_args()

    combine_h_files(args.input_dir, args.output)
    print(f"Combined .h files have been written to '{args.output}'.")

if __name__ == "__main__":
    main()
