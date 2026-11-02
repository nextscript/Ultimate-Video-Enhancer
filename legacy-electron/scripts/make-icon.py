"""Erzeugt das UVE-Player-Icon (build/icon.png, build/icon.ico, src/renderer/assets/icon.png)."""
import os
from PIL import Image, ImageDraw, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
S = 1024


def make():
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    # Hintergrund: abgerundetes Quadrat mit Verlauf Orange -> Magenta
    grad = Image.new("RGBA", (S, S))
    gd = ImageDraw.Draw(grad)
    for y in range(S):
        t = y / S
        r = int(255 * (1 - t) + 214 * t)
        g = int(150 * (1 - t) + 48 * t)
        b = int(30 * (1 - t) + 120 * t)
        gd.line([(0, y), (S, y)], fill=(r, g, b, 255))
    mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(mask).rounded_rectangle([40, 40, S - 40, S - 40], radius=220, fill=255)
    img.paste(grad, (0, 0), mask)

    d = ImageDraw.Draw(img)
    # Innerer dunkler "Bildschirm"
    d.rounded_rectangle([150, 190, S - 150, S - 190], radius=90, fill=(14, 16, 22, 255))
    # Play-Dreieck
    cx, cy = S // 2 + 30, S // 2
    tri = [(cx - 150, cy - 190), (cx - 150, cy + 190), (cx + 190, cy)]
    glow = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    ImageDraw.Draw(glow).polygon(tri, fill=(255, 160, 60, 200))
    img.alpha_composite(glow.filter(ImageFilter.GaussianBlur(28)))
    d.polygon(tri, fill=(255, 255, 255, 255))
    # Funkeln (Enhancer)
    def star(x, y, r, col):
        d.polygon([(x, y - r), (x + r * 0.22, y - r * 0.22), (x + r, y), (x + r * 0.22, y + r * 0.22),
                   (x, y + r), (x - r * 0.22, y + r * 0.22), (x - r, y), (x - r * 0.22, y - r * 0.22)], fill=col)
    star(S - 250, 250, 110, (255, 255, 255, 255))
    star(S - 380, 330, 46, (255, 230, 190, 255))
    return img


def main():
    img = make()
    os.makedirs(os.path.join(ROOT, "build"), exist_ok=True)
    os.makedirs(os.path.join(ROOT, "src", "renderer", "assets"), exist_ok=True)
    img.resize((512, 512), Image.LANCZOS).save(os.path.join(ROOT, "build", "icon.png"))
    img.resize((256, 256), Image.LANCZOS).save(os.path.join(ROOT, "src", "renderer", "assets", "icon.png"))
    img.save(os.path.join(ROOT, "build", "icon.ico"), sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
    print("Icons erzeugt.")


if __name__ == "__main__":
    main()
