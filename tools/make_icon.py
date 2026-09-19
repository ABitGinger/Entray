#!/usr/bin/env python3
"""生成 resources/icon.ico。

Entray 的图标是程序化绘制的，好处是每个尺寸都能单独调整笔画的粗细，
不会像“一张大图缩下来”那样在 16x16 上糊成一团。改设计只要改这个脚本重跑：

    python tools/make_icon.py

设计：绿色圆角方块 + 白色“从托盘启动”图形（向上三角 + 底座横条）。
"""

from __future__ import annotations

import io
import struct
import sys
from pathlib import Path

from PIL import Image, ImageDraw

# 由浅到深的两段绿色，做竖直渐变。
GRADIENT_TOP = (61, 220, 132)
GRADIENT_BOTTOM = (17, 158, 82)

# ICO 里包含的尺寸。Windows 会按显示场景自己挑最合适的一张。
SIZES = (16, 20, 24, 32, 40, 48, 64, 128, 256)

# 每个尺寸的超采样倍数，用来做抗锯齿。
SUPERSAMPLE = 8

OUTPUT = Path(__file__).resolve().parent.parent / "resources" / "icon.ico"


def _lerp(a: float, b: float, t: float) -> float:
    return a + (b - a) * t


def _gradient(size: int) -> Image.Image:
    """竖直渐变底。"""
    image = Image.new("RGB", (size, size))
    pixels = image.load()
    for y in range(size):
        t = y / max(1, size - 1)
        color = tuple(int(round(_lerp(GRADIENT_TOP[i], GRADIENT_BOTTOM[i], t))) for i in range(3))
        for x in range(size):
            pixels[x, y] = color
    return image


def render(size: int) -> Image.Image:
    """按目标尺寸渲染一张 RGBA 图标。"""
    scale = SUPERSAMPLE if size <= 64 else 4
    canvas = size * scale

    # 内容在方形里留多少边距：尺寸越小，图形要越满、笔画要越粗，
    # 否则 16x16 上会显得又小又虚。
    if size <= 24:
        radius = 0.20
        apex_y, base_y = 0.185, 0.615
        half_width = 0.345
        bar_top, bar_bottom = 0.700, 0.820
        bar_half = 0.300
        bar_radius = 0.055
    elif size <= 48:
        radius = 0.215
        apex_y, base_y = 0.215, 0.620
        half_width = 0.320
        bar_top, bar_bottom = 0.705, 0.800
        bar_half = 0.280
        bar_radius = 0.048
    else:
        radius = 0.220
        apex_y, base_y = 0.235, 0.620
        half_width = 0.300
        bar_top, bar_bottom = 0.705, 0.785
        bar_half = 0.265
        bar_radius = 0.040

    # ---- 绿色圆角方块的遮罩 ----
    mask = Image.new("L", (canvas, canvas), 0)
    ImageDraw.Draw(mask).rounded_rectangle(
        (0, 0, canvas - 1, canvas - 1), radius=radius * canvas, fill=255
    )

    # ---- 白色图形 ----
    glyph = Image.new("L", (canvas, canvas), 0)
    draw = ImageDraw.Draw(glyph)

    centre = canvas / 2
    draw.polygon(
        [
            (centre, apex_y * canvas),
            (centre - half_width * canvas, base_y * canvas),
            (centre + half_width * canvas, base_y * canvas),
        ],
        fill=255,
    )
    draw.rounded_rectangle(
        (
            centre - bar_half * canvas,
            bar_top * canvas,
            centre + bar_half * canvas,
            bar_bottom * canvas,
        ),
        radius=bar_radius * canvas,
        fill=255,
    )

    # 超高分辨率下渲染再缩回来，等于免费拿到抗锯齿和柔化的圆角。
    #
    # 注意这里是「把遮罩单独缩小、再在目标尺寸上合成」，而不是直接把 RGBA 大图缩下来：
    # LANCZOS 在非预乘 alpha 下插值会让圆角边缘发暗，出现一圈脏边。
    mask = mask.resize((size, size), Image.LANCZOS)
    glyph = glyph.resize((size, size), Image.LANCZOS)

    icon = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    icon.paste(_gradient(size).convert("RGBA"), (0, 0), mask)
    white = Image.new("RGBA", (size, size), (255, 255, 255, 255))
    icon.paste(white, (0, 0), glyph)
    return icon


def _bmp_frame(image: Image.Image) -> bytes:
    """把一张 RGBA 图编码成 ICO 内部的 32 位 DIB 帧。"""
    width, height = image.size
    pixels = image.load()

    # XOR 位图：BGRA、自下而上。
    rows = []
    for y in range(height - 1, -1, -1):
        row = bytearray()
        for x in range(width):
            red, green, blue, alpha = pixels[x, y]
            row += bytes((blue, green, red, alpha))
        rows.append(bytes(row))
    xor_bitmap = b"".join(rows)

    # AND 掩码：32 位帧靠 alpha 通道决定透明，这里全 0 即可，但每行必须 4 字节对齐。
    row_stride = ((width + 31) // 32) * 4
    and_mask = b"\x00" * (row_stride * height)

    header = struct.pack(
        "<IiiHHIIiiII",
        40,               # biSize
        width,            # biWidth
        height * 2,       # biHeight：XOR + AND 两张图叠起来的高度
        1,                # biPlanes
        32,               # biBitCount
        0,                # biCompression = BI_RGB
        len(xor_bitmap) + len(and_mask),  # biSizeImage
        0,                # biXPelsPerMeter
        0,                # biYPelsPerMeter
        0,                # biClrUsed
        0,                # biClrImportant
    )
    return header + xor_bitmap + and_mask


# 从这个尺寸起改用 PNG 压缩存帧。不压缩的话 256x256 光裸位图就 256KB，
# 白白让 exe 胖一大圈。Vista 之后的 Windows 都能正常读 PNG 帧。
PNG_FROM_SIZE = 64


def _png_frame(image: Image.Image) -> bytes:
    buffer = io.BytesIO()
    image.save(buffer, format="PNG", optimize=True)
    return buffer.getvalue()


def _frame(image: Image.Image) -> bytes:
    if image.size[0] >= PNG_FROM_SIZE:
        return _png_frame(image)
    return _bmp_frame(image)


def write_ico(path: Path, sizes=SIZES) -> None:
    frames = [_frame(render(size)) for size in sizes]

    directory = struct.pack("<HHH", 0, 1, len(sizes))
    offset = len(directory) + 16 * len(sizes)
    entries = b""
    for size, frame in zip(sizes, frames):
        entries += struct.pack(
            "<BBBBHHII",
            0 if size >= 256 else size,  # bWidth，0 表示 256
            0 if size >= 256 else size,  # bHeight
            0,                           # bColorCount：32 位图不需要调色板
            0,                           # bReserved
            1,                           # wPlanes
            32,                          # wBitCount
            len(frame),                  # dwBytesInRes
            offset,                      # dwImageOffset
        )
        offset += len(frame)

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(directory + entries + b"".join(frames))


def main() -> int:
    write_ico(OUTPUT)
    print(f"已生成 {OUTPUT}（{OUTPUT.stat().st_size} 字节，{len(SIZES)} 个尺寸）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
