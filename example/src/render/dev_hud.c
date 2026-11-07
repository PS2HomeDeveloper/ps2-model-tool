#include "dev_hud.h"

#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <kernel.h>

#include <gsKit.h>

#define HUD_X 8.0f
#define HUD_Y 8.0f
#define HUD_FONT_W 10.0f
#define HUD_FONT_H 22.0f
#define HUD_LINE_H HUD_FONT_H
#define GS_VRAM_BYTES (4u * 1024u * 1024u)
#define EE_RAM_FALLBACK_BYTES (32u * 1024u * 1024u)

static u64 hud_color(void)
{
    return GS_SETREG_RGBAQ(0x80, 0xD0, 0xFF, 0x80, 0x00);
}

static unsigned int ram_total_bytes(void)
{
    s32 size = GetMemorySize();

    if (size > 0)
        return (unsigned int)size;

    return EE_RAM_FALLBACK_BYTES;
}

int dev_hud_init(GrDevHud *hud, GSGLOBAL *gs,
                 const unsigned char *font1, unsigned int font1_size,
                 const unsigned char *font2, unsigned int font2_size)
{
    if (!hud || !gs || !font1 || !font2)
        return 0;

    memset(hud, 0, sizeof(*hud));
    hud->visible = 1;
    hud->fps = 0.0f;

    if (font_engine_init(gs, &hud->font, font1, font1_size,
                         font2, font2_size) != 0)
        return 0;

    hud->initialized = 1;
    return 1;
}

void dev_hud_update(GrDevHud *hud, const GrPlayer *player,
                    const GrModel *model, float frame_seconds)
{
    (void)player;
    (void)model;

    if (!hud || !hud->initialized)
        return;

    if (player_debug_toggle_pressed())
        hud->visible = !hud->visible;

    if (frame_seconds > 0.0001f && frame_seconds < 1.0f)
    {
        float instant_fps = 1.0f / frame_seconds;
        hud->fps += (instant_fps - hud->fps) * 0.10f;
    }

    hud->frame_counter++;
}

void dev_hud_draw(GrDevHud *hud, GSGLOBAL *gs,
                  const GrPlayer *player, const GrModel *model)
{
    char line[128];
    struct mallinfo heap;
    unsigned int ram_total;
    unsigned int ram_used;
    unsigned int ram_free;
    unsigned int vram_used;
    unsigned int vram_free;
    u64 color;
    float y;

    if (!hud || !hud->initialized || !hud->visible || !gs || !player || !model)
        return;

    heap = mallinfo();

    ram_total = ram_total_bytes();
    ram_used = heap.uordblks > 0 ? (unsigned int)heap.uordblks : 0u;
    ram_free = ram_used < ram_total ? ram_total - ram_used : 0u;

    vram_used = gs->CurrentPointer;
    if (vram_used > GS_VRAM_BYTES)
        vram_used = GS_VRAM_BYTES;
    vram_free = GS_VRAM_BYTES - vram_used;

    color = hud_color();
    y = HUD_Y;

    snprintf(line, sizeof(line), "DEV HUD  R2:TOGGLE");
    font_engine_draw_string_sized(gs, &hud->font, line, HUD_X, y, 2, color,
                                  HUD_FONT_W, HUD_FONT_H);
    y += HUD_LINE_H;

    snprintf(line, sizeof(line), "FPS:%5.1f  FRAME:%u", hud->fps, hud->frame_counter);
    font_engine_draw_string_sized(gs, &hud->font, line, HUD_X, y, 2, color,
                                  HUD_FONT_W, HUD_FONT_H);
    y += HUD_LINE_H;

    snprintf(line, sizeof(line), "RAM USED:%u / %u KB",
             ram_used / 1024u, ram_total / 1024u);
    font_engine_draw_string_sized(gs, &hud->font, line, HUD_X, y, 2, color,
                                  HUD_FONT_W, HUD_FONT_H);
    y += HUD_LINE_H;

    snprintf(line, sizeof(line), "RAM FREE:%u KB", ram_free / 1024u);
    font_engine_draw_string_sized(gs, &hud->font, line, HUD_X, y, 2, color,
                                  HUD_FONT_W, HUD_FONT_H);
    y += HUD_LINE_H;

    snprintf(line, sizeof(line), "VRAM USED:%u / 4096 KB", vram_used / 1024u);
    font_engine_draw_string_sized(gs, &hud->font, line, HUD_X, y, 2, color,
                                  HUD_FONT_W, HUD_FONT_H);
    y += HUD_LINE_H;

    snprintf(line, sizeof(line), "VRAM FREE:%u KB", vram_free / 1024u);
    font_engine_draw_string_sized(gs, &hud->font, line, HUD_X, y, 2, color,
                                  HUD_FONT_W, HUD_FONT_H);
    y += HUD_LINE_H;

    snprintf(line, sizeof(line), "POS X:%7.2f Y:%7.2f Z:%7.2f",
             player->position[0], player->position[1], player->position[2]);
    font_engine_draw_string_sized(gs, &hud->font, line, HUD_X, y, 2, color,
                                  HUD_FONT_W, HUD_FONT_H);
    y += HUD_LINE_H;

    snprintf(line, sizeof(line), "VIEW YAW:%7.2f PITCH:%7.2f",
             player->yaw, player->pitch);
    font_engine_draw_string_sized(gs, &hud->font, line, HUD_X, y, 2, color,
                                  HUD_FONT_W, HUD_FONT_H);
    y += HUD_LINE_H;

    snprintf(line, sizeof(line), "MODEL V:%u TRI:%u BONES:%u",
             (unsigned)model->vertex_count,
             (unsigned)(model->index_count / 3u),
             (unsigned)model->bone_count);
    font_engine_draw_string_sized(gs, &hud->font, line, HUD_X, y, 2, color,
                                  HUD_FONT_W, HUD_FONT_H);
    y += HUD_LINE_H;

    snprintf(line, sizeof(line), "CLIP %u/%u: %s",
             (unsigned)(model->clip_index + 1u), (unsigned)model->clip_count,
             model->clip_name);
    font_engine_draw_string_sized(gs, &hud->font, line, HUD_X, y, 2, color,
                                  HUD_FONT_W, HUD_FONT_H);
}

void dev_hud_shutdown(GrDevHud *hud)
{
    if (!hud)
        return;

    if (hud->initialized)
        font_engine_shutdown(&hud->font);

    memset(hud, 0, sizeof(*hud));
}
