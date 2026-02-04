#include "libbbf.h"
#include "bbfcodec.h"
#include "xxhash.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <inttypes.h>

#ifdef WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <string.h>
#endif

#define MAX_ENTRIES 256 // Not using vector.

constexpr uint32_t val32(const char *const str, const uint32_t fnvOff = 0x811c9dc5) 
{
    return *str ? val32(str + 1, (fnvOff ^ *str) * 0x01000193) : fnvOff;
}

const char* getMediaType(const char mediaVal)
{
    // Get media type from asset type (see SPECNOTE 4.3.1)
    switch ((uint8_t)mediaVal)
    {
        case (1):
            return ".avif";
        case (2):
            return ".png";
        case (3):
            return ".webp";
        case (4):
            return ".jxl";
        case (5):
            return ".bmp";
        case (7):
            return ".gif";
        case (8):
            return ".tiff";
        case (9):
            return ".jpg";
        default:
            return ".dat"; //unknown data type
    }
}

// String Compare
int qComp(const void* strA, const void* strB)
{
    return (strcmp(*(const char**)strA, *(const char**)strB));
}

char* readTxtFile(const char* fPath)
{
    FILE* file = fopen(fPath, "rb");

    if(!file)
    {
        // can't open file
        printf("[BBFMUX] Unable to read text file: %s", fPath);
        return 0;
    }

    fseek(file, 0, SEEK_END);
    size_t fileSize = ftell(file);
    fseek(file, 0, SEEK_SET);

    char* cString = (char*)malloc(fileSize + 1);
    size_t readSize = fread(cString, 1, fileSize, file);

    if (readSize != fileSize)
    {
        printf("[BBFMUX] Unable to read text file: %s", fPath);
        return 0;
    }

    fclose(file);

    cString[fileSize] = 0;
    return cString;
}

uint64_t resolveTarget(char* val, char** fileList, int count)
{
    if (!val || !*val) return 0;

    char* p = val;
    bool isNum = true;
    while(*p) { if(*p < '0' || *p > '9') { isNum = false; break; } p++; }
    if(isNum) return strtoull(val, NULL, 10);

    #ifdef WIN32
    char sep = '\\';
    #else
    char sep = '/';
    #endif

    for(int i=0; i<count; ++i) 
    {
        char* fileName = strrchr(fileList[i], sep);
        fileName = (fileName) ? fileName + 1 : fileList[i];
        if(strcmp(fileName, val) == 0) return i;
    }
    
    printf("Warning: Could not resolve target '%s'\n", val);
    return 0;
}

char** scanDir(const char* folder, uint64_t* count)
{
    int capacity = 128;
    char** files = (char**)malloc(sizeof(char*) * capacity);
    *count = 0;
    
    // Pre-calculate folder length to avoid re-measuring 1000 times
    size_t folderLen = strlen(folder); 
    
    // Check if folder needs a separator
    #ifdef WIN32
    char sep = '\\';
    #else
    char sep = '/';
    #endif

    // Detect if user already put a slash at end of input
    bool needsSep = (folder[folderLen-1] != '/' && folder[folderLen-1] != '\\');

#ifdef WIN32
    char searchPath[1024];
    snprintf(searchPath, 1024, "%s\\*", folder);
    WIN32_FIND_DATA fd;
    HANDLE hFind = FindFirstFile(searchPath, &fd);
    
    if(hFind != INVALID_HANDLE_VALUE) 
    {
        do 
        {
            if(!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) 
            {
                if(*count >= capacity) 
                {
                    capacity *= 2;
                    files = (char**)realloc(files, sizeof(char*) * capacity);
                }
                
                // Join Path
                size_t nameLen = strlen(fd.cFileName);
                char* joined = (char*)malloc(folderLen + (needsSep?1:0) + nameLen + 1);
                
                memcpy(joined, folder, folderLen);
                if(needsSep) joined[folderLen] = sep;
                strcpy(joined + folderLen + (needsSep?1:0), fd.cFileName);
                
                files[(*count)++] = joined;
            }
        } while(FindNextFile(hFind, &fd));
        FindClose(hFind);
    }
#else
    DIR* d;
    struct dirent* dir;
    d = opendir(folder);
    if (d) 
    {
        while ((dir = readdir(d)) != NULL) 
        {
            if (dir->d_type == DT_REG) 
            { 
                if((int)*count >= capacity) 
                {
                    capacity *= 2;
                    files = (char**)realloc(files, sizeof(char*) * capacity);
                }

                // Join Path
                size_t nameLen = strlen(dir->d_name);
                char* joined = (char*)malloc(folderLen + (needsSep?1:0) + nameLen + 1);
                
                memcpy(joined, folder, folderLen);
                if(needsSep) joined[folderLen] = sep;
                strcpy(joined + folderLen + (needsSep?1:0), dir->d_name);
                
                files[(*count)++] = joined;
            }
        }
        closedir(d);
    }
#endif
    return files;
}

const char* helpText = 
"========[ BBFMUX v3.0 ]====================================================\n"
"| Bound Book Format Muxer                             Developed by EF1500 |\n"
"===========================================================================\n"
"\n"
"USAGE: bbfmux <INPUT_DIR|BBF_FILE> [MODE] [OPTIONS]...\n"
"\n"
"MODES (Mutually Exclusive):\n"
"  (Default)    Mux folder contents into a BBF container\n"
"  --info       Display headers, metadata, and statistics\n"
"  --verify     Validate XXH3-128/64 hashes\n"
"  --extract    Unpack contents to disk\n"
"  --petrify    Linearize BBF file for faster reading\n"
"\n"
"MUXER OPTIONS:\n"
"  --meta=K:V[:P]         Add metadata (Key:Value[:Parent])\n"
"  --metafile=<FILE>      Read K:V:P entries from file\n"
"  --section=N:T[:P]      Add section (Name:Target[:Parent])\n"
"  --sections=<FILE>      Read section entries from file\n"
//"  --order=<FILE>         Define page ordering (File:Index)\n"
"  --ream-size=<N>        Ream size exponent override (2^N)\n"
"  --alignment=<N>        Byte alignment exponent override (2^N)\n"
"  --variable-ream-size   Enable variable ream sizing (reccomended)\n"
"\n"
"VERIFY / EXTRACT OPTIONS:\n"
"  --section=\"NAME\"    Target specific section\n"
"  --rangekey=\"KEY\"    Stop extraction on key substring match\n"
"  --asset=<ID>        Target specific asset ID\n"
"  --outdir=[PATH]     Extract asset(s) to directory\n"
"  --write-meta[=F]    Dump metadata to file [default: path.txt]\n"
"  --write-hashes[=F]  Dump hashes to file [default: hashes.txt]\n"
"\n"
// --footer hash isn't done yet.
"INFO FLAGS:\n"
"  --hashes, --footer, --sections, --counts, --header, --metadata, --offsets\n"
"\n"
"NOTE: Use '%c' as delimiter on this system.\n";

struct Meta
{
    char *key; 
    char *value;
    char *parent; 
};

struct Section
{
    char *name;
    char *target;
    char *parent; 
};

struct Config 
{
    char* bbfFolder;

    char* sectionBlock;
    char* metaBlock;
    char* orderBlock;
    
    enum Mode 
    {
        MUX = 0,
        INFO,
        VERIFY,
        PETRIFY,
        EXTRACT 
    } mode;
    
    // Global Mux Settings
    struct {
        Meta meta[MAX_ENTRIES];
        uint64_t metaCount;
        char* metaFile;
        
        Section sections[MAX_ENTRIES];
        uint64_t sectionCount;
        char* sectionFile;
        
        char* orderFile;
        char* outputFile;
        uint64_t reamSize = BBF::DEFAULT_SMALL_REAM_THRESHOLD;
        uint32_t alignment = BBF::DEFAULT_GUARD_ALIGNMENT;
        bool variableReamSize = false;
    } muxer;

    union 
    {
        struct 
        {
            char* outputFile;
        } petrify;

        struct 
        {
            char* sectionName;
            char* rangeKey;
            uint64_t pageIndex = 0xFFFFFFFFFFFFFFFF;
            uint64_t assetIndex = 0xFFFFFFFFFFFFFFFF;
            const char* metaOut = "meta.txt";
            const char* hashOut = "hashes.txt";
            char* outdir;
        } extract;

        struct 
        {
            char* sectionName;
            uint64_t assetIndex = 0xFFFFFFFFFFFFFFFF;
            uint64_t pageIndex = 0xFFFFFFFFFFFFFFFF;
            bool verifyFooter = false;
        } verify;

        struct 
        {
            bool showHashes;
            bool showSections;
            bool showPages;
            bool showCounts;
            bool showMeta = true;
            bool showHeader;
            bool showFooter;
            bool showStringPool;
            bool showOffsets;
        } info;
    };
};

int main(int argc, char** argv)
{
    Config cfg = {};
    cfg.mode = Config::MUX;

    #ifdef WIN32
    const char DELIMETER = ';';
    #else
    const char DELIMETER = ':';
    #endif

    int iterator = 1;
    for(; iterator < argc; ++iterator)
    {
        char* arg = argv[iterator];

        if (*arg != '-') 
        {
            if (cfg.bbfFolder == nullptr) 
            {
                cfg.bbfFolder = arg;
            }
            else
            {
                cfg.muxer.outputFile = arg;
            }
            continue;
        }

        // Handle k:v[:p]
        char* val = arg; 
        while(*val && *val != '=') val++; 
        if(*val) *val++ = 0; 

        switch(val32(arg))
        {
            // Modes
            case val32("--info"):
                cfg.mode = Config::INFO;
                break;
            case val32("--extract"):
                cfg.mode = Config::EXTRACT;
                break;
            case val32("--verify"):
                cfg.mode = Config::VERIFY;
                break;
            case val32("--petrify"): 
                cfg.mode = Config::PETRIFY; 
                if (*val) cfg.petrify.outputFile = val;
                break;

            case val32("--help"): 
                printf(helpText, DELIMETER); 
                return 0;


            case val32("--meta"): 
            {
                // Find delimeters. Add metadata.
                if(cfg.muxer.metaCount >= MAX_ENTRIES) break;
                Meta* m = &cfg.muxer.meta[cfg.muxer.metaCount++];
                m->key = val;
                
                char* s = val;
                while(*s && (*s != DELIMETER || s[-1] == '\\')) s++;
                if(*s) *s++ = 0, m->value = s;

                while(*s && (*s != DELIMETER || s[-1] == '\\')) s++;
                if(*s) *s++ = 0, m->parent = s;
                break;
            }

            case val32("--metafile"):
                cfg.muxer.metaFile = val;
                break;

            case val32("--section"):
                if (cfg.mode == Config::MUX) 
                {
                    if(cfg.muxer.sectionCount >= MAX_ENTRIES) break;
                    // Syntax: Name:Target[:Parent]
                    Section* s = &cfg.muxer.sections[cfg.muxer.sectionCount++];
                    s->name = val;
                    char* ptr = val;
                    
                    while(*ptr && (*ptr != DELIMETER || ptr[-1] == '\\')) ptr++;
                    if(*ptr) *ptr++ = 0, s->target = ptr;

                    while(*ptr && (*ptr != DELIMETER || ptr[-1] == '\\')) ptr++;
                    if(*ptr) *ptr++ = 0, s->parent = ptr;
                } 
                else if (cfg.mode == Config::EXTRACT) cfg.extract.sectionName = val;
                else if (cfg.mode == Config::VERIFY) cfg.verify.sectionName = val;
                break;

            case val32("--sections"):
                if (cfg.mode == Config::INFO) cfg.info.showSections = true;
                if (cfg.mode == Config::MUX) cfg.muxer.sectionFile = val;
                break;

            case val32("--order"):              cfg.muxer.orderFile = val; break;
            case val32("--ream-size"):          cfg.muxer.reamSize = atoi(val); break;
            case val32("--variable-ream-size"): cfg.muxer.variableReamSize = true; break;
            case val32("--alignment"):          cfg.muxer.alignment = atoi(val); break;

            // Extraction exclusive args
            case val32("--rangekey"):     cfg.extract.rangeKey = val; break;
            case val32("--write-meta"):   cfg.extract.metaOut = *val ? val : (char*)"path.txt"; break;
            case val32("--write-hashes"): cfg.extract.hashOut = *val ? val : (char*)"hashes.txt"; break;
            case val32("--outdir"): cfg.extract.outdir = val; break;
            // Extract + verify
            case val32("--asset"): 
                if (cfg.mode == Config::EXTRACT) cfg.extract.assetIndex = (uint64_t)atoi(val);
                if (cfg.mode == Config::VERIFY) cfg.extract.assetIndex = (uint64_t)atoi(val); 
                break;


            // info exclusive args
            case val32("--hashes"):   cfg.info.showHashes = true; break;
            case val32("--pages"):    cfg.info.showPages = true; break;
            case val32("--counts"):   cfg.info.showCounts = true; break;
            case val32("--metadata"): cfg.info.showMeta = true; break;
            case val32("--header"):   cfg.info.showHeader = true; break;
            case val32("--footer"):   cfg.info.showFooter = true; break;
            case val32("--strings"):  cfg.info.showStringPool = true; break;
            case val32("--offsets"):  cfg.info.showStringPool = true; break;
        }
    }

    if (cfg.mode == Config::MUX) 
    {
        if (!cfg.bbfFolder)
        {
            printf("Invalid Syntax. Run bbfmux --help to display avaliable options.");
            return 1;
        }

        uint32_t headerFlags = 0;
        if (cfg.muxer.variableReamSize)
        {
            headerFlags |= BBF::BBF_VARIABLE_REAM_SIZE_FLAG;
        }

        BBFBuilder bbfBuilder(cfg.muxer.outputFile, cfg.muxer.alignment, cfg.muxer.reamSize, headerFlags);
        // generate a list (char** files) of files in the folder
        uint64_t fileCount;
        char** fileList = nullptr;

        // Use the --order=order.txt if specified to determine the order
        // In which we add pages
        if (cfg.muxer.orderFile)
        {
            // sort the order, respecting negatives. zero idexed.
            // filename:pageindex
            // bbfBuilder.addPage(const char* filePath)
        }
        else
        {
            fileList = scanDir(cfg.bbfFolder, &fileCount);
            qsort(fileList, fileCount, sizeof(char*), qComp);
        }

        uint64_t fileItrator = 0;
        for (; fileItrator < fileCount; fileItrator++)
        {
            bbfBuilder.addPage(fileList[fileItrator]);
        }

        // Open the metafile and sectionfile up for reading
        // and we add these entries to our config.
        if (cfg.muxer.metaFile)
        {
            char* data = readTxtFile(cfg.muxer.metaFile);
            cfg.metaBlock = data;
    
            if(data)
            {
                char* cursor = data;
                while(*cursor)
                {
                    while(*cursor && (*cursor <= 32)) cursor++;
                    if(!*cursor) break;

                    char* key = cursor;
                    char* value = nullptr; 
                    char* parent = nullptr;

                    while(*cursor && *cursor != '\n' && *cursor != '\r')
                    {
                        if (*cursor == DELIMETER && (*(cursor-1) != '\\'))
                        {
                            *cursor = 0; // Terminate prev string
                            if(!value) value = cursor + 1;
                            else if(!parent) parent = cursor + 1;
                        }
                        cursor++;
                    }
                    if(*cursor) *cursor++ = 0; // Terminate line

                    if(cfg.muxer.metaCount < MAX_ENTRIES) {
                        cfg.muxer.meta[cfg.muxer.metaCount].key = key;
                        cfg.muxer.meta[cfg.muxer.metaCount].value = value;
                        cfg.muxer.meta[cfg.muxer.metaCount].parent = parent;
                        cfg.muxer.metaCount++;
                    }
                }
            }
        }

        if (cfg.muxer.sectionFile)
        {
            char* sectionData = readTxtFile(cfg.muxer.sectionFile);
            cfg.sectionBlock = sectionData;
            if(sectionData)
            {
                char* cursor = sectionData;
                while(*cursor)
                {
                    while(*cursor && (*cursor <= 32)) cursor++;
                    if(!*cursor) break;

                    char* name = cursor;
                    char* target = nullptr; 
                    char* parent = nullptr;

                    while(*cursor && *cursor != '\n' && *cursor != '\r')
                    {
                        if (*cursor == DELIMETER && (*(cursor-1) != '\\'))
                        {
                            *cursor = 0; // Terminate prev string
                            if(!target) target = cursor + 1;
                            else if(!parent) parent = cursor + 1;
                        }
                        cursor++;
                    }
                    if(*cursor) *cursor++ = 0; // Terminate line

                    if(cfg.muxer.sectionCount < MAX_ENTRIES) 
                    {
                        cfg.muxer.sections[cfg.muxer.sectionCount].name = name;
                        cfg.muxer.sections[cfg.muxer.sectionCount].target = target;
                        cfg.muxer.sections[cfg.muxer.sectionCount].parent = parent;
                        cfg.muxer.sectionCount++;
                    }
                }
            }
        }

        int metaIterator = 0;
        for(; metaIterator < (int)cfg.muxer.metaCount; metaIterator++) 
        {
            if(!cfg.muxer.meta[metaIterator].parent)
            {
                bbfBuilder.addMeta(cfg.muxer.meta[metaIterator].key, cfg.muxer.meta[metaIterator].value);
            }
            else
            {
                bbfBuilder.addMeta(cfg.muxer.meta[metaIterator].key, cfg.muxer.meta[metaIterator].value, cfg.muxer.meta[metaIterator].parent);
            }
        }

        // Add sections
        int sectionIterator = 0;
        for (; sectionIterator < (int)cfg.muxer.sectionCount; sectionIterator++)
        {
            if(!cfg.muxer.sections[sectionIterator].parent)
            {
                // If not a filename, resolve target to uint64_t.
                // We do this here because we can take both in the syntax
                uint64_t targetPage = resolveTarget(cfg.muxer.sections[sectionIterator].target, fileList, cfg.muxer.sectionCount);
                bbfBuilder.addSection(cfg.muxer.sections[sectionIterator].name, targetPage);
            }
            else
            {
                // Resolve target
                // Add Parent
                uint64_t targetPage = resolveTarget(cfg.muxer.sections[sectionIterator].target, fileList, cfg.muxer.sectionCount);
                bbfBuilder.addSection(cfg.muxer.sections[sectionIterator].name, targetPage, cfg.muxer.sections[sectionIterator].parent);
            }
        }

        bbfBuilder.finalize();
        printf("Muxed %lu files to '%s'...\n", fileCount, cfg.muxer.outputFile);

        // Free
        fileItrator = 0;
        for(; fileItrator < fileCount; fileItrator++)
        {
            free(fileList[fileItrator]);
        }
        free(fileList);

        if (cfg.sectionBlock)
        {
            free(cfg.sectionBlock);
        }
        if (cfg.metaBlock)
        {
            free(cfg.metaBlock);
        }
        if (cfg.orderBlock)
        {
            free(cfg.orderBlock);
        }

        return 0;
    }

    if (cfg.mode == Config::INFO)
    {
        if (!cfg.bbfFolder)
        {
            printf("[BBFMUX] Argument syntax error: missing input file.\n");
            return 1;
        }

        // now print the info
        BBFReader bbfReader(cfg.bbfFolder);

        BBFHeader* pHeader = bbfReader.getHeaderView();

        if (!pHeader)
        {
            printf("[BBFMUX] Unable to read header.\n");
            return 1;
        }

        BBFFooter* pFooter = bbfReader.getFooterView(pHeader->footerOffset);

        if (!pFooter)
        {
            printf("Unable to retrieve footer.\n");
            pHeader = nullptr;
            return 1;
        }

        if (cfg.info.showHeader)
        {
            printf("\n=== HEADER ===\n");
            printf("Signature:    %c%c%c%c\n", pHeader->magic[0], pHeader->magic[1], pHeader->magic[2], pHeader->magic[3]);
            printf("Version:      %u\n", pHeader->version);
            printf("Flags:        0x%08X\n", pHeader->flags);

            bool isPetrified = (pHeader->flags & BBF::BBF_PETRIFICATION_FLAG);
            bool isVariable  = (pHeader->flags & BBF::BBF_VARIABLE_REAM_SIZE_FLAG);

            printf("  [%c] Petrified (Linearized)\n", isPetrified ? 'x' : ' ');
            printf("  [%c] Variable Alignment (Reams)\n", isVariable ? 'x' : ' ');

            printf("Alignment:    %u (Pow2) -> %u bytes\n", pHeader->alignment, (1 << pHeader->alignment));
            printf("Ream Size:    %u (Pow2) -> %u bytes\n", pHeader->reamSize, (1 << pHeader->reamSize));
            printf("Footer Offset:   %" PRIu64 "\n", pHeader->footerOffset);

            // free
            pHeader = nullptr;
        }

        if (cfg.info.showFooter)
        {
            printf("\n=== FOOTER ===\n");
            printf("Offsets:\n");
            printf("  Assets:   0x%016" PRIx64 "\n", pFooter->assetOffset);
            printf("  Pages:    0x%016" PRIx64 "\n", pFooter->pageOffset);
            printf("  Sections: 0x%016" PRIx64 "\n", pFooter->sectionOffset);
            printf("  Meta:     0x%016" PRIx64 "\n", pFooter->metaOffset);
            printf("  Expansion:0x%016" PRIx64 "\n", pFooter->expansionOffset);
            printf("  Strings:  0x%016" PRIx64 "\n", pFooter->stringPoolOffset);
            
            printf("Counts:\n");
            printf("  Assets:   %" PRIu64 "\n", pFooter->assetCount);
            printf("  Pages:    %" PRIu64 "\n", pFooter->pageCount);
            printf("  Metadata: %" PRIu64 "\n", pFooter->metaCount);
            printf("  Sections: %" PRIu64 "\n", pFooter->sectionCount);

            printf("\n");
            printf("  Footer Hash (Index Hash): 0x%016" PRIx64 "\n", pFooter->footerHash);

        }

        if (cfg.info.showCounts)
        {
            printf("\n=== Counts ===\n");
            printf("  Assets:   %" PRIu64 "\n", pFooter->assetCount);
            printf("  Pages:    %" PRIu64 "\n", pFooter->pageCount);
            printf("  Metadata: %" PRIu64 "\n", pFooter->metaCount);
            printf("  Sections: %" PRIu64 "\n", pFooter->sectionCount);
        }

        if (cfg.info.showOffsets)
        {
            printf("\n=== Offsets ===\n");
            printf("  Assets:   0x%016" PRIx64 "\n", pFooter->assetOffset);
            printf("  Pages:    0x%016" PRIx64 "\n", pFooter->pageOffset);
            printf("  Sections: 0x%016" PRIx64 "\n", pFooter->sectionOffset);
            printf("  Meta:     0x%016" PRIx64 "\n", pFooter->metaOffset);
            printf("  Expansion:0x%016" PRIx64 "\n", pFooter->expansionOffset);
            printf("  Strings:  0x%016" PRIx64 "\n", pFooter->stringPoolOffset);
        }

        if (cfg.info.showMeta)
        {
            printf("\n=== Metadata ===\n");
            const uint8_t* metaTable = bbfReader.getMetadataView(pFooter->metaOffset);
            
            uint64_t metadataIterator = 0;
            for (; metadataIterator < pFooter->metaCount; metadataIterator++)
            {
                const BBFMeta* metadata = bbfReader.getMetaEntryView((uint8_t*)metaTable, metadataIterator);
                
                if (!metadata)
                {
                    printf("[BBFMUX] Unable to read metadata.");
                    metadata = nullptr;
                    metaTable = nullptr;
                    pFooter = nullptr;
                    pHeader = nullptr;
                    return 1;
                }

                const char* keyPtr = bbfReader.getStringView(metadata->keyOffset);
                const char* valPtr = bbfReader.getStringView(metadata->valueOffset);

                const char* safeKey = keyPtr ? keyPtr : "<CORRUPT KEY>";
                const char* safeVal = valPtr ? valPtr : "<CORRUPT VALUE>";


                printf("%s : %s\n", safeKey, safeVal);

                if (metadata->parentOffset != 0xFFFFFFFFFFFFFFFF)
                {
                    const char* parentPtr = bbfReader.getStringView(metadata->parentOffset);
                    printf("     (Parent Key: %s)\n", parentPtr ? parentPtr : "<INVALID>");
                    parentPtr = nullptr;
                }

                metadata = nullptr;
                keyPtr = nullptr;
                valPtr = nullptr;
                safeKey = nullptr;
                safeVal = nullptr;
            }

            metaTable = nullptr;

        }

        if (cfg.info.showSections)
        {
            printf("\n=== Sections ===\n");
            const uint8_t* sectionTable = bbfReader.getSectionTableView(pFooter->sectionOffset);
            
            uint64_t sectionIterator = 0;
            for (; sectionIterator < pFooter->sectionCount; sectionIterator++)
            {
                const BBFSection* section = bbfReader.getSectionEntryView((uint8_t*)sectionTable, sectionIterator);
                if (!section)
                {
                    printf("[BBFMUX] Unable to read section data.");
                    section = nullptr;
                    sectionTable = nullptr;
                    pFooter = nullptr;
                    pHeader = nullptr;
                    return 1;
                }

                const char* sectionName = bbfReader.getStringView(section->sectionTitleOffset);
                uint64_t sectionStartIndex = section->sectionStartIndex;

                const char* safeName = sectionName ? sectionName : "<CORRUPT KEY>";

                printf("%s : %llu\n", safeName, (long long unsigned int)sectionStartIndex);

                if (section->sectionParentOffset != 0xFFFFFFFFFFFFFFFF)
                {
                    const char* parentPtr = bbfReader.getStringView(section->sectionParentOffset);
                    printf("(Parent Section: %s)\n", parentPtr ? parentPtr : "<INVALID>");
                    parentPtr = nullptr;
                }

                section = nullptr;
                sectionName = nullptr;
                safeName = nullptr;
            }

            sectionTable = nullptr;

        }

        if (cfg.info.showHashes)
        {
            const uint8_t* assetTable = bbfReader.getAssetTableView(pFooter->assetOffset);

            if (!assetTable)
            {
                printf("[BBFMUX] Unable to read asset table.");
            }

            printf("\n=== ASSET TABLE (%lu entries) ===\n", pFooter->assetCount);
            printf("ID  | Hash (XXH3-128)                  | Offset      | Size     | Type\n");
            printf("----|----------------------------------|-------------|----------|-----\n");




            uint64_t assetIterator = 0;
            for(; assetIterator < pFooter->assetCount; assetIterator++)
            {
                const BBFAsset* asset = bbfReader.getAssetEntryView((uint8_t*)assetTable, assetIterator);

                if (!asset)
                {
                    printf("[BBFMUX] Unable to read asset %lu", assetIterator);
                    assetTable = nullptr;
                    pFooter = nullptr;
                    return 1;
                }

                printf("%3" PRIu64 " | %016" PRIx64 "%016" PRIx64 " | %11" PRIu64 " | %8" PRIu64 " | 0x%02X\n", 
                    assetIterator, 
                    asset->assetHash[1],
                    asset->assetHash[0],
                    asset->fileOffset, 
                    asset->fileSize, 
                    asset->type
                );

                asset = nullptr;
            }

            assetTable = nullptr;
        }

    }

    if (cfg.mode == Config::PETRIFY)
    {
        BBFBuilder bbfPetrifier(cfg.petrify.outputFile);

        if (!cfg.petrify.outputFile)
        {
            printf("[BBFMUX] No file selected for petrification.\n");
            return 1;
        }

        printf("[BBFMUX] Petrifying %s to %s...\n", cfg.bbfFolder, cfg.petrify.outputFile);
        bool petSuccess = bbfPetrifier.petrifyFile(cfg.bbfFolder, cfg.petrify.outputFile);
        if (petSuccess)
        {
            printf("[BBFMUX] Success.\n");
            return 0;
        }
        else
        {
            printf("[BBFMUX] Failed to petrify %s.\n", cfg.bbfFolder);
            return 1;
        }
    }


    if (cfg.mode == Config::VERIFY)
    {
        BBFReader bbfReader(cfg.bbfFolder);

        BBFHeader* pHeader = bbfReader.getHeaderView();
        BBFFooter* pFooter = bbfReader.getFooterView(pHeader->footerOffset);

        if (cfg.verify.assetIndex)
        {

            if (cfg.verify.assetIndex > pFooter->assetCount)
            {
                printf("[BBFMUX] Invalid Asset Index: %llu (Max: %llu)\n", (long long unsigned int)cfg.verify.assetIndex, (long long unsigned int)pFooter->assetCount);
                pHeader = nullptr;
                pFooter = nullptr;
                return 1;
            }
            // Get asset data
            const uint8_t* assetTable = bbfReader.getAssetTableView(pFooter->assetOffset);
            const BBFAsset* pAsset = bbfReader.getAssetEntryView((uint8_t*)assetTable, cfg.verify.assetIndex);


            XXH128_hash_t assetHash = bbfReader.computeAssetHash(pAsset);
            if (assetHash.low64 == pAsset->assetHash[0] && assetHash.high64 == pAsset->assetHash[1])
            {
                printf("[BBFMUX] [OK] Hashes Match (%llx%llx)\n", (long long unsigned int)assetHash.high64, (long long unsigned int)assetHash.low64);
            }
            else
            {
                printf("[BBFMUX] [FAIL] Hash Mismatch.\nComputed Hash: %llx%llx\nAsset Hash: %llx%llx\n", (long long unsigned int)assetHash.high64, (long long unsigned int)assetHash.low64, (long long unsigned int)pAsset->assetHash[1], (long long unsigned int)pAsset->assetHash[0]);
                // free
                assetTable = nullptr;
                pAsset = nullptr;
                pHeader = nullptr;
                pFooter = nullptr;
                return 1;
            }
        }

        if (cfg.verify.sectionName)
        {
            // Find Section
            BBFSection* tSection = nullptr;
            int pageCount = 0;
            const uint8_t* sectionTable = bbfReader.getSectionTableView(pFooter->sectionOffset);
            
            int sectionIterator = 0;
            for (; sectionIterator < (int)pFooter->sectionCount; sectionIterator++)
            {
                const BBFSection* section = bbfReader.getSectionEntryView((uint8_t*)sectionTable, sectionIterator);
                if (!section)
                {
                    printf("[BBFMUX] Unable to read section data.");
                    section = nullptr;
                    sectionTable = nullptr;
                    pFooter = nullptr;
                    pHeader = nullptr;
                    return 1;
                }

                if (strcmp(bbfReader.getStringView(section->sectionTitleOffset),cfg.verify.sectionName) == 0)
                {
                    tSection = (BBFSection*)section;
                    uint64_t endPage = pFooter->pageCount;

                    int lookAheadIterator = sectionIterator + 1;
                    
                    for (; lookAheadIterator < (int)pFooter->sectionCount; (int)lookAheadIterator++)
                    {
                        const BBFSection* checkSection = bbfReader.getSectionEntryView((uint8_t*)sectionTable, lookAheadIterator);
                        if (!checkSection) break;

                        bool isChild = false;

                        // Check if this section lists our target section as its parent
                        if (checkSection->sectionParentOffset != 0xFFFFFFFFFFFFFFFF)
                        {
                            const char* parentName = bbfReader.getStringView(checkSection->sectionParentOffset);
                            if (strcmp(parentName, cfg.verify.sectionName) == 0)
                            {
                                isChild = true;
                            }
                        }

                        // If it is NOT a child, this is the start of the next volume/unrelated section.
                        if (!isChild)
                        {
                            endPage = checkSection->sectionStartIndex;
                            break;
                        }
                    }

                    pageCount = (int)(endPage - tSection->sectionStartIndex);

                    section = nullptr;
                    break;
                }

            }

            if (!tSection)
            {
                printf("[BBFMUX] Unable to find section with title: %s\n", cfg.verify.sectionName);
                pFooter = nullptr;
                pHeader = nullptr;
                sectionTable = nullptr;
                return 1;
            }

            if (pageCount < 0)
            {
                printf("[BBFMUX] Got a negative page count. Unable to verify section %s\n", cfg.verify.sectionName);
                pFooter = nullptr;
                pHeader = nullptr;
                tSection = nullptr;
                sectionTable = nullptr;
                return 1;
            }

            if (pageCount == 0)
            {
                printf("[BBFMUX] No pages to verify. Unable to verify section %s\n", cfg.verify.sectionName);
                pFooter = nullptr;
                pHeader = nullptr;
                tSection = nullptr;
                sectionTable = nullptr;
                return 1;
            }

            // Loop over pages, verify each.
            int pageIterator = 0;
            const uint8_t* pageTable = bbfReader.getPageTableView(pFooter->pageOffset);
            const uint8_t* assetTable = bbfReader.getAssetTableView(pFooter->assetOffset);

            for (; pageIterator < pageCount; pageIterator++)
            {
                const BBFPage* pPage = bbfReader.getPageEntryView((uint8_t*)pageTable, tSection->sectionStartIndex+pageIterator);
                const BBFAsset* pAsset = bbfReader.getAssetEntryView((uint8_t*)assetTable, pPage->assetIndex);

                XXH128_hash_t assetHash = bbfReader.computeAssetHash(pAsset);
                if (assetHash.low64 == pAsset->assetHash[0] && assetHash.high64 == pAsset->assetHash[1])
                {
                    printf("[BBFMUX] [%llu | OK] Hashes Match (%llx%llx)\n", (long long unsigned int)(tSection->sectionStartIndex + pageIterator), (long long unsigned int)assetHash.high64, (long long unsigned int)assetHash.low64);
                }
                else
                {
                    printf("[BBFMUX] [%llu | FAIL] Hash Mismatch (Asset: %llu).\nComputed Hash: %llx%llx\nAsset Hash: %llx%llx\n", (long long unsigned int)(tSection->sectionStartIndex + pageIterator), (long long unsigned int)pPage->assetIndex, (long long unsigned int)assetHash.high64, (long long unsigned int)assetHash.low64, (long long unsigned int)pAsset->assetHash[1], (long long unsigned int)pAsset->assetHash[0]);
                }

                pPage = nullptr;
                pAsset = nullptr;
            }
            printf("[BBFMUX] Finished Verifying Hashes\n");

            tSection = nullptr;
            pageTable = nullptr;
            assetTable = nullptr;
            pFooter = nullptr;
            pHeader = nullptr;

        }

        // Nothing specified, verify the whole book.
        // Note these are quick and dirty, we're technically verifying deduped assets
        // Multiple times.
        if (!cfg.verify.assetIndex && !cfg.verify.sectionName && !cfg.verify.pageIndex)
        {
            const uint8_t* pageTable = bbfReader.getPageTableView(pFooter->pageOffset);
            const uint8_t* assetTable = bbfReader.getAssetTableView(pFooter->assetOffset);
            uint64_t pageIterator = 0;

            for(; pageIterator < pFooter->pageCount; pageIterator++)
            {
                const BBFPage* pPage = bbfReader.getPageEntryView((uint8_t*)pageTable, pageIterator);
                const BBFAsset* pAsset = bbfReader.getAssetEntryView((uint8_t*)assetTable, pPage->assetIndex);

                XXH128_hash_t assetHash = bbfReader.computeAssetHash(pAsset);
                if (assetHash.low64 == pAsset->assetHash[0] && assetHash.high64 == pAsset->assetHash[1])
                {
                    printf("[BBFMUX] [%llu | OK] Hashes Match (%llx%llx)\n", (long long unsigned int)pageIterator, (long long unsigned int)assetHash.high64, (long long unsigned int)assetHash.low64);
                }
                else
                {
                    printf("[BBFMUX] [%llu | FAIL] Hash Mismatch (Asset: %llu).\nComputed Hash: %llx%llx\nAsset Hash: %llx%llx\n", (long long unsigned int)pageIterator, (long long unsigned int)pPage->assetIndex, (long long unsigned int)assetHash.high64, (long long unsigned int)assetHash.low64, (long long unsigned int)pAsset->assetHash[1], (long long unsigned int)pAsset->assetHash[0]);
                }

                pPage = nullptr;
                pAsset = nullptr;
            }
            printf("[BBFMUX] Finished Verifying Hashes\n");
            // Free
            assetTable = nullptr;
            pageTable = nullptr;
            pFooter = nullptr;
            pHeader = nullptr;
            return 0;
        }
    }

    if (cfg.mode == Config::EXTRACT)
    {
        printf("Asset index: %llu\n", (unsigned long long)cfg.extract.assetIndex);
        // open .bbf file
        BBFReader bbfReader(cfg.bbfFolder);

        BBFHeader* pHeader = bbfReader.getHeaderView();
        BBFFooter* pFooter = bbfReader.getFooterView(pHeader->footerOffset);

        const uint8_t* assetTable = bbfReader.getAssetTableView(pFooter->assetOffset);
        const uint8_t* pageTable = bbfReader.getPageTableView(pFooter->pageOffset); 

        if (cfg.extract.hashOut)
        {
            // Open file for reading
            FILE* hFile = fopen(cfg.extract.hashOut, "w");

            if (!hFile)
            {
                printf("[BBFMUX] Unable to open file: %s", cfg.extract.hashOut);
                return 1;
            }

            if (!assetTable)
            {
                printf("[BBFMUX] Unable to read asset table.\n");
            }

            fprintf(hFile, "=== ASSET TABLE (%lu entries) ===\n", pFooter->assetCount);
            fprintf(hFile, "ID  | Hash (XXH3-128)                  | Offset      | Size     | Type\n");
            fprintf(hFile, "----|----------------------------------|-------------|----------|-----\n");
    
            uint64_t assetIterator = 0;
            for(; assetIterator < pFooter->assetCount; assetIterator++)
            {
                const BBFAsset* asset = bbfReader.getAssetEntryView((uint8_t*)assetTable, assetIterator);

                if (!asset)
                {
                    printf("[BBFMUX] Unable to read asset %lu\n", assetIterator);
                    assetTable = nullptr;
                    pFooter = nullptr;
                    return 1;
                }

                fprintf(hFile, "%3" PRIu64 " | %016" PRIx64 "%016" PRIx64 " | %11" PRIu64 " | %8" PRIu64 " | 0x%02X\n", 
                    assetIterator, 
                    asset->assetHash[1],
                    asset->assetHash[0],
                    asset->fileOffset, 
                    asset->fileSize, 
                    asset->type
                );

                asset = nullptr;
            }
            fclose(hFile);
        }

        if (cfg.extract.metaOut)
        {
            // Same Procedure but with metadata.
            FILE* mFile = fopen(cfg.extract.metaOut, "w");

            if (!mFile)
            {
                printf("[BBFMUX] Unable to open file: %s", cfg.extract.metaOut);
                return 1;
            }

            fprintf(mFile, "=== Metadata ===\n");
            const uint8_t* metaTable = bbfReader.getMetadataView(pFooter->metaOffset);
            
            uint64_t metadataIterator = 0;
            for (; metadataIterator < pFooter->metaCount; metadataIterator++)
            {
                const BBFMeta* metadata = bbfReader.getMetaEntryView((uint8_t*)metaTable, metadataIterator);
                
                if (!metadata)
                {
                    printf("[BBFMUX] Unable to read metadata.\n");
                    metadata = nullptr;
                    metaTable = nullptr;
                    pFooter = nullptr;
                    pHeader = nullptr;
                    return 1;
                }

                const char* keyPtr = bbfReader.getStringView(metadata->keyOffset);
                const char* valPtr = bbfReader.getStringView(metadata->valueOffset);

                const char* safeKey = keyPtr ? keyPtr : "<CORRUPT KEY>";
                const char* safeVal = valPtr ? valPtr : "<CORRUPT VALUE>";


                fprintf(mFile,"%s : %s\n", safeKey, safeVal);

                if (metadata->parentOffset != 0xFFFFFFFFFFFFFFFF)
                {
                    const char* parentPtr = bbfReader.getStringView(metadata->parentOffset);
                    fprintf(mFile,"     (Parent Key: %s)\n", parentPtr ? parentPtr : "<INVALID>");
                    parentPtr = nullptr;
                }

                metadata = nullptr;
                keyPtr = nullptr;
                valPtr = nullptr;
                safeKey = nullptr;
                safeVal = nullptr;
            }

            metaTable = nullptr;
            fclose(mFile);
        }

        if (cfg.extract.assetIndex != 0xFFFFFFFFFFFFFFFF)
        {

            if (cfg.extract.assetIndex >= pFooter->assetCount)
            {
                printf("[BBFMUX] Asset index out of bounds.\n");
                return 1;
            }

            // Extract single asset
            const BBFAsset* pAsset = bbfReader.getAssetEntryView((uint8_t*)assetTable, cfg.extract.assetIndex);
            if (!pAsset) 
            {
                printf("[BBFMUX] Could not read asset data.\n");
                return 1;
            }

            // Get media type, create filename
            const char* mediaType = getMediaType((uint8_t)pAsset->type);
            char filePath[1024];

            if (cfg.extract.outdir) 
            {
                // Determine separator
                #ifdef WIN32
                char sep = '\\';
                #else
                char sep = '/';
                #endif
                
                snprintf(filePath, sizeof(filePath), "%s%cpage_%" PRIu64 "%s", cfg.extract.outdir, sep, cfg.extract.assetIndex, mediaType);
            }
            else 
            {
                snprintf(filePath, sizeof(filePath), "page_%" PRIu64 "%s", 
                    cfg.extract.assetIndex, mediaType);
            }

            printf("[BBFMUX] Extracting asset %" PRIu64 " to %s\n", cfg.extract.assetIndex, filePath);
            
            FILE* aFile = fopen(filePath, "wb");
            if (aFile) 
            {
                fwrite(bbfReader.getAssetDataView(pAsset->fileOffset), 1, pAsset->fileSize, aFile);
                fclose(aFile);
            } 
            else 
            {
                printf("[BBFMUX] Failed to write file: %s\n", filePath);
            }
        }
        else
        {
            uint64_t pages = pFooter->pageCount;
            uint64_t pageIteratork = 0;
            for (; pageIteratork < pages; pageIteratork++)
            {
                const BBFAsset* pAsset = bbfReader.getAssetEntryView((uint8_t*)assetTable, pageIteratork);
                if (!pAsset) 
                {
                    printf("[BBFMUX] Could not read asset data.\n");
                    return 1;
                }

                // Get media type, create filename
                const char* mediaType = getMediaType((uint8_t)pAsset->type);
                char filePath[1024];

                if (cfg.extract.outdir) 
                {
                    // Determine separator
                    #ifdef WIN32
                    char sep = '\\';
                    #else
                    char sep = '/';
                    #endif
                    
                    snprintf(filePath, sizeof(filePath), "%s%cpage_%" PRIu64 "%s", cfg.extract.outdir, sep, pageIteratork, mediaType);
                }
                else 
                {
                    snprintf(filePath, sizeof(filePath), "page_%" PRIu64 "%s", 
                        pageIteratork, mediaType);
                }

                printf("[BBFMUX] Extracting asset %" PRIu64 " to %s\n", pageIteratork, filePath);
                
                FILE* aFile = fopen(filePath, "wb");
                if (aFile) 
                {
                    fwrite(bbfReader.getAssetDataView(pAsset->fileOffset), 1, pAsset->fileSize, aFile);
                    fclose(aFile);
                } 
                else 
                {
                    printf("[BBFMUX] Failed to write file: %s\n", filePath);
                }
            }
        }

        if (cfg.extract.sectionName && cfg.extract.rangeKey)
        {
            // Extract using a rangekey.
            const uint8_t* sectionTable = bbfReader.getSectionTableView(pFooter->sectionOffset);

            BBFSection* tSection = nullptr;
            uint64_t startPage = 0;
            uint64_t endPage = pFooter->pageCount;

            uint64_t yIterator = 0;
            for (; yIterator < pFooter->sectionCount; yIterator++) 
            {
                const BBFSection* sec = bbfReader.getSectionEntryView((uint8_t*)sectionTable, yIterator);
                const char* name = bbfReader.getStringView(sec->sectionTitleOffset);

                if (name && strstr(name, cfg.extract.sectionName) == 0)
                {
                    uint64_t kIterator = yIterator + 1;
                    for (; kIterator < pFooter->sectionCount; kIterator++)
                    {
                        const BBFSection* nextSec = bbfReader.getSectionEntryView((uint8_t*)sectionTable, kIterator);
                        bool isChild = false;

                        if (nextSec->sectionParentOffset != 0xFFFFFFFFFFFFFFFF)
                        {
                            const char* pName = bbfReader.getStringView(nextSec->sectionParentOffset);
                            if (pName && strcmp(pName, cfg.extract.sectionName) == 0) isChild = true;
                        }
                        if (!isChild)
                        {
                            endPage = nextSec->sectionStartIndex;
                            break;
                        }
                    }
                    break;
                }
            }

            if (!tSection) 
            {
                printf("[BBFMUX] Section '%s' not found.\n", cfg.extract.sectionName);
                return 1;
            }

            printf("[BBFMUX] Extracting Section '%s' (Pages %" PRIu64 " - %" PRIu64 ")\n", cfg.extract.sectionName, startPage, endPage - 1);

            uint64_t pageIterator = startPage;
            for (; pageIterator < endPage; pageIterator++)
            {
                const BBFPage* pPage = bbfReader.getPageEntryView((uint8_t*)pageTable, pageIterator);
                const BBFAsset* pAsset = bbfReader.getAssetEntryView((uint8_t*)assetTable, pPage->assetIndex);

                if (!pAsset) continue;

                const char* mediaType = getMediaType(pAsset->type);
                char filePath[1024];

                if (cfg.extract.outdir)
                {
                    #ifdef WIN32
                    char sep = '\\';
                    #else
                    char sep = '/';
                    #endif

                    snprintf(filePath, sizeof(filePath), "%s%cpage_%" PRIu64 "%s", cfg.extract.outdir, sep, pageIterator, mediaType);
                }
                else
                {
                    snprintf(filePath, sizeof(filePath), "page_%" PRIu64 "%s", pageIterator, mediaType);
                }

                FILE* fileAsset = fopen(filePath, "wb");
                if (fileAsset)
                {
                    fwrite(bbfReader.getAssetDataView(pAsset->fileOffset), 1, pAsset->fileSize, fileAsset);
                    fclose(fileAsset);
                }
                else
                {
                    printf("Failed to write %s\n", filePath);
                }
            }
        }

        if (cfg.extract.sectionName && !cfg.extract.rangeKey)
        {
            // Show error message
            printf("[BBFMUX] Section Extraction Requires a rangekey.\n");
            return 1;
        }

        return 0;
    }
    
    return 0;
}