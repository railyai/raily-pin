#!/usr/bin/env python3
"""Copies U8g2 font blobs out of u8g2_fonts.c into tools/oled_assets/fonts/.

    python3 hardware/firmware/tools/extract_u8g2_fonts.py [path/to/u8g2_fonts.c]

The keyring's word strips are drawn at build time by gen_oled_assets.py from
these blobs, pixel for pixel as U8g2 would draw them; no font is linked into
the firmware. The default source is the U8g2 library arduino-cli installed
(build_firmware.sh pins it to 2.37.1). The blobs are checked in, so the
generator never needs the library; rerun this only to add a font.
Same parser as the concept pack's extract_fonts.py.
"""
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, 'oled_assets', 'fonts')
FONTS = (
    'u8g2_font_6x13_t_cyrillic',   # ru words
    'u8g2_font_5x8_t_cyrillic',    # ru words that do not fit 6x13
    'u8g2_font_6x13_tf',           # en/es/pt words (Latin-1: á é í ó ú ã õ ç ñ)
    'u8g2_font_5x8_tf',            # Latin words that do not fit 6x13
    'u8g2_font_4x6_t_cyrillic',    # the speech particles of «speaking»
    'u8g2_font_logisoso16_tn',     # the count on the «есть люди» card
    'u8g2_font_logisoso24_tn',     # the big «0» and «+N»
)
ESCAPES = {'n': '\n', 't': '\t', 'r': '\r', '"': '"', '\\': '\\', '?': '?', "'": "'"}


def installed_source():
    out = subprocess.run(['arduino-cli', 'lib', 'list', '--format', 'json'], capture_output=True, text=True,
                         check=True).stdout
    for lib in json.loads(out).get('installed_libraries') or []:
        if lib['library']['name'] == 'U8g2':
            return os.path.join(lib['library']['install_dir'], 'src', 'clib', 'u8g2_fonts.c')
    sys.exit('extract_u8g2_fonts.py: U8g2 is not installed for arduino-cli; pass the u8g2_fonts.c path')


def blob(data, name):
    m = re.search(r'const uint8_t ' + name + r'\[(\d+)\][^=]*=\s*', data)
    if not m:
        sys.exit('extract_u8g2_fonts.py: %s not in the source' % name)
    pos, out = m.end(), bytearray()
    literal = re.compile(r'\s*"((?:[^"\\]|\\.)*)"', re.S)
    while True:
        mm = literal.match(data, pos)
        if not mm:
            break
        pos = mm.end()
        lit, i = mm.group(1), 0
        while i < len(lit):
            c = lit[i]
            if c != '\\':
                out.append(ord(c))
                i += 1
                continue
            nx = lit[i + 1]
            if nx in '01234567':
                j, s = i + 1, ''
                while j < len(lit) and len(s) < 3 and lit[j] in '01234567':
                    s += lit[j]
                    j += 1
                out.append(int(s, 8))
                i = j
            elif nx == 'x':
                j, s = i + 2, ''
                while j < len(lit) and lit[j] in '0123456789abcdefABCDEF':
                    s += lit[j]
                    j += 1
                out.append(int(s, 16))
                i = j
            else:
                out.append(ord(ESCAPES.get(nx, nx)))
                i += 2
    declared = int(m.group(1))
    if len(out) + 1 != declared:  # the C array also holds the string's NUL
        sys.exit('extract_u8g2_fonts.py: %s parsed to %d B, declared %d' % (name, len(out), declared))
    return bytes(out)


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else installed_source()
    data = open(src, 'rb').read().decode('latin-1')
    os.makedirs(OUT, exist_ok=True)
    for name in FONTS:
        b = blob(data, name)
        with open(os.path.join(OUT, name + '.bin'), 'wb') as f:
            f.write(b)
        print('%s %d B' % (name, len(b)))


if __name__ == '__main__':
    main()
