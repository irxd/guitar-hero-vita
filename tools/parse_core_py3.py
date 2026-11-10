#!/usr/bin/env python3
# Runs xyzz/vita-parse-core (written for Python 2) under Python 3 without
# modifying its files: replaces its two Python-2-only helpers in util.py
# (c_str, hexdump) before the other modules import them.
#
# Usage: parse_core_py3.py <vita-parse-core dir> <psp2dmp> <elf>
import string
import sys


def c_str(buf, off):
    end = off
    while end < len(buf) and buf[end] != 0:
        end += 1
    return bytes(buf[off:end]).decode("latin-1")


def hexdump(src, length=16):
    printable = set((string.digits + string.ascii_letters + string.punctuation).encode())
    lines = []
    for c in range(0, len(src), length):
        chunk = bytes(src[c:c + length])
        hexpart = " ".join("%02x" % b for b in chunk)
        if len(hexpart) > 24:
            hexpart = "%s %s" % (hexpart[:24], hexpart[24:])
        text = "".join(chr(b) if b in printable else "." for b in chunk)
        lines.append("%08x:  %-*s  |%s|\n" % (c, length * 3, hexpart, text))
    print("".join(lines))


def main():
    if len(sys.argv) != 4:
        sys.exit("usage: parse_core_py3.py <vita-parse-core dir> <psp2dmp> <elf>")
    tool_dir, dump, elf = sys.argv[1:]
    sys.path.insert(0, tool_dir)
    import util  # noqa: E402  (the tool's own module)

    util.c_str = c_str
    util.hexdump = hexdump
    import main as tool_main  # noqa: E402

    sys.argv = ["main.py", dump, elf]
    tool_main.main()


if __name__ == "__main__":
    main()
