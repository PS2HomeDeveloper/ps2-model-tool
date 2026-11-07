#include <kernel.h>
#include <stdio.h>
#include <sifrpc.h>
#include <loadfile.h>

#include "assets.h"
#include "dbglog.h"
#include "dev_hud.h"
#include "model.h"
#include "player.h"
#include "renderer.h"

static void fatal(const char *stage, const char *message)
{
    debug_log("FATAL: %s: %s\n", stage, message);
    printf("Granny: %s: %s\n", stage, message);
    SleepThread();
    for (;;) {}
}

int main(int argc, char **argv)
{
    GrPlayer player = {0};
    GrModel granny = {0};
    GrDevHud hud = {0};
    const GrAsset *font1;
    const GrAsset *font2;
    float direction[3];
    unsigned debug_frame = 0;

    debug_init(argc, argv);
    debug_log("main: startup\n");

    sceSifInitRpc(0);
    SifLoadFileInit();

    if (SifLoadModule("rom0:SIO2MAN", 0, NULL) < 0)
        fatal("CONTROLLER INIT FAILED", "Could not load SIO2MAN");
    if (SifLoadModule("rom0:PADMAN", 0, NULL) < 0)
        fatal("CONTROLLER INIT FAILED", "Could not load PADMAN");

    if (!player_init(&player, 0.0f, 1.65f, 3.5f))
        fatal("PLAYER INIT FAILED", "Could not initialize controller");
    debug_log("main: player initialized\n");

    if (!assets_init_embedded())
        fatal("EMBEDDED DATA.PAK LOAD FAILED", assets_error());
    debug_log("main: embedded DATA.PAK loaded\n");

    font1 = assets_find("font1.raw");
    font2 = assets_find("font2.raw");
    if (!font1 || !font2)
        fatal("FONT RESOURCE LOAD FAILED", assets_error());
    debug_log("main: font resources loaded font1=%u font2=%u\n",
              (unsigned)font1->size, (unsigned)font2->size);

    if (!model_load(&granny, assets_find("Granny.mesh"),
                    assets_find("Granny.skeleton"),
                    assets_find("Granny.anim"),
                    assets_find("Granny.material")))
        fatal("MODEL LOAD FAILED", model_error());
    debug_log("main: model loaded vertices=%u indices=%u bones=%u\n",
              (unsigned)granny.vertex_count,
              (unsigned)granny.index_count,
              (unsigned)granny.bone_count);

    if (renderer_init() != GR_RENDER_OK)
        fatal("GRAPHICS INIT FAILED", renderer_error());
    if (renderer_load_materials(granny.materials, granny.material_count) != GR_RENDER_OK)
        fatal("TEXTURE LOAD FAILED", renderer_error());

    if (!dev_hud_init(&hud, renderer_get_global(),
                      font1->data, font1->size, font2->data, font2->size))
        fatal("DEV HUD INIT FAILED", "Could not initialize font resources");
    debug_log("main: dev HUD initialized; press R2 to toggle\n");

    for (;;) {
        float delta_seconds = renderer_frame_seconds();

        player_update(&player, delta_seconds);
        player_get_forward(&player, direction);
        model_update(&granny, delta_seconds);
        dev_hud_update(&hud, &player, &granny, delta_seconds);

        renderer_set_camera(
            player.position[0], player.position[1], player.position[2],
            player.position[0] + direction[0],
            player.position[1] + direction[1],
            player.position[2] + direction[2]
        );

        renderer_begin();
        renderer_draw(granny.vertices, granny.vertex_count,
                      granny.indices, granny.index_count,
                      granny.parts, granny.part_count);
        renderer_begin_overlay();
        dev_hud_draw(&hud, renderer_get_global(), &player, &granny);
        renderer_end_overlay();
        renderer_end();

        debug_frame++;
        if ((debug_frame % 30u) == 0u)
            debug_log("frame=%u dt=%f fps=%f pos=%f,%f,%f yaw=%f pitch=%f\n",
                      debug_frame, delta_seconds, hud.fps,
                      player.position[0], player.position[1], player.position[2],
                      player.yaw, player.pitch);
    }

    return 0;
}
