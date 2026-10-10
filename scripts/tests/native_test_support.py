"""Run small C++ policy tests with Windows LLVM, without Visual Studio/IDF."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def run_native_test(case, sources, include_dirs=()):
    if os.name != "nt":
        case.skipTest("Windows freestanding host runner")
    candidates = [shutil.which("clang++"), "C:/Program Files/LLVM/bin/clang++.exe"]
    compiler = None
    for candidate in dict.fromkeys(candidates):
        if not candidate or not Path(candidate).is_file():
            continue
        targets = subprocess.run([candidate, "--print-targets"], capture_output=True,
                                 text=True, timeout=20)
        if targets.returncode == 0 and "x86" in targets.stdout:
            compiler = candidate
            break
    if compiler is None:
        case.skipTest("Windows x86-capable clang++ not installed")
    with tempfile.TemporaryDirectory(prefix="xiaozhi-native-test-") as temp:
        binary = Path(temp) / "test.exe"
        command = [compiler, "--target=x86_64-pc-windows-msvc", "-std=c++17", "-O1",
                   "-Wall", "-Wextra", "-Werror", "-ffreestanding", "-fno-builtin",
                   "-fno-stack-protector", "-nostdlib", "-fuse-ld=lld",
                   "-Wl,/entry:main,/subsystem:console,/nodefaultlib"]
        command += ["-I" + str(ROOT / path) for path in include_dirs]
        command += [str(ROOT / path) for path in sources]
        command += ["-o", str(binary)]
        result = subprocess.run(command, capture_output=True, text=True, timeout=60)
        case.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        result = subprocess.run([str(binary)], capture_output=True, timeout=20)
        case.assertEqual(result.returncode, 0,
                         f"Native test failed at C++ source line {result.returncode}")
