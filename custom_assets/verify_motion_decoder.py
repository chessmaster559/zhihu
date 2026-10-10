"""Run the project's real C GIF decoder against Pillow, without an ESP32.

Windows LLVM builds a freestanding DLL. Python supplies tracked PSRAM allocator
callbacks and checks every decoded frame, loop termination, and allocation failure.
The decoder source is compiled unchanged, including the selected-board PSRAM path.
"""
import ctypes
import json
from pathlib import Path
import subprocess
import tempfile

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
STUBS = {
    "stdlib.h": "#include <stddef.h>\n#include <limits.h>\n",
    "string.h": "#include <stddef.h>\nvoid *memcpy(void*,const void*,size_t);\nvoid *memset(void*,int,size_t);\nint memcmp(const void*,const void*,size_t);\nint strncmp(const char*,const char*,size_t);\n",
    "esp_log.h": "#define ESP_LOGW(...) ((void)0)\n#define ESP_LOGE(...) ((void)0)\n#define ESP_LOGI(...) ((void)0)\n#define ESP_LOGD(...) ((void)0)\n",
    "sdkconfig.h": "#define CONFIG_BOARD_TYPE_ESP_SENSAIRSHUTTLE 1\n",
    "esp_heap_caps.h": """#include <stddef.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
void *heap_caps_malloc(size_t,unsigned);
void *heap_caps_realloc(void*,size_t,unsigned);
void heap_caps_free(void*);
""",
    "lvgl.h": """#include <stddef.h>
#include <stdint.h>
#define LV_GIF_CACHE_DECODE_DATA 0
#define LV_USE_DRAW_SW_ASM 0
#define LV_DRAW_SW_ASM_HELIUM 1
#define LV_FS_MODE_RD 1
#define LV_FS_SEEK_SET 0
#define LV_FS_SEEK_CUR 1
#define LV_FS_RES_OK 0
typedef struct {void *opaque;} lv_fs_file_t;
typedef int lv_fs_res_t;
static inline int lv_fs_open(lv_fs_file_t *f,const void *p,int m){(void)f;(void)p;(void)m;return 1;}
static inline int lv_fs_read(lv_fs_file_t *f,void *b,size_t n,void *r){(void)f;(void)b;(void)n;(void)r;return 1;}
static inline int lv_fs_seek(lv_fs_file_t *f,size_t p,int m){(void)f;(void)p;(void)m;return 1;}
static inline int lv_fs_tell(lv_fs_file_t *f,uint32_t *p){(void)f;*p=0;return 1;}
static inline int lv_fs_close(lv_fs_file_t *f){(void)f;return 1;}
""",
}
SUPPORT = r"""
#include "gifdec.h"
#include <stddef.h>
#define API __declspec(dllexport)
static void *(*allocate_cb)(size_t,unsigned);
static void *(*resize_cb)(void*,size_t,unsigned);
static void (*free_cb)(void*);
void *heap_caps_malloc(size_t n,unsigned c){return allocate_cb(n,c);}
void *heap_caps_realloc(void *p,size_t n,unsigned c){return resize_cb(p,n,c);}
void heap_caps_free(void *p){free_cb(p);}
void *memcpy(void *d,const void *s,size_t n){unsigned char *a=d;const unsigned char *b=s;for(size_t i=0;i<n;i++)a[i]=b[i];return d;}
void *memset(void *d,int v,size_t n){unsigned char *a=d;for(size_t i=0;i<n;i++)a[i]=(unsigned char)v;return d;}
int memcmp(const void *a,const void *b,size_t n){const unsigned char *x=a,*y=b;for(size_t i=0;i<n;i++)if(x[i]!=y[i])return (int)x[i]-(int)y[i];return 0;}
int strncmp(const char *a,const char *b,size_t n){for(size_t i=0;i<n;i++){if(a[i]!=b[i])return (unsigned char)a[i]-(unsigned char)b[i];if(!a[i])return 0;}return 0;}
API void set_callbacks(void *(*a)(size_t,unsigned),void *(*r)(void*,size_t,unsigned),void (*f)(void*)){allocate_cb=a;resize_cb=r;free_cb=f;}
API void *open_gif(const void *p){return gd_open_gif_data(p);}
API int step_gif(void *p){gd_GIF *g=p;int r=gd_get_frame(g);if(r==1)gd_render_frame(g,g->canvas);return r;}
API void *canvas_gif(void *p){return ((gd_GIF*)p)->canvas;}
API unsigned delay_gif(void *p){return ((gd_GIF*)p)->gce.delay*10;}
API void loops_gif(void *p,int n){((gd_GIF*)p)->loop_count=n;}
API void close_gif(void *p){gd_close_gif(p);}
"""


def verify(dll, path):
    print("Verifying " + path.name, flush=True)
    blocks = {}
    peak = 0
    failure = False
    wrong_caps = []
    alloc_type = ctypes.CFUNCTYPE(ctypes.c_void_p, ctypes.c_size_t, ctypes.c_uint)
    realloc_type = ctypes.CFUNCTYPE(ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_uint)
    free_type = ctypes.CFUNCTYPE(None, ctypes.c_void_p)

    def allocate(size, caps):
        nonlocal peak
        if caps != 3:
            wrong_caps.append(caps)
        if failure:
            return None
        block = ctypes.create_string_buffer(size)
        ptr = ctypes.addressof(block)
        blocks[ptr] = block
        peak = max(peak, sum(ctypes.sizeof(b) for b in blocks.values()))
        return ptr

    def resize(ptr, size, caps):
        if failure:
            return None
        new = allocate(size, caps)
        if ptr:
            ctypes.memmove(new, ptr, min(size, ctypes.sizeof(blocks[ptr])))
            del blocks[ptr]
        return new

    def free(ptr):
        if ptr:
            del blocks[ptr]

    callbacks = (alloc_type(allocate), realloc_type(resize), free_type(free))
    dll.set_callbacks(*callbacks)
    data = ctypes.create_string_buffer(path.read_bytes())
    handle = dll.open_gif(data)
    assert handle, f"Decoder open failed: {path}"
    total_ms = 0
    with Image.open(path) as expected:
        for i in range(expected.n_frames):
            expected.seek(i)
            assert dll.step_gif(handle) == 1, f"Decoder frame failure: {path}:{i}"
            if i == 0:
                dll.loops_gif(handle, 1)
            raw = ctypes.string_at(dll.canvas_gif(handle), 128 * 128 * 4)
            actual = Image.frombytes("RGBA", (128, 128), raw, "raw", "BGRA")
            for color in ("white", "#202020"):
                a = Image.new("RGBA", (128, 128), color)
                b = a.copy()
                a.alpha_composite(actual)
                b.alpha_composite(expected.convert("RGBA"))
                assert a.tobytes() == b.tobytes(), f"Decoder pixel mismatch: {path}:{i}"
            delay = dll.delay_gif(handle)
            assert delay == expected.info["duration"], f"Delay mismatch: {path}:{i}"
            total_ms += delay
        frame_count = expected.n_frames
    last = ctypes.string_at(dll.canvas_gif(handle), 128 * 128 * 4)
    assert dll.step_gif(handle) == 0, "Single playback did not stop"
    assert ctypes.string_at(dll.canvas_gif(handle), 128 * 128 * 4) == last, "EOF cleared final frame"
    dll.close_gif(handle)
    assert not blocks and not wrong_caps, "Allocation leak or internal-memory fallback"
    print("  frames and single playback OK", flush=True)

    # Two passes must reproduce identical frames at the loop boundary.
    handle = dll.open_gif(data)
    dll.loops_gif(handle, 0)
    first = None
    for i in range(frame_count + 1):
        assert dll.step_gif(handle) == 1
        if i == 0:
            first = ctypes.string_at(dll.canvas_gif(handle), 128 * 128 * 4)
        elif i == frame_count:
            assert ctypes.string_at(dll.canvas_gif(handle), 128 * 128 * 4) == first
    dll.close_gif(handle)
    assert not blocks
    print("  repeat boundary OK", flush=True)
    failure = True
    assert not dll.open_gif(data), "Canvas OOM did not fail safely"
    print("  canvas OOM OK", flush=True)
    failure = False
    handle = dll.open_gif(data)
    assert handle
    failure = True
    assert dll.step_gif(handle) == -1, "LZW table OOM did not fail safely"
    dll.close_gif(handle)
    assert not blocks
    return {"file": path.name, "frames_verified": frame_count, "duration_ms": total_ms,
            "tracked_peak_bytes_including_realloc_overlap": peak,
            "pixel_comparison": "all frames on white/dark",
            "single_playback": "pass", "loop_boundary": "pass", "oom_cleanup": "pass"}


def main():
    decoder = ROOT / "main/display/lvgl_display/gif"
    sources = sorted((ROOT / "custom_assets/kanshan_motion_v1/staging").glob("pet_*.gif"))
    if len(sources) != 6:
        raise RuntimeError("Build the six motion assets first")
    with tempfile.TemporaryDirectory(prefix="kanshan-decoder-") as temp:
        work = Path(temp)
        for name, text in STUBS.items():
            (work / name).write_text(text, encoding="utf-8")
        (work / "support.c").write_text(SUPPORT, encoding="utf-8")
        library = work / "decoder.dll"
        command = ["C:/Program Files/LLVM/bin/clang.exe", "--target=x86_64-pc-windows-msvc",
                   "-std=c11", "-O1", "-ffreestanding", "-fno-builtin", "-fno-stack-protector",
                   "-nostdlib", "-shared", "-fuse-ld=lld", "-Wl,/noentry,/nodefaultlib",
                   "-I" + str(work), "-I" + str(decoder), str(decoder / "gifdec.c"),
                   str(work / "support.c"), "-o", str(library)]
        compilation = subprocess.run(command, capture_output=True, text=True, timeout=60)
        if compilation.returncode:
            raise RuntimeError(compilation.stdout + compilation.stderr)
        dll = ctypes.CDLL(str(library))
        dll.open_gif.argtypes = [ctypes.c_void_p]
        dll.open_gif.restype = ctypes.c_void_p
        dll.step_gif.argtypes = [ctypes.c_void_p]
        dll.canvas_gif.argtypes = [ctypes.c_void_p]
        dll.canvas_gif.restype = ctypes.c_void_p
        dll.delay_gif.argtypes = [ctypes.c_void_p]
        dll.delay_gif.restype = ctypes.c_uint
        dll.loops_gif.argtypes = [ctypes.c_void_p, ctypes.c_int]
        dll.close_gif.argtypes = [ctypes.c_void_p]
        result = [verify(dll, path) for path in sources]
        # Unload before TemporaryDirectory removes the Windows DLL.
        import _ctypes
        _ctypes.FreeLibrary(dll._handle)
    report = ROOT / "build/kanshan_decoder_verification.json"
    report.write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
