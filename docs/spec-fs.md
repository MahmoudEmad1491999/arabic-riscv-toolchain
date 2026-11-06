# File system specification (stage 5, decisions 50–52; subdirectories: stage 7, decision 61)

A simple file system for the disk of our operating system: nested directories, Arabic names, every file in one run of contiguous blocks. Images are made on the host by `مهيئ`; the kernel reads and writes them through the virtio block device.

## Layout (version 2)

Block size: 512 bytes (one disk sector). All integers little-endian.

| Blocks | Contents |
|---|---|
| 0 | superblock |
| 1–8 | the root directory |
| 9 … | files and other directories |

Every directory, the root included, is **8 contiguous blocks holding 64 entries of 64 bytes**.

Superblock:

| Offset | Size | Field |
|---|---|---|
| 0 | 8 | magic: the UTF-8 bytes of `دليل` |
| 8 | 4 | version (2) |
| 12 | 4 | block size (512) |
| 16 | 4 | number of blocks on the disk |
| 20 | 4 | blocks per directory (8) |
| 24 | 4 | first free block: every block from here to the end is unused |

Directory entry (64 bytes):

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | name length in bytes (0: the entry is empty) |
| 1 | 47 | the name, UTF-8, at most 47 bytes |
| 48 | 4 | first block |
| 52 | 4 | size in bytes (a directory: 4096) |
| 56 | 1 | kind: 0 file, 1 directory |
| 57 | 7 | reserved (0) |

A name is any UTF-8 text without `/` and without a zero byte; names are compared byte for byte.

## Paths (A79)

A path is names separated by `/`, always from the root (there is no current directory). One `/` at the start is allowed and means the same. An empty name (`أ//ب`, or a `/` at the end), `.` and `..` are not special: an empty name is invalid, `.` and `..` are ordinary names. Every directory on the way must exist.

## Rules (A72, A79)

- A file opened for **reading** is read from its blocks; it ends at its size. A directory cannot be opened.
- A file opened for **writing** is always written as a new file: its blocks start at the first free block and it grows as it is written. On close it gets an entry in its directory (an older file with the same name is dropped), and the first free block moves past it. Only one file can be open for writing at a time in the whole system. A name that is a directory cannot be opened for writing.
- **Creating a directory** takes 8 zeroed blocks at the first free block and an entry in its parent. It fails while a file is open for writing (that file grows exactly there), when the parent is missing or full, or when the name is a file; an existing directory of that name is reported, not an error.
- Space of replaced files is not reused (no compaction yet). There is no deletion yet.

## System calls (decisions 52, 61; docs/spec-os.md)

| Number | `نظام.` | Arguments | Result |
|---|---|---|---|
| ٧ | `افتح` | path, mode (`قراءة` ١ or `كتابة` ٢) | a descriptor (0–7), or all ones |
| ٨ | `اقرأ` | descriptor, buffer (`][طبيعي٨`) | bytes read (٠ at the end), or all ones |
| ٩ | `اكتب_في` | descriptor, bytes | bytes written, or all ones |
| ١٠ | `أغلق` | descriptor | ٠, or all ones |
| ١٢ | `مدخل_الدليل` | directory path (`""` or `"/"`: the root), position, buffer | the length of the name of the entry at that position, a directory's name followed by `/`; ٠ after the last (or if there is no such directory) |
| ١٤ | `أنشئ_دليل` | path | ٠ created, ١ already a directory, or all ones |

Each process has 8 descriptors; they are closed when it ends.

## `مهيئ`

```
مهيئ قرص.صورة ]ملف | مسار=ملف | دليل/[...
```

Creates a 32 MiB image (65536 blocks):

- `ملف` puts a host file in the root, named after the last part of its path;
- `مسار=ملف` puts it at that path on the disk;
- `دليل/` makes an empty directory.

Directories on the way are created as needed. `tools/build_os.sh` uses it to put the programs (in our executable format) and the files of `os/ملفات` on the disk (decision 54).
