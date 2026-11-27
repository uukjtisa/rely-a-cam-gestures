"""Draws resources/icon.ico from the same geometry as resources/icon.svg (Pillow).

The SVG is the source of truth for the window icon; Windows needs an .ico for the exe, the
installer and the Start menu. Re-run this after changing the SVG:

    python -I scripts/make_icon.py
"""
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
SIZES = [16, 20, 24, 32, 40, 48, 64, 128, 256]
BLUE, WHITE, YELLOW = (0x28, 0x4C, 0xB8, 255), (255, 255, 255, 255), (0xE5, 0xAF, 0x24, 255)


def draw(size: int) -> Image.Image:
    # Drawn 8x larger on the SVG's 64-unit grid, then scaled down, for smooth edges.
    k = size * 8 / 64
    img = Image.new("RGBA", (size * 8, size * 8), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([2 * k, 2 * k, 62 * k, 62 * k], radius=13 * k, fill=BLUE)
    # Ring: centre (29, 35), radius 16, stroke 6 (so 13..19 from the centre).
    d.ellipse([(29 - 19) * k, (35 - 19) * k, (29 + 19) * k, (35 + 19) * k], fill=WHITE)
    d.ellipse([(29 - 13) * k, (35 - 13) * k, (29 + 13) * k, (35 + 13) * k], fill=BLUE)
    d.ellipse([(29 - 5) * k, (35 - 5) * k, (29 + 5) * k, (35 + 5) * k], fill=WHITE)
    star = [(50, 6), (53, 14), (61, 17), (53, 20), (50, 28), (47, 20), (39, 17), (47, 14)]
    d.polygon([(x * k, y * k) for x, y in star], fill=YELLOW)
    return img.resize((size, size), Image.LANCZOS)


def main() -> None:
    big = draw(256)
    out = ROOT / "resources" / "icon.ico"
    big.save(out, format="ICO", sizes=[(s, s) for s in SIZES])
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
