#!/usr/bin/env python3
import pathlib
import struct
import sys


def align(value, boundary):
    return (value + boundary - 1) & ~(boundary - 1)


def directory(entries):
    return struct.pack("<IIHHHH", 0, 0, 0, 0, 0, len(entries)) + b"".join(
        struct.pack("<II", name, offset) for name, offset in entries
    )


def main():
    exe_path = pathlib.Path(sys.argv[1])
    ico_path = pathlib.Path(sys.argv[2])
    exe = bytearray(exe_path.read_bytes())
    ico = ico_path.read_bytes()

    _, icon_type, count = struct.unpack_from("<HHH", ico, 0)
    if icon_type != 1 or not count:
        raise SystemExit("Invalid ICO file")

    icons = []
    for index in range(count):
        values = struct.unpack_from("<BBBBHHII", ico, 6 + index * 16)
        width, height, colors, reserved, planes, bits, size, offset = values
        icons.append((values, ico[offset:offset + size]))

    pe_offset = struct.unpack_from("<I", exe, 0x3C)[0]
    if exe[pe_offset:pe_offset + 4] != b"PE\x00\x00":
        raise SystemExit("Invalid PE file")
    coff = pe_offset + 4
    section_count = struct.unpack_from("<H", exe, coff + 2)[0]
    optional_size = struct.unpack_from("<H", exe, coff + 16)[0]
    optional = coff + 20
    if struct.unpack_from("<H", exe, optional)[0] != 0x10B:
        raise SystemExit("Only PE32 is supported")

    section_alignment = struct.unpack_from("<I", exe, optional + 32)[0]
    file_alignment = struct.unpack_from("<I", exe, optional + 36)[0]
    section_table = optional + optional_size
    new_header = section_table + section_count * 40
    first_raw = min(
        struct.unpack_from("<I", exe, section_table + i * 40 + 20)[0]
        for i in range(section_count)
    )
    if new_header + 40 > first_raw:
        raise SystemExit("No room for another PE section header")

    last = section_table + (section_count - 1) * 40
    last_virtual_size = struct.unpack_from("<I", exe, last + 8)[0]
    last_virtual_address = struct.unpack_from("<I", exe, last + 12)[0]
    last_raw_size = struct.unpack_from("<I", exe, last + 16)[0]
    resource_rva = align(
        last_virtual_address + max(last_virtual_size, last_raw_size),
        section_alignment,
    )
    resource_raw = align(len(exe), file_alignment)

    root_size = 16 + 2 * 8
    icon_type_offset = root_size
    icon_type_size = 16 + count * 8
    group_type_offset = icon_type_offset + icon_type_size
    group_type_size = 16 + 8
    icon_languages_offset = group_type_offset + group_type_size
    group_language_offset = icon_languages_offset + count * 24
    data_entries_offset = group_language_offset + 24
    blob_offset = align(data_entries_offset + (count + 1) * 16, 4)

    icon_blob_offsets = []
    cursor = blob_offset
    for _, blob in icons:
        icon_blob_offsets.append(cursor)
        cursor = align(cursor + len(blob), 4)
    group_blob_offset = cursor

    group_blob = struct.pack("<HHH", 0, 1, count)
    for index, (values, _) in enumerate(icons):
        width, height, colors, reserved, planes, bits, size, _ = values
        group_blob += struct.pack(
            "<BBBBHHIH", width, height, colors, reserved,
            planes, bits, size, index + 1
        )
    resource_size = group_blob_offset + len(group_blob)
    resource = bytearray(align(resource_size, file_alignment))

    resource[0:root_size] = directory([
        (3, 0x80000000 | icon_type_offset),
        (14, 0x80000000 | group_type_offset),
    ])
    icon_entries = []
    for index in range(count):
        language_offset = icon_languages_offset + index * 24
        icon_entries.append((index + 1, 0x80000000 | language_offset))
    resource[icon_type_offset:icon_type_offset + icon_type_size] = directory(icon_entries)
    resource[group_type_offset:group_type_offset + group_type_size] = directory([
        (1, 0x80000000 | group_language_offset)
    ])

    for index in range(count):
        language_offset = icon_languages_offset + index * 24
        data_offset = data_entries_offset + index * 16
        resource[language_offset:language_offset + 24] = directory([
            (1033, data_offset)
        ])
        blob = icons[index][1]
        struct.pack_into(
            "<IIII", resource, data_offset,
            resource_rva + icon_blob_offsets[index], len(blob), 0, 0
        )
        start = icon_blob_offsets[index]
        resource[start:start + len(blob)] = blob

    group_data_offset = data_entries_offset + count * 16
    resource[group_language_offset:group_language_offset + 24] = directory([
        (1033, group_data_offset)
    ])
    struct.pack_into(
        "<IIII", resource, group_data_offset,
        resource_rva + group_blob_offset, len(group_blob), 0, 0
    )
    resource[group_blob_offset:group_blob_offset + len(group_blob)] = group_blob

    if len(exe) < resource_raw:
        exe.extend(b"\x00" * (resource_raw - len(exe)))
    exe.extend(resource)
    section = struct.pack(
        "<8sIIIIIIHHI", b".rsrc\x00\x00\x00", resource_size, resource_rva,
        len(resource), resource_raw, 0, 0, 0, 0, 0x40000040
    )
    exe[new_header:new_header + 40] = section
    struct.pack_into("<H", exe, coff + 2, section_count + 1)
    struct.pack_into(
        "<I", exe, optional + 56,
        align(resource_rva + resource_size, section_alignment)
    )
    struct.pack_into("<II", exe, optional + 96 + 2 * 8, resource_rva, resource_size)
    struct.pack_into("<I", exe, optional + 64, 0)
    exe_path.write_bytes(exe)
    print("Embedded application icon.")


if __name__ == "__main__":
    main()
