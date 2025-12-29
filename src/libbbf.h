#ifndef LIBBBF_H
#define LIBBBF_H

#include <cstdint>
#include <vector>
#include <string>
#include <fstream>
#include <unordered_map>

#pragma pack(push, 1)


struct BBFHeader
{
    uint8_t magic[4]; // 0x42424631 (BBF1)
    uint8_t version; // 1
    uint64_t reserved; // set to 0
};

// Create the libbbf structs
struct BBFAssetEntry
{
    uint64_t offset; // Offset of the page
    uint64_t length; // length of the file
    uint64_t xxh3Hash; // Hash of image
    uint8_t type; // 0x01 - AVIF, 0x02 PNG
    uint8_t reserved[7]; // padding
};

// Create reading order
struct BBFPageEntry
{
    uint32_t assetIndex; // Index into Asset Entry
    uint32_t flags; // Unused as of now.  
};

// Create custom section struct
struct BBFSection
{
    uint32_t sectionTitleOffset; // Offset into string pool
    uint32_t sectionStartIndex; // Index of BBF Page index (the starting page of this section)
    uint32_t parentSectionIndex; // I.E. volume -> Chapter
};

// Create the metadata
struct BBFMetadata
{
    uint32_t keyOffset; //offset into String pool
    uint32_t valOffset; // offset into String pool
};

// Create the footer
struct BBFFooter
{
    uint64_t stringPoolOffset;
    uint64_t assetTableOffset;
    uint32_t assetCount;

    uint64_t pageTableOffset;
    uint32_t pageCount;

    uint64_t sectionTableOffset;
    uint32_t sectionCount;

    uint64_t metaTableOffset;
    uint32_t keyCount;

    uint64_t indexHash; // Integrity check, hash of everything between index start and the current position.
    uint8_t magic[4]; // 0x42424631 (BBF1) (Verification)
};

#pragma pack(pop)

class BBFBuilder
{
    public:
        BBFBuilder(const std::string &outputFilemame);
        ~BBFBuilder();

        bool addPage(const std::string& imagePath, uint8_t type, uint32_t flags = 0);
        bool addSection(const std::string& title, uint32_t startPage, uint32_t parent = 0xFFFFFFFF);
        bool addMetadata(const std::string& key, const std::string& value);

        bool finalize();
    
    private:
        std::ofstream fileStream;
        uint64_t currentOffset;

        std::vector<BBFAssetEntry> assets;
        std::vector<BBFPageEntry> pages;
        std::vector<BBFSection> sections;
        std::vector<BBFMetadata> metadata;
        std::vector<char> stringPool;

        // deduplication map
        std::unordered_map<uint64_t, uint32_t> dedupeMap; // hash -> Idx
        std::unordered_map<std::string, uint32_t> stringMap; // str -> offset

        // helpers
        uint32_t getOrAddStr(const std::string& str);
        bool alignPadding();
        uint64_t calculateXXH3Hash(const std::vector<char>& buffer);
};

#endif // LIBBBF_H