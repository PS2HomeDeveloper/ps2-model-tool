#ifndef GR_DEV_HUD_H
#define GR_DEV_HUD_H

#include "font_engine.h"
#include "model.h"
#include "player.h"

#include <stdint.h>

typedef struct {
    FontEngine font;
    int initialized;
    int visible;
    unsigned frame_counter;
    float fps;
} GrDevHud;

int dev_hud_init(GrDevHud *hud, GSGLOBAL *gs,
                 const unsigned char *font1, unsigned int font1_size,
                 const unsigned char *font2, unsigned int font2_size);
void dev_hud_update(GrDevHud *hud, const GrPlayer *player,
                    const GrModel *model, float frame_seconds);
void dev_hud_draw(GrDevHud *hud, GSGLOBAL *gs,
                  const GrPlayer *player, const GrModel *model);
void dev_hud_shutdown(GrDevHud *hud);

#endif
