// Custom String-Pool
// No STL, No std::string
#ifndef STRINGPOOL_H
#define STRINGPOOL_H

#include <stdint.h>
#include <stdlib.h>

struct StringEntry
{
    uint64_t hash; // XXH3 Hash
    uint64_t offset; // Offset into string pool
};

// Create String Pool
class BBFStringPool
{
    public:
        BBFStringPool(size_t spoolCap = 4096);
        ~BBFStringPool();

        uint64_t addString(const char* str);
        const char* getString(uint64_t offset) const;

        // get entire block
        const char* getDataRaw() const { return poolData; }
        size_t getUsedSize() const { return poolSize; }
        size_t getEntryCount() const { return entryCount; }

    private:        
        char* poolData;
        size_t poolSize;
        size_t poolCap;
        size_t entryCount;

        StringEntry* hashTable;
        size_t tableCap; // Power of 2.

        void growTable(); // Collision Handler
};

#endif // STRINGPOOL_H