#ifndef FONT_ENGINE_H
#define FONT_ENGINE_H

#include <gsKit.h>

#define FONT_SHEET_W       120
#define FONT_SHEET_H       198
#define FONT_CELL_W        10
#define FONT_CELL_H        22
#define FONT_COLS          12
#define FONT_ROWS          9
#define FONT_CHARS_PER_RAW 108

#define FONT_CHAR_DRAW_W   10
#define FONT_CHAR_DRAW_H   22

#define FONT_NUM_SHEETS    2
#define FONT_TOTAL_CHARS   143

typedef struct {
    unsigned int  codepoint;
    int           sheet;
    int           col;
    int           row;
} FontCharEntry;

typedef struct {
    GSTEXTURE sheets[FONT_NUM_SHEETS];
    const unsigned char *sheet_data[FONT_NUM_SHEETS];
    unsigned int sheet_size[FONT_NUM_SHEETS];
    int        sheet_loaded[FONT_NUM_SHEETS];
    int        loaded;
} FontEngine;

int  font_engine_init(GSGLOBAL *gsGlobal, FontEngine *fe,
                      const unsigned char *data1, unsigned int size1,
                      const unsigned char *data2, unsigned int size2);

void font_engine_draw_char(GSGLOBAL *gsGlobal, FontEngine *fe,
                           unsigned int ch,
                           float x, float y,
                           int z, u64 color);

void font_engine_begin_draw  (GSGLOBAL *gsGlobal);
void font_engine_end_draw    (GSGLOBAL *gsGlobal);
void font_engine_draw_char_raw(GSGLOBAL *gsGlobal, FontEngine *fe,
                               unsigned int ch,
                               float x, float y,
                               int z, u64 color);

void font_engine_draw_cursor_raw(GSGLOBAL *gsGlobal, FontEngine *fe,
                                 float x, float y,
                                 int z, u64 color,
                                 int cursor_w, int cursor_h);

void font_engine_draw_string(GSGLOBAL *gsGlobal, FontEngine *fe,
                             const char *str,
                             float x, float y,
                             int z, u64 color);


void font_engine_draw_string_sized(GSGLOBAL *gsGlobal, FontEngine *fe,
                             const char *str,
                             float x, float y,
                             int z, u64 color,
                             float char_w, float char_h);

void font_engine_draw_string32(GSGLOBAL *gsGlobal, FontEngine *fe,
                               const unsigned int *str32,
                               float x, float y,
                               int z, u64 color);

const FontCharEntry *font_engine_lookup(unsigned int codepoint);

float font_engine_string_width(const char *str);

float font_engine_string32_width(const unsigned int *str32);

void font_engine_shutdown(FontEngine *fe);

#endif
