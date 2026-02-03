// LIBBBF_H
#ifndef LIBBBF_H
#define LIBBBF_H

#include <stdint.h>

// SPEC - v3 UPDATE

// WASM Binary Handling
#ifdef __EMSCRIPTEN__
    #include <emscripten.h>
    #define LIBBBF_API EMSCRIPTEN_KEEPALIVE
#elif defined(_WIN32)
    #ifdef LIBBBF_EXPORT_SYMBOLS
        #define LIBBBF_API __declspec(dllexport)
    #else
        #define LIBBBF_API __declspec(dllimport)
    #endif
#else
    #define LIBBBF_API __attribute__((visibility("default")))
#endif


#pragma pack(push, 1)

struct BBFHeader
{
    uint8_t magic[4]; // BBF
    uint16_t version;
    uint16_t headerLen;
    uint32_t flags;
    uint8_t alignment; // Pow 2. 12 = 4096
    uint8_t reamSize; // Pow 2. Put smaller groups of pages into a "ream".
    // Larger files get full alignment, small files get put in reams.
    uint16_t reservedExtra; // align
    uint64_t footerOffset;

    uint8_t reserved[40];
};

struct BBFFooter
{
    uint64_t assetOffset;
    uint64_t pageOffset;
    uint64_t sectionOffset;
    uint64_t metaOffset;
    uint64_t expansionOffset;

    uint64_t stringPoolOffset;
    uint64_t stringPoolSize; // Size of string pool

    uint64_t assetCount;
    uint64_t pageCount;
    uint64_t sectionCount;
    uint64_t metaCount; // key count
    uint64_t expansionCount; // Expansion entry count

    uint32_t flags;
    uint8_t footerLen; // size of this struct
    uint8_t padding[3];

    uint64_t footerHash; // XXH3-64 hash of the index region

    uint8_t reserved[144];
};

struct BBFAsset
{
    uint64_t fileOffset;
    uint64_t assetHash[2]; // XXH3-128
    uint64_t fileSize; // size of file in bytes
    uint32_t flags;
    uint16_t reservedValue; // reserved, alignment
    uint8_t type;
    uint8_t reserved[9];
};

struct BBFPage
{
    uint64_t assetIndex;
    uint32_t flags;
    uint8_t reserved[4];
};

struct BBFSection
{
    uint64_t sectionTitleOffset; // offset into string pool
    uint64_t sectionStartIndex; // page index of starting page
    uint64_t sectionParentOffset; // offset into string pool
    uint8_t reserved[8];
};

struct BBFMeta
{
    uint64_t keyOffset; // offset into string pool
    uint64_t valueOffset; // offset into string pool
    uint64_t parentOffset; // offset into string pool
    uint8_t reserved[8];
};

struct BBFExpansion
{
    uint64_t expReserved[10];
    uint32_t flags;
    uint8_t reserved[44];
};

#pragma pack(pop)

// Create namespace for default constants
namespace BBF
{
    // Header Flags
    constexpr static uint32_t BBF_PETRIFICATION_FLAG = 0x00000001u; // Petrified Flag. Footer immediately follows header.
    constexpr static uint32_t BBF_VARIABLE_REAM_SIZE_FLAG = 0x00000002u; // Sub-Align Smaller Files (Variable Alignment)

    // Muxer Constants
    constexpr static uint32_t DEFAULT_GUARD_ALIGNMENT = 12; // pow2. Boundary size (Alignment) [4096]
    constexpr static uint64_t DEFAULT_SMALL_REAM_THRESHOLD = 16; // Pow 2. Small ream threshold (Group of pages) for Variable Alignment. [65536]
    
    // Reader constants
    constexpr static uint64_t MAX_BALE_SIZE = 16000000; // Maximum number of bytes the index region must be before we get suspicious.
    //constexpr static uint8_t MAX_METADATA_DEPTH = 256; // So we don't go crazy while checking metadata entries
    constexpr static uint64_t MAX_FORME_SIZE = 2048; // Maximum string length in the string pool
    

    enum class BBFMediaType: uint8_t
    {
        UNKNOWN = 0x00,
        AVIF = 0x01,
        PNG = 0x02,
        WEBP = 0x03,
        JXL = 0x04,
        BMP = 0x05,
        GIF = 0x07,
        TIFF = 0x08,
        JPG = 0x09
    };

    // BBF Version
    constexpr static uint16_t VERSION = 3;
}

#endif // LIBBBF_H