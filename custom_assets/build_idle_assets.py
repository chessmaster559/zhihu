"""Build a reversible XiaoZhi LVGL resource-only Kanshan idle skin.

Uses the project's own packer; retains every original file and index setting
except the neutral emoji mapping. No firmware, sdkconfig or device is modified.
Requires Pillow. Input GIFs are resized, not AI-redrawn.
"""
import argparse
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import struct

from PIL import Image, ImageDraw


def unpack(blob):
    if len(blob) < 12:
        raise ValueError('Truncated assets header')
    count, checksum, length = struct.unpack_from('<III', blob)
    payload = blob[12:]
    if length != len(payload) or sum(payload) & 0xffff != checksum:
        raise ValueError('Assets length/checksum mismatch')
    table_size = count * 44
    if table_size > length:
        raise ValueError('Truncated assets table')
    files = {}
    for i in range(count):
        name, size, offset, _, _ = struct.unpack_from('<32sIIHH', payload, i * 44)
        name = name.split(b'\0')[0].decode('utf-8')
        if not name or '/' in name or '\\' in name or name in ('.', '..') or name in files:
            raise ValueError('Unsafe or duplicate asset name')
        start = table_size + offset
        if start + 2 + size > length or payload[start:start + 2] != b'ZZ':
            raise ValueError('Invalid asset boundary: ' + name)
        files[name] = payload[start + 2:start + 2 + size]
    return files


def make_idle(source, dest, side):
    frames, durations = [], []
    with Image.open(source) as gif:
        for number in range(gif.n_frames):
            gif.seek(number)
            rgba = gif.convert('RGBA').resize((side, side), Image.Resampling.LANCZOS)
            # Fifteen visible colors plus transparency. This keeps the code width
            # small in the bounded-dictionary encoder below.
            frame = rgba.convert('RGB').quantize(colors=15, method=Image.Quantize.MEDIANCUT)
            palette = frame.getpalette()
            frame.putpalette((palette + [0] * 48)[:48])
            mask = rgba.getchannel('A').point(lambda a: 255 if a < 128 else 0)
            frame.paste(15, mask=mask)
            frames.append(frame)
            durations.append(max(10, int(gif.info.get('duration', 50))))
    # Emit literal LZW codes, clearing after every ten pixels. With a 4-bit
    # palette, the dictionary stays below 32 entries, so codes remain 5 bits.
    # gifdec.c allocates its minimum 256-entry table (1536 bytes + header),
    # never reallocating it. Trading flash space for bounded RAM is intentional.
    blob = bytearray(b'GIF89a' + struct.pack('<HHBBB', side, side, 0xB3, 15, 0))
    blob.extend(bytes(frames[0].getpalette()[:48]))
    blob.extend(b'!\xff\x0bNETSCAPE2.0\x03\x01\x00\x00\x00')
    for frame, duration in zip(frames, durations):
        blob.extend(b'!\xf9\x04\x09' + struct.pack('<H', duration // 10) + b'\x0f\x00')
        blob.extend(b',' + struct.pack('<HHHHB', 0, 0, side, side, 0x83))
        blob.extend(bytes(frame.getpalette()[:48]))
        pixels = frame.tobytes()
        codes = []
        for start in range(0, len(pixels), 10):
            codes.append(16)  # clear
            codes.extend(pixels[start:start + 10])
        codes.append(17)  # end
        encoded = bytearray()
        accumulator = bits = 0
        for code in codes:
            accumulator |= code << bits
            bits += 5
            while bits >= 8:
                encoded.append(accumulator & 255)
                accumulator >>= 8
                bits -= 8
        if bits:
            encoded.append(accumulator & 255)
        blob.append(4)  # LZW minimum code size
        for start in range(0, len(encoded), 255):
            block = encoded[start:start + 255]
            blob.append(len(block))
            blob.extend(block)
        blob.append(0)
    blob.append(0x3B)
    dest.write_bytes(blob)
    with Image.open(dest) as checked:
        actual = []
        for i in range(checked.n_frames):
            checked.seek(i)
            actual.append(checked.info['duration'])
            decoded = checked.convert('RGBA')
            expected = frames[i].copy()
            expected.info['transparency'] = 15
            expected = expected.convert('RGBA')
            # Ignore RGB values at fully transparent pixels.
            for background in ('white', '#202020'):
                a = Image.new('RGBA', checked.size, background)
                b = a.copy()
                a.alpha_composite(decoded)
                b.alpha_composite(expected)
                if a.tobytes() != b.tobytes():
                    raise ValueError(f'GIF pixel verification failed at frame {i}')
        if checked.size != (side, side) or checked.info.get('loop') != 0:
            raise ValueError('GIF size/loop verification failed')
        if sum(actual) != sum(durations):
            raise ValueError('GIF timing changed')
        return {'source_frames': len(frames), 'output_frames': checked.n_frames,
                'duration_ms': sum(actual), 'size': [side, side], 'bytes': dest.stat().st_size,
                'pixel_buffer_bytes': 5 * side * side,
                'lzw_table_bytes_excluding_header': 256 * 6,
                'lzw_clear_every_pixels': 10}


def preview(gif_path, output):
    # QA contact sheet: render four decoded frames on light/dark 284x240 canvases.
    with Image.open(gif_path) as gif:
        sheet = Image.new('RGB', (4 * 284, 2 * 264), '#888888')
        for row, background in enumerate(('#ffffff', '#202020')):
            for col, frame in enumerate((0, gif.n_frames // 4, gif.n_frames // 2, gif.n_frames - 1)):
                gif.seek(frame)
                canvas = Image.new('RGBA', (284, 240), background)
                canvas.alpha_composite(gif.convert('RGBA'), ((284 - gif.width) // 2, (240 - gif.height) // 2))
                draw = ImageDraw.Draw(canvas)
                draw.rectangle((0, 0, 283, 19), fill='#447788')
                draw.text((8, 3), 'STANDBY / Wi-Fi', fill='white')
                sheet.paste(canvas.convert('RGB'), (col * 284, row * 264))
                ImageDraw.Draw(sheet).text((col * 284 + 8, row * 264 + 243), f'frame {frame} / layout preview', fill='white')
        sheet.save(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--project', type=Path, required=True)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--side', type=int, default=48)
    args = parser.parse_args()
    if not 24 <= args.side <= 48:
        parser.error('side must be 24..48 for this no-PSRAM low-memory pack')
    project, output = args.project.resolve(), args.output.resolve()
    config = (project / 'sdkconfig').read_text(encoding='utf-8')
    for forbidden in ('CONFIG_USE_EMOTE_MESSAGE_STYLE=y', 'CONFIG_USE_WECHAT_MESSAGE_STYLE=y'):
        if forbidden in config.splitlines():
            raise ValueError('This pack requires the normal LVGL display: ' + forbidden)
    if 'CONFIG_BOARD_TYPE_ESP_SENSAIRSHUTTLE=y' not in config.splitlines():
        raise ValueError('Expected ESP-SensairShuttle board configuration')
    base = (project / 'build/generated_assets.bin').read_bytes()
    files = unpack(base)
    index = json.loads(files['index.json'])
    if index.get('version') != 1:
        raise ValueError('Unsupported index version')
    if any('eaf' in item for item in index.get('emoji_collection', [])):
        raise ValueError('Emote assets are not supported')
    flash = json.loads((project / 'build/flasher_args.json').read_text())
    if int(flash['assets']['offset'], 0) != 0x800000:
        raise ValueError('Unexpected assets offset; review partition layout')
    output.mkdir(parents=True, exist_ok=True)
    rollback = output / 'original_assets.bin'
    if rollback.exists() and rollback.read_bytes() != base:
        raise ValueError('Existing rollback differs; choose a new output directory')
    rollback.write_bytes(base)
    stage = output / 'staging'
    if stage.exists() and any(stage.iterdir()):
        raise ValueError('Use a fresh output directory; staging is not empty')
    stage.mkdir(exist_ok=True)
    for name, data in files.items():
        (stage / name).write_bytes(data)
    gif_name = 'kanshan_idle.gif'
    stats = make_idle(args.source, stage / gif_name, args.side)
    matches = [e for e in index['emoji_collection'] if e['name'] == 'neutral']
    if len(matches) != 1:
        raise ValueError('Expected exactly one neutral emoji mapping')
    matches[0]['file'] = gif_name
    (stage / 'index.json').write_text(json.dumps(index, ensure_ascii=False, indent=2), encoding='utf-8')
    spec = importlib.util.spec_from_file_location('upstream_asset_builder', project / 'scripts/build_default_assets.py')
    builder = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(builder)
    packed = output / 'kanshan_idle_assets.bin'
    builder.pack_assets_simple(str(stage), str(output / 'generated_headers'), str(packed), str(stage))
    result = unpack(packed.read_bytes())
    for name, data in files.items():
        if name != 'index.json' and result.get(name) != data:
            raise ValueError('Original asset changed: ' + name)
    expected = json.loads(files['index.json'])
    for entry in expected['emoji_collection']:
        if entry['name'] == 'neutral':
            entry['file'] = gif_name
    if json.loads(result['index.json']) != expected:
        raise ValueError('Unexpected index changes')
    if packed.stat().st_size > 8 * 1024 * 1024:
        raise ValueError('Pack exceeds assets partition')
    with Image.open(io.BytesIO(result[gif_name])) as embedded:
        if embedded.size != (args.side, args.side):
            raise ValueError('Embedded GIF verification failed')
    preview(stage / gif_name, output / 'idle_preview.png')
    stats.update({'pack_bytes': packed.stat().st_size, 'partition_bytes': 8 * 1024 * 1024,
                  'original_assets_preserved': len(files) - 1,
                  'source_sha256': hashlib.sha256(args.source.read_bytes()).hexdigest(),
                  'base_sha256': hashlib.sha256(base).hexdigest(),
                  'pack_sha256': hashlib.sha256(packed.read_bytes()).hexdigest(),
                  'flash_offset': '0x800000', 'mapping': {'neutral': gif_name},
                  'firmware_modified': False, 'device_flashed': False})
    (output / 'manifest.json').write_text(json.dumps(stats, indent=2), encoding='utf-8')
    print(json.dumps(stats, indent=2))


if __name__ == '__main__':
    main()
