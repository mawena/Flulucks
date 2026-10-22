#!/usr/bin/env python3
"""Rendu du logo Portail (SVG) en PNG, par librsvg (PyGObject) : icônes des mipmaps et aperçu.

Usage : render_icons.py <logo.svg> <dossier res/> [<aperçu.png>]
  écrit mipmap-{m,h,xh,xxh,xxxh}dpi/ic_launcher.png (48, 72, 96, 144, 192 px), ic_launcher_round.png
  (même logo dans un disque) et, si demandé, un aperçu de 512 px.
Depuis l'API 26, Android utilise l'icône adaptative (mipmap-anydpi-v26) : ces PNG ne servent que de
repli et d'aperçu.
"""
import math
import os
import sys

import gi

gi.require_version("Rsvg", "2.0")
import cairo  # noqa: E402
from gi.repository import Rsvg  # noqa: E402

SIZES = {"mdpi": 48, "hdpi": 72, "xhdpi": 96, "xxhdpi": 144, "xxxhdpi": 192}


def render(svg, size, out, round_mask=False):
    handle = Rsvg.Handle.new_from_file(svg)
    surface = cairo.ImageSurface(cairo.FORMAT_ARGB32, size, size)
    ctx = cairo.Context(surface)
    if round_mask:
        ctx.arc(size / 2, size / 2, size / 2, 0, 2 * math.pi)
        ctx.clip()
    vp = Rsvg.Rectangle()
    vp.x, vp.y, vp.width, vp.height = 0, 0, size, size
    handle.render_document(ctx, vp)
    surface.write_to_png(out)


def main(argv):
    if len(argv) < 3:
        print(__doc__, file=sys.stderr)
        return 2
    svg, res = argv[1], argv[2]
    for dpi, px in SIZES.items():
        d = os.path.join(res, "mipmap-" + dpi)
        os.makedirs(d, exist_ok=True)
        render(svg, px, os.path.join(d, "ic_launcher.png"))
        render(svg, px, os.path.join(d, "ic_launcher_round.png"), round_mask=True)
    if len(argv) > 3:
        render(svg, 512, argv[3])
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
