import argparse

MAX_WORDS_PER_LINE = 16  # This results in 32 bytes per line


def binary_to_c_array(
    input_file, output_file, array_name, endian_format, rom_number=4, offset=0
):
    # Read binary file
    with open(input_file, "rb") as f:
        data = f.read()

    # Ensure that the binary data has an even length (since we're handling words)
    if len(data) % 2 != 0:
        raise ValueError(
            "The binary file size should be an even number of bytes for word processing."
        )

    # Start generating C-style array content
    content = f'const uint16_t {array_name}[] __attribute__((aligned(4), section(".rom{rom_number}"))) = {{\n'

    # Convert binary data to comma-separated hex values with MAX_WORDS_PER_LINE words per line
    for i in range(offset, len(data) - offset, 2 * MAX_WORDS_PER_LINE):
        chunk = data[i : i + 2 * MAX_WORDS_PER_LINE]

        if endian_format == "little":
            words = [chunk[j] + (chunk[j + 1] << 8) for j in range(0, len(chunk), 2)]
        else:  # big endian
            words = [(chunk[j] << 8) + chunk[j + 1] for j in range(0, len(chunk), 2)]

        content += "    " + ", ".join(f"0x{word:04X}" for word in words) + ",\n"

    # Remove the trailing comma and add closing brace
    content = content.rstrip(",\n") + "\n};\n"
    content += f"uint16_t {array_name}_length = sizeof({array_name}) / sizeof({array_name}[0]);\n"

    # Write to output .h file
    with open(output_file, "w") as f:
        f.write(content)

    print(f"{output_file} generated successfully!")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Convert a binary file to a C array of words."
    )
    parser.add_argument("input_file", type=str, help="Path to the binary file.")
    parser.add_argument(
        "output_file", type=str, help="Path to the output C header file."
    )
    parser.add_argument("array_name", type=str, help="Name for the C array.")
    parser.add_argument(
        "-e",
        "--endian",
        choices=["little", "big"],
        default="little",
        help="Endian format of the data (default is little-endian).",
    )
    parser.add_argument(
        "-r",
        "--rom",
        choices=["3", "4"],
        default="4",
        help="ROM number to allocate the binary file.",
    )
    parser.add_argument(
        "-s",
        "--steem",
        action="store_const",
        const=4,
        default=0,
        help="Remove the default offset of 4 bytes for Steem compatibility.",
    )
    args = parser.parse_args()

    binary_to_c_array(
        args.input_file,
        args.output_file,
        args.array_name,
        args.endian,
        args.rom,
        args.steem,
    )
