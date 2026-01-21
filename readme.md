# libbbf: Bound Book Format 

![alt text](https://img.shields.io/badge/Format-BBF1-blue.svg)
![alt text](https://img.shields.io/badge/License-MIT-green.svg)

> [!WARNING]
> **Official Source Notice: Please only download releases from this repository (ef1500/libbbf). External mirrors or forks may contain malware.**

Bound Book Format (.bbf) is a high-performance binary container designed specifically for digital comic books and manga. Unlike CBR/CBZ, BBF is built for DirectSotrage/mmap, easy integrity checks, and mixed-codec containerization.

---

## Getting Started

### Prerequisites
- C++17 compliant compiler (GCC/Clang/MSVC), and optionally CMake
- [xxHash](https://github.com/Cyan4973/xxHash) library

### Compilation

#### CMake

```bash
cmake -B build
cmake --build build
sudo cmake --install build
```

#### Manual

Linux
```bash
g++ -std=c++17 bbfenc.cpp libbbf.cpp xxhash.c -o bbfmux -pthread
```

Windows
```bash
g++ -std=c++17 bbfenc.cpp libbbf.cpp xxhash.c -o bbfmux -municode
```

Alternatively, if you need python support, use [libbbf-python](https://github.com/ef1500/libbbf-python). 

---

## Technical Details

BBF is designed as a Footer-indexed binary format. This allows for rapid append-only creation and immediate random access to any page without scanning the entire file.

### MMAP Compatibility
The `bbfmux` reference implementation utilizes **Memory Mapping (mmap/MapViewOfFile)**. Instead of reading file data into intermediate buffers, the tool maps the container directly into the process address space. This allows the CPU to access image data at the speed of your NVMe drive's hardware limit.

### High-Speed Parallel Verification
Integrity checks utilize **Parallel XXH3**. On multi-core systems, the verifier splits the asset table into chunks and validates multiple pages simultaneously. This makes BBF verification up to **10x faster** than ZIP/RAR CRC checks.

### 4KB Alignment
Every asset in a BBF file starts on a **4096-byte boundary**. This alignment is critical for modern hardware, allowing for DirectStorage transfers directly from disk to GPU memory, bypassing CPU bottlenecks entirely.

Note: DirectStorage isn't avaliable for images yet (as far as I know), but I've made sure to accomodate such a thing in the future with this format.

### Binary Layout
1. **Header (13 bytes)**: Magic `BBF1`, versioning, and initial padding.
2. **Page Data**: The raw image payloads (AVIF, PNG, etc.), each padded to **4096-byte boundaries**.
4. **String Pool**: A deduplicated pool of null-terminated strings for metadata and section titles.
5. **Asset Table**: A registry of physical data blobs with XXH3 hashes.
6. **Page Table**: The logical reading order, mapping logical pages to assets.
7. **Section Table**: Markers for chapters, volumes, or gallery sections.
8. **Metadata Table**: Key-Value pairs for archival data (Author, Scanlation team, etc.).
9. **Footer (76 bytes)**: Table offsets and a final integrity hash.

NOTE: `libbbf.h` includes a `flags` field, as well as extra padding for each asset entry. This is so that in the future `libbbf` can accomodate future technical advancements in both readers and image storage. I.E. If images support DirectStorage in the future, then BBF will be able to use it.

### Feature Comparison: Digital Comic & Archival Formats

| Feature | **BBF** | CBZ (Zip) | CBR (Rar) | PDF | EPUB | Folder |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **Random Page Access** | ✅ | ✅[8] | ✅[8] | ✅ | ❌ | ✅ |
| **Native Data Deduplication** | ✅ | ❌ | ❌ | ⚠️ [1] | ❌ | ❌ |
| **Per-Asset Integrity (XXH3)** | ✅ | ⚠️[9] | ⚠️[9] | ❌ | ❌ | ❌ |
| **4KB Sector Alignment** | ✅ | ❌ | ❌ | ❌ | ❌ | ❌ |
| **Native Sections/Chapters** | ✅ | ❌ | ❌ | ✅ | ✅ | ❌ |
| **Arbitrary Metadata (UTF-8)** | ✅ | ⚠️ [2] | ❌ | ✅ | ✅ | ❌ |
| **Mixed-Codec Support** | ✅ | ✅ | ✅ | ❌ | ✅ | ✅ |
| **DirectStorage/mmap Ready** | ✅ | ❌ | ❌ | ❌ | ❌ | ⚠️ [3] |
| **Low Parser Complexity** | ✅ | ⚠️ [4] | ❌ | ❌ | ❌ | ✅ |
| **Bit-Rot Detection** | ✅ | ⚠️ [5] | ⚠️ [5] | ❌ | ❌ | ❌ |
| **Streaming-Friendly Index** | ⚠️ [6] | ⚠️ [6] | ❌ | ✅ [7] | ⚠️ | ❌ |
| **Wide Software Support** | ❌ | ✅ | ✅ | ✅ | ✅ | ✅ |

<font size="2">
[1] - PDF supports XObjects to reuse resources, but lacks native content-hash deduplication; identical images must be manually referenced.<br/>
[2] - CBZ does not support metadata natively in the ZIP spec; it relies on unofficial sidecar files like <code>ComicInfo.xml</code>.<br/>
[3] - While folders allow memory mapping, individual images within them are rarely sector-aligned for optimized DirectStorage throughput.<br/>
[4] - ZIP/RAR require large, complex libraries (zlib/libarchive); BBF is a "Plain Old Data" (POD) format requiring only a few lines of C++ to parse.<br/>
[5] - ZIP/RAR use CRC32, which is aging, collision-prone, and significantly slower to verify than XXH3 for large archival collections. See [8].<br/>
[6] - Because the index is at the end (Footer), web-based streaming requires a "Range Request" to the end of the file before reading pages.<br/>
[7] - PDF supports "Linearization" (Fast Web View), allowing the header and first pages to be read before the rest of the file is downloaded.<br/>
[8] - As Reddit properly corrected me, ZIP/RAR does have random access. <br/>
[9] - While I think CRC32 is a legacy hash format, ZIP/RAR does have verification ability, though somewhat outdated. See [5].<br/>

</font>

### Graphical Comparison (BBF vs. CBZ)
<img width="4372" height="2888" alt="performance_grid" src="https://github.com/user-attachments/assets/34f38fc6-eb25-4b0d-bc8c-cbda00be3d8a" />

---

## Features

### Content Deduplication
BBF uses **[XXH3_64](https://github.com/Cyan4973/xxHash)** hashing to identify identical pages. If a book contains duplicate pages, the data is stored exactly once on disk while being referenced multiple times in the Page Table.

### Archival Integrity
BBF stores a 64-bit hash for *every individual asset*. The `bbfmux --verify` command can pinpoint exactly which page has been damaged, rather than simply failing to open the entire archive.

### Mixed-Codec Support
Preserve covers in **Lossless PNG** while encoding internal story pages in **AVIF** to save 70% space. BBF explicitly flags the codec for every asset, allowing readers to initialize the correct decoder instantly without "guessing" the file type.

---

## CLI Usage: `bbfmux`

The included `bbfmux` tool is a reference implementation for creating and managing BBF files.

## CLI Features

The `bbfmux` utility provides a powerful interface for managing Bound Book files:

*   **Flexible Ingestion**: Create books by passing individual files, entire directories, or a mix of both.
*   **Logical Structuring**: Add named **Sections** (Chapters, Volumes, Extras, Galleries) to define the internal hierarchy of the book.
*   **Custom Metadata**: Embed arbitrary Key:Value pairs into the global string pool for archival indexing.
*   **Content-Aware Extraction**: Extract the entire book or target specific sections by name.

## Usage Examples

### Create a new BBF
You can mix individual images and folders. `bbfmux` sorts inputs alphabetically, deduplicates identical assets, and aligns data to 4096-byte boundaries. See [Advanced CLI Usage](https://github.com/ef1500/libbbf?tab=readme-ov-file#advanced-cli-features) for how to specify your own custom page orders.

```bash
# Basic creation with metadata
bbfmux cover.png ./chapter1/ endcard.png \
  --meta=Title:"Akira" \
  --meta=Author:"Katsuhiro Otomo" \
  --meta=Tags:"[Action, Sci-Fi, Cyberpunk]" \
  akira.bbf
```

### Hierarchical Sections (Volumes & Chapters)
BBF supports nesting sections. By defining a Parent relationship, you can group chapters into volumes. This allows readers to display a nested Table of Contents and enables bulk-extraction of entire volumes.

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
Scan the archive for bit-rot or data corruption. BBF uses **[XXH3_64](https://github.com/Cyan4973/xxHash)** hashes to verify every individual image payload.
```bash
bbfmux input.bbf --verify
```

### Extract Data
Extract the entire book, a specific volume, or a single chapter. When extracting a parent section (like a Volume), `bbfmux` automatically includes all child chapters.

**Extract a specific section:**
```bash
bbfmux input.bbf --extract --section="Volume 1" --outdir="./Volume1"
```

**Extract the entire book:**
```bash
bbfmux input.bbf --extract --outdir="./unpacked_book"
```

### View Metadata & Structure
View the version, page count, deduplication stats, hierarchical sections, and all embedded metadata.
```bash
bbfmux input_book.bbf --info
```

---

## Advanced CLI Features

`bbfmux` also supports more advanced options, allowing full-control over your `.bbf` files.

### Custom Page Ordering (`--order`)
You can precisely control the reading order using a text file or inline arguments.
*   **Positive Integers**: Fixed 1-based index (e.g., `cover.png:1`).
*   **Negative Integers**: Fixed position from the end (e.g., `credits.png:-1` is always the last page).
*   **Unspecified**: Sorted alphabetically between the fixed pages.

```bash
# Using an order file
bbfmux ./images/ --order=pages.txt out.bbf

# pages.txt example:
cover.png:1
page1.png:2
page2.png:3
credits.png:-1
```

### Batch Section Import (`--sections`)
Sections define Chapters or Volumes. You can target a page by its index or filename.
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
BBF allows for verification of data to detect bit-rot.
```bash
# Verify everything (All assets and Directory structure)
bbfmux input.bbf --verify

# Verify only the directory hash (Instant)
bbfmux input.bbf --verify -1

# Verify a specific asset by index
bbfmux input.bbf --verify 42
```

### Range-Key Extraction
The `--rangekey` option allows you to extract a range of sections. The extractor starts at the specified `--section` and stops when it finds a section whose title matches the `rangekey`.

```bash
# Extract Chapter 2 up until it hits Chapter 4
bbfmux manga.bbf --extract --section="Chapter 2" --rangekey="Chapter 4" --outdir="./Ch2_to_Ch4"

# Extract Volume 2 until it encounters the string "Chapter 60"
bbfmux manga.bbf --extract --section="Volume 2" --rangekey="Chapter 60" --outdir="./Volume_2_to_Chapter_60"
```

---

## License
Distributed under the MIT License. See `LICENSE` for more information.
