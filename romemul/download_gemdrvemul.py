import urllib.request
import os
import argparse

MAX_WORDS_PER_LINE = 16  # This results in 32 bytes per line


def download_binary(url):
    with urllib.request.urlopen(url) as response:
        return response.read()


def read_binary_from_file(file_path):
    with open(file_path, "rb") as file:
        return file.read()


def binary_to_c_array(input_source, output_file, array_name, endian_format="little"):
    offset = 0

    # Check if input is a URL or file path
    if input_source.startswith(("http://", "https://")):
        data = download_binary(input_source)
    else:
        data = read_binary_from_file(input_source)

    # Ensure that the binary data has an even length (since we're handling words)
    if len(data) % 2 != 0:
        raise ValueError(
            "The binary file size should be an even number of bytes for word processing."
        )

    # Headers include
    content = """#include "include/firmware_gemdrvemul.h"\n\n"""

    # Start generating C-style array content
    content += f"const uint16_t {array_name}[] = {{\n"

    # Convert binary data to comma-separated hex values with MAX_WORDS_PER_LINE words per line
    for i in range(offset, len(data) - offset, 2 * MAX_WORDS_PER_LINE):
        chunk = data[i : i + 2 * MAX_WORDS_PER_LINE]

        if endian_format == "big":
            words = [chunk[j] + (chunk[j + 1] << 8) for j in range(0, len(chunk), 2)]
        else:  # little endian
            words = [(chunk[j] << 8) + chunk[j + 1] for j in range(0, len(chunk), 2)]

        content += "    " + ", ".join(f"0x{word:04X}" for word in words) + ",\n"

    # Remove the trailing comma and add closing brace
    content = content.rstrip(",\n") + "\n};\n"
    content += f"uint16_t {array_name}_length = sizeof({array_name}) / sizeof({array_name}[0]);\n\n"

    # Write to output .h file
    with open(output_file, "w") as f:
        f.write(content)

    print(f"{output_file} generated successfully!")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Generate C Array from binary file or URL"
    )
    parser.add_argument(
        "--input",
        default="https://github.com/diegoparrilla/atarist-sidecart-gemdrive/releases/download/latest/GEMDRIVE.BIN",
        help="Input URL or file path",
    )
    args = parser.parse_args()

    array_name = "gemdrvemulROM"
    output_file = "firmware_gemdrvemul.c"
    input_source = args.input

    binary_to_c_array(input_source, output_file, array_name)
