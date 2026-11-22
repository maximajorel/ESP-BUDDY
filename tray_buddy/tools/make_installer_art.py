#!/usr/bin/env python3
"""Genera las imágenes del instalador NSIS con la geometría del diseño.

Son dos mapas de bits que NSIS exige y que no se pueden hacer con CSS:

  installer/header.bmp    150x57   franja de arriba del asistente
  installer/sidebar.bmp   164x314  columna lateral de la bienvenida

Los dibuja el mismo código que el icono (papel, tinta, ojos): `make_icons.py`
es la fuente de la marca, así que el instalador y la bandeja no se pueden
desincronizar a mano. El amarillo es `--sun` (#ffd731) y el trazo es `--ink`.

BMP de 24 bits sin compresión y filas de abajo hacia arriba: es lo que NSIS
sabe leer, y no acepta PNG.

Uso:  python3 tools/make_installer_art.py     (escribe en installer/)
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import make_icons  # noqa: E402  (necesita el sys.path de arriba)

OUT_DIR = os.path.join(HERE, "..", "installer")

SUN = (0xFF, 0xD7, 0x31)
INK = (0x00, 0x00, 0x00)
PAPER = (0xFF, 0xFF, 0xFF)
MIST = (0xE9, 0xE9, 0xE9)

HEADER = (150, 57)
SIDEBAR = (164, 314)


def blank(width, height, color=PAPER):
    return [[color for _ in range(width)] for _ in range(height)]


def rect(pixels, left, top, right, bottom, color):
    """Rectángulo relleno, bordes inclusivos."""
    for y in range(max(0, top), min(len(pixels), bottom + 1)):
        row = pixels[y]
        for x in range(max(0, left), min(len(row), right + 1)):
            row[x] = color


def frame(pixels, color=INK):
    height = len(pixels)
    width = len(pixels[0])
    rect(pixels, 0, 0, width - 1, 0, color)
    rect(pixels, 0, height - 1, width - 1, height - 1, color)
    rect(pixels, 0, 0, 0, height - 1, color)
    rect(pixels, width - 1, 0, width - 1, height - 1, color)


def eyes_mark(pixels, left, top, size):
    """El cuadrado amarillo con los dos ojos, a cualquier tamaño.

    Se muestrea el maestro de `make_icons` en lugar de redibujar los ojos: la
    proporción 6x7 al 38% y el borde de 1 unidad viven en un solo archivo.
    """
    master = make_icons.render_master()
    source = make_icons.MASTER
    for y in range(size):
        for x in range(size):
            mx = int(x * source / size)
            my = int(y * source / size)
            offset = mx * 4
            pixel = master[my][offset : offset + 4]
            r, g, b = pixel[0], pixel[1], pixel[2]
            # El maestro es translúcido en los bordes por el promedio de caja:
            # se compone sobre papel para no dejar halos.
            alpha = pixel[3] / 255.0
            out = (
                int(r * alpha + PAPER[0] * (1 - alpha)),
                int(g * alpha + PAPER[1] * (1 - alpha)),
                int(b * alpha + PAPER[2] * (1 - alpha)),
            )
            ty = top + y
            tx = left + x
            if 0 <= ty < len(pixels) and 0 <= tx < len(pixels[0]):
                pixels[ty][tx] = out


def bmp_bytes(pixels):
    height = len(pixels)
    width = len(pixels[0])
    stride = (width * 3 + 3) & ~3  # cada fila se alinea a 4 bytes
    padding = b"".join(b"\x00" for _ in range(stride - width * 3))
    rows = []
    # BMP guarda las filas de abajo hacia arriba y en BGR.
    for y in range(height - 1, -1, -1):
        row = bytearray()
        for r, g, b in pixels[y]:
            row += bytes((b, g, r))
        rows.append(bytes(row) + padding)
    body = b"".join(rows)

    header = (
        b"BM"
        + (14 + 40 + len(body)).to_bytes(4, "little")  # tamaño total
        + (0).to_bytes(4, "little")  # reservado
        + (54).to_bytes(4, "little")  # offset de los píxeles
    )
    info = (
        (40).to_bytes(4, "little")
        + width.to_bytes(4, "little", signed=True)
        + height.to_bytes(4, "little", signed=True)
        + (1).to_bytes(2, "little")  # planos
        + (24).to_bytes(2, "little")  # bits por píxel
        + (0).to_bytes(4, "little")  # sin compresión
        + len(body).to_bytes(4, "little")
        + (2835).to_bytes(4, "little", signed=True)  # 72 dpi
        + (2835).to_bytes(4, "little", signed=True)
        + (0).to_bytes(4, "little")
        + (0).to_bytes(4, "little")
    )
    return header + info + body


def header_pixels():
    width, height = HEADER
    pixels = blank(width, height, PAPER)
    # Una franja de papel con la marca a la izquierda y tres reglas de tinta
    # que imitan la lista del popover, sin texto: el texto del asistente lo
    # pone NSIS y traducirlo acá sería duplicarlo.
    eyes_mark(pixels, 12, 9, 38)
    for i, length in enumerate((56, 44, 30)):
        top = 16 + i * 10
        rect(pixels, 62, top, 62 + length, top, INK)
    frame(pixels)
    return pixels


def sidebar_pixels():
    width, height = SIDEBAR
    pixels = blank(width, height, PAPER)
    # La losa amarilla de arriba es la marca a tamaño de afiche; abajo, un
    # bloque de papel con borde de tinta y una barra de tinta, el vocabulario
    # de stickers del sistema.
    rect(pixels, 16, 16, width - 17, 118, SUN)
    rect(pixels, 16, 16, width - 17, 16, INK)
    rect(pixels, 16, 118, width - 17, 118, INK)
    rect(pixels, 16, 16, 16, 118, INK)
    rect(pixels, width - 17, 16, width - 17, 118, INK)
    eyes_mark(pixels, (width - 64) // 2, 35, 64)

    rect(pixels, 16, 140, width - 17, 236, PAPER)
    frame_rect(pixels, 16, 140, width - 17, 236)
    eyes_mark(pixels, 28, 152, 28)
    for i, length in enumerate((74, 96, 60)):
        top = 196 + i * 12
        rect(pixels, 28, top, 28 + length, top, INK)

    rect(pixels, 16, 258, width - 17, 276, MIST)
    frame_rect(pixels, 16, 258, width - 17, 276)
    frame(pixels)
    return pixels


def frame_rect(pixels, left, top, right, bottom, color=INK):
    rect(pixels, left, top, right, top, color)
    rect(pixels, left, bottom, right, bottom, color)
    rect(pixels, left, top, left, bottom, color)
    rect(pixels, right, top, right, bottom, color)


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    for name, pixels in (
        ("header.bmp", header_pixels()),
        ("sidebar.bmp", sidebar_pixels()),
    ):
        path = os.path.join(OUT_DIR, name)
        with open(path, "wb") as fh:
            fh.write(bmp_bytes(pixels))
        print(f"{name}: {os.path.getsize(path)} bytes")


if __name__ == "__main__":
    main()
