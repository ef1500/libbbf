<div align="center">

# libbbf: Bound Book Format 


![Format](https://img.shields.io/badge/Format-BBFv3-blue.svg?style=flat-square&color=007ec6)
![License](https://img.shields.io/badge/License-MIT-green.svg?style=flat-square&color=44cc11)

<br/>

[![Crates.io](https://img.shields.io/crates/v/boundbook?style=flat-square&logo=rust&color=orange)](https://crates.io/crates/boundbook)
[![AUR](https://img.shields.io/aur/version/libbbf?style=flat-square&logo=arch-linux&color=blue)](https://aur.archlinux.org/packages/libbbf)
[![openSUSE](https://img.shields.io/badge/openSUSE%20Package-64b42d?style=flat-square&logo=opensuse&logoColor=white)](https://build.opensuse.org/package/show/Publishing/libbbf)
[![PyPI](https://img.shields.io/pypi/v/libbbf?style=flat-square&logo=python&logoColor=white&color=3776ab)](https://pypi.org/project/libbbf/)

</div>

---
Bound Book Format (.bbf) is a binary container format intended for the ordered storage of page-based media assets. BBF was designed principally for comics, manga, artbooks, and similar sequential image collections. 

Bound Book Format additionally enables paginated retrival, asset deduplication, fast indexed access to page order and metadata, and MKV-like sectioning.

---

## Getting Started

### Prerequisites (Muxer, Library, General Use)
- C++17 compliant compiler (GCC/Clang/MSVC), and optionally CMake
- [xxHash](https://github.com/Cyan4973/xxHash) library

### Prerequisites (Debugging/Benchmarking)
- [xxHash](https://github.com/Cyan4973/xxHash) library
- [Catch2](https://github.com/catchorg/Catch2)
- [Miniz](https://github.com/richgel999/miniz)
- CMake

### Prerequisites (WASM)
- [xxHash](https://github.com/Cyan4973/xxHash) library
- [Emscripten](https://emscripten.org/)
- CMake

### Compilation

#### CMake

```bash
cmake -B build
cmake --build build
sudo cmake --install build
```

#### Manual Compilation

Linux
```bash
g++ -std=c++17 -O2 -I./src -I./src/muxer -I./src/vend src/muxer/bbfmux.cpp src/bbfcodec.cpp src/muxer/dedupemap.cpp src/muxer/stringpool.cpp src/vend/xxhash.c -o bbfmux
```

Windows
```bash
g++ -std=c++17 -O2 -I./src -I./src/muxer -I./src/vend src/muxer/bbfmux.cpp src/bbfcodec.cpp src/muxer/dedupemap.cpp src/muxer/stringpool.cpp src/vend/xxhash.c -o bbfmux
```

Alternatively, if you need python support, use [libbbf-python](https://github.com/ef1500/libbbf-python). 

---

## Technical Details

BBF files are footer-indexed, and the footer can be at either after the header (`petrified`) for fast reading, or at the bottom (`default`).

### Feature Comparison: BBF v3 (libbbf) and Common Comic Storage Containers

> Legend: ✅ = specified in-format; ⚠️ = optional / conventional; ❌ = not in spec

| Capability | **BBF v3 (libbbf)** | CBZ (ZIP) | CBR (RAR) | PDF | EPUB | Folder |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **Memory-mapped parsing** | ✅ | ✅ [A] | ✅ [B] | ✅ [C] |  ✅ [A][D] | ✅ |
| **Adjustable alignment** | ✅ | ❌ | ❌ | ❌ | ❌ | ❌ |
| **Variable alignment (size-based packing)** | ✅ | ❌ | ❌ | ❌ | ❌ | ❌ |
| **Content-hash deduplication** | ✅ | ❌ | ❌ | ⚠️ [E] | ⚠️ [F] | ⚠️ [G] |
| **Per-asset integrity value stored with each asset** | ✅ (XXH3-128) | ⚠️ (CRC32) [H] | ✅ (CRC32/BLAKE2sp) [I] | ❌ | ⚠️ (CRC32) [H] | ❌ |
| **Single index-region checksum** | ✅ (XXH3-64) | ❌ [J] | ✅ [K] | ❌ | ❌ | ❌ |
| **Index-first layout / linearization** | ✅ (Petrification) | ❌ [L] | ⚠️ [M] | ✅ [N] | ❌ [L] | ❌ |
| **Arbitrary metadata** | ✅ | ⚠️ [O] | ⚠️ [P] | ✅ [Q] | ✅ [R] | ⚠️ [S] |
| **Hierarchical sectioning / chapters** | ✅ | ❌ | ❌ | ✅ [T] | ✅ [U] | ⚠️ [V] |
| **Customizable page ordering independent of filenames** | ✅ | ❌ | ❌ | ✅ [W] | ✅ [U] | ❌ |
| **Customizable expansion entries** | ✅ (expansion table) | ✅ [X] | ✅ [Y] | ✅ [Z] | ✅ [AA] | ❌ |

<sub>
[A] Only in STORE mode, which CBZ is typically in.<br/>
[B] Only in STORE mode, which CBR is typically in.<br/>
[C] PDFs often store page content/images in filtered (compressed) streams; mmap helps parsing.<br/>
[D] EPUB is a ZIP-based container with additional rules; it inherits ZIP's container properties.<br/>
[E] PDF can reuse already-stored objects/resources by reference, but there is no hash-based deduplication mechanism.<br/>
[F] EPUB can reference the same resource from multiple content documents (file-level reuse), but does not have hash-based a deduplication mechanism.<br/>
[G] Folder-level deduplication is a filesystem matter (hardlinks, block-dedupe, etc.).<br/>
[H] ZIP/EPUB store CRC-32 per member.<br/>
[I] RAR stores per-file checksums; RAR5 can use BLAKE2sp instead of CRC32.<br/>
[J] ZIP has per-entry CRC32; an archive-wide checksum is not a baseline ZIP requirement (there are optional signing/encryption features).<br/>
[K] RAR has header/file checksums.<br/>
[L] ZIP's "end of central directory" structure is tail-located by design; the canonical index is at the end.<br/>
[M] RAR5 may include an optional "quick open record" placed at the end to accelerate listing, but it is not index-first linearization.<br/>
[N] PDF defines "Linearized PDF" (fast web view) as a standardized layout.<br/>
[O] ZIP provides extra fields and stores application-defined manifest files.<br/>
[P] RAR provides "extra area" records and archive comments; structured metadata is not standardized as key:value pairs.<br/>
[Q] PDF includes a metadata model (e.g., XMP) and document structure/navigation mechanisms; it is rich, but correspondingly intricate.<br/>
[R] EPUB's package document supports metadata and a defined reading order via the spine.<br/>
[S] Folder metadata is may differ by operating system.<br/>
[T] PDF may contain a document outline (bookmarks) as a navigational hierarchy.<br/>
[U] EPUB defines a default reading order via the spine (and navigation documents for the table of contents).<br/>
[V] A folder can simulate sectioning with subdirectories, but no interoperable "chapter table" exists.<br/>
[W] PDF page order is defined by the page tree structure.<br/>
[X] ZIP extra fields are a per-record entry and not table-based.<br/>
[Y] RAR extra area records are entries with forward-compatible skipping rules.<br/>
[Z] PDF defines formal extension rules (name classes, extension mechanisms).<br/>
[AA] EPUB permits additional resources and metadata.
</sub>


### Graphical Comparison (BBF vs. CBZ)
A graphical representation of `bbfbench` is given below. To verify (or to see BBF's performance metrics on your particular system), use the bbfbench program provided in `./src/bench/`.

<img width="100%" alt="bbf_vs_cbz_benchmark" src="https://github.com/user-attachments/assets/91c43861-6d39-4bdf-86d0-f4e0d3850170">

---

## Features

### Memory Mapping
Libbbf uses memory mapping to load files into memory and read them quickly.

### Adjustable Alignment (Ream Size)
You can adjust the alignment between assets. The default alignment is $2^{12}$ (4096 bytes).

### Deduplication
Libbbf deduplicates assets using `XXH128`, and stores the asset hash for future
verification purposes.

### XXH3-64 Footer Checksum
Libbbf uses an XXH3-64 hash to checksum the asset, page, and string tables.

### Linearization (Petrification)
For those self-hosting, BBF can relocate the index and footer to immediately follow the header. This allows readers to parse the header and footer with a single initial block read of 280 bytes. 

### Arbitrary Metadata
Libbbf supports arbitrary key:value pairs with optional parent labels (key:value\[:parent\]).

### MKV-Like Sectioning
Support for nested chapters and groupings using parent-labeled sections, allowing for table-of-contents structures.

### Customizable Page-Ordering
Logical reading order is decoupled from physical asset storage by using a dedicated Page Table, so custom demuxing/page orders can be specified.

### Customizable Expansion Entries
Libbbf's spec includes expansion entries, which can be used to expand the functionality of bbf files.

### Variable Alignment (Variable Ream Size)
BBF files support power-of-two alignment (up to 2^16). To minimize internal fragmentation, a "Variable Ream" flag allows smaller assets to be packed to 8-byte boundaries while larger assets remain aligned to the primary ream size.

---

## CLI Usage: `bbfmux`

The included `bbfmux` tool is a reference implementation for muxing/demuxing BBF files.

```bash
========[ BBFMUX v3.0 ]====================================================
| Bound Book Format Muxer                             Developed by EF1500 |
===========================================================================

USAGE: bbfmux <INPUT_DIR|BBF_FILE> [MODE] [OPTIONS]...

MODES (Mutually Exclusive):
  (Default)    Mux folder contents into a BBF container
  --info       Display headers, metadata, and statistics
  --verify     Validate XXH3-128/64 hashes
  --extract    Unpack contents to disk
  --petrify    Linearize BBF file for faster reading

MUXER OPTIONS:
  --meta=K:V[:P]         Add metadata (Key:Value[:Parent])
  --metafile=<FILE>      Read K:V:P entries from file
  --section=N:T[:P]      Add section (Name:Target[:Parent])
  --sections=<FILE>      Read section entries from file
  --ream-size=<N>        Ream size exponent override (2^N)
  --alignment=<N>        Byte alignment exponent override (2^N)
  --variable-ream-size   Enable variable ream sizing (reccomended)

VERIFY / EXTRACT OPTIONS:
  --section="NAME"    Target specific section
  --rangekey="KEY"    Stop extraction on key substring match
  --asset=<ID>        Target specific asset ID
  --outdir=[PATH]     Extract asset(s) to directory
  --write-meta[=F]    Dump metadata to file [default: path.txt]
  --write-hashes[=F]  Dump hashes to file [default: hashes.txt]

INFO FLAGS:
  --hashes, --footer, --sections, --counts, --header, --metadata, --offsets
```

## CLI Features

The `bbfmux` utility provides an example interface for managing Bound Book files:

*   Create BBF Files by specifying a directory you wish to mux
*   Add named sections (Chapters, Volumes, Extras, Galleries) to define the internal hierarchy of the book.
*   Embed arbitrary Key:Value pairs into the global string pool for archival indexing.
*   Extract the entire book or target specific sections by name using a `rangekey`.

## Usage Examples

### Create a new BBF
You can mix individual images and folders. `bbfmux` sorts inputs alphabetically, deduplicates identical assets, and aligns data to user-defined boundaries (2^N).

```bash
# Basic creation with metadata
bbfmux ./chapter1/ \
  --meta=Title:"Akira" \
  --meta=Author:"Katsuhiro Otomo" \
  --meta=Tags:"[Action, Sci-Fi, Cyberpunk]" \
  akira.bbf
```

### Hierarchical Sections (Volumes & Chapters)
BBF supports nesting sections. By defining a parent label, you can group chapters into volumes. This allows readers to display a nested Table of Contents and enables bulk-extraction of entire volumes.

**Syntax:** `--section="Name":Page[:ParentName]`

```bash
# Create a book with nested chapters
bbfmux ./manga_folder/ \
  --section="Volume 1":1 \
  --section="Chapter 1":1:"Volume 1" \
  --section="Chapter 2":20:"Volume 1" \
  --section="Volume 2":180 \
  --section="Chapter 3":180:"Volume 2" \
  manga.bbf
```

### Verify Integrity
Scan the archive for data corruption. BBF uses **[XXH128](https://github.com/Cyan4973/xxHash)** hashes to verify every individual image payload.
```bash
bbfmux input.bbf --verify
```

### Extract Data
Extract the entire book, use the `--extract` option with `--asset=-1`. To extract a specific volume, or a single chapter by using the `--extract` option with `--rangekey`.

**Extract a specific section:**
```bash
bbfmux input.bbf --extract --section="Volume 1" --rangekey="Volume 1" --outdir="./Volume1"
```

**Extract the entire book:**
```bash
bbfmux input.bbf --extract --asset=-1 --outdir="./unpacked_book"
```

### View Metadata & Structure
View the version, page count, deduplication stats, hierarchical sections, and all embedded metadata.
```bash
bbfmux input_book.bbf --info --header --footer --metadata --sections
```

---

## Advanced CLI Features

`bbfmux` also supports more advanced options, allowing full-control over your `.bbf` files.

### Batch Section Import (`--sections`)
Sections define Chapters, Volumes or other custom labels. You can target a page by its filename.
```bash
# Target by filename
bbfmux ./folder/ --section="Chapter 1":"001.png" out.bbf

# Using a sections file
bbfmux ./folder/ --sections=sectionexample.txt out.bbf

# sectionexample.txt example (Name:Target[:Parent]):
"Volume 1":"001.png"
"Chapter 1":"001.png":"Volume 1"
"Chapter 2":"050.png":"Volume 1"
```

### Targeted Verification
BBF allows for verification of data to detect data corruption.
```bash
# Verify everything (All assets and Directory structure)
bbfmux input.bbf --verify

# Verify first asset
bbfmux input.bbf --verify --asset=1

# Verify a specific section
bbfmux input.bbf --verify --section="Volume 1"
```

### Range-Key Extraction
The `--rangekey` option allows you to extract a range of sections. The extractor starts at the specified `--section` and stops when it finds a section whose title contains a substring specified by `--rangekey`.

```bash
# Extract Chapter 2 up until it hits Chapter 4
bbfmux manga.bbf --extract --section="Chapter 2" --rangekey="Chapter 4" --outdir="./Ch2_to_Ch4"

# Extract Volume 2 until it encounters the string "Chapter 60"
bbfmux manga.bbf --extract --section="Volume 2" --rangekey="Chapter 60" --outdir="./Volume_2_to_Chapter_60"
```

---

## License
Distributed under the MIT License. See `LICENSE` for more information.
