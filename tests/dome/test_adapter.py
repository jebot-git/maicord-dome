import importlib.util
import struct
import sys
import tempfile
import unittest
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'dome'))
from bitmap_font import build_font, read_bdf, png, checksum
from launch import environment

class BitmapAdapterTests(unittest.TestCase):
    def test_sfnt_contains_bitmap_strikes_and_no_outlines(self):
        with tempfile.TemporaryDirectory() as temp:
            target = Path(temp)/'bitmap.ttf'
            result = build_font(ROOT/'tests/dome/fixture.bdf', 'DomeD Bitmap', target)
            data = target.read_bytes()
            count = struct.unpack_from('>H', data, 4)[0]
            tables = {struct.unpack_from('>4sIII', data, 12+i*16)[0] for i in range(count)}
            self.assertTrue({b'CBLC',b'CBDT',b'DOMB',b'cmap',b'hmtx'} <= tables)
            self.assertFalse({b'glyf',b'CFF ',b'CFF2',b'COLR',b'fpgm',b'prep'} & tables)
            self.assertEqual(checksum(data), 0xb1b0afba)
            self.assertEqual(result['glyphs'], 2)
    def test_source_metrics_and_pixels(self):
        size, ascent, descent, glyphs=read_bdf(ROOT/'tests/dome/fixture.bdf')
        self.assertEqual((size,ascent,descent),(8,7,1))
        self.assertEqual(glyphs[65].rows,(0x70,0x88,0x88,0xf8,0x88,0x88,0x88))
        self.assertTrue(png(glyphs[65]).startswith(b'\x89PNG'))
    def test_profile_isolation_and_appearance_validation(self):
        import os
        from unittest.mock import patch
        with tempfile.TemporaryDirectory() as temp, patch.dict(os.environ, {k:temp for k in ['XDG_DATA_HOME','XDG_STATE_HOME','XDG_CACHE_HOME']}):
            env=environment({'light':True,'accent':'#abcdef','font_size':16,'font_scale':2},Path(temp))
            self.assertTrue(env['XDG_DATA_HOME'].endswith('/dome-discord'))
            self.assertEqual(env['DOMECORD_ZOOM'],'200')
            self.assertEqual(env['DOMECORD_ACCENT'],'#abcdef')
            self.assertEqual(environment({'accent':'bad'},Path(temp))['DOMECORD_ACCENT'],'#6babff')

if __name__=='__main__': unittest.main()
