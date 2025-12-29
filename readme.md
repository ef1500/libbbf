# libbbf: Bound Book Format

![alt text](https://img.shields.io/badge/Format-BBF1-blue.svg)


![alt text](https://img.shields.io/badge/License-MIT-green.svg)

Bound Book Format (.bbf) is a high-performance, archival-grade binary container designed specifically for digital comic books and manga. Unlike CBR/CBZ, BBF is built for DirectSotrage/mmap, easy integrity checks, and mixed-codec containerization.

## Technical Details
BBF is designed as a Footer-indexed binary format. This allows for rapid append-only creation and immediate random access to any page without scanning the entire file.

### Binary Layout
1. **Header (13 bytes)**: Magic `BBF1`, versioning, and initial padding.
2. **Page Data**: The raw image payloads (AVIF, PNG, etc.), each padded to **4096-byte boundaries**.
3. **String Pool**: A deduplicated pool of null-terminated strings for metadata and section titles.
4. **Asset Table**: A registry of physical data blobs with XXH3 hashes.
5. **Page Table**: The logical reading order, mapping logical pages to assets.
6. **Section Table**: Markers for chapters, volumes, or gallery sections.
7. **Metadata Table**: Key-Value pairs for archival data (Author, Scanlation team, etc.).
8. **Footer (76 bytes)**: Table offsets and a final integrity hash.

### 4KB Alignment & DirectStorage
Every asset in a BBF file starts on a 4KB boundary. This alignment is critical for modern NVMe-based systems. It allows developers to utilize `mmap` or **DirectStorage** to transfer image data directly from disk to GPU memory, bypassing the CPU-bottlenecked "copy and decompress" cycles found in Zip-based formats.

---

## Features

### Content Deduplication
BBF uses **[XXH3_64](https://github.com/Cyan4973/xxHash)** hashing to identify identical pages. If a book contains duplicate pages, the data is stored exactly once on disk while being referenced multiple times in the Page Table.

### Archival Integrity
Traditional bit-rot is the enemy of the archivist. BBF stores a 64-bit hash for *every individual asset*. The `bbfmux --verify` command can pinpoint exactly which page in a 2GB file has been damaged, rather than simply failing to open the entire archive.

### Mixed-Codec Support
Preserve covers in **Lossless PNG** while encoding internal story pages in **AVIF** to save 70% space. BBF explicitly flags the codec for every asset, allowing readers to initialize the correct decoder instantly without "guessing" the file type.

---

## CLI Usage: `bbfmux`

The included `bbfmux` tool is a reference implementation for creating and managing BBF files.

## CLI Features

The `bbfmux` utility provides a powerful interface for managing Bound Book files:

*   **Flexible Ingestion**: Create books by passing individual files, entire directories, or a mix of both.
*   **Logical Structuring**: Add named **Sections** (Chapters, Volumes, Galleries) to define the internal hierarchy of the book.
*   **Custom Metadata**: Embed arbitrary Key:Value pairs into the global string pool for archival indexing.
*   **Content-Aware Extraction**: Extract the entire book or target specific sections by name.

## Usage Examples

### Create a new BBF
You can mix individual images and folders. `bbfmux` will sort inputs alphabetically, deduplicate identical assets, and align data to 4KB boundaries. 

NOTE: It's not quite implemented yet in the CLI, but the `AssetTable` enables you to specify custom reading orders.

```bash
bbfmux cover.png ./chapter1/ endcard.png \
  --section="Cover":1 \
  --section="Chapter 1":2 \
  --section="Credits":24 \
  --meta=Title:"Akira" \
  --meta=Author:"Katsuhiro Otomo" \
  akira.bbf
```

### Verify Integrity
Scan for bit-rot or data corruption. Will tell you which assets are corrupted.
```bash
bbfmux input.bbf --verify
```

### Extract Data
Extract a specific section or the entire book.
```bash
bbfmux input.bbf --extract --section="Chapter 1" --outdir="./chapter1"
```

Extract the entire book
```bash
bbfmux input.bbf --extract --outdir="./unpacked_book"
```

### View Metadata
View the metadata for the .bbf file.
```bash
bbfmux input.bbf --info
```

---

## Getting Started

### Prerequisites
- C++17 compliant compiler (GCC/Clang/MSVC)
- [xxHash](https://github.com/Cyan4973/xxHash) library

### Compilation
```bash
g++ -std=c++17 bbfmux.cpp libbbf.cpp xxhash.c -o bbfmux
```

## License
Distributed under the MIT License. See `LICENSE` for more information.