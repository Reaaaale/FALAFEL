#!/usr/bin/env python3
"""Embed the simulator PNG at build time; no runtime working-directory dependency."""
import pathlib
import struct
import sys
source, target = map(pathlib.Path, sys.argv[1:])
if source.is_dir():
    frames = [source / f"frame_{i:03}.png" for i in range(24)]
    lines = ['/* Generated rotation PNGs: compressed, read-only data. */',
             '#include "falafel_gui_internal.h"']
    total = 0
    for i, frame in enumerate(frames):
        data = frame.read_bytes()
        if (data[:8] != b'\x89PNG\r\n\x1a\n' or data[12:16] != b'IHDR'
                or struct.unpack('>II', data[16:24]) != (466, 466)
                or data[24:26] != bytes([8, 6])):
            raise ValueError(f"Expected 466x466 RGBA8 PNG: {frame}")
        total += len(data)
        lines.append(f'static const uint8_t frame_{i:03}[] = {{')
        lines.extend(','.join(f'0x{b:02x}' for b in data[j:j+24])+','
                     for j in range(0, len(data), 24))
        lines.append('};')
    lines.append('const lv_image_dsc_t falafel_asset_rotation[FALAFEL_ROTATION_FRAME_COUNT] = {')
    for i in range(24):
        lines.extend(['{ .header.magic=LV_IMAGE_HEADER_MAGIC,',
                      '.header.cf=LV_COLOR_FORMAT_RAW_ALPHA, .header.w=466, .header.h=466,',
                      f'.data_size=sizeof(frame_{i:03}), .data=frame_{i:03} }},'])
    lines.append('};')
    target.write_text('\n'.join(lines)+'\n')
    print(f"Embedded 24 rotation PNGs: {total} compressed bytes")
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
