"""Check all converted frames with Espressif's unmodified C EAF decoder.

Windows LLVM builds a temporary DLL; Python provides tracked heap callbacks.
Compare RGB565 colors, alpha, and 50 ms timing against the original GIFs.
"""
import ctypes
import io
import json
from pathlib import Path
import struct
import subprocess
import tempfile

from PIL import Image
from verify_motion_decoder import STUBS, SUPPORT

ROOT = Path(__file__).resolve().parents[1]


def compile_decoder(work):
    decoder = ROOT / "managed_components/espressif__esp_lv_eaf_player"
    stubs = dict(STUBS)
    stubs["stdlib.h"] += "void *malloc(size_t); void *calloc(size_t,size_t); void free(void*);\n"
    stubs["sdkconfig.h"] = "#define CONFIG_SPIRAM 1\n"
    stubs["inttypes.h"] = "#include <stdint.h>\n"
    stubs["esp_err.h"] = """#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_SIZE 2
#define ESP_ERR_INVALID_CRC 3
#define ESP_ERR_NO_MEM 4
#define ESP_ERR_NOT_SUPPORTED 5
#define ESP_ERR_INVALID_STATE 6
"""
    stubs["esp_check.h"] = "#define ESP_GOTO_ON_FALSE(c,e,l,...) do {if(!(c)){ret=(e);goto l;}}while(0)\n"
    stubs["esp_cache.h"] = "#include <stddef.h>\nstatic inline void esp_cache_get_alignment(unsigned c,size_t *a){(void)c;*a=4;}\n"
    stubs["esp_private/esp_cache_private.h"] = ""
    stubs["esp_heap_caps.h"] += """
#define MALLOC_CAP_DEFAULT 4
static inline void *heap_caps_aligned_alloc(size_t a,size_t n,unsigned c){(void)a;return heap_caps_malloc(n,c);}
"""
    for name, content in stubs.items():
        path = work / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8")
    support = SUPPORT.split("API void *open_gif")[0].replace('"gifdec.h"', '"esp_eaf_dec.h"')
    support += r"""
void *malloc(size_t n){return heap_caps_malloc(n,4);}
void *calloc(size_t n,size_t s){void *p=malloc(n*s);if(p)memset(p,0,n*s);return p;}
void free(void *p){heap_caps_free(p);}
API void *open_eaf(const void *p,size_t n){void *h=0;return esp_eaf_format_init(p,n,&h)==ESP_OK?h:0;}
API int frames_eaf(void *h){return esp_eaf_format_get_total_frames(h);}
API int decode_eaf(void *h,int i,unsigned char *rgb,unsigned char *alpha){
    const uint8_t *p=esp_eaf_format_get_frame_data(h,i);
    esp_eaf_header_t header;
    if(esp_eaf_header_parse(p,esp_eaf_format_get_frame_size(h,i),&header)!=ESP_EAF_FORMAT_VALID)return -1;
    int result=0;
    if(header.width!=128||header.height!=128){result=-2;goto done;}
    for(int b=0;b<header.blocks;b++){
        if(esp_eaf_block_decode(&header,p,b,rgb+b*16*128*2,alpha,false,0)!=ESP_OK){result=-3;break;}
    }
done:
    esp_eaf_free_header(&header);
    return result;
}
API void close_eaf(void *h){esp_eaf_format_deinit(h);}
"""
    (work / "support.c").write_text(support, encoding="utf-8")
    library = work / "decoder.dll"
    command = ["C:/Program Files/LLVM/bin/clang.exe", "--target=x86_64-pc-windows-msvc",
               "-std=c11", "-O1", "-ffreestanding", "-fno-builtin", "-fno-stack-protector",
               "-nostdlib", "-shared", "-fuse-ld=lld", "-Wl,/noentry,/nodefaultlib",
               "-I" + str(work), "-I" + str(decoder / "include"),
               "-I" + str(decoder / "src"), str(decoder / "src/esp_eaf_dec.c"),
               str(work / "support.c"), "-o", str(library)]
    result = subprocess.run(command, capture_output=True, text=True, timeout=60)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    dll = ctypes.CDLL(str(library))
    dll.open_eaf.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
    dll.open_eaf.restype = ctypes.c_void_p
    dll.frames_eaf.argtypes = [ctypes.c_void_p]
    dll.decode_eaf.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p, ctypes.c_void_p]
    dll.close_eaf.argtypes = [ctypes.c_void_p]
    return dll


def verify(dll, path, original):
    blocks, caps_seen = {}, set()
    peak, fail_alloc = 0, False

    def allocate(size, caps):
        nonlocal peak
        caps_seen.add(caps)
        if fail_alloc:
            return None
        block = ctypes.create_string_buffer(size)
        pointer = ctypes.addressof(block)
        blocks[pointer] = block
        peak = max(peak, sum(ctypes.sizeof(b) for b in blocks.values()))
        return pointer

    def free(pointer):
        if pointer:
            del blocks[pointer]

    callbacks = (ctypes.CFUNCTYPE(ctypes.c_void_p, ctypes.c_size_t, ctypes.c_uint)(allocate),
                 ctypes.CFUNCTYPE(ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_uint)(lambda *_: None),
                 ctypes.CFUNCTYPE(None, ctypes.c_void_p)(free))
    dll.set_callbacks(*callbacks)
    data = ctypes.create_string_buffer(path.read_bytes())
    handle = dll.open_eaf(data, len(data) - 1)
    assert handle, path.name
    rgb, alpha = ctypes.create_string_buffer(32768), ctypes.create_string_buffer(16384)
    tick = 0
    with Image.open(io.BytesIO(original)) as gif:
        for frame in range(gif.n_frames):
            gif.seek(frame)
            expected_rgb, expected_alpha = bytearray(), bytearray()
            for r, g, b, a in gif.convert("RGBA").getdata():
                expected_rgb.extend(struct.pack("<H", ((r & 248) << 8) | ((g & 252) << 3) | (b >> 3)))
                expected_alpha.append(a)
            for _ in range(gif.info["duration"] // 50):
                assert dll.decode_eaf(handle, tick, rgb, alpha) == 0, (path.name, tick)
                actual_rgb, actual_alpha = rgb.raw, alpha.raw
                assert actual_alpha == expected_alpha, (path.name, tick, "alpha")
                assert all(actual_rgb[i*2:i*2+2] == expected_rgb[i*2:i*2+2]
                           for i, a in enumerate(expected_alpha) if a), (path.name, tick, "color")
                tick += 1
    assert tick == dll.frames_eaf(handle), "Timing/frame count mismatch"
    assert dll.decode_eaf(handle, 0, rgb, alpha) == 0, "Loop restart failed"
    assert not blocks or caps_seen == {3}, "Unexpected internal RAM allocation"
    fail_alloc = True
    assert dll.decode_eaf(handle, 0, rgb, alpha) != 0, "Decode OOM did not fail"
    dll.close_eaf(handle)
    assert not blocks, "Decoder allocation leak"
    assert not dll.open_eaf(data, len(data)-1), "Open OOM did not fail"
    assert not blocks
    result = {"file": path.name, "frames_verified": tick, "duration_ms": tick * 50,
              "decoder_heap_peak_bytes": peak, "frame_buffer_bytes": 49152,
              "all_pixels_and_alpha": "pass", "oom_cleanup": "pass"}
    print(json.dumps(result), flush=True)
    return result


def main():
    # Verify from retained GIF source files, without depending on an ignored pack.
    gif_source = ROOT / "custom_assets/kanshan_motion_v1/staging"
    sources = sorted((ROOT / "custom_assets/kanshan_eaf_v1/staging").glob("pet_*.eaf"))
    if len(sources) != 6:
        raise RuntimeError("Build the six EAF assets first")
    with tempfile.TemporaryDirectory(prefix="kanshan-eaf-decoder-") as temp:
        dll = compile_decoder(Path(temp))
        try:
            results = [verify(dll, path, (gif_source / path.with_suffix(".gif").name).read_bytes()) for path in sources]
        finally:
            import _ctypes
            _ctypes.FreeLibrary(dll._handle)
    (ROOT / "custom_assets/kanshan_eaf_v1/decoder_verification.json").write_text(
        json.dumps(results, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
