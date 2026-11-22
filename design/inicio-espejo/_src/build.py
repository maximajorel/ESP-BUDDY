#!/usr/bin/env python3
"""Builds the entrance mirror artboards.

One stylesheet (`_src/core.css`, the landing and login subset of the real
`styles.css`, values unchanged) and one icon sprite, shared by every board:

  01-inicio  the entrance itself
  02-menu    the full-screen menu open
  03-juego   the ta-te-ti board
  04-login   the login gate

Run: python3 _src/build.py
"""

import pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / "_src"

# slug, title, <body> class, parts in order, viewport width, viewport height
BOARDS = [
    ("01-inicio", "Inicio", "landing-body", ["page"], 390, 1280),
    ("02-menu", "Menú de apps y pantallas", "landing-body menu-open", ["page", "launcher"], 390, 844),
    ("03-juego", "Ta-te-ti", "landing-body game-open", ["page", "game"], 390, 844),
    ("04-login", "Login", "login-body", ["login"], 390, 844),
    ("05-inicio-desktop", "Inicio en escritorio", "landing-body", ["page"], 1440, 1120),
]


def build():
    template = (SRC / "template.tmpl").read_text(encoding="utf-8")
    core = (SRC / "core.css").read_text(encoding="utf-8").rstrip()
    sprite = (SRC / "sprite.svg").read_text(encoding="utf-8").rstrip()
    parts = {
        "page": (SRC / "page.part").read_text(encoding="utf-8").rstrip(),
        "launcher": (SRC / "launcher.part").read_text(encoding="utf-8").rstrip(),
        "game": (SRC / "game.part").read_text(encoding="utf-8").rstrip(),
        "login": (SRC / "login.part").read_text(encoding="utf-8").rstrip(),
    }

    for slug, title, body_class, names, _w, _h in BOARDS:
        menu_open = "menu-open" in body_class
        content = "\n\n".join(
            parts[name].replace("{{MENU_EXPANDED}}", "true" if menu_open else "false")
            for name in names
        )
        html = (
            template.replace("{{CORE}}", core)
            .replace("{{SPRITE}}", sprite)
            .replace("{{CONTENT}}", content)
            .replace("{{BODYCLASS}}", body_class)
            .replace("{{TITLE}}", f"Sofía · {title}")
        )
        (ROOT / f"{slug}.html").write_text(html, encoding="utf-8")
        print(f"wrote {slug}.html")


if __name__ == "__main__":
    build()
