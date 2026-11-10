#!/usr/bin/env python3
"""Launch the native bitmap-only DomeD Discord fork with a private profile."""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import sys
from bitmap_font import build_font

ROOT = Path(__file__).resolve().parent.parent

def xdg(name, fallback):
    value = os.environ.get(name, '')
    return Path(value) if value.startswith('/') else Path.home() / fallback


def appearance():
    path = xdg('XDG_CONFIG_HOME', '.config') / 'dome/appearance.json'
    try:
        data = json.loads(path.read_text())
        return data if isinstance(data, dict) else {}
    except (OSError, ValueError):
        return {}


def font_sources(settings):
    roots = [Path('/usr/local/share/fonts/domed'), Path('/opt/fltk-vr/share/fonts/domed')]
    requested = settings.get('font', 'Unifont / Sans-Serif')
    wanted_size = settings.get('font_size', 16)
    try: wanted_size = max(8, min(48, int(wanted_size)))
    except (ValueError, TypeError): wanted_size = 16
    candidates = []
    for root in roots:
        try:
            for line in (root / 'fonts.index').read_text().splitlines():
                fields = line.split('\t')
                if len(fields) >= 5:
                    relative, family, style, pixels, text = fields[:5]
                    path = root / relative
                    if (f'{family} / {style}' == requested and text == '1'
                            and path.is_file() and (path.suffix == '.bdf' or path.name.endswith('.bdf.gz'))):
                        candidates.append((abs(int(pixels) - wanted_size), path))
        except (OSError, ValueError):
            continue
    def installed(relative):
        return next((root / relative for root in roots if (root / relative).is_file()), None)
    fallback = installed('unifont-17.0.04.bdf')
    upper = installed('unifont-upper-17.0.04/unifont_upper-17.0.04.bdf')
    chosen = min(candidates, key=lambda item: item[0])[1] if candidates else fallback
    if not chosen or not fallback or not upper:
        raise RuntimeError('Install the DomeD BDF font collection before starting DomeD Discord.')
    return [(chosen, 'DomeD Bitmap'), (fallback, 'DomeD Fallback'), (upper, 'DomeD Emoji')]


def prepare_fonts(settings):
    cache = xdg('XDG_CACHE_HOME', '.cache') / 'dome-discord/bitmap-v1'
    cache.mkdir(parents=True, exist_ok=True, mode=0o700)
    with (cache / '.lock').open('w') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        records = []
        for source, family in font_sources(settings):
            digest = hashlib.sha256(source.read_bytes()).hexdigest()
            target = cache / (family.replace(' ', '-') + '-' + digest[:16] + '.ttf')
            receipt = target.with_suffix('.json')
            if not target.exists() or not receipt.exists():
                record = build_font(source, family, target)
                receipt.write_text(json.dumps(record, indent=2) + '\n')
            records.append((target, json.loads(receipt.read_text())))
        # Use an immutable directory per selected set. MaiCord never scans old cached faces.
        key = hashlib.sha256(''.join(str(p) for p, _ in records).encode()).hexdigest()[:16]
        directory = cache / key
        directory.mkdir(exist_ok=True)
        for index, (target, _) in enumerate(records):
            link = directory / f'{index}.ttf'
            if not link.exists(): link.symlink_to(target)
        return directory, [r for _, r in records]


def environment(settings, fonts):
    env = os.environ.copy()
    # Preserve actual base directories for this launcher. Only the child gets the private namespace.
    for key, fallback in [('XDG_DATA_HOME', '.local/share'), ('XDG_STATE_HOME', '.local/state'), ('XDG_CACHE_HOME', '.cache')]:
        directory = xdg(key, fallback) / 'dome-discord'
        directory.mkdir(parents=True, exist_ok=True, mode=0o700)
        env[key] = str(directory)
    env['DOMECORD_LAUNCHER'] = str(ROOT / 'dome/run.sh')
    env['DOMECORD_SESSION'] = '1'
    env['DOMECORD_FONT_DIR'] = str(fonts)
    env['DOMECORD_LIGHT'] = '1' if settings.get('light') is True else '0'
    accent = settings.get('accent', '#6babff')
    env['DOMECORD_ACCENT'] = accent if isinstance(accent, str) and len(accent) == 7 and accent[0] == '#' and all(c in '0123456789abcdefABCDEF' for c in accent[1:]) else '#6babff'
    try: zoom = round(int(settings.get('font_size', 16)) / 16 * int(settings.get('font_scale', 1)) * 100)
    except (TypeError, ValueError): zoom = 100
    env['DOMECORD_ZOOM'] = str(max(50, min(400, zoom)))
    return env


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prepare-fonts', action='store_true')
    parser.add_argument('--binary', type=Path, default=ROOT.parent / 'maicord-dome-build/MaiCord')
    args, forwarded = parser.parse_known_args()
    settings = appearance()
    directory, records = prepare_fonts(settings)
    if args.prepare_fonts:
        print(json.dumps({'directory': str(directory), 'fonts': records}, indent=2))
        return
    binary = args.binary.resolve()
    if not binary.is_file():
        raise RuntimeError('Build the native client first with bash dome/build.sh.')
    os.execve(binary, [str(binary), *forwarded], environment(settings, directory))

if __name__ == '__main__':
    try: main()
    except (RuntimeError, OSError, ValueError) as error:
        print('DomeD Discord:', error, file=sys.stderr)
        sys.exit(1)
