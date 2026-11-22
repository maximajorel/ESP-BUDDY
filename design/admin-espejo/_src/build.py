#!/usr/bin/env python3
"""Builds the /admin mirror artboards.

Each artboard is one self-contained HTML file: the panel's own admin CSS
(styles.css, admin-facing rules only, values unchanged) inlined, the real icon
sprite, the real top bar and tab strip, and one tab panel per file. The only
thing this script adds is the active tab and the shell around the section.

Sources live in _src/ and are the only files edited by hand; the *.html files
next to design.json are generated. Run: python3 _src/build.py
"""

import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / "_src"

TABS = [
    ("aplicaciones", "i-apps", "Aplicaciones"),
    ("pantalla", "i-display", "Pantalla"),
    ("luz", "i-light", "Luz RGB"),
    ("juegos", "i-game", "Juegos"),
    ("spotify", "i-music", "Música"),
    ("SEP", None, None),
    ("integraciones", "i-plug", "Integraciones"),
    ("SEP", None, None),
    ("red", "i-wifi", "Red"),
    ("sofia", "i-smile", "Sofía"),
    ("SEP", None, None),
    ("sistema", "i-settings", "Sistema"),
    ("cuenta", "i-lock", "Cuenta"),
]

BOARDS = [
    ("01-aplicaciones", "aplicaciones", "Aplicaciones", 1024, 1440),
    ("02-pantalla", "pantalla", "Pantalla", 1024, 960),
    ("03-luz-rgb", "luz", "Luz RGB", 1024, 720),
    ("04-juegos", "juegos", "Juegos", 1024, 840),
    ("05-musica", "spotify", "Música", 1024, 640),
    ("06-integraciones", "integraciones", "Integraciones", 1024, 1080),
    ("07-red", "red", "Red", 1024, 820),
    ("08-sofia", "sofia", "Sofía", 1024, 900),
    ("09-sistema", "sistema", "Sistema", 1024, 1080),
    ("10-cuenta", "cuenta", "Cuenta", 1024, 760),
]

SCRIPTS = {
    "01-aplicaciones": ["drag-order.js"],
    "06-integraciones": ["subtabs.js"],
    "07-red": ["subtabs.js"],
    "08-sofia": ["sofia-states.js"],
}


def nav(active_id):
    out = []
    for tab_id, icon, label in TABS:
        if tab_id == "SEP":
            out.append('    <span class="tab-sep" aria-hidden="true"></span>')
            continue
        selected = "true" if tab_id == active_id else "false"
        cls = "nav-item active" if tab_id == active_id else "nav-item"
        tabindex = "0" if tab_id == active_id else "-1"
        out.append(
            f'    <button class="{cls}" id="tab-{tab_id}" type="button" role="tab" '
            f'aria-selected="{selected}" aria-controls="{tab_id}" tabindex="{tabindex}">'
            f'<svg class="nav-ico" aria-hidden="true"><use href="#{icon}"/></svg>'
            f"<span>{label}</span></button>"
        )
    return "\n".join(out)


def build():
    template = (SRC / "template.tmpl").read_text(encoding="utf-8")
    core = (SRC / "core.css").read_text(encoding="utf-8").rstrip()
    sprite = (SRC / "sprite.svg").read_text(encoding="utf-8").rstrip()
    for slug, active, title, _w, _h in BOARDS:
        content = (SRC / "sections" / f"{slug}.part").read_text(encoding="utf-8").rstrip()
        parts = ["theme.js"] + SCRIPTS.get(slug, [])
        scripts = [(SRC / "scripts" / name).read_text(encoding="utf-8").rstrip()
                   for name in parts]
        script = "\n".join(f"<script>\n{js}\n</script>" for js in scripts)
        html = (
            template.replace("{{CORE}}", core)
            .replace("{{SPRITE}}", sprite)
            .replace("{{NAV}}", nav(active))
            .replace("{{CONTENT}}", content)
            .replace("{{SCRIPT}}", script)
            .replace("{{TITLE}}", f"ESP BUDDY · {title}")
        )
        (ROOT / f"{slug}.html").write_text(html, encoding="utf-8")
        print(f"wrote {slug}.html")


if __name__ == "__main__":
    build()
