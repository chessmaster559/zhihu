"""Convert the existing 128px Kanshan pack to Espressif EAF (8-bit palette/RLE).

Original assets are read-only. Fonts/models are preserved byte for byte; unused
legacy PNG emotions are excluded. Transparent pixels, colors and timing remain; RGB565
conversion is performed by the official device decoder, not by this packer.
"""
import argparse
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import struct
import tempfile

from PIL import Image
from build_idle_assets import unpack

ROOT = Path(__file__).resolve().parents[1]
FRAME_MS = 50
PET_ACTIONS = ("pet_idle", "pet_greeting", "pet_sway", "pet_thinking", "pet_sleep", "pet_play")


def prepare_pet_assets(original, pack_only=False):
    index = json.loads(original["index.json"])
    files, stats = {}, {}
    for key in ("text_font", "srmodels"):
        name = index[key]
        if not isinstance(name, str) or not name or "/" in name or "\\" in name or len(name) > 31:
            raise ValueError("Invalid resource name: " + key)
        if name not in original or not original[name]:
            raise ValueError("Required font/model missing: " + name)
        files[name] = original[name]
    for action in PET_ACTIONS:
        name = action + (".eaf" if pack_only else ".gif")
        if name not in original:
            raise ValueError("Required 128px pet action missing: " + name)
        if pack_only:
            blob = original[name]
            if len(blob) < 24 or blob[:4] != b"\x89EAF":
                raise ValueError("Invalid EAF: " + name)
            count, checksum, length = struct.unpack_from("<III", blob, 4)
            if not 0 < count <= 2000 or length != len(blob) - 16 or sum(blob[16:]) & 0xffffffff != checksum:
                raise ValueError("Invalid EAF length/checksum: " + name)
            base = 16 + count * 8
            if base > len(blob):
                raise ValueError("Truncated EAF table: " + name)
            for frame in range(count):
                size, offset = struct.unpack_from("<II", blob, 16 + frame * 8)
                start = base + offset
                if size < 20 or start + size > len(blob) or blob[start:start + 4] != b"ZZ_S":
                    raise ValueError("Invalid EAF frame: " + name)
                if struct.unpack_from("<HH", blob, start + 12) != (128, 128):
                    raise ValueError("EAF assets must be 128x128: " + name)
            info = {"frames": count, "frame_ms": FRAME_MS, "duration_ms": count * FRAME_MS, "bytes": len(blob)}
        else:
            blob, info = convert_gif(original[name])
        files[action + ".eaf"] = blob
        stats[action + ".eaf"] = info
    # Only runtime pet names survive. Deleted 32/48px emotions cannot return.
    index["emoji_collection"] = [{"name": "neutral", "file": "pet_idle.eaf"}]
    index["emoji_collection"] += [{"name": action, "file": action + ".eaf"} for action in PET_ACTIONS]
    files["index.json"] = json.dumps(index, ensure_ascii=False, indent=2).encode("utf-8")
    return files, stats


def pack_pet_assets(files, output):
    spec = importlib.util.spec_from_file_location("asset_builder", ROOT / "scripts/build_default_assets.py")
    builder = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(builder)
    output.mkdir(parents=True, exist_ok=True)
    # The packer scans a directory. Use an exact temporary whitelist so stale
    # PNGs or previews left in staging never enter the release image.
    with tempfile.TemporaryDirectory(prefix="kanshan-pack-", dir=output) as temp:
        work = Path(temp)
        stage = work / "staging"
        stage.mkdir()
        for name, data in files.items():
            (stage / name).write_bytes(data)
        packed = work / "kanshan_eaf_assets.bin"
        builder.pack_assets_simple(str(stage), str(work / "headers"), str(packed), str(stage))
        blob = packed.read_bytes()
        if len(blob) > 8 * 1024 * 1024:
            raise ValueError("EAF pack exceeds the 8MiB assets partition")
        if unpack(blob) != files:
            raise ValueError("Packed resources mismatch")
        packed.replace(output / "kanshan_eaf_assets.bin")
    stage = output / "staging"
    stage.mkdir(exist_ok=True)
    for name, data in files.items():
        (stage / name).write_bytes(data)
    return blob


def rle(data):
    result = bytearray()
    start = 0
    while start < len(data):
        end = start + 1
        while end < len(data) and data[end] == data[start] and end - start < 255:
            end += 1
        result.extend((end - start, data[start]))
        start = end
    return bytes(result)


def encode_frame(image):
    rgba = image.convert("RGBA")
    if rgba.size != (128, 128):
        raise ValueError("EAF assets must be 128x128")
    pixels = list(rgba.getdata())
    colors = sorted({p[:3] for p in pixels if p[3] >= 128})
    if len(colors) > 255:
        raise ValueError("More than 255 opaque colors; resize/quantize the source GIF first")
    indices = {color: i + 1 for i, color in enumerate(colors)}
    palette = bytearray((0, 0, 0, 0))  # Index zero is transparent.
    for red, green, blue in colors:
        palette.extend((blue, green, red, 255))  # EAF stores BGRA.
    palette.extend(bytes((0, 0, 0, 255)) * (255 - len(colors)))
    data = bytes(indices[p[:3]] if p[3] >= 128 else 0 for p in pixels)
    blocks = [b"\0" + rle(data[row * 128:(row + 16) * 128]) for row in range(0, 128, 16)]
    # _S frame header: 3-byte format, 6-byte version, depth, geometry, block table.
    frame = bytearray(b"ZZ_S\0" + b"000001" + struct.pack("<BHHHH", 8, 128, 128, 8, 16))
    frame.extend(struct.pack("<8I", *(len(block) for block in blocks)))
    frame.extend(palette)
    frame.extend(b"".join(blocks))
    frame.extend(b"\0" * (-len(frame) % 4))  # Keep table-referenced frames aligned.
    return bytes(frame)


def convert_gif(blob):
    table, frames, unique = [], bytearray(), {}
    source_frames = 0
    with Image.open(io.BytesIO(blob)) as gif:
        for number in range(gif.n_frames):
            gif.seek(number)
            duration = int(gif.info.get("duration", FRAME_MS))
            if duration <= 0 or duration % FRAME_MS:
                raise ValueError("GIF timing must be a positive multiple of 50ms")
            frame = encode_frame(gif)
            if frame not in unique:
                unique[frame] = len(frames)
                frames.extend(frame)
            table.extend([(len(frame), unique[frame])] * (duration // FRAME_MS))
            source_frames += 1
    if not 0 < len(table) <= 2000:
        raise ValueError("Invalid EAF frame count")
    payload = b"".join(struct.pack("<II", *entry) for entry in table) + frames
    eaf = b"\x89EAF" + struct.pack("<III", len(table), sum(payload) & 0xffffffff, len(payload)) + payload
    return eaf, {"source_frames": source_frames, "frames": len(table), "frame_ms": FRAME_MS,
                 "duration_ms": len(table) * FRAME_MS, "bytes": len(eaf)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", type=Path, help="Optional legacy GIF pack; otherwise use source files")
    parser.add_argument("--source", type=Path, default=ROOT / "custom_assets/kanshan_eaf_v1/staging")
    parser.add_argument("--gif-source", type=Path, default=ROOT / "custom_assets/kanshan_motion_v1/staging")
    parser.add_argument("--pack-only", action="store_true", help="Pack existing EAFs without original GIF archives")
    parser.add_argument("--output", type=Path, default=ROOT / "custom_assets/kanshan_eaf_v1")
    args = parser.parse_args()
    if args.base:
        if args.pack_only:
            parser.error("--base is a legacy GIF pack; use --source with --pack-only")
        original = unpack(args.base.read_bytes())
    else:
        original = {"index.json": (args.source / "index.json").read_bytes()}
        index = json.loads(original["index.json"])
        for key in ("text_font", "srmodels"):
            name = index[key]
            if not isinstance(name, str) or not name or "/" in name or "\\" in name or len(name) > 31:
                raise ValueError("Invalid resource name: " + key)
            original[name] = (args.source / name).read_bytes()
        folder, suffix = (args.source, ".eaf") if args.pack_only else (args.gif_source, ".gif")
        for action in PET_ACTIONS:
            original[action + suffix] = (folder / (action + suffix)).read_bytes()
    files, stats = prepare_pet_assets(original, args.pack_only)
    blob = pack_pet_assets(files, args.output)
    size = len(blob)
    manifest = {"animations": stats, "pack_bytes": size, "partition_free_bytes": 8 * 1024 * 1024 - size,
                "sha256": hashlib.sha256(blob).hexdigest(),
                "resource_sha256": {name: hashlib.sha256(data).hexdigest() for name, data in files.items()}}
    (args.output / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(json.dumps(manifest, indent=2))


if __name__ == "__main__":
    main()
