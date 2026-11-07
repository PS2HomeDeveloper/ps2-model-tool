#include "player.h"

#include <libpad.h>
#include <kernel.h>

#include <math.h>
#include <string.h>

#define PLAYER_PAD_PORT 0
#define PLAYER_PAD_SLOT 0

#define PLAYER_EYE_HEIGHT 1.65f

#define PLAYER_DEADZONE 0.18f

#define PLAYER_MIN_PITCH -1.45f
#define PLAYER_MAX_PITCH  1.45f

#define PLAYER_MAX_DELTA 0.1f

static unsigned char pad_buffer[256] __attribute__((aligned(64)));

static struct padButtonStatus pad_status;

static int pad_open;
static int pad_analog_configured;
static int debug_toggle_edge;
static int debug_toggle_down;


static float clampf(float value, float minimum, float maximum)
{
    if (value < minimum)
        return minimum;

    if (value > maximum)
        return maximum;

    return value;
}


static float normalize_axis(unsigned char value)
{
    float axis = ((float)value - 128.0f) / 127.0f;

    if (fabsf(axis) < PLAYER_DEADZONE)
        return 0.0f;

    if (axis > 0.0f)
        axis = (axis - PLAYER_DEADZONE) / (1.0f - PLAYER_DEADZONE);
    else
        axis = (axis + PLAYER_DEADZONE) / (1.0f - PLAYER_DEADZONE);

    return clampf(axis, -1.0f, 1.0f);
}


int player_init(GrPlayer *player, float x, float y, float z)
{
    if (!player)
        return 0;

    (void)y;

    memset(player, 0, sizeof(*player));

    player->position[0] = x;
    player->position[1] = PLAYER_EYE_HEIGHT;
    player->position[2] = z;

    player->yaw = 0.0f;
    player->pitch = 0.0f;

    player->move_speed = 2.2f;
    player->look_sensitivity = 2.5f;

    player->pad_ready = 0;

    pad_open = 0;
    pad_analog_configured = 0;
    debug_toggle_edge = 0;
    debug_toggle_down = 0;

    if (padInit(0) != 1)
        return 0;

    if (!padPortOpen(PLAYER_PAD_PORT, PLAYER_PAD_SLOT, pad_buffer))
    {
        padEnd();
        return 0;
    }

    pad_open = 1;
    player->pad_ready = 1;

    return 1;
}


void player_get_forward(const GrPlayer *player, float direction[3])
{
    float cos_pitch;
    
    if (!player || !direction)
        return;

    cos_pitch = cosf(player->pitch);

    direction[0] = sinf(player->yaw) * cos_pitch;
    direction[1] = sinf(player->pitch);
    direction[2] = -cosf(player->yaw) * cos_pitch;
}


void player_update(GrPlayer *player, float delta_seconds)
{
    float move_x;
    float move_y;

    float look_x;
    float look_y;

    float forward_x;
    float forward_z;

    float right_x;
    float right_z;

    float move_length;

    if (!player || !player->pad_ready || !pad_open)
        return;

    if (!isfinite(delta_seconds) || delta_seconds <= 0.0f)
        return;

    delta_seconds = clampf(delta_seconds, 0.0f, PLAYER_MAX_DELTA);

    if (padGetState(PLAYER_PAD_PORT, PLAYER_PAD_SLOT) != PAD_STATE_STABLE)
        return;

    if (!pad_analog_configured)
    {
        padSetMainMode(
            PLAYER_PAD_PORT,
            PLAYER_PAD_SLOT,
            PAD_MMODE_DUALSHOCK,
            PAD_MMODE_LOCK
        );

        pad_analog_configured = 1;
        return;
    }

    if (!padRead(PLAYER_PAD_PORT, PLAYER_PAD_SLOT, &pad_status))
        return;

    {
        int r2_down = (pad_status.btns & PAD_R2) == 0;
        debug_toggle_edge = r2_down && !debug_toggle_down;
        debug_toggle_down = r2_down;
    }

    move_x = normalize_axis(pad_status.ljoy_h);
    move_y = -normalize_axis(pad_status.ljoy_v);

    look_x = normalize_axis(pad_status.rjoy_h);
    look_y = -normalize_axis(pad_status.rjoy_v);

    player->yaw += look_x * player->look_sensitivity * delta_seconds;

    player->pitch += look_y * player->look_sensitivity * delta_seconds;

    player->pitch = clampf(
        player->pitch,
        PLAYER_MIN_PITCH,
        PLAYER_MAX_PITCH
    );

    forward_x = sinf(player->yaw);
    forward_z = -cosf(player->yaw);

    right_x = cosf(player->yaw);
    right_z = sinf(player->yaw);

    move_length = sqrtf(move_x * move_x + move_y * move_y);

    if (move_length > 1.0f)
    {
        move_x /= move_length;
        move_y /= move_length;
    }

    player->position[0] +=
        (right_x * move_x + forward_x * move_y) *
        player->move_speed * delta_seconds;

    player->position[2] +=
        (right_z * move_x + forward_z * move_y) *
        player->move_speed * delta_seconds;

    player->position[1] = PLAYER_EYE_HEIGHT;
}


int player_debug_toggle_pressed(void)
{
    int pressed = debug_toggle_edge;
    debug_toggle_edge = 0;
    return pressed;
}


void player_shutdown(void)
{
    if (pad_open)
    {
        padPortClose(PLAYER_PAD_PORT, PLAYER_PAD_SLOT);
        pad_open = 0;
    }

    padEnd();
}
