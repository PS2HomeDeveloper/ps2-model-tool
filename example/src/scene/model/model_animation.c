#include "model.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

static uint32_t read_u32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static float read_f32(const unsigned char *p)
{
    uint32_t bits = read_u32(p);
    float value;
    memcpy(&value, &bits, 4);
    return value;
}

static void affine_multiply(float *out, const float *a, const float *b)
{
    unsigned row, col;
    for (row = 0; row < 3; ++row) {
        for (col = 0; col < 3; ++col)
            out[row * 4 + col] = a[row * 4] * b[col] +
                a[row * 4 + 1] * b[4 + col] + a[row * 4 + 2] * b[8 + col];
        out[row * 4 + 3] = a[row * 4] * b[3] + a[row * 4 + 1] * b[7] +
                           a[row * 4 + 2] * b[11] + a[row * 4 + 3];
    }
}

static void normal_matrix(float *out, const float *a)
{
    float c[9], det, inv;
    unsigned i;
    c[0] = a[5] * a[10] - a[6] * a[9];
    c[1] = a[6] * a[8] - a[4] * a[10];
    c[2] = a[4] * a[9] - a[5] * a[8];
    c[3] = a[2] * a[9] - a[1] * a[10];
    c[4] = a[0] * a[10] - a[2] * a[8];
    c[5] = a[1] * a[8] - a[0] * a[9];
    c[6] = a[1] * a[6] - a[2] * a[5];
    c[7] = a[2] * a[4] - a[0] * a[6];
    c[8] = a[0] * a[5] - a[1] * a[4];
    det = a[0] * c[0] + a[1] * c[1] + a[2] * c[2];
    if (fabsf(det) < 1.0e-20f) {
        for (i = 0; i < 9; ++i) out[i] = a[(i / 3) * 4 + i % 3];
        return;
    }
    inv = 1.0f / det;
    for (i = 0; i < 9; ++i) out[i] = c[i] * inv;
}

void model_update(GrModel *m, float delta_seconds)
{
    uint32_t a, b, i, j;
    float fraction = 0;
    if (!m || !m->vertices || !m->frame_count) return;
    if (!m->animation) {
        for (i = 0; i < m->vertex_count; ++i) {
            const GrModelVertex *v = &m->source_vertices[i];
            GrRenderVertex *out = &m->vertices[i];
            out->x = v->position[0]; out->y = v->position[1]; out->z = v->position[2];
            out->nx = v->normal[0]; out->ny = v->normal[1]; out->nz = v->normal[2];
            out->u = v->uv[0]; out->v = v->uv[1];
        }
        return;
    }
    if (isfinite(delta_seconds) && delta_seconds > 0) m->time += delta_seconds;
    a = b = 0;
    if (m->clip_count > 1 && m->duration > 0 && m->time >= m->duration)
        model_set_clip(m, (m->clip_index + 1) % m->clip_count);
    if (m->duration > 0) {
        float frame;
        m->time = fmodf(m->time, m->duration);
        frame = m->time * m->fps;
        a = (uint32_t)frame;
        if (a >= m->frame_count - 1) a = m->frame_count - 2;
        b = a + 1;
        fraction = frame - (float)a;
    }
    for (i = 0; i < m->bone_count; ++i) {
        const unsigned char *pa = m->animation + (a * m->bone_count + i) * 48;
        const unsigned char *pb = m->animation + (b * m->bone_count + i) * 48;
        float world[12];
        for (j = 0; j < 12; ++j) {
            float va = read_f32(pa + j * 4);
            float vb = read_f32(pb + j * 4);
            world[j] = va + (vb - va) * fraction;
        }
        affine_multiply(m->palette + i * 12, world, m->inverse_bind + i * 12);
        normal_matrix(m->normal_palette + i * 9, m->palette + i * 12);
    }
    for (i = 0; i < m->vertex_count; ++i) {
        const GrModelVertex *v = &m->source_vertices[i];
        GrRenderVertex *out = &m->vertices[i];
        float position[3] = {0, 0, 0}, normal[3] = {0, 0, 0};
        float length;
        unsigned influence, row;
        for (influence = 0; influence < 4; ++influence) {
            const float *p, *n;
            float weight;
            if (!v->weights[influence]) continue;
            p = m->palette + v->joints[influence] * 12;
            n = m->normal_palette + v->joints[influence] * 9;
            weight = (float)v->weights[influence] * (1.0f / 255.0f);
            for (row = 0; row < 3; ++row) {
                position[row] += weight * (p[row * 4] * v->position[0] +
                    p[row * 4 + 1] * v->position[1] +
                    p[row * 4 + 2] * v->position[2] + p[row * 4 + 3]);
                normal[row] += weight * (n[row * 3] * v->normal[0] +
                    n[row * 3 + 1] * v->normal[1] + n[row * 3 + 2] * v->normal[2]);
            }
        }
        length = sqrtf(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
        if (length > 1.0e-12f) {
            float inv = 1.0f / length;
            normal[0] *= inv;
            normal[1] *= inv;
            normal[2] *= inv;
        } else {
            normal[0] = normal[2] = 0;
            normal[1] = 1;
        }
        out->x = position[0]; out->y = position[1]; out->z = position[2];
        out->nx = normal[0]; out->ny = normal[1]; out->nz = normal[2];
        out->u = v->uv[0]; out->v = v->uv[1];
    }
}
