#ifndef DEDUPEMAP_H
#define DEDUPEMAP_H

#include <stdint.h>
#include <stdlib.h>
#include "xxhash.h"

struct AssetEntry
{
    XXH128_hash_t assetHash; // use xxh3-128
    uint64_t assetIndex;
};

class BBFAssetTable
{
    public:
        BBFAssetTable(size_t aTableCap = 4096);
        ~BBFAssetTable();

        uint64_t findAsset(XXH128_hash_t fAssetHash) const;
        void addAsset(XXH128_hash_t aAssetHash, uint64_t aAssetIndex);
        
        size_t getAssetCount() const { return assetCount; }
    
    private:
        size_t tableCap;
        size_t assetCount;

        AssetEntry* hashTable;
        void growTable();
};

#endif // DEDUPEMAP_H