#!/usr/bin/env python3
"""Embed simulator PNGs at build time; no runtime working-directory dependency."""
import pathlib
import struct
import sys
source, target = map(pathlib.Path, sys.argv[1:])
if source.is_dir():
    personalities = ('taxi', 'cuoco', 'promemoria')  # falafel_personality_t order
    lines = ['/* Generated outfit PNGs: compressed, read-only data. */',
             '#include "falafel_gui_internal.h"']
    total = 0
    dimensions = None
    for outfit in personalities:
        for i in range(24):
            frame = source / outfit / f"frame_{i:02}.png"
            data = frame.read_bytes()
            if (data[:8] != b'\x89PNG\r\n\x1a\n' or data[12:16] != b'IHDR'
                    or data[24:26] != bytes([8, 6])):
                raise ValueError(f"Expected RGBA8 PNG: {frame}")
            size = struct.unpack('>II', data[16:24])
            if not all(size) or (dimensions is not None and size != dimensions):
                raise ValueError(f"All outfits must share one nonempty canvas: {frame}")
            dimensions = size
            total += len(data)
            lines.append(f'static const uint8_t {outfit}_{i:02}[] = {{')
            lines.extend(','.join(f'0x{b:02x}' for b in data[j:j+24])+','
                         for j in range(0, len(data), 24))
            lines.append('};')
    lines.append('const lv_image_dsc_t falafel_asset_rotation[FALAFEL_PERSONALITY_COUNT][FALAFEL_ROTATION_FRAME_COUNT] = {')
    w, h = dimensions
    for outfit in personalities:
        lines.append('{')
        for i in range(24):
            lines.extend(['{ .header.magic=LV_IMAGE_HEADER_MAGIC,',
                          f'.header.cf=LV_COLOR_FORMAT_RAW_ALPHA, .header.w={w}, .header.h={h},',
                          f'.data_size=sizeof({outfit}_{i:02}), .data={outfit}_{i:02} }},'])
        lines.append('},')
    lines.append('};')
    target.write_text('\n'.join(lines)+'\n')
    print(f"Embedded 72 outfit PNGs ({w}x{h}): {total} compressed bytes")
    sys.exit(0)
data = source.read_bytes()
if data[:8] != b'\x89PNG\r\n\x1a\n' or data[12:16] != b'IHDR':
    raise ValueError('Expected PNG with IHDR')
w, h = struct.unpack('>II', data[16:24])
lines = ['/* Generated from simulator/assets/falafel.png. */', '#include "lvgl.h"',
         'static const uint8_t pixels[] = {']
lines.extend(','.join(f'0x{b:02x}' for b in data[i:i+24])+',' for i in range(0,len(data),24))
lines.extend(['};', 'const lv_image_dsc_t falafel_asset_character = {',
              '.header.magic=LV_IMAGE_HEADER_MAGIC, .header.cf=LV_COLOR_FORMAT_RAW_ALPHA,',
              f'.header.w={w}, .header.h={h}, .data_size=sizeof(pixels), .data=pixels', '};'])
target.write_text('\n'.join(lines)+'\n')
