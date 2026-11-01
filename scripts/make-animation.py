"""Combine native --render-motion frames into README GIF; requires Pillow."""
from pathlib import Path
import sys
from PIL import Image

frames = [Image.open(path).convert("RGB") for path in sorted(Path(sys.argv[1]).glob("*.png"))]
if len(frames) != 31:
    raise ValueError("Expected 31 native render frames")
frames[0].save(sys.argv[2], save_all=True, append_images=frames[1:],
               duration=[300] + [40]*29 + [1400], loop=0, optimize=False)
