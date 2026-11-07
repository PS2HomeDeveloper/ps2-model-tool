#include "assets.h"
#include <string.h>

#define PAK_MAGIC 0x4b415047u
#define PAK_VERSION 1u
#define PAK_HEADER_SIZE 16u
#define PAK_ENTRY_SIZE 64u
#define PAK_MAX_ENTRIES 64u

static GrAsset pak_assets[PAK_MAX_ENTRIES];
static char pak_names[PAK_MAX_ENTRIES][48];
static uint32_t pak_count;
static const char *pak_failure = "resources are not initialized";

static uint32_t read_u32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int parse_pak(const unsigned char *blob, uint32_t total_size)
{
    uint32_t count, index_offset, index_size, i;
    if (!blob || total_size < PAK_HEADER_SIZE || read_u32(blob) != PAK_MAGIC ||
        read_u32(blob + 4) != PAK_VERSION) {
        pak_failure = "embedded DATA.PAK header is invalid";
        return 0;
    }
    count = read_u32(blob + 8);
    index_offset = read_u32(blob + 12);
    index_size = count * PAK_ENTRY_SIZE;
    if (!count || count > PAK_MAX_ENTRIES || index_offset < PAK_HEADER_SIZE ||
        index_offset > total_size || index_size > total_size - index_offset) {
        pak_failure = "embedded DATA.PAK index is invalid";
        return 0;
    }
    for (i = 0; i < count; ++i) {
        const unsigned char *entry = blob + index_offset + i * PAK_ENTRY_SIZE;
        uint32_t offset = read_u32(entry + 48);
        uint32_t size = read_u32(entry + 52);
        uint32_t name_len = 0;
        while (name_len < 48 && entry[name_len]) ++name_len;
        if (!name_len || name_len >= sizeof(pak_names[i]) || offset > total_size ||
            size > total_size - offset) {
            pak_failure = "embedded DATA.PAK contains an invalid entry";
            pak_count = 0;
            return 0;
        }
        memcpy(pak_names[i], entry, name_len);
        pak_names[i][name_len] = 0;
        pak_assets[i].name = pak_names[i];
        pak_assets[i].data = blob + offset;
        pak_assets[i].size = size;
    }
    pak_count = count;
    pak_failure = 0;
    return 1;
}

extern unsigned char gr_pak_blob[];
extern unsigned int size_gr_pak_blob;

int assets_init_embedded(void)
{
    return parse_pak(gr_pak_blob, (uint32_t)size_gr_pak_blob);
}

const GrAsset *assets_find(const char *name)
{
    uint32_t i;
    if (!name) return 0;
    for (i = 0; i < pak_count; ++i)
        if (strcmp(pak_assets[i].name, name) == 0) return &pak_assets[i];
    return 0;
}

const char *assets_error(void)
{
    return pak_failure ? pak_failure : "unknown embedded resource error";
}
