#!/usr/bin/env python3
import pathlib
import sys


def main():
    path = pathlib.Path(sys.argv[1])
    data = bytearray(path.read_bytes())
    old = b"GetSystemTimePreciseAsFileTime\x00"
    new = b"GetSystemTimeAsFileTime\x00"
    position = data.find(old)
    if position < 0:
        print("Win7 import is already compatible or was not found.")
        return
    data[position:position + len(old)] = new.ljust(len(old), b"\x00")
    path.write_bytes(data)
    print("Patched GetSystemTimePreciseAsFileTime for Windows 7.")


if __name__ == "__main__":
    main()
