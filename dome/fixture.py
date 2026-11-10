import sys
from pathlib import Path
from bitmap_font import build_font
build_font(Path(__file__).resolve().parent.parent / 'tests/dome/fixture.bdf', 'DomeD Bitmap', sys.argv[1])
