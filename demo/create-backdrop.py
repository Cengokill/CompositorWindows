"""Render the original vector-style artwork used by the editing demonstration."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

root = Path(__file__).resolve().parent
image = Image.new("RGB", (1600, 1000), "#eee9db")
draw = ImageDraw.Draw(image)
fonts = Path("C:/Windows/Fonts")
serif = lambda size: ImageFont.truetype(str(fonts / "georgia.ttf"), size)
sans = lambda size: ImageFont.truetype(str(fonts / "segoeui.ttf"), size)
ink = "#183d35"
draw.rectangle((850, 0, 1599, 999), fill="#bfcca9")
draw.ellipse((695, 118, 1565, 988), fill="#d9c96e")
draw.ellipse((822, 248, 1438, 864), outline="#e8deaa", width=2)
draw.line((80, 88, 1520, 88), fill=ink, width=2)
draw.text((80, 43), "FIELD NOTES     /     001", fill=ink, font=sans(23))
draw.text((1274, 43), "BOTANICAL STUDIES", fill=ink, font=sans(19))
for text, y in [("After", 198), ("the", 365), ("wind.", 532)]:
    draw.text((75, y), text, fill=ink, font=serif(168), stroke_width=0)
draw.line((82, 817, 570, 817), fill=ink, width=2)
draw.text((82, 841), "A study in light, shape and possibility.", fill=ink, font=sans(26))
draw.text((82, 933), "COMPOSITOR  /  WINDOWS COMMUNITY PREVIEW", fill=ink, font=sans(18))
image.save(root / "01-backdrop.png")
