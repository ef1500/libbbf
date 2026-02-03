//dedupe map

#include "dedupemap.h"
#include "xxhash.h"

#include <stdint.h>
#include <stdlib.h>

BBFAssetTable::BBFAssetTable(size_t aTableCap)
{
    // Initialize AssetTable
    tableCap = aTableCap;
    assetCount = 0;

    hashTable = (AssetEntry*)calloc(tableCap, sizeof(AssetEntry));
}

BBFAssetTable::~BBFAssetTable()
{
    free(hashTable);
}

uint64_t BBFAssetTable::findAsset(XXH128_hash_t fAssetHash) const
{
    size_t slot = (size_t)(fAssetHash.low64 & (tableCap - 1));

    while (hashTable[slot].assetHash.low64 != 0 || hashTable[slot].assetHash.high64 != 0)
    {
        if (hashTable[slot].assetHash.low64 == fAssetHash.low64 && hashTable[slot].assetHash.high64 == fAssetHash.high64)
        {
            return hashTable[slot].assetIndex;
        }
        slot = (slot + 1) & (tableCap - 1);
    }

    // Not found
    return 0xFFFFFFFFFFFFFFFF;
}

void BBFAssetTable::addAsset(XXH128_hash_t aAssetHash, uint64_t aAssetIndex)
{
    // Check size. Grow table if would be too small.
    // See if we're at 70% load capacity, then grow the table
    if (assetCount * 10 > tableCap * 7 )
    {
        growTable();
    }

    size_t slot = (size_t)(aAssetHash.low64 & (tableCap - 1));
    while (hashTable[slot].assetHash.low64 != 0 || hashTable[slot].assetHash.high64 != 0)
    {
        // Assume findAsset was already called.
        slot = (slot + 1) & (tableCap - 1);
    }

    hashTable[slot].assetHash = aAssetHash;
    hashTable[slot].assetIndex = aAssetIndex;
    assetCount++;
}

void BBFAssetTable::growTable()
{
    // Copy from stringpool.
    // Grow the table by a power of two
    size_t oldCap = tableCap;
    AssetEntry* oldTable = hashTable;

    tableCap *= 2;
    hashTable = (AssetEntry*)calloc(tableCap, sizeof(AssetEntry));

    size_t iterator = 0;

    for(; iterator < oldCap; iterator++)
    {
        if(oldTable[iterator].assetHash.low64 != 0 || oldTable[iterator].assetHash.high64 != 0)
        {
            // Insert into new table
            size_t slot = (size_t)(oldTable[iterator].assetHash.low64 & (tableCap - 1));

            while ( hashTable[slot].assetHash.low64 != 0 || hashTable[slot].assetHash.high64 != 0  )
            {
                slot = (slot + 1) & (tableCap - 1);
            }

            hashTable[slot] = oldTable[iterator];
        }
    }

    free(oldTable);
}
