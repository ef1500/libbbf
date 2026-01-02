#define NOMINMAX

#include "libbbf.h"
#include "xxhash.h"
#include <iostream>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
#include <algorithm>
#include <iomanip>
#include <fstream>
#include <future>
#include <system_error>
#include <cstring>

#include <mutex>
#include <thread>

// I HATE WINDOWS (but alas, i'll work with it.)
// kept getting issues with doing utf-8 stuff in the terminal so I added this little thing.
#ifdef _WIN32
#include <windows.h>

std::string UTF16toUTF8(const std::wstring &wstr)
{
    if (wstr.empty())
        return std::string();
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}
#else
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#endif

// Helper Structs
struct PagePlan
{
    std::string path;
    std::string filename;
    int order = 0; // 0 = unspecified, >0 = start, <0 = end
};

struct SectionPlan
{
    std::string name;
    std::string parentName;
    std::string targetFilename;   // Used for filename-based placement
    uint32_t targetPageIndex = 0; // Used for index-based placement
};

struct SecReq
{
    std::string name;
    std::string target; // This replaces the "page" uint32 for parsing
    std::string parent;
    bool isFilename = false;
};

struct MetaReq
{
    std::string k, v;
};

namespace fs = std::filesystem;

struct MemoryMappedFile
{
    void *data = nullptr;
    size_t size = 0;
#ifdef _WIN32
    HANDLE hFile = INVALID_HANDLE_VALUE;
    HANDLE hMap = NULL;
#else
    int fd = -1;
#endif

    bool map(const std::string &path)
    {
#ifdef _WIN32
        hFile = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE)
            return false;
        LARGE_INTEGER li;
        GetFileSizeEx(hFile, &li);
        size = (size_t)li.QuadPart;
        hMap = CreateFileMapping(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
        if (!hMap)
            return false;
        data = MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
#else
        fd = open(path.c_str(), O_RDONLY);
        if (fd < 0)
            return false;
        struct stat st;
        fstat(fd, &st);
        size = st.st_size;
        data = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
#endif
        return data != nullptr;
    }

    ~MemoryMappedFile()
    {
#ifdef _WIN32
        if (data)
            UnmapViewOfFile(data);
        if (hMap)
            CloseHandle(hMap);
        if (hFile != INVALID_HANDLE_VALUE)
            CloseHandle(hFile);
#else
        if (data)
            munmap(data, size);
        if (fd >= 0)
            close(fd);
#endif
    }
};

class BBFReader
{
public:
    BBFFooter footer;
    BBFHeader header;
    MemoryMappedFile mmap;

    bool open(const std::string &path)
    {
        if (!mmap.map(path))
            return false;
        
        // Basic size check
        if (mmap.size < sizeof(BBFHeader) + sizeof(BBFFooter))
            return false;

        // Read the fixed-size part of the header first
        std::memcpy(&header, mmap.data, sizeof(BBFHeader));
        
        if (std::memcmp(header.magic, "BBF1", 4) != 0)
            return false;

        // FUTURE PROOFING: 
        // If header.headerLen > sizeof(BBFHeader), we know there is extra data 
        // in the header we should ignore. We don't need to do anything right now 
        // because we read assets via absolute offsets, but it's good to know.

        // Read Footer
        std::memcpy(&footer, (uint8_t *)mmap.data + mmap.size - sizeof(BBFFooter), sizeof(BBFFooter));
        if (std::memcmp(footer.magic, "BBF1", 4) != 0)
            return false;

        return true;
    }

    // Optimization: Return string_view to avoid allocation/copy
    std::string_view getString(uint32_t offset) const
    {
        const char *poolStart = (const char *)mmap.data + footer.stringPoolOffset;
        size_t poolSize = footer.assetTableOffset - footer.stringPoolOffset;
        if (offset >= poolSize)
            return "OFFSET_ERR";
        return std::string_view(poolStart + offset);
    }

    // Optimized: Provide direct pointer access
    const BBFAssetEntry *getAssetsPtr() const
    {
        return reinterpret_cast<const BBFAssetEntry *>((const uint8_t *)mmap.data + footer.assetTableOffset);
    }

    const BBFPageEntry *getPagesPtr() const
    {
        return reinterpret_cast<const BBFPageEntry *>((const uint8_t *)mmap.data + footer.pageTableOffset);
    }

    const BBFSection *getSectionsPtr() const
    {
        return reinterpret_cast<const BBFSection *>((const uint8_t *)mmap.data + footer.sectionTableOffset);
    }

    const BBFMetadata *getMetaPtr() const
    {
        return reinterpret_cast<const BBFMetadata *>((const uint8_t *)mmap.data + footer.metaTableOffset);
    }
};

bool verifyAssetsParallel(const BBFReader &reader, int targetIndex)
{
    auto assets = reader.getAssetsPtr();
    size_t count = reader.footer.assetCount;

    // Directory Hash Check (Extremely fast via mmap)
    size_t metaStart = reader.footer.stringPoolOffset;
    size_t metaSize = reader.mmap.size - sizeof(BBFFooter) - metaStart;
    uint64_t calcIndexHash = XXH3_64bits((const uint8_t *)reader.mmap.data + metaStart, metaSize);

    if (targetIndex == -1)
    {
        bool ok = (calcIndexHash == reader.footer.indexHash);
        std::cout << "Directory Hash: " << (ok ? "OK" : "CORRUPT") << "\n";
        return ok;
    }

    std::cout << "Verifying integrity using XXH3 (Parallel)...\n";
    bool dirOk = (calcIndexHash == reader.footer.indexHash);
    if (!dirOk)
        std::cerr << " [!!] Directory Hash CORRUPT (" << "Wanted: " << reader.footer.indexHash << " Got: " << calcIndexHash << ")" << std::endl;

    // Batching for parallelism
    auto verifyRange = [&](size_t start, size_t end) -> bool
    {
        bool allOk = true;
        for (size_t i = start; i < end; ++i)
        {
            const auto &a = assets[i];
            uint64_t h = XXH3_64bits((const uint8_t *)reader.mmap.data + a.offset, a.length);
            if (h != a.xxh3Hash)
            {
                // Thread-safe-ish output for errors
                static std::mutex mtx;
                std::lock_guard<std::mutex> lock(mtx);
                std::cerr << " [!!] Asset " << i << " CORRUPT\n";
                allOk = false;
            }
        }
        return allOk;
    };

    if (targetIndex >= 0)
        return verifyRange(targetIndex, targetIndex + 1);

    // Split work across hardware threads
    size_t numThreads = std::thread::hardware_concurrency();
    size_t chunkSize = count / numThreads;
    std::vector<std::future<bool>> futures;

    for (size_t i = 0; i < numThreads; ++i)
    {
        size_t start = i * chunkSize;
        size_t end = (i == numThreads - 1) ? count : start + chunkSize;
        futures.push_back(std::async(std::launch::async, verifyRange, start, end));
    }

    bool allAssetsOk = dirOk;
    for (auto &f : futures)
        if (!f.get())
            allAssetsOk = false;

    if (allAssetsOk)
        std::cout << "All integrity checks passed.\n";
    return allAssetsOk;
}

void printHelp()
{
    std::cout << "Bound Book Format Muxer (bbfmux) - Developed by EF1500                 \n"
                 "-----------------------------------------------------------------------\n"
                 "Usage:\n"
                 "  Muxing:     bbfmux <inputs...> [options] <output.bbf>\n"
                 "  Info:       bbfmux <file.bbf> --info\n"
                 "  Verify:     bbfmux <file.bbf> --verify [assetindex]\n"
                 "  Extract:    bbfmux <file.bbf> --extract [options]\n"
                 "\n"
                 "Inputs:\n"
                 "  Can be individual image files (.png, .avif) or directories.\n"
                 "  By default, files are sorted alphabetically. Data is 4KB aligned.\n"
                 "\n"
                 "Muxing Options:\n"
                 "  --order=path.txt              Use a text file to define page order.\n"
                 "                                Format: filename:index (e.g. cover.png:1)\n"
                 "                                Supports -1 for the very last page.\n"
                 "  --sections=path.txt           Use a text file to define multiple sections.\n"
                 "                                Format: Name:Target[:Parent]\n"
                 "  --section=Name:Target[:Parent] Add a single section marker.\n"
                 "                                Target can be a page index (1-based)\n"
                 "                                or a filename (e.g. Chapter 1:001.png).\n"
                 "  --meta=Key:Value              Add archival metadata (Title, Author, etc.).\n"
                 "\n"
                 "Extraction Options:\n"
                 "  --outdir=path                 Output directory (default: ./extracted).\n"
                 "  --section=\"Name\"              Extract only a specific section.\n"
                 "  --rangekey=\"String\"           Find the end of an extraction by matching\n"
                 "                                this string against the next section title.\n"
                 "\n"
                 "Global Options:\n"
                 "  --info                        Display book structure and metadata.\n"
                 "  --verify                      Perform XXH3 integrity check on all assets.\n"
                 "\n"
                 "Examples:\n"
                 "  [Advanced Muxing]\n"
                 "    bbfmux ./pages/ --order=pages.txt --sections=struct.txt out.bbf\n"
                 "    bbfmux ./pages/ --section=\"Cover\":\"cover.png\" --meta=Title:\"Akira\"\n"
                 "\n"
                 "  [Range Extraction]\n"
                 "    bbfmux manga.bbf --extract --section=\"Vol 1\" --rangekey=\"Vol 2\"\n"
                 "\n"
                 "  [Custom Page Orders (pages.txt)]\n"
                 "    cover.png:1\n"
                 "    credits.png:-1\n"
                 "    page1.png:2\n"
              << std::endl;
}

std::string trimQuotes(const std::string &s)
{
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
    {
        return s.substr(1, s.size() - 2);
    }
    return s;
}

// Custom sort: Positives (1, 2...) -> Zeros (Alphabetical) -> Negatives (-2, -1)
bool comparePages(const PagePlan &a, const PagePlan &b)
{
    // If both have explicit positive orders
    if (a.order > 0 && b.order > 0)
        return a.order < b.order;
    // Positive orders always come before unspecified/negatives
    if (a.order > 0 && b.order <= 0)
        return true;
    if (b.order > 0 && a.order <= 0)
        return false;

    // If both are unspecified (order == 0), sort alphabetically
    if (a.order == 0 && b.order == 0)
        return a.filename < b.filename;
    // Unspecified comes before negative
    if (a.order == 0 && b.order < 0)
        return true;
    if (b.order == 0 && a.order < 0)
        return false;

    // Both are negative: -2 comes before -1
    return a.order < b.order;
}

// Improved range-key search for extraction
uint32_t findSectionEnd(const BBFSection *sections, const BBFReader &reader, size_t currentIdx, const std::string &rangeKey)
{
    uint32_t startPage = sections[currentIdx].sectionStartIndex;

    for (size_t j = currentIdx + 1; j < (size_t)reader.footer.sectionCount; ++j)
    {
        std::string_view title = reader.getString(sections[j].sectionTitleOffset);

        if (rangeKey.empty())
        {
            if (sections[j].sectionStartIndex > startPage)
                return sections[j].sectionStartIndex;
        }
        else if (title.find(rangeKey) != std::string_view::npos)
        {
            return sections[j].sectionStartIndex;
        }
    }
    return (uint32_t)reader.footer.pageCount;
}

// I had to look up how to solve this problem.
#ifdef _WIN32
int wmain(int argc, wchar_t *argv[])
{
    // Set console output to UTF-8 so std::cout works with Korean/Japanese/etc.
    SetConsoleOutputCP(CP_UTF8);
    setvbuf(stdout, nullptr, _IOFBF, 1000); // Buffer fix for some terminals

    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i)
    {
        args.push_back(UTF16toUTF8(argv[i]));
    }
#else
int main(int argc, char *argv[])
{
    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i)
    {
        args.push_back(argv[i]);
    }
#endif
    if (args.size() < 2)
    {
        printHelp(); // Print help if insufficent arguments are provided
        return 1;    // Return lol
    }

    std::vector<std::string> inputs; // Create a vector for the inputs
    std::string outputBbf;           // create a string for the output

    // Create booleans for the possible operating modes
    bool modeInfo = false, modeVerify = false, modeExtract = false;
    std::string outDir = "./extracted";
    std::string targetSection = ""; // target section to export

    std::vector<SecReq> secReqs;
    std::vector<MetaReq> metaReqs;

    // Parse all of the arguments
    std::string rangeKey = "";
    std::string orderFilePath = "";
    std::string sectionsFilePath = "";
    int targetVerifyIndex = -2;

    for (size_t i = 1; i < args.size(); ++i)
    {
        std::string arg = args[i];

        if (arg == "--info")
            modeInfo = true;
        else if (arg == "--verify")
        {
            modeVerify = true;
            // Check if there's an optional index argument following --verify
            if (i + 1 < args.size())
            {
                std::string next = args[i + 1];
                // Check if the next string looks like a number (allowing negative sign)
                bool isNum = !next.empty() && std::all_of(next.begin(), next.end(), [](char c)
                                                          { return std::isdigit(c) || c == '-'; });

                if (isNum)
                {
                    targetVerifyIndex = std::stoi(next);
                    i++; // Consume the index argument
                }
            }
        }
        else if (arg == "--extract")
            modeExtract = true;
        else if (arg.find("--outdir=") == 0)
            outDir = trimQuotes(arg.substr(9));
        else if (arg.find("--rangekey=") == 0)
            rangeKey = trimQuotes(arg.substr(11));
        else if (arg.find("--order=") == 0)
            orderFilePath = trimQuotes(arg.substr(8));
        else if (arg.find("--sections=") == 0)
            sectionsFilePath = trimQuotes(arg.substr(11));
        else if (arg.find("--section=") == 0)
        {
            std::string val = arg.substr(10);
            std::vector<std::string> parts;
            size_t start = 0, end = 0;
            while ((end = val.find(':', start)) != std::string::npos)
            {
                parts.push_back(val.substr(start, end - start));
                start = end + 1;
            }
            parts.push_back(val.substr(start));

            if (modeExtract)
            {
                targetSection = trimQuotes(parts[0]);
            }
            else if (parts.size() >= 2)
            {
                SecReq sr;
                sr.name = trimQuotes(parts[0]);
                sr.target = trimQuotes(parts[1]);
                if (parts.size() >= 3)
                    sr.parent = trimQuotes(parts[2]);

                // Determine if target is a number or filename
                sr.isFilename = !std::all_of(sr.target.begin(), sr.target.end(), ::isdigit);
                secReqs.push_back(sr);
            }
        }
        else if (arg.find("--meta=") == 0)
        {
            std::string val = arg.substr(7);
            size_t colon = val.find(':');
            if (colon != std::string::npos)
                metaReqs.push_back({trimQuotes(val.substr(0, colon)), trimQuotes(val.substr(colon + 1))});
        }
        else
        {
            inputs.push_back(arg);
        }
    }
    // Perform actions
    if (modeInfo || modeVerify || modeExtract)
    {
        // If no inputs given, throw a fit
        if (inputs.empty())
        {
            std::cerr << "Error: No .bbf input specified.\n";
            return 1;
        }

        // Create a reader
        BBFReader reader;
        if (!reader.open(inputs[0]))
        {
            std::cerr << "Error: Failed to open BBF.\n";
            return 1;
        }

        if (modeInfo)
        {
            std::cout << "Bound Book Format (.bbf) Info\n";
            std::cout << "------------------------------\n";
            std::cout << "BBF Version: " << (int)reader.header.version << "\n";
            std::cout << "Pages:       " << reader.footer.pageCount << "\n";
            std::cout << "Assets:      " << reader.footer.assetCount << " (Deduplicated)\n";

            // Print Sections
            std::cout << "\n[Sections]\n";
            auto sections = reader.getSectionsPtr();
            if (reader.footer.sectionCount == 0)
            {
                std::cout << " No sections defined.\n";
            }
            else
            {
                for (uint32_t i = 0; i < reader.footer.sectionCount; ++i)
                {
                    std::cout << " - " << std::left << std::setw(20)
                              << reader.getString(sections[i].sectionTitleOffset)
                              << " (Starting Page: " << sections[i].sectionStartIndex + 1 << ")\n";
                }
            }

            // Print Metadata
            std::cout << "\n[Metadata]\n";
            auto metadata = reader.getMetaPtr();
            if (reader.footer.keyCount == 0)
            {
                std::cout << " No metadata found.\n";
            }
            else
            {
                for (uint32_t i = 0; i < reader.footer.keyCount; ++i)
                {
                    std::string_view key = reader.getString(metadata[i].keyOffset);
                    std::string_view val = reader.getString(metadata[i].valOffset);
                    std::cout << " - " << std::left << std::setw(15) << std::string(key) + ":" << val << "\n";
                }
            }
            std::cout << std::endl;
        }

        if (modeVerify)
        {
            if (!verifyAssetsParallel(reader, targetVerifyIndex))
                return 1;
        }

        if (modeExtract)
        {
            fs::create_directories(outDir);
            auto pages = reader.getPagesPtr();
            auto assets = reader.getAssetsPtr();
            auto sections = reader.getSectionsPtr(); // FIX: Added this

            uint32_t start = 0, end = (uint32_t)reader.footer.pageCount; // FIX: use footer count
            if (!targetSection.empty())
            {
                bool found = false;
                for (uint32_t i = 0; i < reader.footer.sectionCount; ++i)
                {
                    if (reader.getString(sections[i].sectionTitleOffset) == targetSection)
                    {
                        start = sections[i].sectionStartIndex;
                        end = findSectionEnd(sections, reader, i, rangeKey);
                        found = true;
                        break;
                    }
                }
                if (!found)
                {
                    std::cerr << "Section '" << targetSection << "' not found.\n";
                    return 1;
                }
            }

            std::cout << "Extracting: " << (targetSection.empty() ? "Full Book" : targetSection)
                      << " (Pages " << (start + 1) << " to " << end << ")\n";

            for (uint32_t i = start; i < end; ++i)
            {
                const auto &asset = assets[pages[i].assetIndex];

                // FIX: Use the library function to get the extension
                // This automatically handles PNG, JPG, AVIF, JXL, etc.
                std::string ext = MediaTypeToStr(asset.type);

                std::string outPath = (fs::path(outDir) / ("p" + std::to_string(i + 1) + ext)).string();

                std::ofstream ofs(outPath, std::ios::binary);
                ofs.write((const char *)reader.mmap.data + asset.offset, asset.length);
            }
            std::cout << "Done.\n";
        }
    }
    else
    {
        if (inputs.size() < 1)
        {
            std::cerr << "Error: Provide inputs and an output filename.\n";
            return 1;
        }
        outputBbf = inputs.back();
        inputs.pop_back();

        std::vector<PagePlan> manifest;
        std::unordered_map<std::string, int> orderMap;

        // Parse Order File if provided
        if (!orderFilePath.empty())
        {
            std::ifstream ifs(orderFilePath);
            std::string line;
            while (std::getline(ifs, line))
            {
                if (line.empty())
                    continue;
                size_t colon = line.find_last_of(':');
                if (colon != std::string::npos)
                {
                    std::string fname = trimQuotes(line.substr(0, colon));
                    int orderVal = std::stoi(line.substr(colon + 1));
                    orderMap[fname] = orderVal;
                }
                else
                {
                    orderMap[trimQuotes(line)] = 0; // Unspecified but listed
                }
            }
        }

        // Collect all files
        for (const auto &path : inputs)
        {
            if (fs::is_directory(path))
            {
                for (const auto &entry : fs::directory_iterator(path))
                {
                    PagePlan p;
                    p.path = entry.path().string();
                    p.filename = entry.path().filename().string();
                    if (orderMap.count(p.filename))
                        p.order = orderMap[p.filename];
                    manifest.push_back(p);
                }
            }
            else
            {
                PagePlan p;
                p.path = path;
                p.filename = fs::path(path).filename().string();
                if (orderMap.count(p.filename))
                    p.order = orderMap[p.filename];
                manifest.push_back(p);
            }
        }

        if (!sectionsFilePath.empty())
        {
            std::ifstream ifs(sectionsFilePath);
            std::string line;
            while (std::getline(ifs, line))
            {
                if (line.empty())
                    continue;
                size_t colon = line.find(':');
                if (colon != std::string::npos)
                {
                    SecReq sr;
                    sr.name = trimQuotes(line.substr(0, colon));
                    std::string rest = line.substr(colon + 1);
                    size_t pColon = rest.find(':');
                    if (pColon != std::string::npos)
                    {
                        sr.target = trimQuotes(rest.substr(0, pColon));
                        sr.parent = trimQuotes(rest.substr(pColon + 1));
                    }
                    else
                    {
                        sr.target = trimQuotes(rest);
                    }
                    sr.isFilename = !std::all_of(sr.target.begin(), sr.target.end(), ::isdigit);
                    secReqs.push_back(sr);
                }
            }
        }

        // Sort Manifest
        std::stable_sort(manifest.begin(), manifest.end(), comparePages);

        // Build the file
        BBFBuilder builder(outputBbf);
        std::unordered_map<std::string, uint32_t> fileToPage;

        // Add Pages
        for (uint32_t i = 0; i < manifest.size(); ++i)
        {
            std::string ext = fs::path(manifest[i].path).extension().string();
            BBFMediaType mediaType = detectTypeFromExtension(ext);
            uint8_t type = static_cast<uint8_t>(mediaType);
            builder.addPage(manifest[i].path, type);
            fileToPage[manifest[i].filename] = i;
        }

        // Add Sections (Resolving names to indices)
        std::unordered_map<std::string, uint32_t> sectionNameToIdx;
        for (uint32_t i = 0; i < secReqs.size(); ++i)
        {
            auto &s = secReqs[i];
            uint32_t pageIndex = 0;

            if (s.isFilename)
            {
                if (fileToPage.count(s.target))
                {
                    pageIndex = fileToPage[s.target];
                }
                else
                {
                    std::cerr << "Warning: Section target file '" << s.target << "' not found. Defaulting to page 1.\n";
                }
            }
            else
            {
                // If it's a number, convert it. 1-based to 0-based.
                try
                {
                    pageIndex = (uint32_t)std::max(0, std::stoi(s.target) - 1);
                }
                catch (...)
                {
                    pageIndex = 0;
                }
            }

            uint32_t parentIdx = 0xFFFFFFFF; // Default: No parent
            if (!s.parent.empty() && sectionNameToIdx.count(s.parent))
            {
                parentIdx = sectionNameToIdx[s.parent];
            }

            builder.addSection(s.name, pageIndex, parentIdx);
            sectionNameToIdx[s.name] = i; // Map name to the internal section index
        }

        for (auto &m : metaReqs)
        {
            // Use trimQuotes to ensure metadata keys/values don't have stray " characters
            builder.addMetadata(trimQuotes(m.k), trimQuotes(m.v));
        }

        if (builder.finalize())
        {
            std::cout << "Successfully created " << outputBbf << " (" << manifest.size() << " pages)\n";
        }
    } // End of Muxer Else Block

    return 0;
} // End of Main/Wmain