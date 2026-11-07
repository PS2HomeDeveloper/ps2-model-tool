#include <string.h>
#include <malloc.h>
#include <gsKit.h>
#include <dmaKit.h>
#include <gsToolkit.h>

#include "font_engine.h"

extern int gsKit_texture_finish(GSGLOBAL *gsGlobal, GSTEXTURE *Texture);

static int load_embedded_texture(GSGLOBAL *gsGlobal, GSTEXTURE *Texture,
                                  const unsigned char *data, unsigned int data_size)
{
    unsigned int need;
    unsigned int y;
    const unsigned int raw_width = Texture->Width;

    if (Texture->Width == 0 || Texture->Height == 0 ||
        Texture->PSM != GS_PSM_T8 ||
        data_size != raw_width * Texture->Height)
        return -1;

    need = gsKit_texture_size_ee(Texture->Width, Texture->Height, Texture->PSM);
    Texture->Mem = memalign(128, need);

    if (Texture->Mem == NULL)
        return -1;

    Texture->Clut = memalign(128, 256 * sizeof(unsigned int));
    if (Texture->Clut == NULL)
    {
        free(Texture->Mem);
        Texture->Mem = NULL;
        return -1;
    }

    {
        unsigned int *clut = (unsigned int *)Texture->Clut;
        unsigned int i;

        for (i = 0; i < 256; i++)
        {
            unsigned int alpha = (i * 128u + 127u) / 255u;
            clut[i] = (alpha << 24) | 0x00FFFFFFu;
        }

        for (i = 0; i < 256; i++)
        {
            if ((i & 0x18u) == 0x08u)
            {
                unsigned int temp = clut[i];
                clut[i] = clut[i + 8u];
                clut[i + 8u] = temp;
            }
        }
    }

    Texture->ClutPSM = GS_PSM_CT32;
    Texture->VramClut = 0;

    for (y = 0; y < Texture->Height; y++)
    {
        memcpy((unsigned char *)Texture->Mem + y * Texture->Width,
               data + y * raw_width,
               Texture->Width);
    }

    if (gsKit_texture_finish(gsGlobal, Texture) < 0)
    {
        if (Texture->Mem != NULL)
        {
            free(Texture->Mem);
            Texture->Mem = NULL;
        }
        if (Texture->Clut != NULL)
        {
            free(Texture->Clut);
            Texture->Clut = NULL;
        }
        return -1;
    }

    return 0;
}

static int ensure_font_sheet_loaded(GSGLOBAL *gsGlobal, FontEngine *fe, int sheet)
{
    if (!gsGlobal || !fe || sheet < 0 || sheet >= FONT_NUM_SHEETS)
        return -1;

    if (fe->sheet_loaded[sheet])
        return 0;

    if (sheet == 1)
    {
        if (!fe->sheet_data[1] || !fe->sheet_size[1])
            return -1;

        fe->sheets[1].Width  = 70;
        fe->sheets[1].Height = 110;
        fe->sheets[1].PSM    = GS_PSM_T8;
        fe->sheets[1].Filter = GS_FILTER_NEAREST;

        if (load_embedded_texture(gsGlobal, &fe->sheets[1],
                                  fe->sheet_data[1], fe->sheet_size[1]) < 0)
            return -1;

        fe->sheet_loaded[1] = 1;
        return 0;
    }

    return -1;
}

static const FontCharEntry s_font_map[] = {

    { '!', 0, 0, 0 },
    { '"', 0, 1, 0 },
    { '#', 0, 2, 0 },
    { '$', 0, 3, 0 },
    { '%', 0, 4, 0 },
    { ' ', 0, 5, 0 },
    { '\'', 0, 6, 0 },
    { '(', 0, 7, 0 },
    { ')', 0, 8, 0 },
    { '*', 0, 9, 0 },
    { '+', 0, 10, 0 },
    { ',', 0, 11, 0 },

    { '-', 0, 0, 1 },
    { '.', 0, 1, 1 },
    { '/', 0, 2, 1 },
    { '0', 0, 3, 1 },
    { '1', 0, 4, 1 },
    { '2', 0, 5, 1 },
    { '3', 0, 6, 1 },
    { '4', 0, 7, 1 },
    { '5', 0, 8, 1 },
    { '6', 0, 9, 1 },
    { '7', 0, 10, 1 },
    { '8', 0, 11, 1 },

    { '9', 0, 0, 2 },
    { ':', 0, 1, 2 },
    { ';', 0, 2, 2 },
    { '<', 0, 3, 2 },
    { '=', 0, 4, 2 },
    { '>', 0, 5, 2 },
    { '?', 0, 6, 2 },
    { '@', 0, 7, 2 },
    { 'A', 0, 8, 2 },
    { 'B', 0, 9, 2 },
    { 'C', 0, 10, 2 },
    { 'D', 0, 11, 2 },

    { 'E', 0, 0, 3 },
    { 'F', 0, 1, 3 },
    { 'G', 0, 2, 3 },
    { 'H', 0, 3, 3 },
    { 'I', 0, 4, 3 },
    { 'J', 0, 5, 3 },
    { 'K', 0, 6, 3 },
    { 'L', 0, 7, 3 },
    { 'M', 0, 8, 3 },
    { 'N', 0, 9, 3 },
    { 'O', 0, 10, 3 },
    { 'P', 0, 11, 3 },

    { 'Q', 0, 0, 4 },
    { 'R', 0, 1, 4 },
    { 'S', 0, 2, 4 },
    { 'T', 0, 3, 4 },
    { 'U', 0, 4, 4 },
    { 'V', 0, 5, 4 },
    { 'W', 0, 6, 4 },
    { 'X', 0, 7, 4 },
    { 'Y', 0, 8, 4 },
    { 'Z', 0, 9, 4 },
    { '[', 0, 10, 4 },
    { '\\', 0, 11, 4 },

    { ']', 0, 0, 5 },
    { '^', 0, 1, 5 },
    { '_', 0, 2, 5 },
    { '`', 0, 3, 5 },
    { 'a', 0, 4, 5 },
    { 'b', 0, 5, 5 },
    { 'c', 0, 6, 5 },
    { 'd', 0, 7, 5 },
    { 'e', 0, 8, 5 },
    { 'f', 0, 9, 5 },
    { 'g', 0, 10, 5 },
    { 'h', 0, 11, 5 },

    { 'i', 0, 0, 6 },
    { 'j', 0, 1, 6 },
    { 'k', 0, 2, 6 },
    { 'l', 0, 3, 6 },
    { 'm', 0, 4, 6 },
    { 'n', 0, 5, 6 },
    { 'o', 0, 6, 6 },
    { 'p', 0, 7, 6 },
    { 'q', 0, 8, 6 },
    { 'r', 0, 9, 6 },
    { 's', 0, 10, 6 },
    { 't', 0, 11, 6 },

    { 'u', 0, 0, 7 },
    { 'v', 0, 1, 7 },
    { 'w', 0, 2, 7 },
    { 'x', 0, 3, 7 },
    { 'y', 0, 4, 7 },
    { 'z', 0, 5, 7 },
    { '{', 0, 6, 7 },
    { '|', 0, 7, 7 },
    { '}', 0, 8, 7 },
    { '~', 0, 9, 7 },
    { 0x00F7, 0, 10, 7 },
    { 0x00D7, 0, 11, 7 },

    { 0x00B1, 0, 0, 8 },
    { 0x00B0, 0, 1, 8 },
    { 0x00A7, 0, 2, 8 },
    { 0x00A3, 0, 3, 8 },
    { 0x00A5, 0, 4, 8 },
    { 0x20AC, 0, 5, 8 },
    { 0x00A2, 0, 6, 8 },
    { 0x00B5, 0, 7, 8 },
    { 0x00B6, 0, 8, 8 },
    { 0x00A9, 0, 9, 8 },
    { 0x00AE, 0, 10, 8 },
    { 0x2190, 0, 11, 8 },

    /* بداية الأطلس الثاني: font2.raw */

    { 0x2191, 1, 0, 0 },
    { 0x2192, 1, 1, 0 },
    { 0x2193, 1, 2, 0 },
    { 0x221A, 1, 3, 0 },
    { 0x2713, 1, 4, 0 },
    { 0x2715, 1, 5, 0 },
    { 0x2264, 1, 6, 0 },

    { 0x2265, 1, 0, 1 },
    { 0x2260, 1, 1, 1 },
    { 0x00AB, 1, 2, 1 },
    { 0x00BB, 1, 3, 1 },
    { 0x2018, 1, 4, 1 },
    { 0x2019, 1, 5, 1 },
    { 0x2014, 1, 6, 1 },

    { 0x2013, 1, 0, 2 },
    { 0x2605, 1, 1, 2 },
    { 0x2606, 1, 2, 2 },
    { 0x2026, 1, 3, 2 },
    { 0x2022, 1, 4, 2 },
    { 0x2122, 1, 5, 2 },
    { 0x25C6, 1, 6, 2 },

    { 0x25C7, 1, 0, 3 },
    { 0x25A0, 1, 1, 3 },
    { 0x25A1, 1, 2, 3 },
    { 0x03C0, 1, 3, 3 },
    { 0x03A9, 1, 4, 3 },
    { 0x0394, 1, 5, 3 },
    { 0x03A3, 1, 6, 3 },

    { 0x2116, 1, 0, 4 },
    { 0x2139, 1, 1, 4 },
    { 0x00BF, 1, 2, 4 },
    { '&', 1, 3, 4 },
    { 0xE000, 1, 4, 4 },

    { 0, -1, 0, 0 }
};

#define FONT_MAP_COUNT \
    ((int)(sizeof(s_font_map) / sizeof(s_font_map[0])) - 1)

#define LUT_ASCII_SIZE  128
#define LUT_UNI_SIZE    64
#define LUT_UNI_EMPTY   0xFFFFFFFFu

static const FontCharEntry *s_ascii_lut[LUT_ASCII_SIZE];

typedef struct {
    unsigned int         codepoint;
    const FontCharEntry *entry;
} UniSlot;

static UniSlot s_uni_lut[LUT_UNI_SIZE];

static int s_lut_built = 0;

static void build_lookup_tables(void)
{
    int i;

    for (i = 0; i < LUT_ASCII_SIZE; i++)
        s_ascii_lut[i] = NULL;

    for (i = 0; i < LUT_UNI_SIZE; i++)
    {
        s_uni_lut[i].codepoint = LUT_UNI_EMPTY;
        s_uni_lut[i].entry     = NULL;
    }

    for (i = 0; i < FONT_MAP_COUNT; i++)
    {
        unsigned int cp = s_font_map[i].codepoint;

        if (cp < LUT_ASCII_SIZE)
        {
            s_ascii_lut[cp] = &s_font_map[i];
        }
        else
        {
            unsigned int slot = (cp ^ (cp >> 8)) & (LUT_UNI_SIZE - 1);
            int tries = 0;

            while (s_uni_lut[slot].codepoint != LUT_UNI_EMPTY
                   && tries < LUT_UNI_SIZE)
            {
                slot = (slot + 1) & (LUT_UNI_SIZE - 1);
                tries++;
            }

            if (tries < LUT_UNI_SIZE)
            {
                s_uni_lut[slot].codepoint = cp;
                s_uni_lut[slot].entry     = &s_font_map[i];
            }
        }
    }

    s_lut_built = 1;
}

const FontCharEntry *font_engine_lookup(unsigned int codepoint)
{
    if (codepoint < LUT_ASCII_SIZE)
        return s_ascii_lut[codepoint];

    unsigned int slot  = (codepoint ^ (codepoint >> 8)) & (LUT_UNI_SIZE - 1);
    int          tries = 0;

    while (s_uni_lut[slot].codepoint != LUT_UNI_EMPTY
           && tries < LUT_UNI_SIZE)
    {
        if (s_uni_lut[slot].codepoint == codepoint)
            return s_uni_lut[slot].entry;

        slot = (slot + 1) & (LUT_UNI_SIZE - 1);
        tries++;
    }

    return NULL;
}

int font_engine_init(GSGLOBAL *gsGlobal, FontEngine *fe,
                     const unsigned char *data1, unsigned int size1,
                     const unsigned char *data2, unsigned int size2)
{
    if (!gsGlobal || !fe || !data1 || !data2)
        return -1;

    memset(fe, 0, sizeof(FontEngine));

    if (!s_lut_built)
        build_lookup_tables();

    fe->sheet_data[1] = data2;
    fe->sheet_size[1] = size2;

    fe->sheets[0].Width  = 120;
    fe->sheets[0].Height = 198;
    fe->sheets[0].PSM    = GS_PSM_T8;
    fe->sheets[0].Filter = GS_FILTER_NEAREST;

    if (load_embedded_texture(gsGlobal, &fe->sheets[0], data1, size1) < 0)
        return -1;

    fe->sheet_loaded[0] = 1;
    fe->loaded = 1;

    /* Sheet 1 is uploaded only when a glyph from it is actually drawn. */
    return 0;
}

void font_engine_begin_draw(GSGLOBAL *gsGlobal)
{
    gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
    gsKit_set_test(gsGlobal, GS_ATEST_OFF);
}

void font_engine_end_draw(GSGLOBAL *gsGlobal)
{
    gsKit_set_test(gsGlobal, GS_ATEST_ON);
    gsKit_set_primalpha(gsGlobal, GS_BLEND_BACK2FRONT, 0);
}

static void font_engine_draw_char_raw_sized(GSGLOBAL *gsGlobal, FontEngine *fe,
                                            unsigned int ch,
                                            float x, float y,
                                            int z, u64 color,
                                            float char_w, float char_h)
{
    const FontCharEntry *entry;
    GSTEXTURE           *tex;
    float                u1, v1, u2, v2;
    float                x2, y2;

    if (!fe || !fe->loaded || char_w <= 0.0f || char_h <= 0.0f)
        return;

    entry = font_engine_lookup(ch);
    if (!entry || entry->sheet < 0)
        return;

    if (ensure_font_sheet_loaded(gsGlobal, fe, entry->sheet) < 0)
        return;

    tex = &fe->sheets[entry->sheet];

    u1 = (float)(entry->col * FONT_CELL_W);
    v1 = (float)(entry->row * FONT_CELL_H);
    u2 = u1 + (float)FONT_CELL_W;
    v2 = v1 + (float)FONT_CELL_H;

    x2 = x + char_w;
    y2 = y + char_h;

    gsKit_prim_sprite_texture(
        gsGlobal, tex,
        x,  y,  u1, v1,
        x2, y2, u2, v2,
        z, color
    );
}

void font_engine_draw_char_raw(GSGLOBAL *gsGlobal, FontEngine *fe,
                               unsigned int ch,
                               float x, float y,
                               int z, u64 color)
{
    font_engine_draw_char_raw_sized(gsGlobal, fe, ch, x, y, z, color,
                                    (float)FONT_CHAR_DRAW_W,
                                    (float)FONT_CHAR_DRAW_H);
}

void font_engine_draw_cursor_raw(GSGLOBAL *gsGlobal, FontEngine *fe,
                                 float x, float y,
                                 int z, u64 color,
                                 int cursor_w, int cursor_h)
{
    const FontCharEntry *entry;
    GSTEXTURE           *tex;
    float                u1, v1, u2, v2;
    float                x2, y2;
    int                  u_offset, v_offset;

    if (!fe || !fe->loaded)
        return;

    entry = font_engine_lookup(0xE000u);
    if (!entry || entry->sheet < 0)
        return;

    if (ensure_font_sheet_loaded(gsGlobal, fe, entry->sheet) < 0)
        return;

    tex = &fe->sheets[entry->sheet];

    u_offset = (FONT_CELL_W - cursor_w) / 2;
    v_offset = (FONT_CELL_H - cursor_h) / 2;

    u1 = (float)(entry->col * FONT_CELL_W) + (float)u_offset;
    v1 = (float)(entry->row * FONT_CELL_H) + (float)v_offset;
    u2 = u1 + (float)cursor_w;
    v2 = v1 + (float)cursor_h;

    x2 = x + (float)cursor_w;
    y2 = y + (float)cursor_h;

    gsKit_prim_sprite_texture(
        gsGlobal, tex,
        x,  y,  u1, v1,
        x2, y2, u2, v2,
        z, color
    );
}

void font_engine_draw_char_sized(GSGLOBAL *gsGlobal, FontEngine *fe,
                                 unsigned int ch,
                                 float x, float y,
                                 int z, u64 color,
                                 float char_w, float char_h)
{
    font_engine_begin_draw(gsGlobal);
    font_engine_draw_char_raw_sized(gsGlobal, fe, ch, x, y, z, color,
                                    char_w, char_h);
    font_engine_end_draw(gsGlobal);
}

void font_engine_draw_char(GSGLOBAL *gsGlobal, FontEngine *fe,
                           unsigned int ch,
                           float x, float y,
                           int z, u64 color)
{
    font_engine_draw_char_sized(gsGlobal, fe, ch, x, y, z, color,
                                (float)FONT_CHAR_DRAW_W,
                                (float)FONT_CHAR_DRAW_H);
}

void font_engine_draw_string_sized(GSGLOBAL *gsGlobal, FontEngine *fe,
                                   const char *str,
                                   float x, float y,
                                   int z, u64 color,
                                   float char_w, float char_h)
{
    float cursor_x = x;
    float cursor_y = y;
    int i = 0;

    if (!str || char_w <= 0.0f || char_h <= 0.0f)
        return;

    font_engine_begin_draw(gsGlobal);

    while (str[i] != '\0')
    {
        unsigned char ch = (unsigned char)str[i];

        if (ch == '\n')
        {
            cursor_x = x;
            cursor_y += char_h;
            i++;
            continue;
        }

        font_engine_draw_char_raw_sized(gsGlobal, fe, (unsigned int)ch,
                                        cursor_x, cursor_y, z, color,
                                        char_w, char_h);

        cursor_x += char_w;
        i++;
    }

    font_engine_end_draw(gsGlobal);
}

void font_engine_draw_string(GSGLOBAL *gsGlobal, FontEngine *fe,
                             const char *str,
                             float x, float y,
                             int z, u64 color)
{
    font_engine_draw_string_sized(gsGlobal, fe, str, x, y, z, color,
                                  (float)FONT_CHAR_DRAW_W,
                                  (float)FONT_CHAR_DRAW_H);
}

void font_engine_draw_string32_sized(GSGLOBAL *gsGlobal, FontEngine *fe,
                                     const unsigned int *str32,
                                     float x, float y,
                                     int z, u64 color,
                                     float char_w, float char_h)
{
    float cursor_x = x;
    float cursor_y = y;
    int i = 0;

    if (!str32 || char_w <= 0.0f || char_h <= 0.0f)
        return;

    font_engine_begin_draw(gsGlobal);

    while (str32[i] != 0)
    {
        unsigned int ch = str32[i];

        if (ch == (unsigned int)'\n')
        {
            cursor_x = x;
            cursor_y += char_h;
            i++;
            continue;
        }

        font_engine_draw_char_raw_sized(gsGlobal, fe, ch,
                                        cursor_x, cursor_y, z, color,
                                        char_w, char_h);

        cursor_x += char_w;
        i++;
    }

    font_engine_end_draw(gsGlobal);
}

void font_engine_draw_string32(GSGLOBAL *gsGlobal, FontEngine *fe,
                               const unsigned int *str32,
                               float x, float y,
                               int z, u64 color)
{
    font_engine_draw_string32_sized(gsGlobal, fe, str32, x, y, z, color,
                                    (float)FONT_CHAR_DRAW_W,
                                    (float)FONT_CHAR_DRAW_H);
}

float font_engine_string_width_sized(const char *str, float char_w)
{
    int count = 0;
    int i = 0;

    if (!str || char_w <= 0.0f)
        return 0.0f;

    while (str[i] != '\0')
    {
        if (str[i] != '\n')
            count++;
        i++;
    }

    return (float)count * char_w;
}

float font_engine_string_width(const char *str)
{
    return font_engine_string_width_sized(str,
                                          (float)FONT_CHAR_DRAW_W);
}

float font_engine_string32_width_sized(const unsigned int *str32,
                                       float char_w)
{
    int count = 0;
    int i = 0;

    if (!str32 || char_w <= 0.0f)
        return 0.0f;

    while (str32[i] != 0)
    {
        if (str32[i] != (unsigned int)'\n')
            count++;
        i++;
    }

    return (float)count * char_w;
}

float font_engine_string32_width(const unsigned int *str32)
{
    return font_engine_string32_width_sized(str32,
                                            (float)FONT_CHAR_DRAW_W);
}

void font_engine_shutdown(FontEngine *fe)
{
    if (!fe)
        return;

    memset(fe, 0, sizeof(FontEngine));
}
