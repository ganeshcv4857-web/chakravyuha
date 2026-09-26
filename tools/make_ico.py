"""Pack res/icon_<size>.png into res/chakravyuha.ico (PNG-compressed entries).

Windows Vista and later read PNG data inside .ico files directly, so each
size is stored exactly as make_icon rendered it.
"""
import struct
import sys
from pathlib import Path

SIZES = [16, 24, 32, 48, 64, 128, 256]


def main(root: Path) -> None:
    images = [(s, (root / "res" / f"icon_{s}.png").read_bytes()) for s in SIZES]

    header = struct.pack("<HHH", 0, 1, len(images))  # reserved, type = icon, count
    offset = len(header) + 16 * len(images)
    entries, blobs = b"", b""
    for size, png in images:
        dim = 0 if size >= 256 else size  # 0 means 256 in the directory entry
        entries += struct.pack("<BBBBHHII", dim, dim, 0, 0, 1, 32, len(png), offset)
        blobs += png
        offset += len(png)

    out = root / "res" / "chakravyuha.ico"
    out.write_bytes(header + entries + blobs)
    print(f"wrote {out} ({len(images)} sizes)")


if __name__ == "__main__":
    main(Path(sys.argv[1] if len(sys.argv) > 1 else "."))
