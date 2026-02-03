// stringpool.cpp
// definitions of functions from stringpool.h
#include "stringpool.h"
#include "xxhash.h"

#include <stdint.h>
#include <cstring>


BBFStringPool::BBFStringPool(size_t spoolCap)
{
    poolCap = spoolCap;
    poolData = (char*)malloc(poolCap);
    poolSize = 0;

    entryCount = 0;

    tableCap = 4096; // set to power of two.
    hashTable = (StringEntry*)calloc(tableCap, sizeof(StringEntry));
}

BBFStringPool::~BBFStringPool()
{
    free(poolData);
    free(hashTable);
}

uint64_t BBFStringPool::addString(const char* str)
{
    // Handle edge case when string is just nothing.
    if (!str)
    {
        return 0xFFFFFFFFFFFFFFFF;
    }

    // Need to add new string. See if we need to grow table.
    // if 4(entries) > 3(max), grow. Putting it here so we don't
    // Need to recalculate the slot position.
    if ((entryCount + 1) * 4 > tableCap * 3 )
    {
        growTable();
    }

    // Add string into the hash table
    size_t cstrlen = strlen(str);
    XXH64_hash_t xxhash = XXH3_64bits(str, cstrlen);

    size_t slot = xxhash & (tableCap - 1); // Fast modulus

    while(hashTable[slot].hash != 0)
    {
        if (hashTable[slot].hash == xxhash)
        {
            const char* tmpStr = poolData + hashTable[slot].offset;
            if (strcmp(tmpStr, str) == 0)
            {
                return hashTable[slot].offset;
            }
        }
        slot = (slot + 1) & (tableCap - 1);
    }

    // If string not in hashmap, add it
    ++cstrlen;
    if ( poolSize + cstrlen > poolCap )
    {
        //poolCap *= 2;
        size_t newCap = poolCap * 2;
        void *tmp = realloc(poolData, newCap);

        if (!tmp)
        {
            --cstrlen;
            return 0xFFFFFFFFFFFFFFFF;
        }

        poolData = (char*)tmp;
        poolCap = newCap;
    }

    // Increase pool size
    uint64_t offset = (uint64_t)poolSize;
    memcpy(poolData + poolSize, str, cstrlen);
    poolSize += cstrlen;

    // Put in hash table
    hashTable[slot].hash = xxhash;
    hashTable[slot].offset = offset;
    entryCount++;

    // return offset.
    return offset;
};

const char* BBFStringPool::getString(uint64_t offset) const
{
    // get string from the hash table
    if (offset >= poolSize) return nullptr;
    return poolData + offset;
};

void BBFStringPool::growTable()
{
    // Grow the table by a power of two
    size_t oldCap = tableCap;
    StringEntry* oldTable = hashTable;

    tableCap *= 2;
    hashTable = (StringEntry*)calloc(tableCap, sizeof(StringEntry));

    size_t iterator = 0;

    for(; iterator < oldCap; iterator++)
    {
        if(oldTable[iterator].hash != 0)
        {
            // Insert into new table
            size_t slot = (size_t)(oldTable[iterator].hash & (tableCap - 1));

            while ( hashTable[slot].hash != 0 )
            {
                slot = (slot + 1) & (tableCap - 1);
            }

            hashTable[slot] = oldTable[iterator];
        }
    }

    free(oldTable);
}