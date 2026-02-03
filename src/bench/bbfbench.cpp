#include "libbbf.h"
#include "bbfcodec.h"
#include "xxhash.h"
#include "miniz.h"

// Just for creating test data.
#include <string>
#include <vector>
#include <fstream>
#include <random>
#include <algorithm>

#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

const char* OUTPUT = "testBBF.bbf";
const char* PETRIFIEDOUTPUT = "testPETRIFIED.bbf";
const char* ZIP_OUT = "ZIPOUT.zip";

// Create Test Image
void createTestFile(const std::string& filename, size_t size, char fillVal = 'A', const std::string content = "")
{
    std::ofstream oF(filename, std::ios::binary);
    if (!content.empty())
    {
        oF << content;
        if (content.size() < size)
        {
            std::vector<char> padding(size - content.size(), 0);
            oF.write(padding.data(), padding.size());
        }
    }
    else
    {
        std::vector<char> buf(size, fillVal);
        oF.write(buf.data(), size);
    }
    oF.close();
}

// Create file with random stuff in it.
void createRandomFile(const std::string& filename, size_t size) 
{
    std::ofstream oF(filename, std::ios::binary);
    

    static std::mt19937 gen(std::random_device{}());
    std::uniform_int_distribution<uint32_t> dist(0, 255);

    std::vector<char> buffer(65536);
    size_t written = 0;
    while (written < size)
    {
        size_t toWrite = std::min(buffer.size(), size - written);
        size_t iterator;
        for (size_t iterator = 0; iterator < toWrite; ++iterator)
        {
            buffer[iterator] = static_cast<char>(dist(gen));
        }
        oF.write(buffer.data(), toWrite);
        written += toWrite;
    }
    oF.close();
}

void createTestFileContent(const std::string& filename, size_t size, const std::string content = "")
{
    std::ofstream oF(filename, std::ios::binary);
    if (!content.empty())
    {
        oF << content;
        if (content.size() < size)
        {
            std::vector<char> padding(size - content.size(), 0);
            oF.write(padding.data(), padding.size());
        }
    }
    oF.close();
}

void writeZip(const char* zipName, const std::vector<std::string>& files, int level) {
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    if (!mz_zip_writer_init_file(&zip, zipName, 0)) return;

    for (const auto& f : files) 
    {
        mz_zip_writer_add_file(&zip, f.c_str(), f.c_str(), nullptr, 0, (mz_uint)level);
    }

    mz_zip_writer_finalize_archive(&zip);
    mz_zip_writer_end(&zip);
}

void deleteFile(const std::string& filename)
{
    std::remove(filename.c_str());
}

// Test BBF Test Cases
TEST_CASE("BBFBuilder - Constructor")
{
    REQUIRE_NOTHROW(BBFBuilder(OUTPUT));
}

TEST_CASE("BBFBuilder - Add Page (INVALID PATH)")
{
    BBFBuilder bbfBuilder(OUTPUT);
    REQUIRE_FALSE(bbfBuilder.addPage("../../randimgs/call100.txt"));
    REQUIRE(bbfBuilder.getPageCount() == 0);
    REQUIRE(bbfBuilder.getAssetCount() == 0);
}

TEST_CASE("BBFBuilder - Add Page (Unknown Extension)")
{
    BBFBuilder bbfBuilder(OUTPUT);
    createTestFile("testpage.txt", 1024);
    CHECK(bbfBuilder.addPage("testpage.txt"));
    deleteFile("testpage.txt");
    REQUIRE(bbfBuilder.getPageCount() == 1);
    REQUIRE(bbfBuilder.getAssetCount() == 1);
}

TEST_CASE("BBFBuilder - Add Page (Invalid Filename)")
{
    BBFBuilder bbfBuilder("output.bbf");
    REQUIRE_FALSE(bbfBuilder.addPage("notreal.png"));
    REQUIRE(bbfBuilder.getPageCount() == 0);
    REQUIRE(bbfBuilder.getAssetCount() == 0);

}

TEST_CASE("BBFBuilder - Finalize")
{
    BBFBuilder bbfBuilder("output.bbf");
    // Add page
    createTestFile("testImage.png", 2048);
    bbfBuilder.addPage("testImage.png");
    deleteFile("testImage.png");
    REQUIRE(bbfBuilder.finalize());
    REQUIRE(bbfBuilder.getPageCount() == 1);
    REQUIRE(bbfBuilder.getAssetCount() == 1);
}

TEST_CASE("BBFBuilder - Finalize (Unknown Asset Extension)")
{
    BBFBuilder bbfBuilder("output.bbf");
    // Add page
    createTestFile("testImage.txt", 2048);
    bbfBuilder.addPage("testImage.txt");
    deleteFile("testImage.txt");
    REQUIRE(bbfBuilder.finalize());
    REQUIRE(bbfBuilder.getPageCount() == 1);
    REQUIRE(bbfBuilder.getAssetCount() == 1);
}

TEST_CASE("BBFBuilder - Finalize (No Assets/No Pages)")
{
    BBFBuilder bbfBuilder("output.bbf");
    REQUIRE_FALSE(bbfBuilder.finalize());
    REQUIRE(bbfBuilder.getPageCount() == 0);
    REQUIRE(bbfBuilder.getAssetCount() == 0);
}

TEST_CASE("BBFBuilder - Add Section")
{
    BBFBuilder bbfBuilder("output.bbf");
    // Add page
    bbfBuilder.addPage("../../testimgs/Balloons.png");
    REQUIRE(bbfBuilder.addSection("Section", 0));
    REQUIRE(bbfBuilder.getSectionCount() == 1);
}

TEST_CASE("BBFBuilder - Add Section (with Parent)")
{
    BBFBuilder bbfBuilder("output.bbf");
    // Add page
    bbfBuilder.addPage("../../testimgs/Balloons.png");
    REQUIRE(bbfBuilder.addSection("Section", 0, "Parent Label"));
    REQUIRE(bbfBuilder.getSectionCount() == 1);
}

TEST_CASE("BBFBuilder - Add Section (Index out of bounds)")
{
    BBFBuilder bbfBuilder("output.bbf");
    // Add page
    bbfBuilder.addPage("../../testimgs/Balloons.png");
    REQUIRE_FALSE(bbfBuilder.addSection("Section", 1664));
    REQUIRE(bbfBuilder.getSectionCount() == 0);
}

TEST_CASE("BBFBuilder - Add Metadata")
{
    BBFBuilder bbfBuilder("output.bbf");
    REQUIRE(bbfBuilder.addMeta("TestKey", "TestVal"));
    REQUIRE(bbfBuilder.getKeyCount() == 1);
}

TEST_CASE("BBFBuilder - Add Metadata (With Parent)")
{
    BBFBuilder bbfBuilder("output.bbf");
    REQUIRE(bbfBuilder.addMeta("TestKey", "TestVal", "TestParent"));
    REQUIRE(bbfBuilder.getKeyCount() == 1);
}


TEST_CASE("BBFBuilder - Petrify")
{
    BBFBuilder bbfBuilder(OUTPUT);
    // Add page
    createTestFile("testImage.png", 2048);
    bbfBuilder.addPage("testImage.png");
    deleteFile("testImage.png");
    CHECK(bbfBuilder.finalize());
    REQUIRE(bbfBuilder.getPageCount() == 1);
    REQUIRE(bbfBuilder.getAssetCount() == 1);

    REQUIRE(bbfBuilder.petrifyFile(OUTPUT, PETRIFIEDOUTPUT));
}

TEST_CASE("BBFBuilder - Deduplication Test")
{
    BBFBuilder bbfBuilder(OUTPUT);
    // Add page
    createTestFile("testImage.png", 2048);
    bbfBuilder.addPage("testImage.png");
    bbfBuilder.addPage("testImage.png");
    deleteFile("testImage.png");
    REQUIRE(bbfBuilder.getAssetCount() == 1);
    REQUIRE(bbfBuilder.getPageCount() == 2);
}

TEST_CASE("BBFReader - Constructor")
{
    BBFBuilder bbfBuilder(OUTPUT);
    createTestFile("testImage.png", 2048);
    bbfBuilder.addPage("testImage.png");
    deleteFile("testImage.png");

    bbfBuilder.finalize();

    REQUIRE_NOTHROW(BBFReader(OUTPUT));
}

TEST_CASE("BBFReader - Header and Footer Navigation")
{
    BBFBuilder bbfBuilder(OUTPUT);
    createTestFile("test.png", 1024);
    bbfBuilder.addPage("test.png");
    deleteFile("test.png");
    REQUIRE(bbfBuilder.finalize());

    BBFReader reader(OUTPUT);

    BBFHeader* header = reader.getHeaderView();
    REQUIRE(header != nullptr);
    CHECK(reader.checkMagic(header) == true);
    CHECK(header->version == BBF::VERSION);

    BBFFooter* footer = reader.getFooterView(header->footerOffset);
    REQUIRE(footer != nullptr);

    CHECK(footer->assetCount == 1);
    CHECK(footer->pageCount == 1);
}

TEST_CASE("BBFReader - Reading Asset Data")
{
    const std::string content = "THIS_IS_IMAGE_DATA";
    createTestFileContent("img.png", 100, content);
    
    BBFBuilder bbfBuilder(OUTPUT);
    bbfBuilder.addPage("img.png");
    bbfBuilder.finalize();
    deleteFile("img.png");

    BBFReader reader(OUTPUT);
    BBFHeader* h = reader.getHeaderView();
    BBFFooter* f = reader.getFooterView(h->footerOffset);

    const uint8_t* assetTable = reader.getAssetTableView(f->assetOffset);
    REQUIRE(assetTable != nullptr);

    const BBFAsset* asset = reader.getAssetEntryView((uint8_t*)assetTable, 0);
    REQUIRE(asset != nullptr);
    CHECK(asset->fileSize == 100);

    const uint8_t* data = reader.getAssetDataView(asset->fileOffset);
    REQUIRE(data != nullptr);

    CHECK(memcmp(data, content.c_str(), content.length()) == 0);
}

TEST_CASE("BBFReader - Metadata and Strings")
{
    BBFBuilder bbfBuilder(OUTPUT);

    createTestFile("test.png", 1024);
    bbfBuilder.addPage("test.png");
    deleteFile("test.png");

    bbfBuilder.addMeta("TestKey", "TestVal", "TestParent");
    bbfBuilder.finalize();

    BBFReader reader(OUTPUT);
    BBFHeader* h = reader.getHeaderView();
    BBFFooter* f = reader.getFooterView(h->footerOffset);

    REQUIRE(f->metaCount == 1);

    const uint8_t* metaTable = reader.getMetadataView(f->metaOffset);
    REQUIRE(metaTable != nullptr);

    const BBFMeta* metaEntry = reader.getMetaEntryView((uint8_t*)metaTable, 0);
    REQUIRE(metaEntry != nullptr);

    const char* key = reader.getStringView(metaEntry->keyOffset);
    const char* val = reader.getStringView(metaEntry->valueOffset);
    const char* parent = reader.getStringView(metaEntry->parentOffset);

    REQUIRE(std::string(key) == "TestKey");
    REQUIRE(std::string(val) == "TestVal");
    REQUIRE(std::string(parent) == "TestParent");
}

TEST_CASE("BBFReader - Verify Hashes")
{
    createTestFile("hash_test.png", 512, 'X'); // Fill with 'X'
    BBFBuilder builder(OUTPUT);
    builder.addPage("hash_test.png");
    builder.finalize();
    deleteFile("hash_test.png");

    BBFReader reader(OUTPUT);
    BBFHeader* h = reader.getHeaderView();
    BBFFooter* f = reader.getFooterView(h->footerOffset);
    const uint8_t* aTable = reader.getAssetTableView(f->assetOffset);
    const BBFAsset* asset = reader.getAssetEntryView((uint8_t*)aTable, 0);

    XXH128_hash_t calcHash = reader.computeAssetHash(asset);

    REQUIRE(calcHash.low64 == asset->assetHash[0]);
    REQUIRE(calcHash.high64 == asset->assetHash[1]);
}

TEST_CASE("BBFReader - Check Bounds")
{
    BBFBuilder builder(OUTPUT);
    createTestFile("test.png", 10);
    builder.addPage("test.png"); 
    builder.finalize();
    deleteFile("test.png");

    BBFReader reader(OUTPUT);
    BBFHeader* h = reader.getHeaderView();
    BBFFooter* f = reader.getFooterView(h->footerOffset);
    
    SECTION("Invalid Footer Offset")
    {
        CHECK(reader.getFooterView(99999999) == nullptr);
    }

    SECTION("Asset Index Out of Bounds") 
    {
        const uint8_t* aTable = reader.getAssetTableView(f->assetOffset);
        CHECK_FALSE(reader.getAssetEntryView((uint8_t*)aTable, 1) == nullptr);
        CHECK(reader.getAssetEntryView((uint8_t*)aTable, -1) == nullptr);
    }

    SECTION("Invalid String Offset") 
    {
        CHECK(reader.getStringView(99999999) == nullptr);
    }

    SECTION("Invalid Data View") 
    {
        CHECK(reader.getAssetDataView(99999999) == nullptr);
    }
}


// Performance
TEST_CASE("Performance Benchmarks", "[Benchmark]")
{
    const std::string smallAsset = "smallAsset.png"; // 4KB
    const std::string mediumAsset = "medAsset.png"; // 4MB
    const std::string largeAsset = "largeAsset.png"; // 40MB
    const std::string hugeAsset = "hugeAsset.png"; // 400MB

    const std::string writeOut = "test.bbf";
    const std::string writeOutPet = "testPetrified.bbf";

    createTestFile(smallAsset, 4096, 's');
    createTestFile(mediumAsset, 4 * 1024 * 1024, 'm');
    createTestFile(largeAsset, 40 * 1024 * 1024, 'l');
    createTestFile(hugeAsset, 400 * 1024 * 1024, 'h');

    {
        BBFBuilder setup(writeOut.c_str());
        setup.addPage(smallAsset.c_str());
        setup.addPage(largeAsset.c_str());

        int assetIterator = 0;
        for(assetIterator; assetIterator<100; ++assetIterator) {
            std::string k = "Key" + std::to_string(assetIterator);
            std::string v = "Val" + std::to_string(assetIterator);
            setup.addMeta(k.c_str(), v.c_str());
        }
        setup.finalize();
    }

    BENCHMARK("BBFWriter - Constructor")
    {
        BBFBuilder builder("tmp.bbf");
        return builder;
    };
    deleteFile("tmp.bbf");

    BENCHMARK_ADVANCED("BBFWriter - Add Page (4KB)")(Catch::Benchmark::Chronometer meter)
    {
        meter.measure([&] 
        {
            BBFBuilder builder(writeOut.c_str());
            return builder.addPage(smallAsset.c_str());
        });
    };

    BENCHMARK_ADVANCED("BBFWriter - Add Page (4MB)")(Catch::Benchmark::Chronometer meter)
    {
        meter.measure([&] 
        {
            BBFBuilder builder(writeOut.c_str());
            return builder.addPage(mediumAsset.c_str());
        });
    };

    BENCHMARK_ADVANCED("BBFWriter - Add Page (40MB)")(Catch::Benchmark::Chronometer meter)
    {
        meter.measure([&] 
        {
            BBFBuilder builder(writeOut.c_str());
            return builder.addPage(largeAsset.c_str());
        });
    };

    // BENCHMARK_ADVANCED("BBFWriter - Add Page (400MB)")(Catch::Benchmark::Chronometer meter)
    // {
    //     meter.measure([&] 
    //     {
    //         BBFBuilder builder(writeOut.c_str());
    //         return builder.addPage(hugeAsset.c_str());
    //     });
    // };

    BENCHMARK_ADVANCED("BBFWriter - Add Deduplicated Page (4KB)")(Catch::Benchmark::Chronometer meter)
    {
        BBFBuilder b(writeOut.c_str());
        b.addPage(smallAsset.c_str()); 

        meter.measure([&] 
        {
            return b.addPage(smallAsset.c_str()); 
        });
    };

    BENCHMARK_ADVANCED("BBFWriter - Add Deduplicated Page (4MB)")(Catch::Benchmark::Chronometer meter)
    {
        BBFBuilder b(writeOut.c_str());
        b.addPage(mediumAsset.c_str()); 

        meter.measure([&] 
        {
            return b.addPage(mediumAsset.c_str()); 
        });
    };

    BENCHMARK_ADVANCED("BBFWriter - Add Deduplicated Page (40MB)")(Catch::Benchmark::Chronometer meter)
    {
        BBFBuilder b(writeOut.c_str());
        b.addPage(largeAsset.c_str()); 

        meter.measure([&] 
        {
            return b.addPage(largeAsset.c_str()); 
        });
    };

    // BENCHMARK_ADVANCED("BBFWriter - Add Deduplicated Page (400MB)")(Catch::Benchmark::Chronometer meter)
    // {
    //     BBFBuilder b(writeOut.c_str());
    //     b.addPage(hugeAsset.c_str()); 

    //     meter.measure([&] 
    //     {
    //         return b.addPage(hugeAsset.c_str()); 
    //     });
    // };

    BENCHMARK_ADVANCED("BBFWriter - Add Metadata (No Parent)")(Catch::Benchmark::Chronometer meter)
    {
        BBFBuilder b(writeOut.c_str());

        meter.measure([&] 
        {
            return b.addMeta("BenchKey", "BenchVal"); 
        });
    };

    BENCHMARK_ADVANCED("BBFWriter - Add Metadata (With Parent)")(Catch::Benchmark::Chronometer meter)
    {
        BBFBuilder b(writeOut.c_str());

        meter.measure([&] 
        {
            return b.addMeta("BenchKey", "BenchVal", "BenchParentLabel"); 
        });
    };

    BENCHMARK_ADVANCED("BBFWriter - Add Section (No Parent)")(Catch::Benchmark::Chronometer meter)
    {
        BBFBuilder b(writeOut.c_str());
        b.addPage(mediumAsset.c_str()); 

        meter.measure([&] 
        {
            return b.addSection("BenchKey", 1); 
        });
    };

    BENCHMARK_ADVANCED("BBFWriter - Add Section (With Parent)")(Catch::Benchmark::Chronometer meter)
    {
        BBFBuilder b(writeOut.c_str());
        b.addPage(mediumAsset.c_str()); 

        meter.measure([&] 
        {
            return b.addSection("BenchKey", 1, "BenchParent"); 
        });
    };

    // Reading
    BBFBuilder builder(writeOut.c_str());

    builder.addPage(mediumAsset.c_str());
    builder.addPage(mediumAsset.c_str());
 
    builder.finalize();

    BENCHMARK("BBFReader - Constructor")
    {
        return BBFReader(writeOut.c_str());
    };

    {
        BBFReader reader(writeOut.c_str());
        BBFHeader* h = reader.getHeaderView();
        BBFFooter* f = reader.getFooterView(h->footerOffset);
        
        const uint8_t* assetTable = reader.getAssetTableView(f->assetOffset);
        const uint8_t* metaTable = reader.getMetadataView(f->metaOffset);

        BENCHMARK("BBFReader - Get Asset Entry View (Struct Lookup)") 
        {
            return reader.getAssetEntryView(assetTable, 0);
        };

        const BBFMeta* mEntry = reader.getMetaEntryView(metaTable, 0);
        BENCHMARK("BBFReader - Get String View") 
        {
            return reader.getStringView(mEntry->keyOffset);
        };

        const BBFAsset* mediumAssetEntry = reader.getAssetEntryView(assetTable, 1);
        BENCHMARK("BBFReader - Compute Hash (4MB Data)")
        {
            return reader.computeAssetHash(mediumAssetEntry);
        };

        mediumAssetEntry = nullptr;
        mEntry = nullptr;
        metaTable = nullptr;
        assetTable = nullptr;
    }

    deleteFile(smallAsset);
    deleteFile(largeAsset);
    deleteFile(mediumAsset);
    deleteFile(hugeAsset);
    deleteFile(writeOut);
}

// Versus Competitor
TEST_CASE("BBF versus ZIP Benchmarks", "[Compmark]") 
{
    const int BATCH_SIZE = 100;
    const size_t FILE_SIZE = 1024 * 2000; // 2MB per file
    std::vector<std::string> batchFiles;

    int iterator = 0;
    for (; iterator < BATCH_SIZE; ++iterator) 
    {
        std::string name = "rand_" + std::to_string(iterator) + ".dat";
        createRandomFile(name, FILE_SIZE);
        batchFiles.push_back(name);
    }

    {
        writeZip("bench_batch.zip", batchFiles, 0);

        BBFBuilder bbf("bench_batch.bbf");
        for (const auto& f : batchFiles) 
        {
            bbf.addPage(f.c_str());
        }
        bbf.finalize();
        bbf.petrifyFile("bench_batch.bbf", "bench_batch_petrified.bbf");
    }

    BENCHMARK_ADVANCED("BBFWriter - Write 100 Files")(Catch::Benchmark::Chronometer meter) 
    {
        meter.measure([&] 
        {
            BBFBuilder bbf("bench_batch.bbf"); 
            for (const auto& f : batchFiles) 
            {
                bbf.addPage(f.c_str());
            }
            return bbf.finalize();
        });
    };

    BENCHMARK_ADVANCED("BBFWriter - Petrify BBF")(Catch::Benchmark::Chronometer meter) 
    {
        meter.measure([&] 
        {
            BBFBuilder bbf("rand.bbf"); 
            return bbf.petrifyFile("bench_batch.bbf", "bench_batch_petrified.bbf");
        });
    };

    BENCHMARK_ADVANCED("MINIZ - ZIP Store (100 Unique Files)")(Catch::Benchmark::Chronometer meter) 
    {
        meter.measure([&] 
        {
            writeZip("bench_batch.zip", batchFiles, 0);
        });
    };

    {
        BBFBuilder bbf("bench_batch.bbf");
        for (const auto& f : batchFiles) 
        {
            bbf.addPage(f.c_str());
        }
        bbf.finalize();
        bbf.petrifyFile("bench_batch.bbf", "bench_batch_petrified.bbf");
    }

    BENCHMARK("BBFReader - Read Header (Non-Petrified)") 
    {
        // BBF header is at the start. Pure sequential IO/Cache hit.
        BBFReader reader("bench_batch.bbf");
        return reader.getHeaderView();
    };

    BENCHMARK("BBFReader - Read Header (Petrified)") 
    {
        BBFReader reader("bench_batch_petrified.bbf");
        return reader.getHeaderView();
    };

    BENCHMARK("BBFReader - Read Footer") 
    {
        // Requires reading header, then a seek to the end.
        BBFReader reader("bench_batch.bbf");
        auto* h = reader.getHeaderView();
        return reader.getFooterView(h->footerOffset);
    };

    BENCHMARK("BBFReader - Read Footer (Petrified)") 
    {
        // BBF header is at the start. Pure sequential IO/Cache hit.
        BBFReader reader("bench_batch_petrified.bbf");
        auto* h = reader.getHeaderView();
        return reader.getFooterView(h->footerOffset);
    };

    BENCHMARK("MINIZ - ZIP Parse Central Directory") 
    {
        // ZIP must seek to the end, scan for EOCD, then parse the directory.
        mz_zip_archive zip;
        memset(&zip, 0, sizeof(zip));
        bool ok = mz_zip_reader_init_file(&zip, "bench_batch.zip", 0);
        if (ok) mz_zip_reader_end(&zip);
        return ok;
    };

    {
        writeZip("bench_batch.zip", batchFiles, 0);

        BBFReader bbfReader("bench_batch.bbf");
        auto* h = bbfReader.getHeaderView();
        auto* f = bbfReader.getFooterView(h->footerOffset);

        BBFReader reader("bench_batch_petrified.bbf");
        auto* header = reader.getHeaderView();
        auto* footer = reader.getFooterView(header->footerOffset);

        const uint8_t* assetTable = bbfReader.getAssetTableView(f->assetOffset);
        const uint8_t* assetTablePetrified = reader.getAssetTableView(footer->assetOffset);


        BENCHMARK("BBFReader - Locate Asset (Non-Petrified)")
        {
            return bbfReader.getAssetEntryView(assetTable, BATCH_SIZE / 2);
        };

        BENCHMARK("BBFReader - Locate Asset (Petrified)")
        {
            return reader.getAssetEntryView(assetTablePetrified, BATCH_SIZE / 2);
        };

        mz_zip_archive zip;
        memset(&zip, 0, sizeof(zip));
        mz_zip_reader_init_file(&zip, "bench_batch.zip", 0);

        BENCHMARK("MINIZ - Locate Asset") 
        {
            return mz_zip_reader_locate_file(&zip, batchFiles[BATCH_SIZE / 2].c_str(), nullptr, 0);
        };
        mz_zip_reader_end(&zip);
    }

    // Cleanup
    for (const auto& f : batchFiles) deleteFile(f);
    deleteFile("bench_batch.bbf");
    deleteFile("bench_batch.zip");
    deleteFile("bench_batch_petrified.bbf");
}
