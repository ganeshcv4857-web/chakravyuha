"""Assemble dist/Chakravyuha-v<version>-windows.zip from the release build.

    python tools/package_windows.py 1.0.0
"""
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def main(version: str) -> None:
    out = ROOT / "dist" / f"Chakravyuha-v{version}-windows.zip"
    out.parent.mkdir(exist_ok=True)
    files = {
        "Chakravyuha/Chakravyuha.exe": ROOT / "build" / "windows" / "Chakravyuha.exe",
        "Chakravyuha/README.txt": ROOT / "packaging" / "README.txt",
        "Chakravyuha/assets/README.txt": ROOT / "packaging" / "assets-README.txt",
    }
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for name, src in files.items():
            z.write(src, name)
    print(f"built {out.relative_to(ROOT)} ({out.stat().st_size // 1024} KB)")


if __name__ == "__main__":
    main(sys.argv[1])
