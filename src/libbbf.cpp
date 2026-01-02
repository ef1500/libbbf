#include "libbbf.h"
#include "xxhash.h"

#include <iostream>
#include <vector>
#include <algorithm>
#include <string>
#include <cctype>

BBFBuilder::BBFBuilder(const std::string& outputFilename) : currentOffset(0)
{
    // Open the file for writing
    fileStream.open(outputFilename, std::ios::binary | std::ios::out );

    // If we can't open it...
    if ( !fileStream.is_open() )
    {
        // Throw a fit!
        throw std::runtime_error("Cannot open output file!");
    }

    // Otherwise, write the magic number to the beginning of the file
    BBFHeader header;
    header.magic[0] = 'B';
    header.magic[1] = 'B';
    header.magic[2] = 'F';
    header.magic[3] = '1';
    header.version = 2;
    header.flags = 0; // reserved for now as well.
    header.headerLen = sizeof(BBFHeader);
    header.reserved = 0; // Reserved for future expansions

    // Write the header
    fileStream.write(reinterpret_cast<char*>(&header), sizeof(BBFHeader));

    // Set the current offset to the current location in the file
    currentOffset = sizeof(BBFHeader);
}

// Destructor
BBFBuilder::~BBFBuilder()
{
    // Just close the file.
    if (fileStream.is_open())
    {
        fileStream.close();
    }
}

bool BBFBuilder::alignPadding()
{
    // Pad the files such that they're on 4kb boundaries.
    uint64_t padding = (4096 - (currentOffset % 4096)) % 4096;

    // If the padding is greater than zero...
    if (padding > 0)
    {
        // write padding
        std::vector<char> zeroes(padding, 0);
        fileStream.write(zeroes.data(), padding);
        currentOffset += padding;
        return true;
    }
    // otherwise don't.
    else {return false; }
}

uint64_t BBFBuilder::calculateXXH3Hash(const std::vector<char> &buffer)
{
    return XXH3_64bits(buffer.data(), buffer.size());
}

bool BBFBuilder::addPage(const std::string& imagePath, uint8_t type, uint32_t flags)
{
    // open file up for reading
    std::ifstream input(imagePath, std::ios::binary | std::ios::ate);
    if ( !input ) return false; // return false if we can't open it
    
    std::streamsize size = input.tellg(); // figure out how big the stream is
    input.seekg(0, std::ios::beg); // seek to the beginning of the file

    std::vector<char> buffer(size); // create a buffer for the file
    if (!input.read(buffer.data(), size)) return false; // read the data into the buffer

    uint64_t hash = calculateXXH3Hash(buffer); // calculate hash
    uint32_t assetIndex = 0; // set the asset index (will set momentarily)

    // dedupe
    auto it = dedupeMap.find(hash); // try to see if the file already exists
    if (it != dedupeMap.end())
    {
        // dupe found. set asset index to the index of the pre-existing asset
        assetIndex = it->second;
    }
    else
    {
        // No dupe found. create a new asset.
        alignPadding(); // start by allocating necessary padding.

        BBFAssetEntry newAsset = {0};
        newAsset.offset = currentOffset;
        newAsset.length = size;
        newAsset.decodedLength = size;
        newAsset.xxh3Hash = hash;
        newAsset.type = type;
        newAsset.flags = 0; // no flags yet.

        // set reserved equal to zero
        //newAsset.reserved[4] = {0};

        // same for padding
        //newAsset.padding[7] = {0};

        fileStream.write(buffer.data(), size);
        currentOffset += size;

        assetIndex = static_cast<uint32_t>(assets.size()); // (may change later on to just be numeric)
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
    // Create the string table. Do same thing as add page but slightly different.
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
    // Initialize XXH3 State
    XXH3_state_t* const state = XXH3_createState();
    if (state == nullptr) return false;
    XXH3_64bits_reset(state);

    // Helper lambda to write to file and update hash simultaneously
    auto writeAndHash = [&](const void* data, size_t size) {
        if (size == 0) return;
        fileStream.write(reinterpret_cast<const char*>(data), size);
        XXH3_64bits_update(state, data, size);
        currentOffset += size;
    };

    //write footer
    BBFFooter footer;
    footer.stringPoolOffset = currentOffset;
    footer.extraOffset = 0; // set the extraOffset to 0 since we aren't using it.

    //fileStream.write(stringPool.data(), stringPool.size());
    //currentOffset += stringPool.size();
    // Use writeAndHash instead
    writeAndHash(stringPool.data(), stringPool.size());

    // write assets
    footer.assetTableOffset = currentOffset;
    footer.assetCount = static_cast<uint32_t>(assets.size());

    //fileStream.write(reinterpret_cast<char*>(assets.data()), assets.size() * sizeof (BBFAssetEntry));
    //currentOffset += assets.size() *sizeof(BBFAssetEntry);
    writeAndHash(assets.data(), assets.size() * sizeof (BBFAssetEntry));

    // write page table
    footer.pageTableOffset = currentOffset;
    footer.pageCount = static_cast<uint32_t>(pages.size());

    //fileStream.write(reinterpret_cast<char*>(pages.data()), pages.size() * sizeof(BBFPageEntry));
    //currentOffset += pages.size() * sizeof(BBFPageEntry);
    writeAndHash(pages.data(), pages.size() * sizeof(BBFPageEntry));

    // write section table
    footer.sectionTableOffset = currentOffset;
    footer.sectionCount = static_cast<uint32_t>(sections.size());

    //fileStream.write(reinterpret_cast<char*>(sections.data()), sections.size() * sizeof(BBFSection));
    //currentOffset += sections.size() * sizeof(BBFSection);
    writeAndHash(sections.data(), sections.size() * sizeof(BBFSection));


    // write metadata
    footer.metaTableOffset = currentOffset;
    footer.keyCount = static_cast<uint32_t>(metadata.size());

    //fileStream.write(reinterpret_cast<char*>(metadata.data()), metadata.size() * sizeof(BBFMetadata));
    // currentOffset += metadata.size() * sizeof(BBFMetadata);
    writeAndHash(metadata.data(), metadata.size() * sizeof(BBFMetadata));

    // calculate directory hash (everything from the index beginning to the currentOffset)
    footer.indexHash = XXH3_64bits_digest(state);
    XXH3_freeState(state);

    // write footer
    footer.magic[0] = 'B';
    footer.magic[1] = 'B';
    footer.magic[2] = 'F';
    footer.magic[3] = '1';

    fileStream.write(reinterpret_cast<char*>(&footer), sizeof(BBFFooter));
    fileStream.close();
    return true;
}

BBFMediaType detectTypeFromExtension(const std::string &extension) 
{
    std::string ext = extension;
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == ".png") return BBFMediaType::PNG;
    if (ext == ".jpg" || ext == ".jpeg") return BBFMediaType::JPG;
    if (ext == ".avif") return BBFMediaType::AVIF;
    if (ext == ".webp") return BBFMediaType::WEBP;
    if (ext == ".jxl") return BBFMediaType::JXL;
    if (ext == ".bmp") return BBFMediaType::BMP;
    if (ext == ".gif") return BBFMediaType::GIF;
    if (ext == ".tiff") return BBFMediaType::TIFF;
    
    return BBFMediaType::UNKNOWN;
}

std::string MediaTypeToStr(uint8_t type)
{
    BBFMediaType mediaType = static_cast<BBFMediaType>(type);

    switch (mediaType)
    {
        case BBFMediaType::AVIF: return ".avif";
        case BBFMediaType::PNG:  return ".png";
        case BBFMediaType::JPG:  return ".jpg";
        case BBFMediaType::WEBP: return ".webp";
        case BBFMediaType::JXL:  return ".jxl";
        case BBFMediaType::BMP:  return ".bmp";
        case BBFMediaType::GIF:  return ".gif";
        case BBFMediaType::TIFF: return ".tiff";

        case BBFMediaType::UNKNOWN: 
        default: 
            return ".png"; 
    }
}