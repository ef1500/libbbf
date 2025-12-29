#include "libbbf.h"
#include "xxhash.h"

#include <iostream>
#include <vector>

BBFBuilder::BBFBuilder(const std::string& outputFilename) : currentOffset(0)
{
    fileStream.open(outputFilename, std::ios::binary | std::ios::out );
    if ( !fileStream.is_open())
    {
        throw std::runtime_error("Cannot open output file!");
    }

    BBFHeader header;
    header.magic[0] = 'B';
    header.magic[1] = 'B';
    header.magic[2] = 'F';
    header.magic[3] = '1';
    header.version = 1;
    header.reserved = 0;

    fileStream.write(reinterpret_cast<char*>(&header), sizeof(BBFHeader));

    currentOffset = sizeof(BBFHeader);
}

BBFBuilder::~BBFBuilder()
{
    if (fileStream.is_open())
    {
        fileStream.close();
    }
}

bool BBFBuilder::alignPadding()
{
    uint64_t padding = (4096 - (currentOffset % 4096)) % 4096;

    if (padding > 0)
    {
        std::vector<char> zeroes(padding, 0);
        fileStream.write(zeroes.data(), padding);
        currentOffset += padding;
        return true;
    }
    else {return false; }
}

uint64_t BBFBuilder::calculateXXH3Hash(const std::vector<char> &buffer)
{
    return XXH3_64bits(buffer.data(), buffer.size());
}

bool BBFBuilder::addPage(const std::string& imagePath, uint8_t type, uint32_t flags)
{
    std::ifstream input(imagePath, std::ios::binary | std::ios::ate);
    if ( !input ) return false;
    
    std::streamsize size = input.tellg();
    input.seekg(0, std::ios::beg);

    std::vector<char> buffer(size);
    if (!input.read(buffer.data(), size)) return false;

    uint64_t hash = calculateXXH3Hash(buffer);
    uint32_t assetIndex = 0;

    // dedupe
    auto it = dedupeMap.find(hash);
    if (it != dedupeMap.end())

    {
        // dupe found.
        assetIndex = it->second;
    }
    else
    {
        alignPadding();

        BBFAssetEntry newAsset;
        newAsset.offset = currentOffset;
        newAsset.length = size;
        newAsset.xxh3Hash = hash;
        newAsset.type = type;

        for ( int i = 0; i < 7; i++ )
        {
            newAsset.reserved[i] = 0;
        }

        fileStream.write(buffer.data(), size);
        currentOffset += size;

        assetIndex = static_cast<uint32_t>(assets.size());
        assets.push_back(newAsset);
        dedupeMap[hash] = assetIndex;
    }

    // Add page entry
    BBFPageEntry page;
    page.assetIndex = assetIndex;
    page.flags = flags;
    pages.push_back(page);

    return true;
}

uint32_t BBFBuilder::getOrAddStr(const std::string& str)
{
    auto it = stringMap.find(str);
    if (it != stringMap.end())
    {
        return it->second;
    }

    uint32_t offset = static_cast<uint32_t>(stringPool.size());
    stringPool.insert(stringPool.end(), str.begin(), str.end());
    stringPool.push_back('\0');

    stringMap[str] = offset;
    return offset;
}

bool BBFBuilder::addSection(const std::string& sectionName, uint32_t startPage, uint32_t parentSection)
{
    BBFSection section;
    section.sectionTitleOffset = getOrAddStr(sectionName);
    section.sectionStartIndex = startPage;
    section.parentSectionIndex = parentSection;
    sections.push_back(section);

    return true;
}

bool BBFBuilder::addMetadata(const std::string& key, const std::string& value)
{
    BBFMetadata meta;
    meta.keyOffset = getOrAddStr(key);
    meta.valOffset = getOrAddStr(value);
    metadata.push_back(meta);

    return true;
}

bool BBFBuilder::finalize()
{
    uint64_t indexStart = currentOffset;

    //write footer
    BBFFooter footer;
    footer.stringPoolOffset = currentOffset;
    fileStream.write(stringPool.data(), stringPool.size());
    currentOffset += stringPool.size();

    // write assets
    footer.assetTableOffset = currentOffset;
    footer.assetCount = static_cast<uint32_t>(assets.size());

    fileStream.write(reinterpret_cast<char*>(assets.data()), assets.size() * sizeof (BBFAssetEntry));
    currentOffset += assets.size() *sizeof(BBFAssetEntry);

    // write page table
    footer.pageTableOffset = currentOffset;
    footer.pageCount = static_cast<uint32_t>(pages.size());

    fileStream.write(reinterpret_cast<char*>(pages.data()), pages.size() * sizeof(BBFPageEntry));
    currentOffset += pages.size() * sizeof(BBFPageEntry);

    // write section table
    footer.sectionTableOffset = currentOffset;
    footer.sectionCount = static_cast<uint32_t>(sections.size());

    fileStream.write(reinterpret_cast<char*>(sections.data()), sections.size() * sizeof(BBFSection));
    currentOffset += sections.size() * sizeof(BBFSection);

    // write metadata
    footer.metaTableOffset = currentOffset;
    footer.keyCount = static_cast<uint32_t>(metadata.size());

    fileStream.write(reinterpret_cast<char*>(metadata.data()), metadata.size() * sizeof(BBFMetadata));
    currentOffset += metadata.size() * sizeof(BBFMetadata);

    // calculate directory hash (everything from the index beginning to the currentOffset)
    // placeholder for now
    footer.indexHash = 0xB00B5000;

    // write footer
    footer.magic[0] = 'B';
    footer.magic[1] = 'B';
    footer.magic[2] = 'F';
    footer.magic[3] = '1';

    fileStream.write(reinterpret_cast<char*>(&footer), sizeof(BBFFooter));
    fileStream.close();
    return true;
}