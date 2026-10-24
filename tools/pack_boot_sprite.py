#!/usr/bin/env python3
"""Pack the supplied skull sprite into firmware data (requires Pillow).

Usage: python3 tools/pack_boot_sprite.py /path/to/assets/skull-sprite.png
The HTML/CSS reference is not needed by the device; only eight 1-bit poses.
"""
from pathlib import Path
import hashlib
import sys
from PIL import Image

source = Path(sys.argv[1])
sheet = Image.open(source).convert("RGBA")
assert sheet.size == (640, 45), "Expected eight 80 x 45 poses"
background = Image.new("RGBA", sheet.size, (0, 0, 0, 255))
background.alpha_composite(sheet)
gray = background.convert("L")
frames = []
for frame in range(8):
    data = []
    for y in range(45):
        for byte_x in range(10):
            byte = 0
            for bit in range(8):
                if gray.getpixel((frame * 80 + byte_x * 8 + bit, y)) >= 128:
                    byte |= 0x80 >> bit
            data.append(byte)
    frames.append(data)

text = """#pragma once
// Converted from the user's skull-glitch/assets/skull-sprite.png reference.
// Eight 80 x 45 poses. 1-bit MSB-first; 10 bytes per row; white on black.
// RGB is composited against black before thresholding: transparent white is off.
// Regenerate with tools/pack_boot_sprite.py and the original supplied PNG.
"""
text += "// Source SHA256: " + hashlib.sha256(source.read_bytes()).hexdigest() + "\n"
text += "#include <Arduino.h>\n\nconst uint8_t skull_boot_frames[8][450] PROGMEM = {\n"
for data in frames:
    text += "  {\n"
    for start in range(0, len(data), 15):
        text += "    " + ",".join(f"0x{x:02X}" for x in data[start:start + 15]) + ",\n"
    text += "  },\n"
text += "};\n"
target = Path(__file__).resolve().parents[1] / "sharkdiv/SkullBootFrames.h"
target.write_text(text)
print(f"Packed {sum(map(len, frames))} bytes into {target.name}")
