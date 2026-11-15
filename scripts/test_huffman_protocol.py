#!/usr/bin/env python3
"""Assert the Android/ESP32 canonical Huffman codebooks and bitstream agree."""
import random
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
cpp = (ROOT / "src/navigation_state.cpp").read_text()
start = cpp.index("kHuffLengths[256]")
block = cpp[cpp.index("{", start) + 1:cpp.index("};", start)]
lengths = [int(value) for value in re.findall(r"\d+", block)]
assert len(lengths) == 256

kotlin = (ROOT / "android/app/src/main/java/com/esp32/googlemaps/util/NavigationBinaryProtocol.kt").read_text()
start = kotlin.index("CODES = longArrayOf(")
block = kotlin[kotlin.index("(", start) + 1:kotlin.index("\n    )", start)]
android_codes = [int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]+)L", block)]
assert len(android_codes) == 256

codes = {}
code = 0
for length in range(1, 16):
    for symbol, symbol_length in enumerate(lengths):
        if symbol_length == length:
            codes[symbol] = (code, length)
            code += 1
    code <<= 1
assert len(codes) == 256 and code == 1 << 16
assert sum(2.0 ** -length for length in lengths) == 1.0
assert all((android_codes[symbol] >> 4, android_codes[symbol] & 0xF) == codes[symbol]
           for symbol in range(256))

counts = [0] * 16
for length in lengths:
    counts[length] += 1
first_code = [0] * 16
first_symbol = [0] * 16
symbols = [0] * 256
code = offset = 0
for length in range(1, 16):
    first_code[length], first_symbol[length] = code, offset
    for symbol, symbol_length in enumerate(lengths):
        if symbol_length == length:
            symbols[offset] = symbol
            offset += 1
    code = (code + counts[length]) << 1
assert offset == 256 and code == 1 << 16


def encode(data):
    output = bytearray()
    accumulator = bits = 0
    for symbol in data:
        value, length = codes[symbol]
        accumulator = (accumulator << length) | value
        bits += length
        while bits >= 8:
            bits -= 8
            output.append((accumulator >> bits) & 0xFF)
        accumulator &= (1 << bits) - 1
    if bits:
        output.append((accumulator << (8 - bits)) & 0xFF)
    return bytes(output)


def decode(data, size):
    output = bytearray()
    bit_position = 0
    for _ in range(size):
        value = 0
        for length in range(1, 16):
            if bit_position >= len(data) * 8:
                raise ValueError("truncated Huffman stream")
            value = (value << 1) | ((data[bit_position >> 3] >> (7 - (bit_position & 7))) & 1)
            bit_position += 1
            index = value - first_code[length]
            if 0 <= index < counts[length]:
                output.append(symbols[first_symbol[length] + index])
                break
        else:
            raise ValueError("invalid Huffman code")
    if (bit_position + 7) // 8 != len(data):
        raise ValueError("trailing Huffman data")
    if bit_position & 7 and data[-1] & ((1 << (8 - (bit_position & 7))) - 1):
        raise ValueError("nonzero Huffman padding")
    return bytes(output)


def row_diff(icon):
    assert len(icon) == 128
    output = bytearray(icon)
    for row in range(1, 32):
        for column in range(4):
            index = row * 4 + column
            output[index] = icon[index] ^ icon[index - 4]
    return bytes(output)


def row_restore(encoded):
    assert len(encoded) == 128
    output = bytearray(encoded)
    for row in range(1, 32):
        for column in range(4):
            index = row * 4 + column
            output[index] ^= output[index - 4]
    return bytes(output)


rng = random.Random(0xAA55)
for _ in range(3000):
    data = bytes(rng.choice((rng.randrange(256), 0, 0xFF, 32, 48, 97))
                 for _ in range(rng.randint(12, 400)))
    assert decode(encode(data), len(data)) == data
for _ in range(300):
    icon = bytes(rng.randrange(256) for _ in range(128))
    assert row_restore(row_diff(icon)) == icon
    assert decode(encode(row_diff(icon)), 128) == row_diff(icon)
print("Huffman tables match; 3000 bitstream and 300 icon row-filter roundtrips pass.")
