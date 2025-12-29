#include "libbbf.h"
#include "xxhash.h"
#include <iostream>
#include <filesystem>
#include <string>
#include <vector>
#include <algorithm>
#include <iomanip>

namespace fs = std::filesystem;

class BBFReader {
public:
    BBFFooter footer;
    BBFHeader header;   // Added to store header info
    std::ifstream stream;
    std::vector<char> stringPool;

    bool open(const std::string& path) {
        stream.open(path, std::ios::binary | std::ios::ate);
        if (!stream.is_open()) return false;

        size_t fileSize = stream.tellg();

        // read header
        stream.seekg(0, std::ios::beg);
        stream.read(reinterpret_cast<char*>(&header), sizeof(BBFHeader));

        // validate header
        if (std::string((char*)header.magic, 4) != "BBF1") return false;

        // read footer
        stream.seekg(fileSize - sizeof(BBFFooter));
        stream.read(reinterpret_cast<char*>(&footer), sizeof(BBFFooter));

        if (std::string((char*)footer.magic, 4) != "BBF1") return false;

        // Load string pool
        stringPool.resize(footer.assetTableOffset - footer.stringPoolOffset);
        stream.seekg(footer.stringPoolOffset);
        stream.read(stringPool.data(), stringPool.size());
        return true;
    }

    std::string getString(uint32_t offset) {
        if (offset >= stringPool.size()) return "OFFSET_ERR";
        return std::string(stringPool.data() + offset);
    }

    std::vector<BBFAssetEntry> getAssets() {
        std::vector<BBFAssetEntry> assets(footer.assetCount);
        stream.seekg(footer.assetTableOffset);
        stream.read(reinterpret_cast<char*>(assets.data()), footer.assetCount * sizeof(BBFAssetEntry));
        return assets;
    }

    std::vector<BBFPageEntry> getPages() {
        std::vector<BBFPageEntry> pages(footer.pageCount);
        stream.seekg(footer.pageTableOffset);
        stream.read(reinterpret_cast<char*>(pages.data()), footer.pageCount * sizeof(BBFPageEntry));
        return pages;
    }

    std::vector<BBFSection> getSections() {
        std::vector<BBFSection> sections(footer.sectionCount);
        stream.seekg(footer.sectionTableOffset);
        stream.read(reinterpret_cast<char*>(sections.data()), footer.sectionCount * sizeof(BBFSection));
        return sections;
    }

    std::vector<BBFMetadata> getMetadata() {
        std::vector<BBFMetadata> meta(footer.keyCount);
        if (footer.keyCount > 0) {
            stream.seekg(footer.metaTableOffset);
            stream.read(reinterpret_cast<char*>(meta.data()), footer.keyCount * sizeof(BBFMetadata));
        }
        return meta;
    }
};



void printHelp() {
    std::cout << "Bound Book Format Muxer (bbfmux)\n\n"
              << "Usage (Creation):\n"
              << "  bbfmux <inputs...> [options] <output.bbf>\n"
              << "  Inputs can be individual images or directories.\n\n"
              << "Options:\n"
              << "  --section=\"Name\":PageIdx  Add a section marker (1-based index)\n"
              << "  --meta=Key:\"Value\"        Add metadata\n\n"
              << "Usage (Operations):\n"
              << "  bbfmux <input.bbf> --info\n"
              << "  bbfmux <input.bbf> --verify\n"
              << "  bbfmux <input.bbf> --extract [--outdir=path] [--section=\"Name\"]\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) { printHelp(); return 1; }

    std::vector<std::string> inputs;
    std::string outputBbf;
    bool modeInfo = false, modeVerify = false, modeExtract = false;
    std::string outDir = "./extracted";
    std::string targetSection = "";
    
    struct SecReq { std::string name; uint32_t page; };
    struct MetaReq { std::string k, v; };
    std::vector<SecReq> secReqs;
    std::vector<MetaReq> metaReqs;

    // Parse all of the arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--info") modeInfo = true;
        else if (arg == "--verify") modeVerify = true;
        else if (arg == "--extract") modeExtract = true;
        else if (arg.find("--outdir=") == 0) outDir = arg.substr(9);
        else if (arg.find("--section=") == 0) {
            std::string val = arg.substr(10);
            size_t colon = val.find_last_of(':');
            if (colon != std::string::npos && !modeExtract) {
                secReqs.push_back({val.substr(0, colon), (uint32_t)std::stoi(val.substr(colon+1))});
            } else {
                targetSection = val; // For extraction
            }
        }
        else if (arg.find("--meta=") == 0) {
            std::string val = arg.substr(7);
            size_t colon = val.find(':');
            if (colon != std::string::npos) 
                metaReqs.push_back({val.substr(0, colon), val.substr(colon+1)});
        }
        else {
            inputs.push_back(arg);
        }
    }

    // Perform actions
    if (modeInfo || modeVerify || modeExtract) {
        if (inputs.empty()) { std::cerr << "Error: No .bbf input specified.\n"; return 1; }
        BBFReader reader;
        if (!reader.open(inputs[0])) { std::cerr << "Error: Failed to open BBF.\n"; return 1; }

        if (modeInfo) {
            std::cout << "Bound Book Format (.bbf) Info\n";
            std::cout << "------------------------------\n";
            std::cout << "BBF Version: " << (int)reader.header.version << "\n";
            std::cout << "Pages:       " << reader.footer.pageCount << "\n";
            std::cout << "Assets:      " << reader.footer.assetCount << " (Deduplicated)\n";
            
            // Print Sections
            std::cout << "\n[Sections]\n";
            auto sections = reader.getSections();
            if (sections.empty()) {
                std::cout << " No sections defined.\n";
            } else {
                for (auto& s : sections) {
                    std::cout << " - " << std::left << std::setw(20) 
                              << reader.getString(s.sectionTitleOffset) 
                              << " (Starts Page: " << s.sectionStartIndex + 1 << ")\n";
                }
            }

            // Print Metadata
            std::cout << "\n[Metadata]\n";
            auto metadata = reader.getMetadata();
            if (metadata.empty()) {
                std::cout << " No metadata found.\n";
            } else {
                for (auto& m : metadata) {
                    std::string key = reader.getString(m.keyOffset);
                    std::string val = reader.getString(m.valOffset);
                    std::cout << " - " << std::left << std::setw(15) << (key + ":") << val << "\n";
                }
            }
            std::cout << std::endl;
        }

        if (modeVerify) {
            std::cout << "Verifying asset integrity...\n";
            auto assets = reader.getAssets();
            bool clean = true;
            for (size_t i = 0; i < assets.size(); ++i) {
                std::vector<char> buf(assets[i].length);
                reader.stream.seekg(assets[i].offset);
                reader.stream.read(buf.data(), assets[i].length);
                if (XXH3_64bits(buf.data(), buf.size()) != assets[i].xxh3Hash) {
                    std::cerr << "Mismatch in asset " << i << "\n";
                    clean = false;
                }
            }
            if (clean) std::cout << "Integrity Check Passed.\n";
        }

        if (modeExtract) {
            fs::create_directories(outDir);
            auto pages = reader.getPages();
            auto assets = reader.getAssets();
            auto sections = reader.getSections();

            uint32_t start = 0, end = pages.size();
            if (!targetSection.empty()) {
                bool found = false;
                for (size_t i = 0; i < sections.size(); ++i) {
                    if (reader.getString(sections[i].sectionTitleOffset) == targetSection) {
                        start = sections[i].sectionStartIndex;
                        end = (i + 1 < sections.size()) ? sections[i+1].sectionStartIndex : pages.size();
                        found = true; break;
                    }
                }
                if (!found) { std::cerr << "Section not found.\n"; return 1; }
            }

            for (uint32_t i = start; i < end; ++i) {
                auto& asset = assets[pages[i].assetIndex];
                std::string ext = (asset.type == 0x01) ? ".avif" : ".png";
                std::string outPath = outDir + "/page_" + std::to_string(i+1) + ext;
                std::vector<char> buf(asset.length);
                reader.stream.seekg(asset.offset);
                reader.stream.read(buf.data(), asset.length);
                std::ofstream ofs(outPath, std::ios::binary);
                ofs.write(buf.data(), asset.length);
            }
            std::cout << "Extracted " << (end - start) << " pages to " << outDir << "\n";
        }
    } 
    else {
        // CREATE MODE
        if (inputs.size() < 2) { std::cerr << "Error: Provide inputs and an output filename.\n"; return 1; }
        outputBbf = inputs.back();
        inputs.pop_back();

        BBFBuilder builder(outputBbf);
        
        // Gather all image paths
        std::vector<std::string> imagePaths;
        for (const auto& path : inputs) {
            if (fs::is_directory(path)) {
                for (const auto& entry : fs::directory_iterator(path))
                    imagePaths.push_back(entry.path().string());
            } else {
                imagePaths.push_back(path);
            }
        }
        std::sort(imagePaths.begin(), imagePaths.end());

        for (const auto& p : imagePaths) {
            std::string ext = fs::path(p).extension().string();
            uint8_t type = (ext == ".avif" || ext == ".AVIF") ? 1 : 2;
            if (!builder.addPage(p, type)) std::cerr << "Warning: Failed to add " << p << "\n";
        }

        for (auto& s : secReqs) builder.addSection(s.name, s.page - 1);
        for (auto& m : metaReqs) builder.addMetadata(m.k, m.v);

        if (builder.finalize()) std::cout << "Successfully created " << outputBbf << "\n";
    }

    return 0;
}