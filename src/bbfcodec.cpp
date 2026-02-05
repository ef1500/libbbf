#include "bbfcodec.h"
#include "libbbf.h"
#include "xxhash.h"

#include <stdio.h>
#include <stdlib.h>
#include <cstring>

#ifdef _WIN32
    #include <windows.h>
#endif

// Macros to speed up media detection
#define PACK4(a, b, c, d) ((uint32_t)((uint8_t)a) | ((uint32_t)((uint8_t)b) << 8) | ((uint32_t)((uint8_t)c) << 16) | ((uint32_t)((uint8_t)d) << 24))

constexpr uint32_t TO_LOWER4(uint32_t k)
{
    return (k | 0x20202020u);
}

// Packed Format list
static const uint32_t PACK4_AVIF_ = PACK4('a', 'v', 'i', 'f');
static const uint32_t PACK4_PNG_ = PACK4('p', 'n', 'g', ' ');
static const uint32_t PACK4_WEBP_ = PACK4('w', 'e', 'b', 'p');
static const uint32_t PACK4_JXL_ = PACK4('j', 'x', 'l', ' ');
static const uint32_t PACK4_BMP_ = PACK4('b', 'm', 'p', ' ');
static const uint32_t PACK4_GIF_ = PACK4('g', 'i', 'f', ' ');
static const uint32_t PACK4_TIFF_ = PACK4('t', 'i', 'f', 'f');
static const uint32_t PACK4_JPG_ = PACK4('j', 'p', 'g', ' ');
static const uint32_t PACK4_JPEG_ = PACK4('j', 'p', 'e', 'g');


// End Macros and things.

BBFBuilder::BBFBuilder(const char* oFile, uint32_t alignment, uint32_t reamSize, uint32_t hFlags) : stringPool(4096), assetLookupTable(4096)
{
    // Open the file for writing
    this->file = fopen(oFile, "wb");
    setvbuf(this->file, nullptr, _IOFBF, 64 * 1024);

    if ( !this->file )
    {
        fprintf(stderr, "[BBFCODEC] Could not open file: %s\n", oFile);
        exit(1);
    }

    this->guardValue = alignment;
    this->reamValue = reamSize;
    this->headerFlags = hFlags;

    this->assetCount = 0;
    this->assetCap = 64;
    this->assets = (BBFAsset*)calloc(this->assetCap, sizeof(BBFAsset));

    this->pageCount = 0;
    this->pageCap = 128; // Assume 50% deduplication
    this->pages = (BBFPage*)calloc(this->pageCap, sizeof(BBFPage));

    this->sectionCount = 0;
    this->sectionCap = 16; // Assume very little sections
    this->sections = (BBFSection*)calloc(this->sectionCap, sizeof(BBFSection));

    this->keyCount = 0;
    this->keyCap = 16; // Assume similar metadata
    this->metadata = (BBFMeta*)calloc(this->keyCap, sizeof(BBFMeta));

    // Create blank header
    uint8_t blankHeader[sizeof(BBFHeader)] = {0};
    size_t written = fwrite(blankHeader, 1, sizeof(BBFHeader), this->file);

    if (written != sizeof(BBFHeader))
    {
        fprintf(stderr, "[BBFCODEC] Failed to write blank header to %s\n",oFile);
        exit(1);
    }

    // set current offset after writing header
    this->currentOffset = sizeof(BBFHeader);
}

BBFBuilder::~BBFBuilder()
{
    // Close file
    if(this->file)
    {
        fclose(this->file);
        this->file = nullptr;
    }

    // Free pointers.
    if(this->assets)
    {
        free(this->assets);
    }

    if (this->pages)
    {
        free(this->pages);
    }

    if (this->sections)
    {
        free(this->sections);
    }

    if (this->metadata)
    {
        free(this->metadata);
    }
}

void BBFBuilder::growAssets()
{
    size_t newCap = this->assetCap * 2;

    // Make list bigger
    BBFAsset* pAsset = (BBFAsset*)realloc(this->assets, newCap * sizeof(BBFAsset));

    // Memory error.
    if (!pAsset)
    {
        fprintf(stderr, "[BBFCODEC] Unable to allocate %zu bytes for assets.", newCap);
        exit(1);
    }

    // Zero out the memory area so it doesn't turn to garbage.
    memset(pAsset + this->assetCount, 0, (newCap - this->assetCount) * sizeof(BBFAsset));
    this->assets = pAsset;
    this->assetCap = newCap;
}

void BBFBuilder::growPages()
{
    size_t newCap = this->pageCap * 2;
    BBFPage* pPage = (BBFPage*)realloc(this->pages, newCap * sizeof(BBFPage));

    if (!pPage)
    {
        fprintf(stderr, "[BBFCODEC] Unable to allocate %zu bytes for page sector.", newCap);
        exit(1);
    }

    memset(pPage + this->pageCount, 0, (newCap - this->pageCount) * sizeof(BBFPage));
    this->pages = pPage;
    this->pageCap = newCap;
}

void BBFBuilder::growSections()
{
    size_t newCap = this->sectionCap * 2;
    BBFSection* pSection = (BBFSection*)realloc(this->sections, newCap * sizeof(BBFSection));

    if (!pSection)
    {
        fprintf(stderr, "[BBFCODEC] Unable to allocate %zu bytes for section sector.", newCap);
        exit(1);
    }

    memset(pSection + this->sectionCount, 0, (newCap - this->sectionCount) * sizeof(BBFSection));
    this->sections = pSection;
    this->sectionCap = newCap;
}

void BBFBuilder::growMeta()
{
    size_t newCap = this->keyCap * 2;
    BBFMeta* pMeta = (BBFMeta*)realloc(this->metadata, newCap * sizeof(BBFMeta));

    if (!pMeta)
    {
        fprintf(stderr, "[BBFCODEC] Unable to allocate %zu bytes for metadata sector.", newCap);
        exit(1);
    }

    memset(pMeta + this->keyCount, 0, (newCap - this->keyCount) * sizeof(BBFMeta));
    this->metadata = pMeta;
    this->keyCap = newCap;
}

void BBFBuilder::writePadding(uint64_t alignBoundary)
{
    uint64_t remainder = this->currentOffset % alignBoundary;

    if (remainder == 0)
    {
        return;
    }

    uint64_t padding = alignBoundary - remainder;

    static const uint8_t zeros[4096] = {0};

    uint64_t bytesLeft = padding;
    while (bytesLeft > 0)
    {
        uint64_t chunk = (bytesLeft > sizeof(zeros)) ? sizeof(zeros) : bytesLeft;
        fwrite(zeros, 1, chunk, this->file);
        bytesLeft -= chunk;
    }

    this->currentOffset += padding;
}

uint8_t BBFBuilder::detectType(const char* fPath)
{
    // Get media type
    // or with 0x20 to make sure everything is uppercase.
    if ( !fPath )
    {
        return (uint8_t)BBF::BBFMediaType::UNKNOWN;
    }

    const char* fExt = strrchr(fPath, '.');
    if ( !fExt )
    {
        return (uint8_t)BBF::BBFMediaType::UNKNOWN;
    }

    fExt++;

    uint32_t PACKED = 0;
    int iterator = 0;

    for (; iterator < 4; iterator++)
    {
        if (fExt[iterator] == '\0')
        {
            break;
        }

        PACKED |= ((uint32_t)(uint8_t)fExt[iterator]) << (iterator * 8);
    }

    // Lower Mask
    PACKED |= 0x20202020u;

    switch (PACKED)
    {
        case PACK4_AVIF_: return (uint8_t)BBF::BBFMediaType::AVIF;
        case PACK4_PNG_ : return (uint8_t)BBF::BBFMediaType::PNG;
        case PACK4_WEBP_ : return (uint8_t)BBF::BBFMediaType::WEBP;
        case PACK4_JXL_ : return (uint8_t)BBF::BBFMediaType::JXL;
        case PACK4_BMP_ : return (uint8_t)BBF::BBFMediaType::BMP;
        case PACK4_GIF_ : return (uint8_t)BBF::BBFMediaType::GIF;
        case PACK4_TIFF_ : return (uint8_t)BBF::BBFMediaType::TIFF;
        case PACK4_JPG_ : return (uint8_t)BBF::BBFMediaType::JPG;
        case PACK4_JPEG_ : return (uint8_t)BBF::BBFMediaType::JPG;
        default : return (uint8_t)BBF::BBFMediaType::UNKNOWN;
    }

}

bool BBFBuilder::addPage(const char* fPath, uint32_t pFlags, uint32_t aFlags)
{
    // Program flow
    // Open file, hash it, see if it's in the hashtable.
    // If not, check the size. If the file size is below the variable ream size flag,
    // and --variable-alignment is enabled, sub-align the file, put it in the hash table. 
    // If --variable-alignment is not enabled, align to the chosen alignment (from the -a/--alignment option)
    // or if not specified, use the default alignment value from the BBF namespace.

    // Get media type
    uint8_t mediaType = detectType(fPath);

    FILE* iImg = fopen(fPath, "rb");

    if (!iImg)
    {
        fprintf(stderr, "[BBFCODEC] Unable to open %s for reading.\n", fPath);
        return false;
    }

    // Filesize
    fseek(iImg, 0, SEEK_END);
    uint64_t fileSize = ftell(iImg); // may not work for large files. TODO: handle biiig files.
    fseek(iImg, 0, SEEK_SET);

    //Hash
    XXH3_state_t* state = XXH3_createState();
    XXH3_128bits_reset(state); // Use 128-bit xxh3

    uint8_t iBuffer[16384];
    size_t rBytes = 0;

    while((rBytes = fread(iBuffer, 1, sizeof(iBuffer), iImg)) > 0)
    {
        XXH3_128bits_update(state, iBuffer, rBytes);
    }

    XXH128_hash_t iHash = XXH3_128bits_digest(state);
    XXH3_freeState(state);

    // TODO: Load small files into RAM for reading
    fseek(iImg, 0, SEEK_SET);
    uint64_t aIndex = this->assetLookupTable.findAsset(iHash);

    // If already added...
    if (aIndex != 0xFFFFFFFFFFFFFFFF)
    {
        if (this->pageCount >= this->pageCap)
        {
            growPages();
        }

        this->pages[this->pageCount].assetIndex = aIndex;
        this->pages[this->pageCount].flags = pFlags;
        this->pageCount++;

        fclose(iImg);
        return true;
    }

    // If new asset...
    uint64_t alignmentBytes = 1ULL << this->guardValue;
    uint64_t thresholdBytes = 1ULL << this->reamValue;

    bool variableAlign = headerFlags & BBF::BBF_VARIABLE_REAM_SIZE_FLAG;

    if ( variableAlign )
    {
        if ( fileSize < thresholdBytes )
        {
            alignmentBytes = 8;
        }
    }

    writePadding(alignmentBytes);
    uint64_t aStartOffset = this->currentOffset;

    // write
    while ((rBytes = fread(iBuffer, 1, sizeof(iBuffer), iImg)) > 0)
    {
        fwrite(iBuffer, 1, rBytes, this->file);
        this->currentOffset += rBytes;
    }

    // close image
    fclose(iImg);

    // update builder
    if (this->assetCount >= this->assetCap)
    {
        growAssets();
    }

    if (this->pageCount >= this->pageCap)
    {
        growPages();
    }

    this->assets[this->assetCount].fileOffset = aStartOffset;
    this->assets[this->assetCount].assetHash[0] = iHash.low64;
    this->assets[this->assetCount].assetHash[1] = iHash.high64;
    this->assets[this->assetCount].fileSize = fileSize;
    this->assets[this->assetCount].flags = aFlags;
    this->assets[this->assetCount].type = mediaType;

    this->assetLookupTable.addAsset(iHash, this->assetCount);

    this->pages[this->pageCount].assetIndex = this->assetCount;
    this->pages[this->pageCount].flags = pFlags;

    this->assetCount++;
    this->pageCount++;

    return true;
}

bool BBFBuilder::addMeta(const char* key, const char* value, const char* parent)
{
    // Add support for simple metadata.
    if ( !key || !value )
    {
        return false;
    }

    if ( this->keyCount >= this->keyCap)
    {
        growMeta();
    }

    uint64_t keyOffset = this->stringPool.addString(key);
    uint64_t valueOffset = this->stringPool.addString(value);

    uint64_t parentOffset = 0xFFFFFFFFFFFFFFFF;
    if (parent)
    {
        parentOffset = this->stringPool.addString(parent);
    }

    this->metadata[this->keyCount].keyOffset = keyOffset;
    this->metadata[this->keyCount].valueOffset = valueOffset;
    this->metadata[this->keyCount].parentOffset = parentOffset;

    this->keyCount++;
    return true;
}

bool BBFBuilder::addSection(const char* sectionName, uint64_t startIndex, const char* parentName)
{
    // Add support for sectioning.

    if(!sectionName)
    {
        return false;
    }

    if (startIndex > this->pageCount)
    {
        printf("[BBFCODEC] Cannot add section %s: Index out of bounds.", sectionName);
        return false;
    }

    if (this->sectionCount >= this->sectionCap)
    {
        growSections();
    }

    uint64_t parentOffset = 0xFFFFFFFFFFFFFFFF;
    if (parentName)
    {
        parentOffset = this->stringPool.addString(parentName);
    }

    uint64_t sectionOffset = this->stringPool.addString(sectionName);

    this->sections[this->sectionCount].sectionTitleOffset = sectionOffset;
    this->sections[this->sectionCount].sectionStartIndex = startIndex;
    this->sections[this->sectionCount].sectionParentOffset = parentOffset;

    this->sectionCount++;
    return true;
}

bool BBFBuilder::finalize()
{
    // Save the file. If patrifying, then we move the footer to after the header.
    // Write Header
    // (Calculate footer offset)
    // If Petrified: Write Footer
    // Write Assets
    // Write Pages
    // Write Sections
    // Write Metadata
    // Write Expansions (if present)
    // Write Strings
    // If not petrified: Write Footer

    if (!this->file)
    {
        return false;
    }

    if (this->assetCount == 0)
    {
        printf("[BBFCODEC] No assets to finalize.");
        return false;
    }

    XXH3_state_t* hashState = XXH3_createState();
    XXH3_64bits_reset(hashState);

    // Write assets
    uint64_t offsetAssets = this->currentOffset;
    if (this->assetCount > 0)
    {
        size_t bytes = sizeof(BBFAsset)*this->assetCount;
        fwrite(this->assets, 1, bytes, this->file);
        XXH3_64bits_update(hashState, this->assets, bytes);
        this->currentOffset += bytes;
    }

    // Write pages
    uint64_t offsetPages = this->currentOffset;
    if (this->pageCount > 0)
    {
        size_t bytes = sizeof(BBFPage)*this->pageCount;
        fwrite(this->pages, 1, bytes, this->file);
        XXH3_64bits_update(hashState, this->pages, bytes);
        this->currentOffset += bytes;
    }

    // Write Sections
    uint64_t offsetSections = this->currentOffset;
    if (this->sectionCount > 0)
    {
        size_t bytes = sizeof(BBFSection)*this->sectionCount;
        fwrite(this->sections, 1, bytes, this->file);
        XXH3_64bits_update(hashState, this->sections, bytes);
        this->currentOffset += bytes;
    }

    // Write metadata
    uint64_t offsetMeta = this->currentOffset;
    if (this->keyCount > 0)
    {
        size_t bytes = sizeof(BBFMeta)*this->keyCount;
        fwrite(this->metadata, 1, bytes, this->file);
        XXH3_64bits_update(hashState, this->metadata, bytes);
        this->currentOffset += bytes;
    }

    // TODO: No expansions yet, will imlement later

    // Write strings
    uint64_t offsetStrings = this->currentOffset;
    size_t strPoolSize = this->stringPool.getUsedSize();
    if (strPoolSize > 0)
    {
        const char* rawStrPool = this->stringPool.getDataRaw();
        fwrite(rawStrPool, 1, strPoolSize, this->file);
        XXH3_64bits_update(hashState, rawStrPool, strPoolSize);
        this->currentOffset += strPoolSize;
    }

    uint64_t indexHash = XXH3_64bits_digest(hashState);
    XXH3_freeState(hashState);

    uint64_t footerOffset = this->currentOffset;

    BBFFooter footer = {0};
    footer.assetOffset = offsetAssets;
    footer.pageOffset = offsetPages;
    footer.sectionOffset = offsetSections;
    footer.metaOffset = offsetMeta;
    footer.expansionOffset = 0; // Unused right now. Putting here for clariy.
    footer.stringPoolOffset = offsetStrings;
    footer.stringPoolSize = strPoolSize; // set string pool size
    
    footer.assetCount = this->assetCount;
    footer.pageCount = this->pageCount;
    footer.sectionCount = this->sectionCount;
    footer.metaCount = this->keyCount;
    // expansion count is zero.

    footer.flags = 0; // also unused right now. Putting here for clarity.
    footer.footerLen = (uint8_t)sizeof(BBFFooter);
    footer.footerHash = indexHash;

    fwrite(&footer, 1, sizeof(BBFFooter), this->file);

    // Write header
    fseek(this->file, 0, SEEK_SET);

    BBFHeader header = {0};
    header.magic[0] = 0x42;
    header.magic[1] = 0x42;
    header.magic[2] = 0x46;
    header.magic[3] = 0x33;

    header.version = BBF::VERSION;
    header.headerLen = sizeof(BBFHeader);
    header.flags = this->headerFlags;

    header.alignment = (uint8_t)this->guardValue;
    header.reamSize = (uint8_t)this->reamValue;

    header.footerOffset = footerOffset;

    fwrite(&header, 1, sizeof(BBFHeader), this->file);
    fclose(this->file);
    this->file = nullptr;

    return true;
}

// Copy helper
static bool copyRange(FILE* source, FILE* dest, uint64_t bToCopy)
{
    static uint8_t buffer[65536];
    uint64_t remaining = bToCopy;

    while ( remaining > 0)
    {
        size_t fChunk = (remaining > sizeof(buffer)) ? sizeof(buffer) : (size_t)remaining;

        if (fread(buffer, 1, fChunk, source) != fChunk)
        {
            return false;
        }

        if (fwrite(buffer, 1, fChunk, dest) != fChunk)
        {
            return false;
        }

        remaining -= fChunk;
    }

    return true;
}

bool BBFBuilder::petrifyFile(const char* iPath, const char* oPath)
{
    // if (this->file != nullptr)
    // {
    //     return false;
    // }

    FILE* sourceBBF = fopen(iPath, "rb");
    const char* tmpPath = "petrified.bbf.tmp"; 

    if (!sourceBBF)
    {
        fprintf(stderr, "[BBFCODEC] Cannot petrify an already open file.\n");
        return false;
    }

    // read header
    BBFHeader header;
    size_t headerSize = fread(&header, 1, sizeof(BBFHeader), sourceBBF);

    if (headerSize != sizeof(BBFHeader))
    {
        fclose(sourceBBF);
        fprintf(stderr, "[BBFCODEC] Invalid Header.\n");
        return false;
    }

    //Check Magic
    if (header.magic[0] != 'B' || header.magic[1] != 'B' || header.magic[2] != 'F' || header.magic[3] != '3')
    {
        fprintf(stderr, "[BBFCODEC] Invalid Magic Detected. Closing File.\n");
        fclose(sourceBBF);
        return false;
    }

    if (header.flags & BBF::BBF_PETRIFICATION_FLAG)
    {
        fprintf(stderr, "[BBFCODEC] File Already Petrified. Closing File.\n");
        fclose(sourceBBF);
        return false;
    }

    uint64_t oldFooter = header.footerOffset;
    fseek(sourceBBF, (long)oldFooter, SEEK_SET);
    BBFFooter footer;

    size_t footerSize = fread(&footer, 1, sizeof(BBFFooter), sourceBBF);
    if (footerSize != sizeof(BBFFooter))
    {
        // invalid footer
        fclose(sourceBBF);
        fprintf(stderr, "[BBFCODEC] Invalid Footer.\n");
        return false;
    }

    // get first offset.
    uint64_t indexStart = footer.assetOffset;
    // size of both header + footer combined
    uint64_t newIndexStart = sizeof(BBFHeader) + sizeof(BBFFooter);

    fseek(sourceBBF, 0, SEEK_END);
    long endPos = ftell(sourceBBF);

    if (endPos < 0)
    {
        fclose(sourceBBF);
        fprintf(stderr, "[BBFCODEC] ftell failed. Got: %ld\n", endPos);
        return false;
    }

    uint64_t fileSize = (uint64_t)endPos;

    uint64_t indexSize = header.footerOffset - indexStart; // don't copy footer
    uint64_t dataSize = indexStart - header.headerLen;

    FILE* tmpBBF = fopen(tmpPath, "wb+");
    if (!tmpBBF)
    {
        fclose(sourceBBF);
        fprintf(stderr, "[BBFCODEC] Failed to open petrified.bbf.tmp\n");
        return false;
    }

    BBFHeader newHeader = header;
    BBFFooter newFooter = footer; // make new footer
    newHeader.flags |= BBF::BBF_PETRIFICATION_FLAG;

    newHeader.footerOffset = sizeof(BBFHeader); // + indexSize - sizeof(BBFFooter);
    // Write header
    fwrite(&newHeader, 1, sizeof(BBFHeader), tmpBBF);

    // Read footer, calculate shifts
    int64_t shiftIndex = (int64_t)newIndexStart - (int64_t)indexStart;

    uint64_t newDataStart = newIndexStart + indexSize;
    int64_t shiftData = (int64_t)newDataStart - (int64_t)header.headerLen;

    fseek(tmpBBF, (long)newHeader.footerOffset, SEEK_SET);

    // Shift offsets
    // TODO: Alignment, maybe?
    newFooter.assetOffset += shiftIndex;
    newFooter.pageOffset += shiftIndex;
    newFooter.sectionOffset += shiftIndex;
    newFooter.metaOffset += shiftIndex;
    newFooter.expansionOffset = (newFooter.expansionOffset == 0) ? 0 : newFooter.expansionOffset + shiftIndex;
    newFooter.stringPoolOffset += shiftIndex;

    // Write footer
    fwrite(&newFooter, 1, sizeof(BBFFooter), tmpBBF);

    // copy index
    fseek(sourceBBF, (long)indexStart, SEEK_SET);
    if (!copyRange(sourceBBF, tmpBBF, indexSize))
    {
        fclose(sourceBBF);
        fclose(tmpBBF);
        fprintf(stderr, "[BBFCODEC] Could not copy Index to TMPBBF\n");
        return false;
    }

    // copy data
    fseek(sourceBBF, (long)header.headerLen, SEEK_SET);
    if (!copyRange(sourceBBF, tmpBBF, dataSize))
    {
        fclose(sourceBBF);
        fclose(tmpBBF);
        fprintf(stderr, "[BBFCODEC] Could not copy header bytes to tmpBBF.\n");
        return false;
    }

    fseek(tmpBBF, (long)newFooter.assetOffset, SEEK_SET);
    BBFAsset assetBuffer[64];

    uint64_t remainingAssets = newFooter.assetCount;
    while (remainingAssets > 0)
    {
        uint64_t assetBatch = (remainingAssets > 64) ? 64 : remainingAssets;

        long cursorPos = ftell(tmpBBF);
        size_t readCount = fread(assetBuffer, sizeof(BBFAsset), assetBatch, tmpBBF);

        if (readCount != assetBatch)
        {
            fprintf(stderr, "[BBFCODEC] Error patching assets.\n");
            fclose(sourceBBF);
            fclose(tmpBBF);
            remove(tmpPath);
            return false;
        }

        uint64_t iterator = 0;
        for(; iterator < assetBatch; iterator++)
        {
            assetBuffer[iterator].fileOffset += shiftData;
        }

        // Write assets
        fseek(tmpBBF, cursorPos, SEEK_SET);
        fwrite(assetBuffer, sizeof(BBFAsset), assetBatch, tmpBBF);

        fseek(tmpBBF, cursorPos + (assetBatch * sizeof(BBFAsset)), SEEK_SET);
        remainingAssets -= assetBatch;
    }

    // Close, finally.
    fclose(sourceBBF);
    fclose(tmpBBF);

    #ifdef _WIN32
        if (MoveFileEx("petrified.bbf.tmp", oPath, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED) == 0)
        {
            DWORD err = GetLastError();
            fprintf(stderr, "[BBFCODEC] MoveFileEx failed. Error: %lu\n", err);
            return false;
        }
    #else
        if (rename("petrified.bbf.tmp", oPath) != 0)
        {
            fprintf(stderr, "[BBFCODEC] Could not rename temp file. Result is in 'petrified.bbf.tmp'\n");
            return false;
        }
    #endif

    return true;
}



// READER FUNCTIONS

BBFReader::BBFReader(const char* iFile)
{
    this->fileBuffer = nullptr;
    this->fileSize = 0;
    this->footerCache = nullptr;

    // Windows memory mapping
    #ifdef _WIN32
        this->hFile = CreateFileA(iFile, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

        if (this->hFile == INVALID_HANDLE_VALUE)
        {
            fprintf(stderr, "[BBFCODEC] Unable to open file %s\n", iFile);
            return;
        }

        // Get Filesize
        LARGE_INTEGER size;
        GetFileSizeEx(this->hFile, &size);
        this->fileSize = (size_t)size.QuadPart;

        //Map to memory
        this->hMap = CreateFileMappingA(this->hFile, NULL, PAGE_READONLY, 0, 0, NULL);

        if (this->hMap == NULL)
        {
            fprintf(stderr, "[BBFCODEC] Failed to map file (CreateFileMapping)\n");
            CloseHandle(this->hFile);
            return;
        }

        this->fileBuffer = (uint8_t*)MapViewOfFile(this->hMap, FILE_MAP_READ, 0, 0, 0);
        if (this->fileBuffer == NULL)
        {
            fprintf(stderr, "[BBFCODEC] Failed to map file (MapViewOfFile)\n");
            CloseHandle(this->hMap);
            CloseHandle(this->hFile);
            return;
        }
    #else
        this->fileDescriptor = open(iFile, O_RDONLY);
        if (this->fileDescriptor == -1)
        {
            fprintf(stderr, "[BBFCODEC] Unable to open file %s\n", iFile);
            return;
        }

        struct stat fileStat;
        if (fstat(this->fileDescriptor, &fileStat) == -1)
        {
            fprintf(stderr, "[BBFCODEC] Unable to stat file\n");
            close(this->fileDescriptor);
            return;
        }
        this->fileSize = fileStat.st_size;

        void* fMap = mmap(NULL, this->fileSize, PROT_READ, MAP_PRIVATE, this->fileDescriptor, 0);
        if (fMap == MAP_FAILED)
        {
            fprintf(stderr, "[BBFCODEC] mmap failed\n");
            close(this->fileDescriptor);
            this->fileBuffer = nullptr;
            return;
        }
        this->fileBuffer = (uint8_t*)fMap;
    #endif


    // if (!this->file)
    // {
    //     printf("[BBFCODEC] Unable to open file %s", iFile);
    //     return;
    // }

    // fseek(file, 0, SEEK_END);
    // fileSize = ftell(file);
    // rewind(file);

    // this->fileBuffer = new uint8_t[this->fileSize];
    // fread(this->fileBuffer, 1, this->fileSize, this->file);

    // fclose(this->file);
}

BBFReader::~BBFReader()
{
    if (this->fileBuffer)
    {

        #ifdef _WIN32
            UnmapViewOfFile(this->fileBuffer);
            CloseHandle(this->hMap);
            CloseHandle(this->hFile);
        #else
            munmap(this->fileBuffer, this->fileSize);
            close(this->fileDescriptor);
        #endif

        this->fileBuffer = nullptr;
    }

    this->footerCache = nullptr;
}

bool BBFReader::isSafe(uint64_t offset, uint64_t size) const
{
    if (!this->fileBuffer)
    {
        return false;
    }

    // Size should be size of the struct, or the string pool size

    // check if the given offset is out of bounds
    // return true if it's safe

    // Check overflow condition
    if (offset + size < offset)
    {
        return false;
    }

    // Check if pointer is outta file range
    if (offset + size > this->fileSize)
    {
        return false;
    }

    // success.
    return true;
}

bool BBFReader::isSafe(uint64_t offset) const
{
    if (!this->fileBuffer)
    {
        return false;
    }

    if (offset > this->fileSize)
    {
        return false;
    }

    return true;
}

bool BBFReader::isSafe(uint64_t count, int index) const
{
    // Check if file exists
    if (!this->fileBuffer)
    {
        return false;
    }
    // Size should be size of the struct.

    if (index < 0)
    {
        return false;
    }

    if ((uint64_t)index > count)
    {
        return false;
    }

    return true;

}

BBFFooter* BBFReader::getFooterView(uint64_t fOffset)
{
    if (!this->fileBuffer)
    {
        return nullptr;
    }

    if (!isSafe(fOffset, (uint64_t)sizeof(BBFFooter)))
    {
        return nullptr;
    }

    // Set the footerCache to the footer.
    footerCache = (BBFFooter*)(this->fileBuffer + fOffset);

    return footerCache;
}

const char* BBFReader::getStringView(uint64_t strOffset)
{
    if (!this->footerCache)
    {
        printf("[BBFCODEC] Cannot Access String Pool. Ensure File is opened.");
        return nullptr;
    }

    if ((this->footerCache->stringPoolOffset) + strOffset < (this->footerCache->stringPoolOffset) || strOffset >= (this->footerCache->stringPoolOffset + this->footerCache->stringPoolSize))
    {
        return nullptr;
    }

    uint64_t bytesLeft = (this->footerCache->stringPoolOffset + this->footerCache->stringPoolSize) - strOffset;
    uint64_t scanLimit = (BBF::MAX_FORME_SIZE < bytesLeft) ? BBF::MAX_FORME_SIZE : bytesLeft;

    const char* pPtr = (const char*)(this->fileBuffer + strOffset + this->footerCache->stringPoolOffset);

    uint64_t iterator = 0;
    for(; iterator < scanLimit; iterator++)
    {
        if (pPtr[iterator] == '\0')
        {
            return pPtr;
        }
    }

    return nullptr;

}

bool BBFReader::checkMagic(BBFHeader* pHeader)
{
    // Check the magic number of the file
    uint32_t fileMagic = (pHeader->magic[0] << 24) | (pHeader->magic[1] << 16) | (pHeader->magic[2] << 8)  | (pHeader->magic[3]);
    if (!(fileMagic == 0x42424633))
    {
        return false;
    }
    return true;
}

XXH128_hash_t BBFReader::computeAssetHash(const BBFAsset* assetView)
{
    const uint8_t* dataView = this->getAssetDataView(assetView->fileOffset);

    if (!dataView)
    {
        printf("[BBFCODEC] ERROR: Asset data out of bounds");
        return {0,0};
    }

    return XXH3_128bits(dataView, assetView->fileSize);
}

XXH128_hash_t BBFReader::computeAssetHash(uint8_t* assetTableView, int assetIndex)
{
    const BBFAsset* assetView = getAssetEntryView(assetTableView, assetIndex);
    if (!assetView)
    {
        printf("[BBFCODEC] ERROR: Unable to get pointer for asset %i", assetIndex);
        return {0,0};
    }

    const uint8_t* dataView = this->getAssetDataView(assetView->fileOffset);

    if (!dataView)
    {
        printf("[BBFCODEC] ERROR: Unable to calculate hash for asset %i", assetIndex);
        return {0,0};
    }

    return XXH3_128bits(dataView, assetView->fileSize);
}