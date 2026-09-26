"""Turn a binary file into a C header so it is compiled into the game.

    python tools/embed.py res/intro.mp3 res/intro_mp3.h INTRO_MP3
"""
import sys
from pathlib import Path


def main(src: str, dst: str, name: str) -> None:
    data = Path(src).read_bytes()
    lines = [f"/* Generated from {Path(src).as_posix()} by tools/embed.py - do not edit. */",
             f"static const unsigned int {name}_SIZE = {len(data)}u;",
             f"static const unsigned char {name}[] = {{"]
    for i in range(0, len(data), 20):
        lines.append("    " + ",".join(str(b) for b in data[i:i + 20]) + ",")
    lines.append("};")
    Path(dst).write_text("\n".join(lines) + "\n")
    print(f"embedded {src} ({len(data)} bytes) as {name} in {dst}")


if __name__ == "__main__":
    main(*sys.argv[1:4])
