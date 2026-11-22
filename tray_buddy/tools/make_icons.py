#!/usr/bin/env python3
"""Genera los iconos del Buddy desde la geometría del diseño, no desde un PNG
dibujado a mano.

El icono de la bandeja es la marca que ya usa el panel web: cuadrado amarillo
(`--sun`, #ffd731) de 26 unidades con borde de tinta de 1 unidad y dos ojos
negros de 6x7 con las esquinas redondeadas al 38%. Ver `design/tray-buddy/01-popover-bandeja.html`
(bloque `.tray-buddy`).

Se dibuja un maestro de 1024x1024 y se reduce por promedio de caja, así los
tamaños chicos (16 px, el de la barra de tareas) quedan legibles y no con un ojo
de un píxel.

Uso:  python3 tools/make_icons.py      (escribe en src-tauri/icons/)
"""

import os
import struct
import zlib

DESIGN = 26.0  # lado del cuadrado en unidades de diseño
MASTER = 1024
BORDER = 1.0  # borde de tinta
EYE_W, EYE_H = 6.0, 7.0
EYE_GAP = 3.0
EYE_R = 0.38  # border-radius: 38%

# El papel del icono no es blanco: es el amarillo del sistema (--sun,
# #ffd731), el mismo que ya pinta `.brand .logo` en el popover y el logo del
# panel web. El dueño lo pidió así y con los ojos negros, que ya lo eran.
PAPER = (255, 215, 49, 255)  # --sun
INK = (0, 0, 0, 255)

HERE = os.path.dirname(os.path.abspath(__file__))
OUT_DIR = os.path.join(HERE, "..", "src-tauri", "icons")


def _rounded_rect_inside(u, v, left, top, right, bottom, rx, ry):
    qx = max(left + rx - u, u - (right - rx), 0.0)
    qy = max(top + ry - v, v - (bottom - ry), 0.0)
    if qx == 0.0 and qy == 0.0:
        return True
    return (qx / rx) ** 2 + (qy / ry) ** 2 <= 1.0


def render_master():
    """Maestro RGBA de MASTER x MASTER con el cuadrado, el borde y los ojos."""
    scale = MASTER / DESIGN
    rows = []
    eye_w, eye_h = EYE_W, EYE_H
    gap = EYE_GAP
    # Los dos ojos van centrados: el ancho total es 2*ancho + separación.
    total = 2 * eye_w + gap
    left_eye_x = (DESIGN - total) / 2.0
    right_eye_x = left_eye_x + eye_w + gap
    eye_top = (DESIGN - eye_h) / 2.0
    eye_bottom = eye_top + eye_h
    rx, ry = eye_w * EYE_R, eye_h * EYE_R
    inner = BORDER

    for y in range(MASTER):
        v = (y + 0.5) / scale
        row = bytearray()
        for x in range(MASTER):
            u = (x + 0.5) / scale
            if u < inner or u > DESIGN - inner or v < inner or v > DESIGN - inner:
                row += bytes(INK)
                continue
            inside_eye = _rounded_rect_inside(
                u, v, left_eye_x, eye_top, left_eye_x + eye_w, eye_bottom, rx, ry
            ) or _rounded_rect_inside(
                u, v, right_eye_x, eye_top, right_eye_x + eye_w, eye_bottom, rx, ry
            )
            row += bytes(INK if inside_eye else PAPER)
        rows.append(bytes(row))
    return rows


def downsample(rows, size):
    """Promedio de caja del maestro a `size`; el maestro es opaco, así que
    promediar los cuatro canales alcanza."""
    step = MASTER // size
    out = []
    for oy in range(size):
        row = bytearray()
        for ox in range(size):
            r = g = b = a = 0
            for dy in range(step):
                src = rows[oy * step + dy]
                base = ox * step * 4
                for dx in range(step):
                    i = base + dx * 4
                    r += src[i]
                    g += src[i + 1]
                    b += src[i + 2]
                    a += src[i + 3]
            n = step * step  # un promedio por canal: píxeles sumados
            row += bytes((r // n, g // n, b // n, a // n))
        out.append(bytes(row))
    return bytes(b"".join(out))


def png_bytes(size, raw):
    def chunk(tag, data):
        return (
            struct.pack(">I", len(data))
            + tag
            + data
            + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
        )

    stride = size * 4
    scanlines = b"".join(
        b"\x00" + raw[y * stride : (y + 1) * stride] for y in range(size)
    )
    ihdr = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)
    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", ihdr)
        + chunk(b"IDAT", zlib.compress(scanlines, 9))
        + chunk(b"IEND", b"")
    )


def ico_bytes(entries):
    """ICO con payload PNG por entrada (soportado desde Vista; es lo que espera
    Windows para 256x256 y lo que Tauri empaqueta sin conversión)."""
    count = len(entries)
    header = struct.pack("<HHH", 0, 1, count)
    directory = b""
    blob = b""
    offset = 6 + 16 * count
    for size, payload in entries:
        dim = 0 if size >= 256 else size
        directory += struct.pack(
            "<BBBBHHII", dim, dim, 0, 0, 1, 32, len(payload), offset
        )
        offset += len(payload)
        blob += payload
    return header + directory + blob


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    master = render_master()
    sizes = [16, 24, 32, 48, 64, 128, 256, 512]
    rendered = {}
    for size in sizes:
        rendered[size] = png_bytes(size, downsample(master, size))

    named = {
        32: "32x32.png",
        128: "128x128.png",
        256: "128x128@2x.png",
        512: "icon.png",
    }
    for size, name in named.items():
        with open(os.path.join(OUT_DIR, name), "wb") as fh:
            fh.write(rendered[size])

    with open(os.path.join(OUT_DIR, "tray.png"), "wb") as fh:
        fh.write(rendered[32])

    ico = ico_bytes([(s, rendered[s]) for s in (16, 24, 32, 48, 256)])
    with open(os.path.join(OUT_DIR, "icon.ico"), "wb") as fh:
        fh.write(ico)

    for name in list(named.values()) + ["tray.png", "icon.ico"]:
        path = os.path.join(OUT_DIR, name)
        print(f"{name}: {os.path.getsize(path)} bytes")


if __name__ == "__main__":
    main()
