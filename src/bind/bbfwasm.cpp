#include "libbbf.h"
#include "bbfcodec.h"

// WASM - EXTERNAL FUNCTIONS
extern "C"
{
    LIBBBF_API BBFReader* create_bbf_reader(const char* file) { return new BBFReader(file); }
    LIBBBF_API void close_bbf_reader(BBFReader* bbfreader) { if (bbfreader) delete bbfreader; }

    LIBBBF_API BBFHeader* get_bbf_header(BBFReader* bbfreader) { return bbfreader ? bbfreader->getHeaderView() : nullptr; }
    LIBBBF_API BBFFooter* get_bbf_footer(BBFReader* bbfreader, BBFHeader* header) { return (bbfreader && header) ? bbfreader->getFooterView(header->footerOffset) : nullptr; }
    
    // Get Tables
    LIBBBF_API const uint8_t* get_bbf_page_table(BBFReader* reader, BBFFooter* footer) { return (reader && footer) ? reader->getPageTableView(footer->pageOffset) : nullptr; }
    LIBBBF_API const uint8_t* get_bbf_asset_table(BBFReader* reader, BBFFooter* footer) { return (reader && footer) ? reader->getAssetTableView(footer->assetOffset) : nullptr; }
    LIBBBF_API const uint8_t* get_bbf_section_table(BBFReader* reader, BBFFooter* footer) { return (reader && footer) ? reader->getSectionTableView(footer->sectionOffset) : nullptr; }
    LIBBBF_API const uint8_t* get_bbf_meta_table(BBFReader* reader, BBFFooter* footer) { return (reader && footer) ? reader->getMetadataView(footer->metaOffset) : nullptr; }
    LIBBBF_API const uint8_t* get_bbf_expansion_table(BBFReader* reader, BBFFooter* footer) { return (reader && footer) ? reader->getExpansionTableView(footer->expansionOffset) : nullptr; }

    // Get entries
    LIBBBF_API const BBFPage* get_bbf_page_entry(BBFReader* reader, const uint8_t* table, int16_t index) { return (reader && table) ? reader->getPageEntryView(table, index) : nullptr; }
    LIBBBF_API const BBFAsset* get_bbf_asset_entry(BBFReader* reader, const uint8_t* table, int index) { return (reader && table) ? reader->getAssetEntryView(table, index) : nullptr; }
    LIBBBF_API const BBFSection* get_bbf_section_entry(BBFReader* reader, const uint8_t* table, int index) { return (reader && table) ? reader->getSectionEntryView(table, index) : nullptr; }
    LIBBBF_API const BBFMeta* get_bbf_meta_entry(BBFReader* reader, const uint8_t* table, int index) { return (reader && table) ? reader->getMetaEntryView(table, index) : nullptr; }
    LIBBBF_API const BBFExpansion* get_bbf_expansion_entry(BBFReader* reader, const uint8_t* table, int index) { return (reader && table) ? reader->getExpansionEntryView(table, index) : nullptr; }

    // Get data
    LIBBBF_API const uint8_t* get_bbf_asset_data(BBFReader* reader, uint64_t fileOffset) { return reader ? reader->getAssetDataView(fileOffset) : nullptr; }
    LIBBBF_API const char* get_bbf_string(BBFReader* reader, uint64_t stringOffset) { return reader ? reader->getStringView(stringOffset) : nullptr; }

    // Utilities
    LIBBBF_API int check_bbf_magic(BBFReader* reader, BBFHeader* header) { return (reader && header) ? (int)reader->checkMagic(header) : 0; }
    LIBBBF_API XXH128_hash_t compute_asset_hash_from_struct(BBFReader* reader, const BBFAsset* asset) { if (reader && asset) return reader->computeAssetHash(asset); XXH128_hash_t empty = {0, 0}; return empty; }
    LIBBBF_API XXH128_hash_t compute_asset_hash_from_index(BBFReader* reader, uint8_t* table, int index) { if (reader && table) return reader->computeAssetHash(table, index); XXH128_hash_t empty = {0, 0}; return empty; }
}
