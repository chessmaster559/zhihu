import io
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

from native_test_support import run_native_test

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "custom_assets"))
from build_eaf_assets import PET_ACTIONS, convert_gif, pack_pet_assets, prepare_pet_assets
from build_idle_assets import unpack
from PIL import Image


class EafAssetTests(unittest.TestCase):
    def pet_sources(self):
        source = io.BytesIO()
        Image.new("RGB", (128, 128), "white").save(source, format="GIF", duration=50)
        files = {action + ".gif": source.getvalue() for action in PET_ACTIONS}
        files.update({"font.bin": b"font data", "models.bin": b"wake model data"})
        # The PNG was intentionally deleted. Its old index entry must not matter.
        files["index.json"] = json.dumps({"version": 1, "text_font": "font.bin",
            "srmodels": "models.bin", "emoji_collection": [
                {"name": "happy", "file": "deleted_48px.png"}]}).encode()
        return files

    def test_deleted_png_and_stale_files_excluded_from_real_pack(self):
        files, _ = prepare_pet_assets(self.pet_sources())
        self.assertEqual(len(files), 9)
        index = json.loads(files["index.json"])
        self.assertEqual({item["name"] for item in index["emoji_collection"]},
                         {"neutral", *PET_ACTIONS})
        self.assertTrue(all(item["file"].endswith(".eaf") for item in index["emoji_collection"]))
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp)
            (output / "staging").mkdir()
            stale = output / "staging/happy.png"
            stale.write_bytes(b"unused image")
            blob = pack_pet_assets(files, output)
            self.assertEqual(unpack(blob), files)
            self.assertTrue(stale.exists()) # Do not delete unrelated files on disk.

    def test_pack_only_does_not_need_original_gif_archive(self):
        files, _ = prepare_pet_assets(self.pet_sources())
        rebuilt, stats = prepare_pet_assets(files, pack_only=True)
        self.assertEqual(rebuilt, files)
        self.assertTrue(all(item["frames"] == 1 for item in stats.values()))

    def test_required_action_and_model_cannot_be_deleted(self):
        for name in ("pet_sleep.gif", "models.bin", "font.bin"):
            with self.subTest(name=name):
                files = self.pet_sources()
                del files[name]
                with self.assertRaisesRegex(ValueError, "missing"):
                    prepare_pet_assets(files)

    def test_pack_only_rejects_small_animation(self):
        files, _ = prepare_pet_assets(self.pet_sources())
        blob = bytearray(files["pet_idle.eaf"])
        struct.pack_into("<H", blob, 24 + 12, 48)
        struct.pack_into("<I", blob, 8, sum(blob[16:]) & 0xffffffff)
        files["pet_idle.eaf"] = bytes(blob)
        with self.assertRaisesRegex(ValueError, "128x128"):
            prepare_pet_assets(files, pack_only=True)

    def test_oversize_does_not_replace_previous_pack(self):
        files, _ = prepare_pet_assets(self.pet_sources())
        files["font.bin"] = bytes(8 * 1024 * 1024)
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp)
            previous = output / "kanshan_eaf_assets.bin"
            previous.write_bytes(b"previous release")
            with self.assertRaisesRegex(ValueError, "8MiB"):
                pack_pet_assets(files, output)
            self.assertEqual(previous.read_bytes(), b"previous release")

    def test_runtime_bounds(self):
        run_native_test(self, ["scripts/tests/eaf_asset_test.cc"], ["main/display/lvgl_display"])

    def test_transparency_and_timing(self):
        first = Image.new("RGBA", (128, 128), (0, 0, 0, 0))
        first.putpixel((10, 10), (24, 112, 232, 255))
        second = first.copy()
        second.putpixel((10, 10), (232, 24, 112, 255))
        source = io.BytesIO()
        first.save(source, format="GIF", save_all=True, append_images=[second],
                   duration=[50, 150], loop=0, disposal=2)
        blob, stats = convert_gif(source.getvalue())
        self.assertEqual(stats["frames"], 4)
        self.assertEqual(stats["duration_ms"], 200)
        count, checksum, length = struct.unpack_from("<III", blob, 4)
        self.assertEqual(count, 4)
        self.assertEqual(length, len(blob) - 16)
        self.assertEqual(checksum, sum(blob[16:]) & 0xffffffff)
        entries = [struct.unpack_from("<II", blob, 16 + i * 8) for i in range(4)]
        self.assertEqual(entries[1], entries[2])
        self.assertEqual(entries[2], entries[3])
        # Decode the serialized palette and all RLE blocks independently.
        base = 16 + count * 8
        for number, expected in ((0, (24, 112, 232, 255)), (1, (232, 24, 112, 255))):
            size, offset = entries[number]
            frame = blob[base + offset:base + offset + size]
            lengths = struct.unpack_from("<8I", frame, 20)
            pixels = bytearray()
            pos = 1076
            for block_length in lengths:
                self.assertEqual(frame[pos], 0)
                for p in range(pos + 1, pos + block_length, 2):
                    pixels.extend(bytes([frame[p + 1]]) * frame[p])
                pos += block_length
            palette = frame[52:1076]
            index = pixels[10 * 128 + 10]
            blue, green, red, alpha = palette[index * 4:index * 4 + 4]
            self.assertEqual((red, green, blue, alpha), expected)
            self.assertEqual(palette[pixels[0] * 4 + 3], 0)

    def test_unrepresentable_timing_rejected(self):
        source = io.BytesIO()
        Image.new("RGB", (128, 128), "white").save(source, format="GIF", duration=70)
        with self.assertRaisesRegex(ValueError, "50ms"):
            convert_gif(source.getvalue())


if __name__ == "__main__":
    unittest.main()
