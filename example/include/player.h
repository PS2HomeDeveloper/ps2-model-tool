#ifndef GRANNY_PLAYER_H
#define GRANNY_PLAYER_H

typedef struct {
    float position[3];

    float yaw;
    float pitch;

    float move_speed;
    float look_sensitivity;

    int pad_ready;
} GrPlayer;

int player_init(GrPlayer *player, float x, float y, float z);

void player_update(GrPlayer *player, float delta_seconds);

void player_get_forward(const GrPlayer *player, float direction[3]);

int player_debug_toggle_pressed(void);

void player_shutdown(void);

#endif
