#!/bin/sh
# Generate a small uncompressed 24-bit BMP for framebuffer testing.

python3 - <<'PY'
import struct

width = 300
height = 200
bytes_per_pixel = 3
row_size = ((width * bytes_per_pixel + 3) // 4) * 4
pixel_data_size = row_size * height
file_header_size = 14
dib_header_size = 40
pixel_offset = file_header_size + dib_header_size
file_size = pixel_offset + pixel_data_size

def bgr(color):
    r, g, b = color
    return bytes((b, g, r))

def pixel_at(x, y):
    # y is top-down here. BMP positive height stores rows bottom-up later.
    if y < height // 2:
        if x < width // 3:
            return bgr((255, 0, 0))
        if x < width * 2 // 3:
            return bgr((0, 255, 0))
        return bgr((0, 0, 255))

    if x < width // 3:
        return bgr((0, 0, 0))
    if x < width * 2 // 3:
        return bgr((255, 255, 255))
    return bgr((128, 128, 128))

with open("test.bmp", "wb") as f:
    f.write(b"BM")
    f.write(struct.pack("<IHHI", file_size, 0, 0, pixel_offset))
    f.write(struct.pack("<IiiHHIIiiII",
                        dib_header_size,
                        width,
                        height,
                        1,
                        24,
                        0,
                        pixel_data_size,
                        0,
                        0,
                        0,
                        0))

    padding = b"\x00" * (row_size - width * bytes_per_pixel)

    # Positive-height BMP stores the bottom row first.
    for y in range(height - 1, -1, -1):
        row = bytearray()
        for x in range(width):
            row.extend(pixel_at(x, y))
        row.extend(padding)
        f.write(row)

print("generated test.bmp: 300x200, 24-bit, uncompressed")
PY
