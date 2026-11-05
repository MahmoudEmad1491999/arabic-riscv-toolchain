# File system specification (stage 5, decisions 50–52)

A simple file system for the disk of our operating system: one flat directory, Arabic file names, every file in one run of contiguous blocks. Images are made on the host by `مهيئ`; the kernel reads and writes them through the virtio block device.

## Layout

Block size: 512 bytes (one disk sector). All integers little-endian.

| Blocks | Contents |
|---|---|
| 0 | superblock |
| 1–8 | directory: 64 entries of 64 bytes |
| 9 … | file data |

Superblock:

| Offset | Size | Field |
|---|---|---|
| 0 | 8 | magic: the UTF-8 bytes of `دليل` |
| 8 | 4 | version (1) |
| 12 | 4 | block size (512) |
| 16 | 4 | number of blocks on the disk |
| 20 | 4 | number of directory blocks (8) |
| 24 | 4 | first free block: every block from here to the end is unused |

Directory entry (64 bytes):

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | name length in bytes (0: the entry is empty) |
| 1 | 47 | the name, UTF-8, at most 47 bytes |
| 48 | 4 | first block |
| 52 | 4 | size in bytes |
| 56 | 8 | reserved (0) |

A name is any UTF-8 text without `/` and without a zero byte; names are compared byte for byte.

## Rules (A72)

- A file opened for **reading** is read from its blocks; it ends at its size.
- A file opened for **writing** is always written as a new file: its blocks start at the first free block and it grows as it is written. On close it gets its directory entry (an older file with the same name is dropped), and the first free block moves past it. Only one file can be open for writing at a time in the whole system.
- Space of dropped files is not reused (no compaction yet). There is no deletion and no subdirectory yet.

## System calls (decision 52, docs/spec-os.md)

| Number | `نظام.` | Arguments | Result |
|---|---|---|---|
| ٧ | `افتح` | name, mode (`قراءة` ١ or `كتابة` ٢) | a descriptor (0–7), or all ones |
| ٨ | `اقرأ` | descriptor, buffer (`][طبيعي٨`) | bytes read (٠ at the end), or all ones |
| ٩ | `اكتب_في` | descriptor, bytes | bytes written, or all ones |
| ١٠ | `أغلق` | descriptor | ٠, or all ones |

Each process has 8 descriptors; they are closed when it ends.

## `مهيئ`

```
مهيئ قرص.صورة [ملف...]
```

Creates a 4 MiB image (8192 blocks) holding the given files; each file is named after the last part of its path. `tools/build_os.sh` uses it to put the programs (in our executable format) and the files of `os/ملفات` on the disk (decision 54).
