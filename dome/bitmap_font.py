#!/usr/bin/env python3
"""Losslessly wrap Unicode BDF strikes in SFNT/CBDT for MaiCord's bitmap renderer.
No outline conversion. PNGs contain only transparent or opaque white pixels.
"""
from dataclasses import dataclass
import gzip
import hashlib
from pathlib import Path
import struct
import zlib

@dataclass(frozen=True)
class Glyph:
    codepoint: int
    advance: int
    width: int
    height: int
    x: int
    y: int
    rows: tuple


def read_bdf(path):
    path = Path(path)
    raw = path.read_bytes()
    text = (gzip.decompress(raw) if path.suffix == '.gz' else raw).decode('utf-8', errors='replace')
    pixels, ascent, descent = 16, 14, 2
    glyphs = {}
    glyph = None
    bitmap = False
    for line in text.splitlines():
        fields = line.split()
        if not fields:
            continue
        key = fields[0]
        if key == 'PIXEL_SIZE': pixels = int(fields[1])
        elif key == 'FONT_ASCENT': ascent = int(fields[1])
        elif key == 'FONT_DESCENT': descent = int(fields[1])
        elif key == 'STARTCHAR': glyph = {'rows': []}; bitmap = False
        elif key == 'ENCODING' and glyph is not None: glyph['codepoint'] = int(fields[1])
        elif key == 'DWIDTH' and glyph is not None: glyph['advance'] = int(fields[1])
        elif key == 'BBX' and glyph is not None:
            glyph.update(zip(('width', 'height', 'x', 'y'), map(int, fields[1:])))
        elif key == 'BITMAP': bitmap = True
        elif key == 'ENDCHAR' and glyph is not None:
            bitmap = False
            if 0 <= glyph.get('codepoint', -1) <= 0x10ffff and not 0xd800 <= glyph['codepoint'] <= 0xdfff:
                glyph['rows'] = tuple(glyph['rows'])
                g = Glyph(**glyph)
                if not (0 <= g.width <= 128 and 0 <= g.height <= 128 and 0 <= g.advance <= 255
                        and -128 <= g.x <= 127 and -128 <= g.y + g.height <= 127
                        and len(g.rows) == g.height):
                    raise ValueError('Unsupported BDF metrics')
                glyphs[g.codepoint] = g
            glyph = None
        elif bitmap and glyph is not None:
            # BDF rows are MSB-aligned to a byte boundary.
            glyph['rows'].append(int(fields[0], 16) if fields[0] else 0)
    if not glyphs or not 1 <= pixels <= 127 or not 0 <= ascent <= 127 or not 0 <= descent <= 127:
        raise ValueError('Invalid BDF strike')
    return pixels, ascent, descent, glyphs


def png(g):
    width, height = max(1, g.width), max(1, g.height)
    rows = bytearray()
    bits = ((g.width + 7) // 8) * 8
    for y in range(height):
        rows.append(0)
        for x in range(width):
            on = x < g.width and y < g.height and (g.rows[y] >> (bits - x - 1)) & 1
            rows.extend(b'\xff\xff\xff\xff' if on else b'\x00\x00\x00\x00')
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(rows, 9)) + chunk(b'IEND', b''))


def checksum(data):
    data += b'\0' * (-len(data) % 4)
    return sum(struct.unpack('>' + 'I' * (len(data) // 4), data)) & 0xffffffff


def build_font(path, family, output):
    pixels, ascent, descent, source = read_bdf(path)
    if len(source) > 65534:
        raise ValueError('SFNT glyph limit exceeded')
    missing = source.get(0xfffd, source.get(63, next(iter(source.values()))))
    glyphs = [missing] + [source[c] for c in sorted(source)]
    count = len(glyphs)
    units = 1024
    scaled = lambda n: round(n * units / pixels)
    cbdt = bytearray(struct.pack('>I', 0x30000))
    offsets = []
    for g in glyphs:
        offsets.append(len(cbdt) - 4)
        image = png(g)
        cbdt += struct.pack('>BBbbBI', max(1, g.height), max(1, g.width), g.x, g.y + g.height,
                            g.advance, len(image)) + image
    offsets.append(len(cbdt) - 4)
    subtable = struct.pack('>HHI', 1, 17, 4) + struct.pack('>' + 'I' * len(offsets), *offsets)
    metrics = struct.pack('>bbBbbbbbbbbb', ascent, -descent, max(g.width for g in glyphs), 1, 0, 0, 0, 0, ascent, -descent, 0, 0)
    strike = (struct.pack('>IIII', 56, 8 + len(subtable), 1, 0) + metrics + bytes(12)
              + struct.pack('>HHBBBB', 0, count - 1, pixels, pixels, 32, 1))
    assert len(strike) == 48
    cblc = struct.pack('>II', 0x30000, 1) + strike + struct.pack('>HHI', 0, count - 1, 8) + subtable
    head = bytearray(54)
    struct.pack_into('>IIIIHH', head, 0, 0x10000, 0x10000, 0, 0x5f0f3cf5, 3, units)
    struct.pack_into('>hhhhHHhhh', head, 36, 0, -scaled(descent), units, scaled(ascent), 0, pixels, 2, 0, 0)
    hhea = bytearray(36)
    struct.pack_into('>IhhhH', hhea, 0, 0x10000, scaled(ascent), -scaled(descent), 0, max(scaled(g.advance) for g in glyphs))
    struct.pack_into('>h', hhea, 18, 1)
    struct.pack_into('>H', hhea, 34, count)
    hmtx = b''.join(struct.pack('>Hh', scaled(g.advance), scaled(g.x)) for g in glyphs)
    groups = b''.join(struct.pack('>III', c, c, i + 1) for i, c in enumerate(sorted(source)))
    cmap12 = struct.pack('>HHIII', 12, 0, 16 + len(groups), 0, len(source)) + groups
    cmap = struct.pack('>HHHHI', 0, 1, 3, 10, 12) + cmap12
    names = {1: family, 2: 'Regular', 4: family, 6: family.replace(' ', '-'), 16: family, 17: 'Regular'}
    records, strings = bytearray(), bytearray()
    for key, text in names.items():
        value = text.encode('utf-16be')
        records += struct.pack('>HHHHHH', 3, 1, 0x409, key, len(value), len(strings))
        strings += value
    name = struct.pack('>HHH', 0, len(names), 6 + len(records)) + records + strings
    os2 = bytearray(96)
    struct.pack_into('>HhHH', os2, 0, 4, scaled(missing.advance), 400, 5)
    os2[58:62] = b'DOME'
    struct.pack_into('>HHHhhhHH', os2, 62, 0x40, min(min(source), 0xffff), min(max(source), 0xffff), scaled(ascent), -scaled(descent), 0, scaled(ascent), scaled(descent))
    tables = {'head': bytes(head), 'hhea': bytes(hhea), 'hmtx': hmtx, 'maxp': struct.pack('>IH', 0x5000, count),
              'cmap': cmap, 'name': name, 'OS/2': bytes(os2), 'post': struct.pack('>I', 0x30000) + bytes(28),
              'CBLC': cblc, 'CBDT': bytes(cbdt), 'DOMB': struct.pack('>I', 1)}
    n = len(tables); power = 2 ** (n.bit_length() - 1)
    directory = bytearray(struct.pack('>IHHHH', 0x10000, n, power * 16, power.bit_length() - 1, n * 16 - power * 16))
    payload = bytearray(); head_offset = 0
    for key, data in sorted(tables.items()):
        offset = 12 + n * 16 + len(payload)
        directory += struct.pack('>4sIII', key.encode(), checksum(data), offset, len(data))
        if key == 'head': head_offset = offset
        payload += data + b'\0' * (-len(data) % 4)
    result = directory + payload
    struct.pack_into('>I', result, head_offset + 8, (0xb1b0afba - checksum(result)) & 0xffffffff)
    output = Path(output); output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix('.tmp'); temporary.write_bytes(result); temporary.replace(output)
    return {'family': family, 'source': str(Path(path).resolve()), 'source_sha256': hashlib.sha256(Path(path).read_bytes()).hexdigest(),
            'glyphs': count, 'pixels': pixels, 'sha256': hashlib.sha256(result).hexdigest()}
