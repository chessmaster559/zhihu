"""Resize the supplied Kanshan GIFs and build a verified 128px resource pack.

Run with desktop Python/Pillow. Original GIFs and the base pack are read-only.
Firmware decoding is serialized and allocated in PSRAM, not internal SRAM.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path

from PIL import Image, ImageDraw
from build_idle_assets import unpack

MOTIONS = {
    "pet_idle": "待机", "pet_greeting": "打招呼", "pet_sway": "晃悠",
    "pet_thinking": "电脑", "pet_sleep": "瞌睡", "pet_play": "运球",
}


def resize_gif(source, dest):
    frames, durations = [], []
    with Image.open(source) as gif:
        for i in range(gif.n_frames):
            gif.seek(i)
            rgba = gif.convert("RGBA").resize((128, 128), Image.Resampling.LANCZOS)
            frame = rgba.convert("RGB").quantize(colors=63, method=Image.Quantize.MEDIANCUT)
            frame.paste(63, mask=rgba.getchannel("A").point(lambda a: 255 if a < 128 else 0))
            frame.info["transparency"] = 63
            frames.append(frame)
            durations.append(max(10, int(gif.info.get("duration", 50))))
    frames[0].save(dest, save_all=True, append_images=frames[1:], duration=durations,
                   loop=0, transparency=63, background=63, disposal=2, optimize=False)
    # Verify decoded pixels/timing on both backgrounds; disposal and local palettes
    # must survive conversion. Pillow may merge identical adjacent frames.
    elapsed = 0
    source_index = 0
    next_source_ms = durations[0]
    output_durations = []
    with Image.open(dest) as result:
        if result.size != (128, 128) or result.info.get("loop") != 0:
            raise ValueError("Invalid GIF dimensions/loop: " + str(dest))
        for i in range(result.n_frames):
            result.seek(i)
            while elapsed >= next_source_ms and source_index + 1 < len(frames):
                source_index += 1
                next_source_ms += durations[source_index]
            actual = result.convert("RGBA")
            expected = frames[source_index].convert("RGBA")
            for color in ("white", "#202020"):
                a = Image.new("RGBA", (128, 128), color)
                b = a.copy()
                a.alpha_composite(actual)
                b.alpha_composite(expected)
                if a.tobytes() != b.tobytes():
                    raise ValueError(f"Disposal/palette mismatch: {dest}, frame {i}")
            duration = result.info["duration"]
            output_durations.append(duration)
            elapsed += duration
        if elapsed != sum(durations):
            raise ValueError("GIF duration changed: " + str(dest))
        output_frames = result.n_frames
    return {"source": str(source), "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
            "source_frames": len(frames), "output_frames": output_frames,
            "size": [128, 128], "duration_ms": elapsed,
            "frame_delays_ms": sorted(set(output_durations)), "bytes": dest.stat().st_size,
            "sha256": hashlib.sha256(dest.read_bytes()).hexdigest()}


def make_preview(stage, output):
    sheet = Image.new("RGB", (640, len(MOTIONS) * 185), "#dddddd")
    for row, name in enumerate(MOTIONS):
        with Image.open(stage / (name + ".gif")) as gif:
            for col, number in enumerate((0, gif.n_frames // 3, 2 * gif.n_frames // 3, gif.n_frames - 1)):
                gif.seek(number)
                tile = Image.new("RGBA", (160, 160), "white" if col % 2 == 0 else "#202020")
                tile.alpha_composite(gif.convert("RGBA"), (16, 16))
                sheet.paste(tile.convert("RGB"), (160 * col, 185 * row + 22))
        ImageDraw.Draw(sheet).text((6, 185 * row + 4), name + " / 128px", fill="black")
    sheet.save(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, required=True)
    parser.add_argument("--base", type=Path, default=Path("custom_assets/kanshan_idle_lowmem_v2/kanshan_idle_assets.bin"))
    parser.add_argument("--output", type=Path, default=Path("custom_assets/kanshan_motion_v1"))
    args = parser.parse_args()
    base = args.base.read_bytes()
    original = unpack(base)
    index = json.loads(original["index.json"])
    output = args.output
    output.mkdir(parents=True, exist_ok=True)
    stage = output / "staging"
    stage.mkdir(exist_ok=True)
    for name, data in original.items():
        (stage / name).write_bytes(data)
    stats = {}
    for name, prefix in MOTIONS.items():
        matches = list(args.source_dir.glob(prefix + "*.gif"))
        if len(matches) != 1:
            raise ValueError("Expected exactly one source for " + prefix)
        stats[name] = resize_gif(matches[0], stage / (name + ".gif"))
    names = set(MOTIONS)
    index["emoji_collection"] = [entry for entry in index["emoji_collection"] if entry["name"] not in names]
    index["emoji_collection"] += [{"name": name, "file": name + ".gif"} for name in MOTIONS]
    (stage / "index.json").write_text(json.dumps(index, ensure_ascii=False, indent=2), encoding="utf-8")
    project = Path(__file__).resolve().parents[1]
    spec = importlib.util.spec_from_file_location("asset_builder", project / "scripts/build_default_assets.py")
    builder = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(builder)
    packed = output / "kanshan_motion_assets.bin"
    builder.pack_assets_simple(str(stage), str(output / "generated_headers"), str(packed), str(stage))
    extracted = unpack(packed.read_bytes())
    for name, data in original.items():
        if name != "index.json" and extracted.get(name) != data:
            raise ValueError("Original asset changed: " + name)
    if json.loads(extracted["index.json"]) != index:
        raise ValueError("Index verification failed")
    for name in MOTIONS:
        if extracted[name + ".gif"] != (stage / (name + ".gif")).read_bytes():
            raise ValueError("Packed GIF mismatch: " + name)
    partition_bytes = 8 * 1024 * 1024
    if packed.stat().st_size > partition_bytes:
        raise ValueError("Assets exceed 8MiB partition")
    manifest = {"motions": stats, "pack_bytes": packed.stat().st_size,
                "partition_bytes": partition_bytes, "partition_free_bytes": partition_bytes - packed.stat().st_size,
                "base_sha256": hashlib.sha256(base).hexdigest(),
                "pack_sha256": hashlib.sha256(packed.read_bytes()).hexdigest(),
                "original_assets_preserved": len(original) - 1, "flash_offset": "0x800000",
                "decoder_pixel_bytes": 5 * 128 * 128, "lzw_table_max_bytes_approx": 4096 * 6,
                "active_decoders": 1, "decoder_heap": "PSRAM only on SensairShuttle",
                "device_flashed": False}
    (output / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8")
    make_preview(stage, output / "preview.png")
    print(json.dumps(manifest, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
