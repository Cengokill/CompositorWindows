"""Build the frozen subject_input fixtures from protocol.json.

The original protocol, generator and fixtures-02 tree were never committed.
This reconstructs the documented oracle: Pillow triangle/bilinear resample
(Resample.c bb1d8e8) with 22-bit coefficients, two-pass byte rounding, then
ImageNet mean/std, matching src/imaging/subject_input.cpp.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import struct
from pathlib import Path

SIZE = 1024
PRECISION = 22
MAXIMUM_KERNEL = 61
MEANS = (0.485, 0.456, 0.406)
DEVIATIONS = (0.229, 0.224, 0.225)
SHA = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()


def kernel_size(input_size: int) -> int:
    return int(math.ceil(max(1.0, input_size / SIZE))) * 2 + 1


def axis(input_size: int) -> tuple[int, list[tuple[int, int]], list[int]]:
    kernel = kernel_size(input_size)
    bounds = []
    coefficients = [0] * (SIZE * kernel)
    scale = input_size / SIZE
    support = max(1.0, scale)
    inverse = 1.0 / support
    for i in range(SIZE):
        center = (i + 0.5) * scale
        first = max(0, int(center - support + 0.5))
        count = min(input_size, int(center + support + 0.5)) - first
        bounds.append((first, count))
        weights = []
        total = 0.0
        for j in range(count):
            distance = abs((j + first - center + 0.5) * inverse)
            weight = 1.0 - distance if distance < 1.0 else 0.0
            weights.append(weight)
            total += weight
        for j in range(count):
            coefficients[i * kernel + j] = int(0.5 + (weights[j] / total) * (1 << PRECISION))
    return kernel, bounds, coefficients


def straight(pixel: bytes, channel: int) -> int:
    alpha = pixel[3]
    if alpha == 0:
        return 0
    return min(255, (pixel[channel] * 255 + alpha // 2) // alpha)


def rounded(total: int) -> int:
    return max(0, min(255, total >> PRECISION))


def put_pixel(buffer: bytearray, stride: int, x: int, y: int, r: int, g: int, b: int, a: int) -> None:
    offset = y * stride + x * 4
    buffer[offset] = min(r, a)
    buffer[offset + 1] = min(g, a)
    buffer[offset + 2] = min(b, a)
    buffer[offset + 3] = a


def fill_source(width: int, height: int, stride: int, pattern: str) -> bytearray:
    pixels = bytearray(stride * height)
    if pattern == "padded":
        for i in range(len(pixels)):
            pixels[i] = 0x5A
    for y in range(height):
        for x in range(width):
            if pattern == "transparent":
                put_pixel(pixels, stride, x, y, 0, 0, 0, 0)
            elif pattern == "constant":
                put_pixel(pixels, stride, x, y, 40, 80, 160, 255)
            elif pattern == "alpha_gradient":
                alpha = 64 + ((x * 191) // max(1, width - 1))
                put_pixel(
                    pixels, stride, x, y,
                    (x * alpha) // max(1, width - 1),
                    (y * alpha) // max(1, height - 1),
                    ((x + y) * alpha) // max(1, width + height - 2),
                    alpha,
                )
            elif pattern == "stripes":
                tone = 32 + ((x + y) % 8) * 24
                put_pixel(pixels, stride, x, y, tone, 255 - tone, (x * 255) // max(1, width - 1), 255)
            else:
                put_pixel(
                    pixels, stride, x, y,
                    (x * 255) // max(1, width - 1),
                    (y * 255) // max(1, height - 1),
                    ((x * y) * 255) // max(1, (width - 1) * (height - 1)),
                    255,
                )
    return pixels


def resample(width: int, height: int, stride: int, pixels: bytearray) -> tuple[bytes, bytes]:
    x_kernel, x_bounds, x_coeff = axis(width)
    y_kernel, y_bounds, y_coeff = axis(height)
    vertical_first = height > width * 100 and height > SIZE
    rows = bytearray(y_kernel * SIZE * 3)
    tags = [-1] * y_kernel
    rgb = bytearray(SIZE * SIZE * 3)
    tensor = bytearray(SIZE * SIZE * 3 * 4)
    vertical_row = bytearray(SIZE * 3)

    def sample_row(source_y: int) -> None:
        slot = source_y % y_kernel
        if tags[slot] == source_y:
            return
        destination = slot * SIZE * 3
        row = memoryview(pixels)[source_y * stride:(source_y + 1) * stride]
        if vertical_first:
            for x in range(width):
                pixel = bytes(row[x * 4:x * 4 + 4])
                for channel in range(3):
                    rows[destination + x * 3 + channel] = straight(pixel, channel)
        else:
            for x in range(SIZE):
                first, count = x_bounds[x]
                sums = [1 << (PRECISION - 1)] * 3
                weights = x_coeff[x * x_kernel:]
                for j in range(count):
                    pixel = bytes(row[(first + j) * 4:(first + j) * 4 + 4])
                    weight = weights[j]
                    for channel in range(3):
                        sums[channel] += straight(pixel, channel) * weight
                for channel in range(3):
                    rows[destination + x * 3 + channel] = rounded(sums[channel])
        tags[slot] = source_y

    for y in range(SIZE):
        first, count = y_bounds[y]
        for source_y in range(first, first + count):
            sample_row(source_y)
        weights = y_coeff[y * y_kernel:]
        vertical_width = width if vertical_first else SIZE
        for x in range(vertical_width):
            sums = [1 << (PRECISION - 1)] * 3
            for j in range(count):
                source = ((first + j) % y_kernel) * SIZE * 3 + x * 3
                for channel in range(3):
                    sums[channel] += rows[source + channel] * weights[j]
            for channel in range(3):
                value = rounded(sums[channel])
                if vertical_first:
                    vertical_row[x * 3 + channel] = value
                else:
                    rgb[(y * SIZE + x) * 3 + channel] = value
                    packed = (value / 255.0 - MEANS[channel]) / DEVIATIONS[channel]
                    struct.pack_into("<f", tensor, (channel * SIZE * SIZE + y * SIZE + x) * 4, packed)
        if vertical_first:
            for x in range(SIZE):
                first_x, count_x = x_bounds[x]
                sums = [1 << (PRECISION - 1)] * 3
                weights_x = x_coeff[x * x_kernel:]
                for j in range(count_x):
                    for channel in range(3):
                        sums[channel] += vertical_row[(first_x + j) * 3 + channel] * weights_x[j]
                for channel in range(3):
                    value = rounded(sums[channel])
                    rgb[(y * SIZE + x) * 3 + channel] = value
                    packed = (value / 255.0 - MEANS[channel]) / DEVIATIONS[channel]
                    struct.pack_into("<f", tensor, (channel * SIZE * SIZE + y * SIZE + x) * 4, packed)
    return bytes(rgb), bytes(tensor)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parent / "fixtures-02")
    args = parser.parse_args()
    base = Path(__file__).resolve().parent
    protocol_path = base / "protocol.json"
    protocol = json.loads(protocol_path.read_text(encoding="utf-8"))
    output = args.output
    output.mkdir(parents=True, exist_ok=True)
    rows = []
    for fixture in protocol["fixtures"]:
        directory = output / fixture["id"]
        directory.mkdir(parents=True, exist_ok=True)
        source = fill_source(fixture["width"], fixture["height"], fixture["stride"], fixture["pattern"])
        source_path = directory / "source.rgba"
        source_path.write_bytes(source)
        rgb, tensor = resample(fixture["width"], fixture["height"], fixture["stride"], source)
        rgb_path = directory / "expected.rgb"
        f32_path = directory / "expected.f32"
        rgb_path.write_bytes(rgb)
        f32_path.write_bytes(tensor)
        meta = {
            "id": fixture["id"],
            "width": fixture["width"],
            "height": fixture["height"],
            "stride": fixture["stride"],
            "pattern": fixture["pattern"],
        }
        meta_path = directory / "fixture.json"
        meta_path.write_text(json.dumps(meta, indent=2) + "\n", encoding="utf-8")
        files = []
        for path in (source_path, meta_path, rgb_path, f32_path):
            relative = path.relative_to(output).as_posix()
            files.append({"path": relative, "bytes": path.stat().st_size, "sha256": SHA(path)})
        rows.append({"id": fixture["id"], "files": files})
    manifest = {
        "protocol_sha256": SHA(protocol_path),
        "generator_sha256": SHA(Path(__file__)),
        "fixtures": rows,
    }
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print("Wrote", output / "manifest.json")


if __name__ == "__main__":
    main()
