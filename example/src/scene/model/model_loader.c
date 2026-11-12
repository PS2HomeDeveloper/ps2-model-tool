#include "model.h"
#include "core/gr_limits.h"
#include "model_internal.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t read_u32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint16_t read_u16(const unsigned char *p)
{
    return (uint16_t)((uint16_t)p[0] | (uint16_t)p[1] << 8);
}

static float read_f32(const unsigned char *p)
{
    uint32_t bits = read_u32(p);
    float value;
    memcpy(&value, &bits, 4);
    return value;
}

static int fail(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(gr_model_error, sizeof(gr_model_error), format, args);
    va_end(args);
    return 0;
}

static int valid_header(const GrAsset *asset, const char *magic, uint32_t minimum, uint32_t version)
{
    if (!asset || !asset->data || asset->size < minimum)
        return fail("Missing or truncated %.4s resource", magic);
    if (memcmp(asset->data, magic, 4) || read_u32(asset->data + 4) != version)
        return fail("Invalid %.4s format/version", magic);
    if (read_u32(asset->data + 8) != asset->size)
        return fail("Invalid %.4s file size", magic);
    return 1;
}

static int valid_mesh_header(const GrAsset *asset, uint32_t *version)
{
    uint32_t v;
    if (!asset || !asset->data || asset->size < 64) return fail("Missing or truncated GMSH resource");
    v = read_u32(asset->data + 4);
    if (memcmp(asset->data, "GMSH", 4) || (v != 1 && v != 2)) return fail("Invalid GMSH format/version");
    if (read_u32(asset->data + 8) != asset->size) return fail("Invalid GMSH file size");
    *version = v;
    return 1;
}

static int valid_section(const GrAsset *asset, uint32_t offset, uint32_t count,
                         uint32_t stride, uint32_t minimum)
{
    if (offset < minimum || (offset & 15) || offset > asset->size ||
        count > (asset->size - offset) / stride)
        return fail("Invalid section in %s", asset->name);
    return 1;
}

static int finite_values(const unsigned char *data, uint32_t count)
{
    uint32_t i;
    for (i = 0; i < count; ++i)
        if (!isfinite(read_f32(data + i * 4))) return 0;
    return 1;
}

int model_set_clip(GrModel *m, uint32_t index)
{
    const unsigned char *e;
    if (!m || !m->clip_table || index >= m->clip_count) return 0;
    e = m->clip_table + index * 64;
    memcpy(m->clip_name, e, 32);
    m->clip_name[32] = 0;
    m->frame_count = read_u32(e + 32);
    m->fps = read_f32(e + 36);
    m->duration = read_f32(e + 40);
    m->animation = m->animation_file + read_u32(e + 44);
    m->clip_index = index;
    m->time = 0;
    return 1;
}

int model_load(GrModel *out, const GrAsset *mesh, const GrAsset *skeleton,
               const GrAsset *animation, const GrAsset *material)
{
    GrModel m;
    uint32_t vo, io, po, bo, ao, mo, i, j, mesh_version, index_stride;
    if (!out) return fail("Null model destination");
    memset(&m, 0, sizeof(m));
    gr_model_error[0] = 0;
    if (!valid_mesh_header(mesh, &mesh_version) ||
        !valid_header(skeleton, "GSKL", 32, 1) ||
        (animation && !valid_header(animation, "GANM", 32, 2)) ||
        !valid_header(material, "GMAT", 32, 1)) return 0;
    m.vertex_count = read_u32(mesh->data + 12);
    m.index_count = read_u32(mesh->data + 16);
    m.part_count = read_u32(mesh->data + 20);
    m.bone_count = read_u32(skeleton->data + 12);
    m.material_count = read_u32(material->data + 12);
    index_stride = mesh_version == 2 ? 2 : 4;
    m.clip_count = animation ? read_u32(animation->data + 16) : 0;
    if (!m.vertex_count || m.vertex_count > GR_MAX_VERTICES || !m.index_count ||
        m.index_count > GR_MAX_INDICES || m.index_count % 3 || !m.part_count ||
        m.part_count > GR_MAX_PARTS || !m.bone_count || m.bone_count > GR_MAX_BONES ||
        !m.material_count || m.material_count > GR_MAX_MATERIALS ||
        m.clip_count > GR_MAX_CLIPS || (animation && read_u32(animation->data + 12) != m.bone_count) ||
        (animation && animation->size > GR_MAX_ANIMATION_BYTES))
        return fail("Resource exceeds prototype budgets or has inconsistent counts");
    vo = read_u32(mesh->data + 24);
    io = read_u32(mesh->data + 28);
    po = read_u32(mesh->data + 32);
    bo = read_u32(skeleton->data + 16);
    ao = animation ? read_u32(animation->data + 20) : 0;
    mo = read_u32(material->data + 16);
    if (!valid_section(mesh, vo, m.vertex_count, 48, 64) ||
        !valid_section(mesh, io, m.index_count, index_stride, 64) ||
        !valid_section(mesh, po, m.part_count, 16, 64) ||
        !valid_section(skeleton, bo, m.bone_count, 128, 32) ||
        (animation && !valid_section(animation, ao, m.clip_count, 64, 32)) ||
        !valid_section(material, mo, m.material_count, 128, 32)) return 0;
    if (!finite_values(mesh->data + 40, 6))
        return fail("Non-finite values in model resources");
    m.clip_table = animation ? animation->data + ao : NULL;
    m.animation_file = animation ? animation->data : NULL;
    m.animation_size = animation ? animation->size : 0;
    for (i = 0; i < m.clip_count; ++i) {
        const unsigned char *e = m.clip_table + i * 64;
        uint32_t frames = read_u32(e + 32);
        float fps = read_f32(e + 36), duration = read_f32(e + 40);
        if (!frames || frames > GR_MAX_FRAMES || !isfinite(fps) || fps < GR_MIN_FPS ||
            fps > GR_MAX_FPS || !isfinite(duration) || duration < 0 ||
            fabsf(duration - (float)(frames - 1) / fps) > 0.001f)
            return fail("Invalid animation clip %u timing", (unsigned)i);
        if (!valid_section(animation, read_u32(e + 44), frames, m.bone_count * 48, 32) ||
            !finite_values(animation->data + read_u32(e + 44), frames * m.bone_count * 12))
            return fail("Invalid animation clip %u data", (unsigned)i);
    }
    if (animation) model_set_clip(&m, 0);
    else m.frame_count = 1;
    m.source_vertices = calloc(m.vertex_count, sizeof(*m.source_vertices));
    m.vertices = calloc(m.vertex_count, sizeof(*m.vertices));
    m.indices = calloc(m.index_count, sizeof(*m.indices));
    m.parts = calloc(m.part_count, sizeof(*m.parts));
    m.materials = calloc(m.material_count, sizeof(*m.materials));
    m.inverse_bind = calloc(m.bone_count * 12, sizeof(float));
    m.palette = calloc(m.bone_count * 12, sizeof(float));
    m.normal_palette = calloc(m.bone_count * 9, sizeof(float));
    if (!m.source_vertices || !m.vertices || !m.indices || !m.parts ||
        !m.materials || !m.inverse_bind || !m.palette || !m.normal_palette) {
        fail("Not enough EE memory for model");
        goto error;
    }
    for (i = 0; i < m.bone_count; ++i) {
        const unsigned char *p = skeleton->data + bo + i * 128;
        int32_t parent = (int32_t)read_u32(p);
        if (parent < -1 || parent >= (int32_t)i || !finite_values(p + 72, 12)) {
            fail("Invalid bone hierarchy or inverse bind matrix at %u", (unsigned)i);
            goto error;
        }
        for (j = 0; j < 12; ++j) m.inverse_bind[i * 12 + j] = read_f32(p + 72 + j * 4);
    }
    for (i = 0; i < m.vertex_count; ++i) {
        const unsigned char *p = mesh->data + vo + i * 48;
        GrModelVertex *v = &m.source_vertices[i];
        unsigned weight_sum = 0;
        if (!finite_values(p, 8)) {
            fail("Invalid vertex at %u", (unsigned)i);
            goto error;
        }
        for (j = 0; j < 3; ++j) {
            v->position[j] = read_f32(p + j * 4);
            v->normal[j] = read_f32(p + 12 + j * 4);
        }
        v->uv[0] = read_f32(p + 24);
        v->uv[1] = read_f32(p + 28);
        for (j = 0; j < 4; ++j) {
            v->joints[j] = read_u16(p + 32 + j * 2);
            v->weights[j] = p[40 + j];
            weight_sum += v->weights[j];
            if (v->joints[j] >= m.bone_count) {
                fail("Vertex references an invalid bone");
                goto error;
            }
        }
        if (weight_sum != 255) {
            fail("Vertex weights do not sum to 255");
            goto error;
        }
    }
    for (i = 0; i < m.index_count; ++i) {
        m.indices[i] = mesh_version == 2 ? read_u16(mesh->data + io + i * 2) : read_u32(mesh->data + io + i * 4);
        if (m.indices[i] >= m.vertex_count) {
            fail("Invalid mesh index");
            goto error;
        }
    }
    for (i = 0; i < m.part_count; ++i) {
        const unsigned char *p = mesh->data + po + i * 16;
        GrRenderPart *part = &m.parts[i];
        part->first_index = read_u32(p);
        part->index_count = read_u32(p + 4);
        part->material_index = read_u32(p + 8);
        part->flags = read_u32(p + 12);
        if (part->first_index > m.index_count ||
            part->index_count > m.index_count - part->first_index ||
            part->first_index % 3 || part->index_count % 3 ||
            part->material_index >= m.material_count) {
            fail("Invalid mesh part");
            goto error;
        }
    }
    for (i = 0; i < m.material_count; ++i) {
        const unsigned char *p = material->data + mo + i * 128;
        GrRenderMaterial *mat = &m.materials[i];
        const char *texture_name = (const char *)p + 48;
        const GrAsset *texture;
        if (!memchr(p, 0, 48) || !memchr(p + 48, 0, 48) || !finite_values(p + 96, 4)) {
            fail("Invalid material record");
            goto error;
        }
        for (j = 0; j < 4; ++j) {
            mat->rgba[j] = read_f32(p + 96 + j * 4);
            if (mat->rgba[j] < 0 || mat->rgba[j] > 1) {
                fail("Material color outside range 0..1");
                goto error;
            }
        }
        mat->flags = read_u32(p + 112);
        mat->texture_name = texture_name;
        if (!*texture_name) continue;
        texture = assets_find(texture_name);
        if (!texture) {
            fail("Embedded texture missing: %.47s", texture_name);
            goto error;
        }
        mat->texture_data = texture->data;
        mat->texture_size = texture->size;
    }
    *out = m;
    model_update(out, 0);
    return 1;
error:
    model_destroy(&m);
    return 0;
}
