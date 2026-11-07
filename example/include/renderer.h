#ifndef GR_RENDERER_H
#define GR_RENDERER_H

#include <stdint.h>
#include <gsKit.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    GR_RENDER_OK = 0,
    GR_RENDER_ERROR = -1,
    GR_RENDER_TRANSPARENT = 1u,
    GR_RENDER_DOUBLE_SIDED = 2u
};

typedef struct GrRenderVertex {
    float x, y, z, nx, ny, nz, u, v;
} GrRenderVertex;

typedef struct GrRenderPart {
    uint32_t first_index, index_count, material_index, flags;
} GrRenderPart;

typedef struct GrRenderMaterial {
    float rgba[4];
    uint32_t flags;
    const unsigned char *texture_data;
    uint32_t texture_size;
    const char *texture_name;
} GrRenderMaterial;

int renderer_init(void);
GSGLOBAL *renderer_get_global(void);
int renderer_load_materials(const GrRenderMaterial *materials, unsigned count);
void renderer_set_camera(float eye_x, float eye_y, float eye_z,
                         float target_x, float target_y, float target_z);
void renderer_begin(void);
void renderer_begin_overlay(void);
void renderer_end_overlay(void);
void renderer_draw(const GrRenderVertex *vertices, unsigned vertex_count,
                   const uint32_t *indices, unsigned index_count,
                   const GrRenderPart *parts, unsigned part_count);
void renderer_end(void);
float renderer_frame_seconds(void);
const char *renderer_error(void);
void renderer_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif
