// BBF Codec
// Builder
#ifndef BBFCODEC_H
#define BBFCODEC_H

#include "xxhash.h"
#include "libbbf.h"
#include "dedupemap.h"
#include "stringpool.h"

// Handle Memory Mapping
#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#else
    #include <sys/mman.h>
    #include <sys/stat.h>
    #include <fcntl.h>
    #include <unistd.h>
#endif

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

class BBFBuilder
{
    public:
        BBFBuilder(const char* oFile, uint32_t alignment = BBF::DEFAULT_GUARD_ALIGNMENT, uint32_t reamSize = BBF::DEFAULT_SMALL_REAM_THRESHOLD, uint32_t hFlags = BBF::BBF_VARIABLE_REAM_SIZE_FLAG);
        ~BBFBuilder(); // Deconstructor.
        // TODO: Copy constructor.

        // Default flag is variable alignment.
        bool addPage(const char* fPath, uint32_t pFlags = 0, uint32_t aFlags = 0);
        bool addMeta(const char* key, const char* value, const char* parent = nullptr);
        bool addSection(const char* sectionName, uint64_t startIndex, const char* parentName = nullptr);

        bool finalize();
        static bool petrifyFile(const char* iPath, const char* oPath); // Petrify!

        // Getters
        size_t getAssetCount() { if(!assetCount) {return 0;} return assetCount; }
        size_t getPageCount() { if(!pageCount) {return 0;} return pageCount; }
        size_t getSectionCount() { if(!sectionCount) {return 0;} return sectionCount; }
        size_t getKeyCount() { if(!keyCount) {return 0;} return keyCount; }

    
    private:
        FILE* file;
        uint64_t currentOffset;

        BBFStringPool stringPool;
        BBFAssetTable assetLookupTable;

        // Config from args
        uint32_t headerFlags;
        uint32_t guardValue;
        uint32_t reamValue;

        // Values
        BBFAsset* assets;
        size_t assetCount;
        size_t assetCap;

        BBFPage* pages; // read order.
        size_t pageCount;
        size_t pageCap;

        BBFSection* sections;
        size_t sectionCount;
        size_t sectionCap;

        BBFMeta* metadata;
        size_t keyCount;
        size_t keyCap;

        void growAssets(); // realloc(this->assets)
        void growPages();
        void growSections();
        void growMeta();

        // Other Helpers
        void writePadding(uint64_t alignmentBoundary);
        uint8_t detectType(const char* iPath);
};

class BBFReader
{
    public:
        BBFReader(const char* iFile);
        ~BBFReader();
        // TODO: Copy constructor.

        BBFHeader* getHeaderView() {if(!this->fileBuffer){ return nullptr; } return (BBFHeader*)this->fileBuffer; }
        BBFFooter* getFooterView(uint64_t fOffset);

        // Unsure what type these pointers should be
        const uint8_t* getPageTableView(uint64_t pgOffset) { if (!isSafe(pgOffset)){return nullptr;} return ((const uint8_t*)this->fileBuffer + pgOffset); }
        const uint8_t* getAssetTableView(uint64_t aOffset) { if (!isSafe(aOffset)){return nullptr;} return ((const uint8_t*)this->fileBuffer + aOffset); }
        const uint8_t* getSectionTableView(uint64_t sOffset) { if (!isSafe(sOffset)){return nullptr;} return ((const uint8_t*)this->fileBuffer + sOffset); }
        const uint8_t* getMetadataView(uint64_t mOffset) { if (!isSafe(mOffset)){return nullptr;} return ((const uint8_t*)this->fileBuffer + mOffset); }
        const uint8_t* getExpansionTableView(uint64_t eOffset) { if (!isSafe(eOffset)){return nullptr;} return ((const uint8_t*)this->fileBuffer + eOffset); }

        // This, however, is
        const BBFAsset* getAssetEntryView(uint8_t* assetTable, int assetIndex) { if (!this->footerCache) {return nullptr;} if (!isSafe(this->footerCache->assetCount, assetIndex)) {return nullptr;} return (const BBFAsset*)(assetTable + sizeof(BBFAsset) * assetIndex); }
        const BBFPage* getPageEntryView(uint8_t* pageTable, int pageIndex) { if (!this->footerCache) {return nullptr;} if (!isSafe(this->footerCache->pageCount, pageIndex)) {return nullptr;}  return (const BBFPage*)(pageTable + sizeof(BBFPage) * pageIndex); }
        const BBFSection* getSectionEntryView(uint8_t* sectionTable, int sectionIndex) { if (!this->footerCache) {return nullptr;} if (!isSafe(this->footerCache->sectionCount, sectionIndex)) {return nullptr;}  return ((const BBFSection*)(sectionTable + sizeof(BBFSection) * sectionIndex)); }
        const BBFMeta* getMetaEntryView(uint8_t* metaTable, int metaIndex) { if (!this->footerCache) {return nullptr;} if (!isSafe(this->footerCache->metaCount, metaIndex)) {return nullptr;}  return ((const BBFMeta*)(metaTable + sizeof(BBFMeta) * metaIndex)); }
        const BBFExpansion* getExpansionEntryView(uint8_t* expansionTable, int expansionIndex) { if (!this->footerCache) {return nullptr;} if (!isSafe(this->footerCache->expansionCount, expansionIndex)) {return nullptr;} return ((const BBFExpansion*)(expansionTable + sizeof(BBFExpansion) * expansionIndex)); }

        // Gonna Copy the previous functions and make them accept const args cause we aren't modifying them
        const BBFAsset* getAssetEntryView(const uint8_t* assetTable, int assetIndex) { if (!this->footerCache) {return nullptr;} if (!isSafe(this->footerCache->assetCount, assetIndex)) {return nullptr;} return (const BBFAsset*)(assetTable + sizeof(BBFAsset) * assetIndex); }
        const BBFPage* getPageEntryView(const uint8_t* pageTable, int pageIndex) { if (!this->footerCache) {return nullptr;} if (!isSafe(this->footerCache->pageCount, pageIndex)) {return nullptr;}  return (const BBFPage*)(pageTable + sizeof(BBFPage) * pageIndex); }
        const BBFSection* getSectionEntryView(const uint8_t* sectionTable, int sectionIndex) { if (!this->footerCache) {return nullptr;} if (!isSafe(this->footerCache->sectionCount, sectionIndex)) {return nullptr;}  return ((const BBFSection*)(sectionTable + sizeof(BBFSection) * sectionIndex)); }
        const BBFMeta* getMetaEntryView(const uint8_t* metaTable, int metaIndex) { if (!this->footerCache) {return nullptr;} if (!isSafe(this->footerCache->metaCount, metaIndex)) {return nullptr;}  return ((const BBFMeta*)(metaTable + sizeof(BBFMeta) * metaIndex)); }
        const BBFExpansion* getExpansionEntryView(const uint8_t* expansionTable, int expansionIndex) { if (!this->footerCache) {return nullptr;} if (!isSafe(this->footerCache->expansionCount, expansionIndex)) {return nullptr;} return ((const BBFExpansion*)(expansionTable + sizeof(BBFExpansion) * expansionIndex)); }


        // Get asset data
        const uint8_t* getAssetDataView(uint64_t fileOffset) { if (!isSafe(fileOffset)){return nullptr;} return ((const uint8_t*)this->fileBuffer + fileOffset); }
        // Get strings
        const char* getStringView(uint64_t strOffset);

        
        // Reader Utilities
        bool checkMagic(BBFHeader* pHeader);
        
        // compute asset hashes (xx3-128)
        XXH128_hash_t computeAssetHash(const BBFAsset* assetView);
        XXH128_hash_t computeAssetHash(uint8_t* assetTableView, int assetIndex);

        // compute index hash
        //uint64_t computeFooterHash(const uint8_t* indexStartPr);
        
        //FILE* file;

    private:

        #ifdef _WIN32
            HANDLE hFile;
            HANDLE hMap;
        #else
            int fileDescriptor;
        #endif

        bool isSafe(uint64_t offset, uint64_t size) const;
        bool isSafe(uint64_t count, int index) const;
        bool isSafe(uint64_t offset) const;

        char* getString(uint64_t stringOffset) { if(!isSafe(stringOffset)) {return nullptr;} return (char*)stringOffset; };

        uint8_t* fileBuffer;
        BBFFooter* footerCache;
        size_t fileSize;

};

#endif // BBFCODEC_H