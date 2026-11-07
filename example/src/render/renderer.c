#include "renderer.h"
#include "render/tim2.h"

#include <gsKit.h>
#include <gsInline.h>
#include <dmaKit.h>
#include <kernel.h>
#include <timer.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GR_NEAR 0.10f
#define GR_FAR 60.0f
#define GR_TAN_Y 0.41421356237f
#define GR_TAN_X (GR_TAN_Y * (4.0f / 3.0f))
#define GR_NO_TEXTURE UINT_MAX

typedef struct GrCameraVertex {
    float x, y, z, nx, ny, nz, u, v;
} GrCameraVertex;

typedef struct GrTexture {
    GSTEXTURE gs;
    GrTim2Image image;
    const unsigned char *source;
    uint32_t source_size;
} GrTexture;

typedef struct GrMaterial {
    float rgba[4];
    uint32_t flags;
    unsigned texture;
} GrMaterial;

typedef struct GrTriangle {
    uint32_t indices[3];
    unsigned material;
    uint32_t flags;
    float depth;
    size_t order;
} GrTriangle;

static GSGLOBAL *graphics;
static GrMaterial *materials;
static GrTexture *textures;
static unsigned material_count, texture_count;
static GrCameraVertex *camera_vertices;
static size_t camera_capacity;
static GrTriangle *transparent_triangles;
static size_t transparent_capacity;
static float eye[3] = {0.0f, 1.35f, 4.2f};
static float view_right[3] = {1.0f, 0.0f, 0.0f};
static float view_up[3] = {0.0f, 1.0f, 0.0f};
static float view_forward[3] = {0.0f, 0.0f, -1.0f};
static const GrMaterial fallback_material = {{0.82f, 0.82f, 0.82f, 1.0f}, 0, GR_NO_TEXTURE};
static int frame_open;
static int overlay_open;
static unsigned bound_texture = GR_NO_TEXTURE;
static u64 last_frame_time;
static float frame_seconds = 1.0f / 60.0f;
static char last_error[256];

static void fail(const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(last_error, sizeof(last_error), format, arguments);
    va_end(arguments);
}


GSGLOBAL *renderer_get_global(void)
{
    return graphics;
}

static float limit(float value, float low, float high)
{
    return value < low ? low : value > high ? high : value;
}

static int normalize(float v[3])
{
    float length = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (!isfinite(length) || length < 0.000001f)
        return 0;
    v[0] /= length;
    v[1] /= length;
    v[2] /= length;
    return 1;
}

void renderer_set_camera(float ex, float ey, float ez, float tx, float ty, float tz)
{
    float forward[3] = {tx - ex, ty - ey, tz - ez};
    float right[3], up[3];
    if (!isfinite(ex) || !isfinite(ey) || !isfinite(ez) || !normalize(forward)) {
        fail("Camera eye and target must be finite and distinct");
        return;
    }
    right[0] = -forward[2];
    right[1] = 0.0f;
    right[2] = forward[0];
    if (!normalize(right)) {
        right[0] = 1.0f;
        right[1] = 0.0f;
        right[2] = 0.0f;
    }
    up[0] = right[1] * forward[2] - right[2] * forward[1];
    up[1] = right[2] * forward[0] - right[0] * forward[2];
    up[2] = right[0] * forward[1] - right[1] * forward[0];
    eye[0] = ex;
    eye[1] = ey;
    eye[2] = ez;
    memcpy(view_forward, forward, sizeof(forward));
    memcpy(view_right, right, sizeof(right));
    memcpy(view_up, up, sizeof(up));
}

static void register_write(u64 value, u64 reg)
{
    u64 *packet = gsKit_heap_alloc(graphics, 1, 16, GIF_AD);
    *packet++ = GIF_TAG_AD(1);
    *packet++ = GIF_AD;
    *packet++ = value;
    *packet = reg;
}

static void depth_write(int enabled)
{
    register_write(GS_SETREG_ZBUF(graphics->ZBuffer / 8192, graphics->PSMZ,
                                  enabled ? 0 : 1),
                   GS_ZBUF_1 + graphics->PrimContext);
}

static void queue_space(void)
{
    GSQUEUE *queue = graphics->CurQueue;
    if ((unsigned char *)queue->pool_max[queue->dbuf] -
        (unsigned char *)queue->pool_cur < 8192)
        gsKit_queue_exec(graphics);
}

static void wait_for_graphics(void)
{
    if (graphics && !graphics->FirstFrame) {
        dmaKit_wait_fast();
        gsKit_finish();
    }
}

static void release_materials(void)
{
    unsigned i;
    for (i = 0; i < texture_count; ++i) {
        gsKit_TexManager_free(graphics, &textures[i].gs);
        gr_tim2_free(&textures[i].image);
    }
    free(textures);
    free(materials);
    textures = NULL;
    materials = NULL;
    texture_count = material_count = 0;
    bound_texture = GR_NO_TEXTURE;
}

int renderer_init(void)
{
    if (graphics)
        return GR_RENDER_OK;
    last_error[0] = '\0';
    dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC,
                D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);
    dmaKit_chan_init(DMA_CHANNEL_GIF);
    graphics = gsKit_init_global_custom(1024 * 1024, 16 * 1024);
    if (!graphics) {
        fail("gsKit global allocation failed");
        return GR_RENDER_ERROR;
    }
    graphics->Width = 640;
    graphics->Height = 448;
    graphics->Aspect = GS_ASPECT_4_3;
    graphics->PSM = GS_PSM_CT32;
    graphics->PSMZ = GS_PSMZ_16;
    graphics->DoubleBuffering = GS_SETTING_ON;
    graphics->ZBuffering = GS_SETTING_ON;
    graphics->Dithering = GS_SETTING_OFF;
    graphics->PrimAlphaEnable = GS_SETTING_ON;
    graphics->PrimAAEnable = GS_SETTING_OFF;
    graphics->Test->ATE = GS_SETTING_ON;
    graphics->Test->ATST = 6;
    graphics->Test->AREF = 0;
    graphics->Test->AFAIL = 0;
    graphics->Test->DATE = GS_SETTING_OFF;
    graphics->Test->ZTE = GS_SETTING_ON;
    graphics->Test->ZTST = 2;
    gsKit_init_screen(graphics);
    gsKit_mode_switch(graphics, GS_ONESHOT);
    gsKit_TexManager_init(graphics);
    gsKit_TexManager_setmode(graphics, ETM_INLINE);
    if (graphics->CurrentPointer >= 4u * 1024u * 1024u || !graphics->ZBuffer) {
        fail("Framebuffer and depth buffer exceed GS VRAM");
        gsKit_deinit_global(graphics);
        graphics = NULL;
        return GR_RENDER_ERROR;
    }
    register_write(GS_SETREG_TEXA(0, 0, 0x80), GS_TEXA);
    gsKit_set_clamp(graphics, GS_CMODE_CLAMP);
    gsKit_set_texfilter(graphics, GS_FILTER_LINEAR);
    gsKit_set_primalpha(graphics, GS_SETREG_ALPHA(0, 2, 2, 2, 0x80), 0);
    renderer_set_camera(4.2f, 1.35f, 0.0f, 0.0f, 1.0f, 0.0f);
    frame_seconds = graphics->Mode == GS_MODE_PAL ? 1.0f / 50.0f : 1.0f / 60.0f;
    last_frame_time = 0;
    return GR_RENDER_OK;
}

int renderer_load_materials(const GrRenderMaterial *input, unsigned count)
{
    GrMaterial *new_materials;
    GrTexture *new_textures;
    unsigned new_texture_count = 0, i, j;
    char texture_error[160];
    if (!graphics || frame_open || (count && !input)) {
        fail("Load materials after renderer_init and outside a frame");
        return GR_RENDER_ERROR;
    }
    if (count > SIZE_MAX / sizeof(*new_materials) || count > SIZE_MAX / sizeof(*new_textures)) {
        fail("Material allocation size overflow");
        return GR_RENDER_ERROR;
    }
    new_materials = count ? calloc(count, sizeof(*new_materials)) : NULL;
    new_textures = count ? calloc(count, sizeof(*new_textures)) : NULL;
    if (count && (!new_materials || !new_textures)) {
        free(new_materials);
        free(new_textures);
        fail("Material allocation failed");
        return GR_RENDER_ERROR;
    }
    for (i = 0; i < count; ++i) {
        for (j = 0; j < 4; ++j) {
            if (!isfinite(input[i].rgba[j])) {
                fail("Material %u has a non-finite diffuse color", i);
                goto bad;
            }
            new_materials[i].rgba[j] = limit(input[i].rgba[j], 0.0f, 1.0f);
        }
        new_materials[i].flags = input[i].flags;
        if (new_materials[i].rgba[3] < 0.999f)
            new_materials[i].flags |= GR_RENDER_TRANSPARENT;
        new_materials[i].texture = GR_NO_TEXTURE;
        if (!input[i].texture_data && !input[i].texture_size) {
            if (input[i].texture_name && input[i].texture_name[0]) {
                fail("Texture %s is not present in the embedded asset table", input[i].texture_name);
                goto bad;
            }
            continue;
        }
        if (!input[i].texture_data || !input[i].texture_size) {
            fail("Material %u has an incomplete embedded texture", i);
            goto bad;
        }
        for (j = 0; j < new_texture_count; ++j)
            if (new_textures[j].source == input[i].texture_data &&
                new_textures[j].source_size == input[i].texture_size)
                break;
        if (j == new_texture_count) {
            GrTexture *texture = &new_textures[j];
            if (gr_tim2_load(input[i].texture_data, input[i].texture_size,
                             &texture->image, texture_error, sizeof(texture_error)) < 0) {
                fail("Texture %s: %s", input[i].texture_name ? input[i].texture_name : "(unnamed)", texture_error);
                goto bad;
            }
            ++new_texture_count;
            texture->source = input[i].texture_data;
            texture->source_size = input[i].texture_size;
            texture->gs.Width = texture->image.width;
            texture->gs.Height = texture->image.height;
            texture->gs.PSM = texture->image.psm;
            texture->gs.Mem = (u32 *)texture->image.pixels;
            texture->gs.Filter = GS_FILTER_LINEAR;
            texture->gs.Delayed = 1;
            if (gsKit_texture_size(texture->gs.Width, texture->gs.Height, texture->gs.PSM) >
                4u * 1024u * 1024u - graphics->CurrentPointer) {
                fail("Texture %s cannot fit in the GS texture cache", input[i].texture_name ? input[i].texture_name : "(unnamed)");
                goto bad;
            }
        }
        new_materials[i].texture = j;
    }
    wait_for_graphics();
    release_materials();
    materials = new_materials;
    textures = new_textures;
    material_count = count;
    texture_count = new_texture_count;
    last_error[0] = '\0';
    return GR_RENDER_OK;

bad:
    for (i = 0; i < new_texture_count; ++i)
        gr_tim2_free(&new_textures[i].image);
    free(new_textures);
    free(new_materials);
    return GR_RENDER_ERROR;
}

static GrCameraVertex camera_vertex(const GrRenderVertex *vertex)
{
    GrCameraVertex result;
    float x = vertex->x - eye[0], y = vertex->y - eye[1], z = vertex->z - eye[2];
    result.x = x * view_right[0] + y * view_right[1] + z * view_right[2];
    result.y = x * view_up[0] + y * view_up[1] + z * view_up[2];
    result.z = x * view_forward[0] + y * view_forward[1] + z * view_forward[2];
    result.nx = vertex->nx;
    result.ny = vertex->ny;
    result.nz = vertex->nz;
    result.u = vertex->u;
    result.v = 1.0f - vertex->v;
    return result;
}

static float plane_distance(const GrCameraVertex *v, unsigned plane)
{
    switch (plane) {
        case 0: return v->z - GR_NEAR;
        case 1: return GR_FAR - v->z;
        case 2: return v->x + v->z * GR_TAN_X;
        case 3: return v->z * GR_TAN_X - v->x;
        case 4: return v->y + v->z * GR_TAN_Y;
        default: return v->z * GR_TAN_Y - v->y;
    }
}

static GrCameraVertex interpolate(const GrCameraVertex *a, const GrCameraVertex *b, float t)
{
    GrCameraVertex r;
    r.x = a->x + (b->x - a->x) * t;
    r.y = a->y + (b->y - a->y) * t;
    r.z = a->z + (b->z - a->z) * t;
    r.nx = a->nx + (b->nx - a->nx) * t;
    r.ny = a->ny + (b->ny - a->ny) * t;
    r.nz = a->nz + (b->nz - a->nz) * t;
    r.u = a->u + (b->u - a->u) * t;
    r.v = a->v + (b->v - a->v) * t;
    return r;
}

static unsigned clip_triangle(const GrCameraVertex input[3], GrCameraVertex output[16])
{
    GrCameraVertex a[16], b[16];
    unsigned count = 3, plane, i;
    memcpy(a, input, 3 * sizeof(*a));
    for (plane = 0; plane < 6 && count; ++plane) {
        unsigned next_count = 0;
        GrCameraVertex previous = a[count - 1];
        float previous_distance = plane_distance(&previous, plane);
        for (i = 0; i < count; ++i) {
            GrCameraVertex current = a[i];
            float distance = plane_distance(&current, plane);
            if ((previous_distance >= 0.0f) != (distance >= 0.0f))
                b[next_count++] = interpolate(&previous, &current,
                    previous_distance / (previous_distance - distance));
            if (distance >= 0.0f)
                b[next_count++] = current;
            previous = current;
            previous_distance = distance;
        }
        count = next_count;
        memcpy(a, b, count * sizeof(*a));
    }
    memcpy(output, a, count * sizeof(*a));
    return count;
}

static int valid_vertex(const GrCameraVertex *v)
{
    return isfinite(v->x) && isfinite(v->y) && isfinite(v->z) &&
           isfinite(v->nx) && isfinite(v->ny) && isfinite(v->nz) &&
           isfinite(v->u) && isfinite(v->v);
}

static void project(const GrCameraVertex *v, float *x, float *y, int *z)
{
    float reciprocal = 1.0f / v->z;
    float depth = (reciprocal - 1.0f / GR_FAR) / (1.0f / GR_NEAR - 1.0f / GR_FAR);
    *x = limit(320.0f * (1.0f + v->x * reciprocal / GR_TAN_X), 0.0f, 640.0f);
    *y = limit(224.0f * (1.0f - v->y * reciprocal / GR_TAN_Y), 0.0f, 448.0f);
    *z = 1 + (int)(limit(depth, 0.0f, 1.0f) * 65534.0f);
}

static gs_rgbaq vertex_color(const GrCameraVertex *v, const GrMaterial *material,
                            uint32_t flags, int textured, float q)
{
    float normal[3] = {v->nx, v->ny, v->nz};
    float diffuse = 0.0f, brightness, scale;
    unsigned char r, g, b, a;
    if (normalize(normal)) {
        diffuse = normal[0] * 0.36514837f + normal[1] * 0.73029674f + normal[2] * 0.57735027f;
        if (flags & GR_RENDER_DOUBLE_SIDED)
            diffuse = fabsf(diffuse);
    }
    brightness = 0.35f + 0.65f * limit(diffuse, 0.0f, 1.0f);
    scale = textured ? 128.0f : 255.0f;
    if (flags & GR_RENDER_TRANSPARENT)
        scale *= material->rgba[3];
    r = (unsigned char)limit(material->rgba[0] * brightness * scale + 0.5f, 0.0f, 255.0f);
    g = (unsigned char)limit(material->rgba[1] * brightness * scale + 0.5f, 0.0f, 255.0f);
    b = (unsigned char)limit(material->rgba[2] * brightness * scale + 0.5f, 0.0f, 255.0f);
    a = (unsigned char)limit(material->rgba[3] * 128.0f + 0.5f, 0.0f, 128.0f);
    return color_to_RGBAQ(r, g, b, a, q);
}

static void emit_triangle(const GrCameraVertex vertices[3], const GrMaterial *material,
                          uint32_t flags)
{
    float x[3], y[3], area;
    int z[3];
    unsigned i, pass, passes;
    GSTEXTURE *texture = material->texture == GR_NO_TEXTURE ? NULL : &textures[material->texture].gs;
    GSPRIMSTQPOINT textured[3] __attribute__((aligned(16)));
    GSPRIMPOINT plain[3] __attribute__((aligned(16)));
    for (i = 0; i < 3; ++i) {
        if (!valid_vertex(&vertices[i]) || vertices[i].z <= 0.0f)
            return;
        project(&vertices[i], &x[i], &y[i], &z[i]);
    }
    area = (x[1] - x[0]) * (y[2] - y[0]) - (y[1] - y[0]) * (x[2] - x[0]);
    if (fabsf(area) < 0.001f || (!(flags & GR_RENDER_DOUBLE_SIDED) && area >= 0.0f))
        return;
    queue_space();
    if (texture && bound_texture != material->texture) {
        gsKit_TexManager_bind(graphics, texture);
        bound_texture = material->texture;
    }
    for (i = 0; i < 3; ++i) {
        float q = 1.0f / vertices[i].z;
        gs_rgbaq color = vertex_color(&vertices[i], material, flags, texture != NULL, q);
        if (texture) {
            textured[i].rgbaq = color;
            textured[i].stq = vertex_to_STQ(vertices[i].u * q, vertices[i].v * q);
            textured[i].xyz2 = vertex_to_XYZ2(graphics, x[i], y[i], z[i]);
        } else {
            plain[i].rgbaq = color;
            plain[i].xyz2 = vertex_to_XYZ2(graphics, x[i], y[i], z[i]);
        }
    }
    passes = flags & GR_RENDER_TRANSPARENT ? 2 : 1;
    for (pass = 0; pass < passes; ++pass) {
        if (passes == 2)
            gsKit_set_primalpha(graphics, pass == 0 ?
                GS_SETREG_ALPHA(2, 1, 0, 1, 0) : GS_SETREG_ALPHA(0, 2, 2, 1, 0x80), 0);
        else
            gsKit_set_primalpha(graphics, GS_SETREG_ALPHA(0, 2, 2, 2, 0x80), 0);
        if (texture)
            gsKit_prim_list_triangle_goraud_texture_stq_3d(graphics, texture, 3, textured);
        else
            gsKit_prim_list_triangle_gouraud_3d(graphics, 3, plain);
    }
}

static void draw_triangle(const uint32_t index[3], unsigned material_index, uint32_t flags)
{
    GrCameraVertex input[3], clipped[16], triangle[3];
    const GrMaterial *material = material_count ? &materials[material_index] : &fallback_material;
    unsigned count, i;
    for (i = 0; i < 3; ++i) {
        input[i] = camera_vertices[index[i]];
        if (!valid_vertex(&input[i]))
            return;
    }
    count = clip_triangle(input, clipped);
    for (i = 1; i + 1 < count; ++i) {
        triangle[0] = clipped[0];
        triangle[1] = clipped[i];
        triangle[2] = clipped[i + 1];
        emit_triangle(triangle, material, flags);
    }
}

static void grid_line(float x1, float z1, float x2, float z2, u64 color)
{
    GrRenderVertex world = {0};
    GrCameraVertex a, b;
    unsigned plane;
    float ax, ay, bx, by;
    int az, bz;
    world.x = x1;
    world.z = z1;
    a = camera_vertex(&world);
    world.x = x2;
    world.z = z2;
    b = camera_vertex(&world);
    for (plane = 0; plane < 6; ++plane) {
        float da = plane_distance(&a, plane), db = plane_distance(&b, plane);
        if (da < 0.0f && db < 0.0f)
            return;
        if ((da >= 0.0f) != (db >= 0.0f)) {
            GrCameraVertex intersection = interpolate(&a, &b, da / (da - db));
            if (da < 0.0f)
                a = intersection;
            else
                b = intersection;
        }
    }
    project(&a, &ax, &ay, &az);
    project(&b, &bx, &by, &bz);
    queue_space();
    gsKit_prim_line_3d(graphics, ax, ay, az, bx, by, bz, color);
}

void renderer_begin(void)
{
    u64 now;
    int i;
    if (!graphics || frame_open) {
        fail("renderer_begin requires initialized renderer and no open frame");
        return;
    }
    now = GetTimerSystemTime();
    if (last_frame_time && now > last_frame_time)
        frame_seconds = limit((float)(now - last_frame_time) / (float)kBUSCLK, 0.0001f, 0.25f);
    last_frame_time = now;
    frame_open = 1;
    overlay_open = 0;
    bound_texture = GR_NO_TEXTURE;
    graphics->PrimAlphaEnable = GS_SETTING_ON;
    queue_space();
    depth_write(1);
    gsKit_set_primalpha(graphics, GS_SETREG_ALPHA(0, 2, 2, 2, 0x80), 0);
    gsKit_clear(graphics, GS_SETREG_RGBAQ(24, 28, 35, 0x80, 0));
    gsKit_set_test(graphics, GS_ZTEST_ON);
    for (i = -10; i <= 10; ++i) {
        float p = i * 0.5f;
        u64 gray = GS_SETREG_RGBAQ(47, 54, 62, 0x80, 0);
        grid_line(p, -5.0f, p, 5.0f,
                  i == 0 ? GS_SETREG_RGBAQ(65, 87, 112, 0x80, 0) : gray);
        grid_line(-5.0f, p, 5.0f, p,
                  i == 0 ? GS_SETREG_RGBAQ(112, 66, 64, 0x80, 0) : gray);
    }
}

void renderer_begin_overlay(void)
{
    if (!graphics || !frame_open || overlay_open) {
        fail("renderer_begin_overlay requires an open frame and no active overlay");
        return;
    }
    queue_space();
    depth_write(0);
    gsKit_set_test(graphics, GS_ZTEST_OFF);
    overlay_open = 1;
}

void renderer_end_overlay(void)
{
    if (!graphics || !frame_open || !overlay_open) {
        fail("renderer_end_overlay requires an active overlay");
        return;
    }
    queue_space();
    depth_write(1);
    gsKit_set_test(graphics, GS_ZTEST_ON);
    overlay_open = 0;
}

static int triangle_order(const void *left, const void *right)
{
    const GrTriangle *a = left, *b = right;
    if (a->depth > b->depth)
        return -1;
    if (a->depth < b->depth)
        return 1;
    return a->order < b->order ? -1 : a->order > b->order ? 1 : 0;
}

void renderer_draw(const GrRenderVertex *vertices, unsigned vertex_count,
                   const uint32_t *indices, unsigned index_count,
                   const GrRenderPart *parts, unsigned part_count)
{
    GrRenderPart fallback_part;
    size_t transparent_count = 0, needed = 0, t;
    unsigned i, p;
    if (!graphics || !frame_open || (vertex_count && !vertices) || (index_count && !indices) ||
        (part_count && !parts)) {
        fail("renderer_draw requires an open frame and valid vertex/index/part buffers");
        return;
    }
    if (!vertex_count || !index_count)
        return;
    if (!part_count) {
        fallback_part.first_index = 0;
        fallback_part.index_count = index_count;
        fallback_part.material_index = 0;
        fallback_part.flags = 0;
        parts = &fallback_part;
        part_count = 1;
    }
    for (p = 0; p < part_count; ++p) {
        const GrRenderPart *part = &parts[p];
        uint32_t flags = part->flags;
        if (part->first_index > index_count || part->index_count > index_count - part->first_index ||
            part->index_count % 3 || (material_count && part->material_index >= material_count) ||
            (!material_count && part->material_index != 0)) {
            fail("Part %u has an invalid triangle range or material index", p);
            return;
        }
        if (material_count)
            flags |= materials[part->material_index].flags;
        if (flags & GR_RENDER_TRANSPARENT) {
            if (part->index_count / 3 > SIZE_MAX - needed) {
                fail("Transparent triangle count overflow");
                return;
            }
            needed += part->index_count / 3;
        }
        for (i = 0; i < part->index_count; ++i)
            if (indices[part->first_index + i] >= vertex_count) {
                fail("Part %u references a vertex outside the vertex buffer", p);
                return;
            }
    }
    if (vertex_count > SIZE_MAX / sizeof(*camera_vertices) || needed > SIZE_MAX / sizeof(*transparent_triangles)) {
        fail("Renderer scratch allocation size overflow");
        return;
    }
    if (vertex_count > camera_capacity) {
        GrCameraVertex *grown = realloc(camera_vertices, vertex_count * sizeof(*grown));
        if (!grown) {
            fail("Camera vertex allocation failed");
            return;
        }
        camera_vertices = grown;
        camera_capacity = vertex_count;
    }
    if (needed > transparent_capacity) {
        GrTriangle *grown = realloc(transparent_triangles, needed * sizeof(*grown));
        if (!grown) {
            fail("Transparent sort allocation failed");
            return;
        }
        transparent_triangles = grown;
        transparent_capacity = needed;
    }
    for (i = 0; i < vertex_count; ++i)
        camera_vertices[i] = camera_vertex(&vertices[i]);
    queue_space();
    depth_write(1);
    for (p = 0; p < part_count; ++p) {
        const GrRenderPart *part = &parts[p];
        uint32_t flags = part->flags | (material_count ? materials[part->material_index].flags : 0);
        for (i = 0; i < part->index_count; i += 3) {
            const uint32_t *index = indices + part->first_index + i;
            if (flags & GR_RENDER_TRANSPARENT) {
                GrTriangle *triangle = &transparent_triangles[transparent_count];
                float depth = (camera_vertices[index[0]].z + camera_vertices[index[1]].z + camera_vertices[index[2]].z) / 3.0f;
                if (!isfinite(depth))
                    continue;
                memcpy(triangle->indices, index, sizeof(triangle->indices));
                triangle->material = part->material_index;
                triangle->flags = flags;
                triangle->depth = depth;
                triangle->order = transparent_count;
                ++transparent_count;
            } else {
                draw_triangle(index, part->material_index, flags);
            }
        }
    }
    if (transparent_count) {
        qsort(transparent_triangles, transparent_count, sizeof(*transparent_triangles), triangle_order);
        queue_space();
        depth_write(0);
        for (t = 0; t < transparent_count; ++t) {
            GrTriangle *triangle = &transparent_triangles[t];
            draw_triangle(triangle->indices, triangle->material, triangle->flags);
        }
        queue_space();
        depth_write(1);
    }
}

void renderer_end(void)
{
    if (!graphics || !frame_open) {
        fail("renderer_end requires an open frame");
        return;
    }
    if (overlay_open)
        renderer_end_overlay();
    gsKit_queue_exec(graphics);
    dmaKit_wait_fast();
    gsKit_finish();
    gsKit_sync_flip(graphics);
    dmaKit_wait_fast();
    gsKit_TexManager_nextFrame(graphics);
    frame_open = 0;
    overlay_open = 0;
}

float renderer_frame_seconds(void)
{
    return frame_seconds;
}

const char *renderer_error(void)
{
    return last_error;
}

void renderer_shutdown(void)
{
    if (graphics) {
        if (frame_open)
            renderer_end();
        wait_for_graphics();
        release_materials();
        gsKit_deinit_global(graphics);
        graphics = NULL;
    }
    free(camera_vertices);
    free(transparent_triangles);
    camera_vertices = NULL;
    transparent_triangles = NULL;
    camera_capacity = transparent_capacity = 0;
    frame_open = 0;
    overlay_open = 0;
    last_frame_time = 0;
}
