#ifndef GRANNY_MODEL_H
#define GRANNY_MODEL_H

#include "assets.h"
#include "renderer.h"
#include <stdint.h>

typedef struct {
    float position[3];
    float normal[3];
    float uv[2];
    uint16_t joints[4];
    uint8_t weights[4];
} GrModelVertex;

typedef struct {
    GrModelVertex *source_vertices;
    GrRenderVertex *vertices;
    uint32_t *indices;
    GrRenderPart *parts;
    GrRenderMaterial *materials;
    float *inverse_bind;
    float *palette;
    float *normal_palette;
    const unsigned char *animation;
    const unsigned char *clip_table;
    const unsigned char *animation_file;
    uint32_t animation_size;
    uint32_t clip_count;
    uint32_t clip_index;
    uint32_t vertex_count;
    uint32_t index_count;
    uint32_t part_count;
    uint32_t material_count;
    uint32_t bone_count;
    uint32_t frame_count;
    float fps;
    float duration;
    float time;
    char clip_name[33];
} GrModel;

int model_load(GrModel *model, const GrAsset *mesh, const GrAsset *skeleton,
               const GrAsset *animation, const GrAsset *material);
int model_set_clip(GrModel *model, uint32_t index);
void model_update(GrModel *model, float delta_seconds);
void model_destroy(GrModel *model);
const char *model_error(void);

#endif
