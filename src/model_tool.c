#include "ufbx.h"
#include "gr_limits.h"
#define TOOL_VERSION "1.0.0"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <float.h>
#include <limits.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>

#define MAX_BONES GR_MAX_BONES
#define MAX_VERTICES GR_MAX_VERTICES
#define MAX_MATERIALS GR_MAX_MATERIALS
#define MAX_INSTANCES 256u
#define MAX_INDICES GR_MAX_INDICES
#define MAX_ANIM_BYTES GR_MAX_ANIMATION_BYTES
#define PATH_CAP 4096u
#define HASH_CAP 131072u
#define UFBX_PIN "5955c5c0b042ac2dc6f32955ab206aaec0620cfc"

typedef struct { unsigned char *data; size_t size; } Blob;
typedef struct { float p[3], n[3], uv[2]; uint16_t j[4]; unsigned char w[4], pad[4]; } Vertex;
typedef struct { uint16_t j[4]; unsigned char w[4]; } Influence;
typedef struct { uint32_t *data; size_t count, capacity; } IndexList;
typedef struct {
    ufbx_node *node;
    ufbx_skin_cluster *cluster;
    ufbx_matrix binding;
    int parent;
    uint32_t instance;
    int synthetic;
} Bone;
typedef struct {
    ufbx_material *source;
    ufbx_texture *texture;
    char name[48], filename[48], match[32];
    float color[4];
    uint32_t flags, width, height, psm;
    uint64_t bytes, transparent_pixels;
    IndexList indices;
} Material;
typedef struct {
    ufbx_node *node;
    ufbx_skin_deformer *skin;
    uint16_t clusters[MAX_BONES];
    int rigid;
    Influence *weights;
} Instance;
typedef struct {
    ufbx_anim_stack *stack;
    char name[32];
    uint32_t frames;
    double begin, end, duration;
    float *data;
} Clip;
typedef struct {
    ufbx_scene *scene;
    ufbx_anim_stack *clip;
    Clip clips[GR_MAX_CLIPS];
    uint32_t num_clips;
    const char *texture_ext;
    const char *double_sided[16];
    uint32_t num_double_sided;
    uint64_t total_frames;
    Bone bones[MAX_BONES];
    uint32_t num_bones;
    Instance instances[MAX_INSTANCES];
    uint32_t num_instances;
    Material materials[MAX_MATERIALS];
    uint32_t num_materials;
    Vertex *vertices;
    uint32_t num_vertices;
    uint32_t *hash;
    uint32_t num_indices, num_parts, num_frames;
    uint64_t source_vertices, source_faces, source_corners, dropped_weights, truncated_vertices, quantized_weights, unweighted_vertices, skipped_faces;
    double dropped_mass, max_dropped_mass;
    double fps, source_begin, source_end, duration, scale, center[3];
    double raw_min[3], raw_max[3], bounds_min[3], bounds_max[3];
    float *animation;
    const char *input, *textures, *output;
    char basename[256];
    char **texture_names;
    size_t num_texture_names;
} Model;

static void fail(const char *fmt, ...)
{
    va_list args;
    fprintf(stderr, "ps2-model-tool: error: ");
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);
    exit(1);
}

static void *allocate(size_t count, size_t size)
{
    void *p;
    if (size && count > SIZE_MAX / size) fail("allocation size overflow");
    p = calloc(count ? count : 1, size ? size : 1);
    if (!p) fail("out of memory allocating %zu bytes", count * size);
    return p;
}

static void copy_text(char *dst, size_t cap, const char *src)
{
    size_t len = strlen(src);
    if (len >= cap) fail("name too long for %zu-byte field: '%s'", cap, src);
    memcpy(dst, src, len + 1);
}

static int equal_ci(const char *a, const char *b)
{
    while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) { ++a; ++b; }
    return *a == *b;
}

static int contains_ci(const char *a, const char *b)
{
    size_t n = strlen(b);
    for (; *a; ++a) {
        size_t i;
        for (i = 0; i < n && a[i] && tolower((unsigned char)a[i]) == tolower((unsigned char)b[i]); ++i) { }
        if (i == n) return 1;
    }
    return n == 0;
}

static const char *leaf_name(const char *s)
{
    const char *p, *result = s;
    for (p = s; *p; ++p) if (*p == '/' || *p == '\\') result = p + 1;
    return result;
}

static void stem_name(char *dst, size_t cap, const char *s)
{
    char *dot;
    copy_text(dst, cap, leaf_name(s));
    dot = strrchr(dst, '.');
    if (dot) *dot = 0;
}

static void path_join(char *dst, size_t cap, const char *a, const char *b)
{
    int n = snprintf(dst, cap, "%s%s%s", a, a[0] && a[strlen(a)-1] != '/' ? "/" : "", b);
    if (n < 0 || (size_t)n >= cap) fail("path is too long");
}

static float checked_float(double v)
{
    if (!isfinite(v) || fabs(v) > FLT_MAX) fail("non-finite or out-of-range geometry/animation value");
    return v == 0.0 ? 0.0f : (float)v;
}

static uint16_t get16(const unsigned char *p) { return (uint16_t)((uint16_t)p[0] | (uint16_t)p[1] << 8); }
static uint32_t get32(const unsigned char *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static void put16(unsigned char *p, uint16_t v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); }
static void put32(unsigned char *p, uint32_t v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24); }
static void putf(unsigned char *p, double v) { float f = checked_float(v); uint32_t u; memcpy(&u, &f, 4); put32(p, u); }
static size_t align16(size_t v) { if (v > UINT32_MAX - 15u) fail("binary file exceeds 4 GiB"); return (v + 15u) & ~(size_t)15u; }

static void matrix_floats(float *out, const ufbx_matrix *m, const Model *model, int normalize)
{
    size_t row, col;
    for (row = 0; row < 3; ++row) for (col = 0; col < 4; ++col) {
        double v = m->cols[col].v[row];
        if (normalize) v = (v - (col == 3 ? model->center[row] : 0.0)) * model->scale;
        out[row * 4 + col] = checked_float(v);
    }
}

static ufbx_matrix bone_world(const Model *model, const Bone *b, const ufbx_scene *scene)
{
    const ufbx_node *n;
    if (!model->clip && b->cluster) return b->cluster->bind_to_world;
    if (b->node->typed_id >= scene->nodes.count) fail("evaluated scene lost a source node");
    n = scene->nodes.data[b->node->typed_id];
    return b->synthetic ? n->geometry_to_world : n->node_to_world;
}

static ufbx_vec3 vertex_world(const Model *m, const Vertex *v, const ufbx_scene *scene)
{
    ufbx_vec3 p, out = {0};
    unsigned k;
    p.x = v->p[0]; p.y = v->p[1]; p.z = v->p[2];
    for (k = 0; k < 4; ++k) if (v->w[k]) {
        const Bone *b = &m->bones[v->j[k]];
        ufbx_matrix w = bone_world(m, b, scene);
        ufbx_matrix palette = ufbx_matrix_mul(&w, &b->binding);
        ufbx_vec3 t = ufbx_transform_position(&palette, p);
        double weight = (double)v->w[k] / 255.0;
        out.x += t.x * weight; out.y += t.y * weight; out.z += t.z * weight;
    }
    return out;
}

static int compare_names(const void *a, const void *b) { return strcmp(*(const char *const *)a, *(const char *const *)b); }

static void scan_textures(Model *m)
{
    DIR *dir;
    struct dirent *entry;
    if (!m->textures) return;
    dir = opendir(m->textures);
    if (!dir) fail("cannot open texture directory '%s': %s", m->textures, strerror(errno));
    m->texture_names = allocate(4096, sizeof(char *));
    while ((entry = readdir(dir)) != NULL) {
        const char *dot = strrchr(entry->d_name, '.');
        if (!dot || !equal_ci(dot, ".tm2")) continue;
        if (m->num_texture_names == 4096) fail("texture directory has more than 4096 TIM2 files");
        m->texture_names[m->num_texture_names] = allocate(strlen(entry->d_name) + 1, 1);
        strcpy(m->texture_names[m->num_texture_names++], entry->d_name);
    }
    closedir(dir);
    qsort(m->texture_names, m->num_texture_names, sizeof(char *), compare_names);
}

static const char *match_texture(Model *m, const char *path)
{
    char wanted[PATH_CAP], candidate[PATH_CAP];
    const char *found = NULL;
    size_t i;
    if (!path || !path[0]) return NULL;
    stem_name(wanted, sizeof(wanted), path);
    for (i = 0; i < m->num_texture_names; ++i) {
        stem_name(candidate, sizeof(candidate), m->texture_names[i]);
        if (equal_ci(candidate, wanted)) {
            if (found) fail("ambiguous case-insensitive TIM2 match for '%s'", wanted);
            found = m->texture_names[i];
        }
    }
    return found;
}

static Blob read_file(const char *path, size_t limit)
{
    FILE *f = fopen(path, "rb");
    long length;
    Blob b;
    if (!f) fail("cannot open '%s': %s", path, strerror(errno));
    if (fseek(f, 0, SEEK_END) || (length = ftell(f)) < 0 || fseek(f, 0, SEEK_SET)) fail("cannot seek '%s'", path);
    if ((uint64_t)length > limit) fail("file '%s' exceeds %zu-byte safety limit", path, limit);
    b.size = (size_t)length;
    b.data = allocate(b.size, 1);
    if (fread(b.data, 1, b.size, f) != b.size) fail("cannot read '%s'", path);
    if (fclose(f)) fail("cannot close '%s'", path);
    return b;
}

static void inspect_tim2(Model *m, Material *mat)
{
    char path[PATH_CAP];
    Blob b;
    size_t offset, header, image, pixels, i, color_bytes = 0;
    uint32_t image_type, clut_size, clut_type;
    unsigned char *p, *data;
    path_join(path, sizeof(path), m->textures, mat->filename);
    b = read_file(path, 64u * 1024u * 1024u);
    if (b.size < 64 || memcmp(b.data, "TIM2", 4) || !get16(b.data + 6)) fail("not a valid TIM2 texture: '%s'", path);
    offset = b.data[5] == 0 ? 16 : b.data[5] == 1 ? 128 : 0;
    if (!offset || offset + 48 > b.size) fail("invalid TIM2 picture alignment in '%s'", path);
    p = b.data + offset;
    header = get16(p + 12); image = get32(p + 8); clut_size = get32(p + 4);
    if (header < 48 || header > b.size - offset || image > b.size - offset - header || clut_size > b.size - offset - header - image) fail("invalid TIM2 section bounds in '%s'", path);
    mat->width = get16(p + 20); mat->height = get16(p + 22); mat->psm = (get32(p + 24) >> 20) & 63u;
    if (!mat->width || !mat->height || mat->width > GR_MAX_TEXTURE_SIZE || mat->height > GR_MAX_TEXTURE_SIZE ||
        (mat->width & (mat->width - 1)) || (mat->height & (mat->height - 1))) fail("TIM2 dimensions must be powers of two up to %u in '%s'", GR_MAX_TEXTURE_SIZE, path);
    if (b.data[4] != 4 || b.data[5] != 0 || get16(b.data + 6) != 1 || header != 48 || p[16] != 1 ||
        clut_size || get16(p + 14) || p[17] || p[19] || (mat->psm != 0 && mat->psm != 2))
        fail("runtime requires the supplied raw single-level CT16/CT32 TIM2 variant: '%s'", path);
    mat->bytes = b.size;
    pixels = (size_t)mat->width * mat->height;
    image_type = p[19]; clut_type = p[18] & 63u;
    if (image_type == 3 || (image_type == 0 && mat->psm == 0)) color_bytes = 4;
    if (image_type == 1 || (image_type == 0 && (mat->psm == 2 || mat->psm == 10))) color_bytes = 2;
    data = p + header;
    if (color_bytes) {
        if (pixels > image / color_bytes) fail("TIM2 image data is shorter than its dimensions: '%s'", path);
        for (i = 0; i < pixels; ++i) {
            int alpha = color_bytes == 4 ? data[i * 4 + 3] : (get16(data + i * 2) >> 15) * 128;
            if (alpha < 128) mat->transparent_pixels++;
        }
    } else if (clut_size) {
        size_t step = clut_type == 1 ? 2 : clut_type == 3 ? 4 : 0;
        if (step) for (i = 0; i + step <= clut_size; i += step) {
            unsigned char *c = data + image + i;
            int alpha = step == 4 ? c[3] : (get16(c) >> 15) * 128;
            if (alpha < 128) mat->transparent_pixels++;
        }
    }
    if (mat->transparent_pixels) mat->flags |= 1u;
    free(b.data);
}

static ufbx_texture *color_texture(ufbx_material *mat)
{
    ufbx_texture *t = mat->pbr.base_color.texture;
    if (!t) t = mat->fbx.diffuse_color.texture;
    if (t && t->type != UFBX_TEXTURE_FILE) {
        if (t->file_textures.count != 1) fail("material '%s' has a layered/procedural color map that cannot be represented by one TIM2 texture", mat->name.data);
        t = t->file_textures.data[0];
    }
    return t;
}

static uint32_t material_index(Model *m, ufbx_material *source, const ufbx_node *node)
{
    uint32_t i, k;
    Material *mat;
    const char *match = NULL;
    for (i = 0; i < m->num_materials; ++i) if (m->materials[i].source == source) return i;
    if (m->num_materials == MAX_MATERIALS) fail("material budget exceeded (%u)", MAX_MATERIALS);
    i = m->num_materials++;
    mat = &m->materials[i];
    mat->source = source;
    mat->color[0] = mat->color[1] = mat->color[2] = mat->color[3] = 1.0f;
    if (!source) { strcpy(mat->name, "Default"); return i; }
    copy_text(mat->name, sizeof(mat->name), source->name.length ? source->name.data : "Unnamed");
    mat->texture = color_texture(source);
    if (mat->texture) {
        match = match_texture(m, mat->texture->relative_filename.data);
        if (!match) match = match_texture(m, mat->texture->filename.data);
        if (!match) match = match_texture(m, mat->texture->absolute_filename.data);
        if (match) strcpy(mat->match, "diffuse/base-color filename");
    }
    if (!match) {
        match = match_texture(m, source->name.data);
        if (match) strcpy(mat->match, "material name");
    }
    if (mat->texture && !match && m->textures) fail("unresolved textured material '%s': color map '%s'. Supply a matching .tm2 basename in --textures '%s'", mat->name, mat->texture->relative_filename.data, m->textures);
    if (mat->texture && !match) {
        const char *src = mat->texture->relative_filename.length ? mat->texture->relative_filename.data : mat->texture->filename.data;
        const char *dot = strrchr(leaf_name(src), '.');
        char stem[48];
        stem_name(stem, sizeof(stem), src);
        if (strlen(stem) + 8 >= sizeof(mat->filename)) fail("texture name too long for material '%s'", mat->name);
        snprintf(mat->filename, sizeof(mat->filename), "%s%s", stem, m->texture_ext ? m->texture_ext : (dot ? dot : ""));
        strcpy(mat->match, "source filename (not inspected)");
    }
    if (match) {
        copy_text(mat->filename, sizeof(mat->filename), match);
        inspect_tim2(m, mat);
    }
    if (source->fbx.diffuse_color.has_value) {
        double factor = source->fbx.diffuse_factor.has_value ? source->fbx.diffuse_factor.value_real : 1.0;
        mat->color[0] = checked_float(source->fbx.diffuse_color.value_vec3.x * factor);
        mat->color[1] = checked_float(source->fbx.diffuse_color.value_vec3.y * factor);
        mat->color[2] = checked_float(source->fbx.diffuse_color.value_vec3.z * factor);
    } else if (source->pbr.base_color.has_value) {
        mat->color[0] = checked_float(source->pbr.base_color.value_vec3.x);
        mat->color[1] = checked_float(source->pbr.base_color.value_vec3.y);
        mat->color[2] = checked_float(source->pbr.base_color.value_vec3.z);
    }
    if (source->pbr.opacity.has_value) mat->color[3] = checked_float(source->pbr.opacity.value_real);
    else if (source->fbx.transparency_factor.has_value) mat->color[3] = checked_float(1.0 - source->fbx.transparency_factor.value_real);
    if (mat->color[3] < 0.0f || mat->color[3] > 1.0f) fail("material '%s' has opacity outside [0,1]", mat->name);
    if (mat->color[3] < 0.999f || source->pbr.opacity.texture || source->fbx.transparency_color.texture) mat->flags |= 1u;
    if (mat->filename[0] && mat->psm == 2 && mat->color[3] >= 0.999f) mat->flags &= ~1u;
    if (source->features.double_sided.enabled || ufbx_find_bool(&source->props, "DoubleSided", false) || ufbx_find_bool(&node->props, "DoubleSided", false)) mat->flags |= 2u;
    for (k = 0; k < m->num_double_sided; ++k) {
        char a[PATH_CAP], b[PATH_CAP];
        stem_name(a, sizeof(a), m->double_sided[k]); stem_name(b, sizeof(b), mat->filename);
        if (equal_ci(a, b)) mat->flags |= 2u;
    }
    return i;
}

static uint16_t add_bone(Model *m, Instance *inst, uint32_t instance, ufbx_skin_cluster *cluster)
{
    Bone *b;
    uint16_t index;
    if (m->num_bones == MAX_BONES) fail("deformation binding budget exceeded (%u); bindings are per mesh, not unique scene nodes", MAX_BONES);
    index = (uint16_t)m->num_bones++;
    b = &m->bones[index];
    b->node = cluster ? cluster->bone_node : inst->node;
    if (!b->node) fail("skin cluster has no bone node");
    if (b->node->name.length >= 64) fail("bone name exceeds 63 UTF-8 bytes: '%s'", b->node->name.data);
    b->cluster = cluster;
    b->instance = instance;
    b->synthetic = !cluster;
    b->parent = -1;
    if (cluster) b->binding = cluster->geometry_to_bone;
    else { memset(&b->binding, 0, sizeof(b->binding)); b->binding.m00 = b->binding.m11 = b->binding.m22 = 1.0; }
    return index;
}

static int vertex_has_weight(const Instance *inst, size_t vertex)
{
    const ufbx_skin_vertex *sv;
    size_t k;
    if (!inst->skin || vertex >= inst->skin->vertices.count) return 0;
    sv = &inst->skin->vertices.data[vertex];
    for (k = 0; k < sv->num_weights; ++k) if (inst->skin->weights.data[sv->weight_begin + k].weight > 0.0) return 1;
    return 0;
}

static void gather_bindings(Model *m)
{
    size_t i, k, v;
    for (i = 0; i < m->scene->nodes.count; ++i) {
        ufbx_node *node = m->scene->nodes.data[i];
        ufbx_mesh *mesh = node->mesh;
        Instance *inst;
        uint32_t instance;
        int rigid = 0;
        if (!mesh || !mesh->num_triangles) continue;
        if (m->num_instances == MAX_INSTANCES) fail("mesh-instance budget exceeded (%u)", MAX_INSTANCES);
        if (mesh->skin_deformers.count > 1) fail("mesh '%s' has multiple sequential skin deformers; flatten to one linear skin before export", node->name.data);
        if (mesh->blend_deformers.count || mesh->cache_deformers.count) fail("mesh '%s' uses morph/cache deformation, not representable by the matrix-only runtime", node->name.data);
        instance = m->num_instances++;
        inst = &m->instances[instance];
        inst->node = node;
        inst->rigid = -1;
        inst->skin = mesh->skin_deformers.count ? mesh->skin_deformers.data[0] : NULL;
        if (inst->skin) {
            if (inst->skin->skinning_method != UFBX_SKINNING_METHOD_LINEAR && inst->skin->skinning_method != UFBX_SKINNING_METHOD_RIGID) fail("mesh '%s' uses dual-quaternion skinning; export linear skinning", node->name.data);
            if (inst->skin->clusters.count > MAX_BONES) fail("mesh '%s' exceeds %u clusters", node->name.data, MAX_BONES);
            for (k = 0; k < inst->skin->clusters.count; ++k) inst->clusters[k] = add_bone(m, inst, instance, inst->skin->clusters.data[k]);
        }
        for (v = 0; v < mesh->num_vertices; ++v) if (!vertex_has_weight(inst, v)) { rigid = 1; break; }
        if (rigid || !inst->skin) inst->rigid = add_bone(m, inst, instance, NULL);
        m->source_vertices += mesh->num_vertices;
        m->source_faces += mesh->num_faces;
        m->source_corners += mesh->num_indices;
        if (m->source_vertices > 1000000u || m->source_corners > MAX_INDICES) fail("source geometry exceeds safety budget (1M control points / 3M polygon corners)");
    }
    if (!m->num_instances) fail("scene contains no polygonal triangle geometry");
    for (i = 0; i < m->num_bones; ++i) {
        Bone *b = &m->bones[i];
        ufbx_node *parent;
        if (b->synthetic) continue;
        for (parent = b->node->parent; parent; parent = parent->parent) {
            for (k = 0; k < m->num_bones; ++k) if (m->bones[k].instance == b->instance && !m->bones[k].synthetic && m->bones[k].node == parent) { b->parent = (int)k; break; }
            if (b->parent >= 0) break;
        }
    }
    {
        Bone sorted[MAX_BONES];
        uint16_t remap[MAX_BONES];
        unsigned char done[MAX_BONES] = {0};
        uint32_t count = 0;
        while (count < m->num_bones) {
            uint32_t before = count;
            for (i = 0; i < m->num_bones; ++i) if (!done[i] && (m->bones[i].parent < 0 || done[m->bones[i].parent])) {
                sorted[count] = m->bones[i]; remap[i] = (uint16_t)count++; done[i] = 1;
            }
            if (count == before) fail("cycle in deformation-binding hierarchy");
        }
        for (i = 0; i < m->num_bones; ++i) if (sorted[i].parent >= 0) sorted[i].parent = remap[sorted[i].parent];
        memcpy(m->bones, sorted, m->num_bones * sizeof(Bone));
        for (i = 0; i < m->num_instances; ++i) {
            Instance *inst = &m->instances[i];
            if (inst->rigid >= 0) inst->rigid = remap[inst->rigid];
            if (inst->skin) for (k = 0; k < inst->skin->clusters.count; ++k) inst->clusters[k] = remap[inst->clusters[k]];
        }
    }
}

static void make_weights(Model *m, Instance *inst)
{
    size_t v;
    inst->weights = allocate(inst->node->mesh->num_vertices, sizeof(Influence));
    for (v = 0; v < inst->node->mesh->num_vertices; ++v) {
        Influence *out = &inst->weights[v];
        double best[4] = {0}, sum = 0.0, all = 0.0, remainder[4] = {0};
        uint16_t joints[4] = {0};
        size_t k;
        uint32_t positive = 0, total = 0;
        if (inst->skin && v < inst->skin->vertices.count) {
            const ufbx_skin_vertex *sv = &inst->skin->vertices.data[v];
            for (k = 0; k < sv->num_weights; ++k) {
                ufbx_skin_weight sw = inst->skin->weights.data[sv->weight_begin + k];
                unsigned j;
                if (!isfinite(sw.weight) || sw.weight < 0.0) fail("mesh '%s' vertex %zu has a negative/non-finite skin weight", inst->node->name.data, v);
                if (sw.weight == 0.0) continue;
                if (sw.cluster_index >= inst->skin->clusters.count) fail("skin weight cluster index is out of range");
                all += sw.weight;
                positive++;
                for (j = 0; j < 4; ++j) if (sw.weight > best[j]) {
                    unsigned t;
                    for (t = 3; t > j; --t) { best[t] = best[t-1]; joints[t] = joints[t-1]; }
                    best[j] = sw.weight; joints[j] = inst->clusters[sw.cluster_index]; break;
                }
            }
        }
        for (k = 0; k < 4; ++k) sum += best[k];
        if (sum == 0.0) {
            if (inst->rigid < 0) fail("unweighted vertex without a synthetic binding");
            out->j[0] = (uint16_t)inst->rigid; out->w[0] = 255;
            m->unweighted_vertices++;
            continue;
        }
        if (!isfinite(all) || !isfinite(sum)) fail("overflow in skin weights");
        if (positive > 4) {
            double fraction = (all - sum) / all;
            m->dropped_weights += positive - 4;
            m->truncated_vertices++;
            m->dropped_mass += fraction;
            if (fraction > m->max_dropped_mass) m->max_dropped_mass = fraction;
        }
        for (k = 0; k < 4; ++k) {
            double scaled = best[k] / sum * 255.0;
            out->w[k] = (unsigned char)floor(scaled);
            out->j[k] = joints[k];
            remainder[k] = scaled - out->w[k];
            total += out->w[k];
        }
        while (total < 255) {
            unsigned j = 0;
            for (k = 1; k < 4; ++k) if (remainder[k] > remainder[j]) j = (unsigned)k;
            if (out->w[j] == 255) fail("internal skin weight quantization overflow");
            out->w[j]++; remainder[j] = -1.0; total++;
        }
        if (total != 255) fail("internal skin weight quantization sum exceeds 255");
        for (k = 0; k < 4; ++k) if (!out->w[k]) { if (best[k] > 0.0) m->quantized_weights++; out->j[k] = 0; }
    }
}

static void encode_vertex(unsigned char *p, const Vertex *v)
{
    size_t k;
    for (k = 0; k < 3; ++k) putf(p + k * 4, v->p[k]);
    for (k = 0; k < 3; ++k) putf(p + 12 + k * 4, v->n[k]);
    for (k = 0; k < 2; ++k) putf(p + 24 + k * 4, v->uv[k]);
    for (k = 0; k < 4; ++k) put16(p + 32 + k * 2, v->j[k]);
    memcpy(p + 40, v->w, 4);
    memset(p + 44, 0, 4);
}

static uint32_t intern_vertex(Model *m, const Vertex *v)
{
    unsigned char bytes[48], existing[48];
    uint32_t hash = 2166136261u, slot;
    size_t i;
    encode_vertex(bytes, v);
    for (i = 0; i < sizeof(bytes); ++i) { hash ^= bytes[i]; hash *= 16777619u; }
    slot = hash & (HASH_CAP - 1u);
    while (m->hash[slot]) {
        uint32_t index = m->hash[slot] - 1;
        encode_vertex(existing, &m->vertices[index]);
        if (!memcmp(bytes, existing, 48)) return index;
        slot = (slot + 1u) & (HASH_CAP - 1u);
    }
    if (m->num_vertices == MAX_VERTICES) fail("deduplicated vertex budget exceeded (%u); simplify or split the model", MAX_VERTICES);
    m->vertices[m->num_vertices] = *v;
    m->hash[slot] = m->num_vertices + 1;
    return m->num_vertices++;
}

static void append_index(Model *m, IndexList *list, uint32_t index)
{
    if (m->num_indices == MAX_INDICES) fail("triangle index budget exceeded (%u)", MAX_INDICES);
    if (list->count == list->capacity) {
        size_t capacity = list->capacity ? list->capacity * 2 : 1024;
        uint32_t *data = realloc(list->data, capacity * sizeof(uint32_t));
        if (!data) fail("out of memory growing triangle indices");
        list->data = data; list->capacity = capacity;
    }
    list->data[list->count++] = index;
    m->num_indices++;
}

static void build_geometry(Model *m)
{
    uint32_t i;
    m->vertices = allocate(MAX_VERTICES, sizeof(Vertex));
    m->hash = allocate(HASH_CAP, sizeof(uint32_t));
    for (i = 0; i < m->num_instances; ++i) {
        Instance *inst = &m->instances[i];
        ufbx_mesh *mesh = inst->node->mesh;
        size_t f, tri_capacity = mesh->max_face_triangles * 3;
        uint32_t *tri;
        if (tri_capacity > MAX_INDICES) fail("polygon triangulation exceeds safety budget");
        tri = allocate(tri_capacity, sizeof(uint32_t));
        make_weights(m, inst);
        for (f = 0; f < mesh->faces.count; ++f) {
            ufbx_face face = mesh->faces.data[f];
            uint32_t material_id = mesh->face_material.count ? mesh->face_material.data[f] : 0;
            ufbx_material *source = NULL;
            uint32_t mat_id, nt;
            size_t k;
            Material *mat;
            const ufbx_vertex_vec2 *uv = &mesh->vertex_uv;
            if (face.num_indices < 3 || (mesh->face_hole.count && mesh->face_hole.data[f])) { m->skipped_faces++; continue; }
            if (inst->node->materials.count) {
                if (material_id >= inst->node->materials.count) fail("material index out of range in mesh '%s'", inst->node->name.data);
                source = inst->node->materials.data[material_id];
            }
            mat_id = material_index(m, source, inst->node);
            mat = &m->materials[mat_id];
            if (mat->texture && mat->texture->uv_set.length) {
                for (k = 0; k < mesh->uv_sets.count; ++k) if (equal_ci(mesh->uv_sets.data[k].name.data, mat->texture->uv_set.data)) { uv = &mesh->uv_sets.data[k].vertex_uv; break; }
            }
            if (mat->filename[0] && !uv->exists) fail("textured mesh '%s' has no UV coordinates", inst->node->name.data);
            nt = ufbx_triangulate_face(tri, tri_capacity, mesh, face);
            if (nt != face.num_indices - 2u) fail("cannot triangulate polygon %zu of mesh '%s'", f, inst->node->name.data);
            for (k = 0; k < (size_t)nt * 3; ++k) {
                uint32_t corner = tri[k], point;
                Vertex v;
                ufbx_vec3 p, n;
                double length;
                size_t c;
                if (corner >= mesh->num_indices) fail("triangulation returned an out-of-range corner");
                point = mesh->vertex_indices.data[corner];
                if (point >= mesh->num_vertices) fail("mesh vertex index out of range");
                memset(&v, 0, sizeof(v));
                p = ufbx_get_vertex_vec3(&mesh->vertex_position, corner);
                n = ufbx_get_vertex_vec3(&mesh->vertex_normal, corner);
                length = sqrt(n.x*n.x + n.y*n.y + n.z*n.z);
                if (!isfinite(length) || length < 1e-20) fail("mesh '%s' has a zero/invalid normal at corner %u", inst->node->name.data, corner);
                for (c = 0; c < 3; ++c) { v.p[c] = checked_float(p.v[c]); v.n[c] = checked_float(n.v[c] / length); }
                if (uv->exists) {
                    ufbx_vec2 tex = ufbx_get_vertex_vec2(uv, corner);
                    if (mat->texture && mat->texture->has_uv_transform) {
                        ufbx_vec3 t = {0};
                        t.x = tex.x; t.y = tex.y;
                        t = ufbx_transform_position(&mat->texture->uv_to_texture, t);
                        tex.x = t.x; tex.y = t.y;
                    }
                    v.uv[0] = checked_float(tex.x); v.uv[1] = checked_float(tex.y);
                }
                memcpy(v.j, inst->weights[point].j, sizeof(v.j));
                memcpy(v.w, inst->weights[point].w, sizeof(v.w));
                append_index(m, &mat->indices, intern_vertex(m, &v));
            }
        }
        free(tri);
    }
    if (!m->num_indices) fail("no renderable triangles after triangulation");
    for (i = 0; i < m->num_materials; ++i) if (m->materials[i].indices.count) m->num_parts++;
}

static void select_clip(Model *m, const char *requested)
{
    size_t i;
    if (requested) {
        for (i = 0; i < m->scene->anim_stacks.count; ++i) if (!strcmp(m->scene->anim_stacks.data[i]->name.data, requested)) { m->clip = m->scene->anim_stacks.data[i]; break; }
        if (!m->clip) for (i = 0; i < m->scene->anim_stacks.count; ++i) if (equal_ci(m->scene->anim_stacks.data[i]->name.data, requested)) {
            if (m->clip) fail("ambiguous case-insensitive clip name '%s'", requested);
            m->clip = m->scene->anim_stacks.data[i];
        }
        if (!m->clip) fail("animation stack '%s' not found; use --inspect to list source stacks", requested);
    } else {
        for (i = 0; i < m->scene->anim_stacks.count; ++i) if (equal_ci(m->scene->anim_stacks.data[i]->name.data, "idle")) { m->clip = m->scene->anim_stacks.data[i]; break; }
        if (!m->clip) for (i = 0; i < m->scene->anim_stacks.count; ++i) if (contains_ci(m->scene->anim_stacks.data[i]->name.data, "idle")) { m->clip = m->scene->anim_stacks.data[i]; break; }
        if (!m->clip && m->scene->anim_stacks.count) m->clip = m->scene->anim_stacks.data[0];
    }
    m->num_frames = 1;
    if (m->clip) {
        double intervals, span;
        if (m->clip->name.length >= 32) fail("selected clip name exceeds the 31-byte binary field: '%s'", m->clip->name.data);
        m->source_begin = m->clip->time_begin; m->source_end = m->clip->time_end;
        span = m->source_end - m->source_begin;
        if (!isfinite(m->source_begin) || !isfinite(m->source_end) || span < 0.0 || span > 3600.0) fail("animation time range is invalid or exceeds one hour");
        intervals = floor(span * m->fps + 0.5);
        if (span > 0.0 && intervals < 1.0) intervals = 1.0;
        if (intervals >= GR_MAX_FRAMES) fail("animation frame budget exceeded (%u frames)", GR_MAX_FRAMES);
        m->num_frames = (uint32_t)intervals + 1;
        m->duration = intervals / m->fps;
    } else fprintf(stderr, "ps2-model-tool: warning: no source animation stacks; emitting one static bind frame named BindPose\n");
}

static ufbx_scene *evaluate(Model *m, ufbx_anim_stack *stack, double time)
{
    ufbx_evaluate_opts opts = {0};
    ufbx_error error;
    ufbx_scene *scene;
    char message[4096];
    opts.temp_allocator.memory_limit = 256u * 1024u * 1024u;
    opts.result_allocator.memory_limit = 256u * 1024u * 1024u;
    scene = ufbx_evaluate_scene(m->scene, stack->anim, time, &opts, &error);
    if (!scene) { ufbx_format_error(message, sizeof(message), &error); fail("animation evaluation at %.9g seconds failed: %s", time, message); }
    return scene;
}

static void normalize_model(Model *m)
{
    ufbx_scene *pose = m->clip ? evaluate(m, m->clip, m->source_begin) : m->scene;
    uint32_t i;
    size_t k;
    for (k = 0; k < 3; ++k) { m->raw_min[k] = DBL_MAX; m->raw_max[k] = -DBL_MAX; }
    for (i = 0; i < m->num_vertices; ++i) {
        ufbx_vec3 p = vertex_world(m, &m->vertices[i], pose);
        for (k = 0; k < 3; ++k) {
            if (!isfinite(p.v[k])) fail("non-finite deformed model bounds");
            if (p.v[k] < m->raw_min[k]) m->raw_min[k] = p.v[k];
            if (p.v[k] > m->raw_max[k]) m->raw_max[k] = p.v[k];
        }
    }
    if (m->raw_max[1] - m->raw_min[1] < 1e-9) fail("model has zero height in right-handed Y-up space");
    m->scale = 2.0 / (m->raw_max[1] - m->raw_min[1]);
    m->center[0] = (m->raw_min[0] + m->raw_max[0]) * 0.5;
    m->center[1] = m->raw_min[1];
    m->center[2] = (m->raw_min[2] + m->raw_max[2]) * 0.5;
    for (k = 0; k < 3; ++k) { m->bounds_min[k] = (m->raw_min[k] - m->center[k]) * m->scale; m->bounds_max[k] = (m->raw_max[k] - m->center[k]) * m->scale; }
    if (pose != m->scene) ufbx_free_scene(pose);
}

static void stack_range(const ufbx_anim_stack *st, double *begin, double *end)
{
    size_t l, p, c, k;
    double lo = DBL_MAX, hi = -DBL_MAX;
    for (l = 0; l < st->layers.count; ++l) {
        const ufbx_anim_layer *layer = st->layers.data[l];
        for (p = 0; p < layer->anim_props.count; ++p) {
            const ufbx_anim_value *v = layer->anim_props.data[p].anim_value;
            for (c = 0; c < 3; ++c) {
                const ufbx_anim_curve *curve = v->curves[c];
                if (!curve || !curve->keyframes.count) continue;
                k = curve->keyframes.count - 1;
                if (curve->keyframes.data[0].time < lo) lo = curve->keyframes.data[0].time;
                if (curve->keyframes.data[k].time > hi) hi = curve->keyframes.data[k].time;
            }
        }
    }
    if (lo <= hi) { *begin = lo; *end = hi; }
    else { *begin = st->time_begin; *end = st->time_end; }
}

static void collect_clips(Model *m)
{
    size_t i, j;
    if (!m->scene->anim_stacks.count) {
        Clip *c = &m->clips[m->num_clips++];
        strcpy(c->name, "BindPose"); c->frames = 1;
        m->total_frames = 1;
        return;
    }
    if (m->scene->anim_stacks.count > GR_MAX_CLIPS) fail("animation clip budget exceeded (%u clips)", GR_MAX_CLIPS);
    for (i = 0; i < m->scene->anim_stacks.count; ++i) {
        ufbx_anim_stack *st = m->scene->anim_stacks.data[i];
        Clip *c = &m->clips[m->num_clips++];
        double span, intervals;
        c->stack = st;
        if (st->name.length >= sizeof(c->name)) fail("clip name exceeds the 31-byte binary field: '%s'", st->name.data);
        if (st->name.length) copy_text(c->name, sizeof(c->name), st->name.data);
        else snprintf(c->name, sizeof(c->name), "clip_%zu", i);
        for (j = 0; j < i; ++j) if (!strcmp(m->clips[j].name, c->name)) fail("duplicate clip name '%s'", c->name);
        stack_range(st, &c->begin, &c->end);
        span = c->end - c->begin;
        if (!isfinite(c->begin) || !isfinite(c->end) || span < 0.0 || span > 3600.0) fail("clip '%s' has an invalid time range", c->name);
        intervals = floor(span * m->fps + 0.5);
        if (span > 0.0 && intervals < 1.0) intervals = 1.0;
        if (intervals >= GR_MAX_FRAMES) fail("clip '%s' exceeds the frame budget (%u frames)", c->name, GR_MAX_FRAMES);
        c->frames = (uint32_t)intervals + 1;
        c->duration = intervals / m->fps;
        m->total_frames += c->frames;
    }
    if (m->total_frames * m->num_bones * 48u > MAX_ANIM_BYTES) fail("animation data exceeds %u bytes; lower --fps or split the file", MAX_ANIM_BYTES);
}

static void bake_animation(Model *m)
{
    uint32_t ci, frame, bone;
    for (ci = 0; ci < m->num_clips; ++ci) {
        Clip *c = &m->clips[ci];
        c->data = allocate((size_t)c->frames * m->num_bones * 12, sizeof(float));
        for (frame = 0; frame < c->frames; ++frame) {
            double time = c->begin;
            ufbx_scene *pose;
            if (c->frames > 1) time = frame == c->frames - 1 ? c->end : fmin(c->begin + frame / m->fps, c->end);
            pose = c->stack ? evaluate(m, c->stack, time) : m->scene;
            for (bone = 0; bone < m->num_bones; ++bone) {
                ufbx_matrix w = bone_world(m, &m->bones[bone], pose);
                matrix_floats(c->data + ((size_t)frame * m->num_bones + bone) * 12, &w, m, 1);
            }
            if (pose != m->scene) ufbx_free_scene(pose);
        }
        fprintf(stderr, "ps2-model-tool: baked clip '%s' (%u frames)\n", c->name, c->frames);
    }
}

static Blob new_binary(size_t size, const char *magic)
{
    Blob b;
    if (size > UINT32_MAX) fail("binary exceeds 32-bit file-size contract");
    b.size = size;
    b.data = allocate(size, 1);
    memcpy(b.data, magic, 4); put32(b.data + 4, 1); put32(b.data + 8, (uint32_t)size);
    return b;
}

static Blob mesh_binary(Model *m)
{
    size_t vertices = 64, indices = align16(vertices + (size_t)m->num_vertices * 48), parts = align16(indices + (size_t)m->num_indices * 4);
    Blob b = new_binary(align16(parts + (size_t)m->num_parts * 16), "GMSH");
    size_t i, k, cursor = 0, part = 0;
    put32(b.data + 12, m->num_vertices); put32(b.data + 16, m->num_indices); put32(b.data + 20, m->num_parts);
    put32(b.data + 24, (uint32_t)vertices); put32(b.data + 28, (uint32_t)indices); put32(b.data + 32, (uint32_t)parts);
    for (k = 0; k < 3; ++k) { putf(b.data + 40 + k*4, m->bounds_min[k]); putf(b.data + 52 + k*4, m->bounds_max[k]); }
    for (i = 0; i < m->num_vertices; ++i) encode_vertex(b.data + vertices + i * 48, &m->vertices[i]);
    for (i = 0; i < m->num_materials; ++i) {
        Material *mat = &m->materials[i];
        unsigned char *p;
        if (!mat->indices.count) continue;
        p = b.data + parts + part++ * 16;
        put32(p, (uint32_t)cursor); put32(p + 4, (uint32_t)mat->indices.count); put32(p + 8, (uint32_t)i); put32(p + 12, mat->flags);
        for (k = 0; k < mat->indices.count; ++k) put32(b.data + indices + cursor++ * 4, mat->indices.data[k]);
    }
    return b;
}

static Blob skeleton_binary(Model *m)
{
    Blob b = new_binary(32 + (size_t)m->num_bones * 128, "GSKL");
    uint32_t i;
    put32(b.data + 12, m->num_bones); put32(b.data + 16, 32);
    for (i = 0; i < m->num_bones; ++i) {
        unsigned char *p = b.data + 32 + (size_t)i * 128;
        float f[12];
        size_t k;
        Bone *bone = &m->bones[i];
        put32(p, (uint32_t)bone->parent); put32(p + 4, bone->node->element_id);
        memcpy(p + 8, bone->node->name.data, bone->node->name.length);
        matrix_floats(f, &bone->binding, m, 0);
        for (k = 0; k < 12; ++k) putf(p + 72 + k * 4, f[k]);
    }
    return b;
}

static Blob animation_binary(Model *m)
{
    size_t table = 32, offset = align16(table + (size_t)m->num_clips * 64), total = offset, i, ci;
    Blob b;
    for (ci = 0; ci < m->num_clips; ++ci) total = align16(total + (size_t)m->clips[ci].frames * m->num_bones * 48);
    b = new_binary(total, "GANM");
    put32(b.data + 4, 2);
    put32(b.data + 12, m->num_bones); put32(b.data + 16, m->num_clips); put32(b.data + 20, 32);
    for (ci = 0; ci < m->num_clips; ++ci) {
        Clip *c = &m->clips[ci];
        unsigned char *e = b.data + table + ci * 64;
        size_t count = (size_t)c->frames * m->num_bones * 12;
        memcpy(e, c->name, strlen(c->name));
        put32(e + 32, c->frames); putf(e + 36, m->fps); putf(e + 40, c->duration); put32(e + 44, (uint32_t)offset);
        for (i = 0; i < count; ++i) putf(b.data + offset + i * 4, c->data[i]);
        offset = align16(offset + count * 4);
    }
    return b;
}

static Blob material_binary(Model *m)
{
    Blob b = new_binary(32 + (size_t)m->num_materials * 128, "GMAT");
    uint32_t i;
    put32(b.data + 12, m->num_materials); put32(b.data + 16, 32);
    for (i = 0; i < m->num_materials; ++i) {
        unsigned char *p = b.data + 32 + (size_t)i * 128;
        size_t k;
        memcpy(p, m->materials[i].name, strlen(m->materials[i].name));
        memcpy(p + 48, m->materials[i].filename, strlen(m->materials[i].filename));
        for (k = 0; k < 4; ++k) putf(p + 96 + k*4, m->materials[i].color[k]);
        put32(p + 112, m->materials[i].flags);
    }
    return b;
}

static void make_directory(const char *path)
{
    char copy[PATH_CAP];
    size_t i;
    struct stat st;
    copy_text(copy, sizeof(copy), path);
    for (i = 1; ; ++i) if (copy[i] == '/' || !copy[i]) {
        char saved = copy[i];
        copy[i] = 0;
        if (copy[0] && mkdir(copy, 0755) && errno != EEXIST) fail("cannot create output directory '%s': %s", copy, strerror(errno));
        if (stat(copy, &st) || !S_ISDIR(st.st_mode)) fail("output path '%s' is not a directory", copy);
        copy[i] = saved;
        if (!saved) break;
    }
}

static void save_binary(Model *m, const char *ext, Blob b)
{
    char filename[300], path[PATH_CAP], temporary[PATH_CAP];
    FILE *f;
    snprintf(filename, sizeof(filename), "%s.%s", m->basename, ext);
    path_join(path, sizeof(path), m->output, filename);
    if (snprintf(temporary, sizeof(temporary), "%s.tmp", path) >= (int)sizeof(temporary)) fail("output path too long");
    f = fopen(temporary, "wb");
    if (!f) fail("cannot write '%s': %s", temporary, strerror(errno));
    if (fwrite(b.data, 1, b.size, f) != b.size) fail("short write to '%s'", temporary);
    if (fclose(f)) fail("cannot close '%s': %s", temporary, strerror(errno));
    if (rename(temporary, path)) fail("cannot replace '%s': %s", path, strerror(errno));
    printf("Wrote %s (%zu bytes)\n", path, b.size);
    free(b.data);
}

static void json_string(FILE *f, const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    fputc('"', f);
    for (; *p; ++p) {
        if (*p == '"' || *p == '\\') { fputc('\\', f); fputc(*p, f); }
        else if (*p < 32) fprintf(f, "\\u%04x", (unsigned)*p);
        else fputc(*p, f);
    }
    fputc('"', f);
}

static void write_report(Model *m)
{
    char path[PATH_CAP], temporary[PATH_CAP];
    FILE *f;
    uint32_t i;
    uint32_t synthetic = 0, textured = 0;
    path_join(path, sizeof(path), m->output, "report.json");
    if (snprintf(temporary, sizeof(temporary), "%s.tmp", path) >= (int)sizeof(temporary)) fail("output path too long");
    f = fopen(temporary, "wb");
    if (!f) fail("cannot write '%s': %s", temporary, strerror(errno));
    for (i = 0; i < m->num_bones; ++i) synthetic += m->bones[i].synthetic != 0;
    for (i = 0; i < m->num_materials; ++i) textured += m->materials[i].filename[0] != 0;
    fputs("{\n  \"format_version\": 2,\n  \"ufbx_commit\": \"" UFBX_PIN "\",\n  \"input\": ", f); json_string(f, m->input);
    fputs(",\n  \"reference_clip\": ", f); json_string(f, m->clip ? m->clip->name.data : "BindPose");
    fprintf(f, ",\n  \"fps\": %.9g,\n  \"clips\": [\n", m->fps);
    for (i = 0; i < m->num_clips; ++i) {
        fputs("    {\"name\": ", f); json_string(f, m->clips[i].name);
        fprintf(f, ", \"frames\": %u, \"duration\": %.12g, \"source_begin\": %.12g, \"source_end\": %.12g}%s\n", m->clips[i].frames, m->clips[i].duration, m->clips[i].begin, m->clips[i].end, i + 1 == m->num_clips ? "" : ",");
    }
    fputs("  ],\n", f);
    fprintf(f, "  \"geometry\": {\"mesh_instances\": %u, \"source_control_points\": %llu, \"source_faces\": %llu, \"source_corners\": %llu, \"vertices\": %u, \"indices\": %u, \"triangles\": %u, \"parts\": %u, \"skipped_faces\": %llu, \"bones\": %u, \"cluster_bindings\": %u, \"synthetic_bindings\": %u},\n", m->num_instances, (unsigned long long)m->source_vertices, (unsigned long long)m->source_faces, (unsigned long long)m->source_corners, m->num_vertices, m->num_indices, m->num_indices/3, m->num_parts, (unsigned long long)m->skipped_faces, m->num_bones, m->num_bones - synthetic, synthetic);
    fprintf(f, "  \"weights\": {\"max_influences\": 4, \"u8_sum\": 255, \"dropped_influences\": %llu, \"vertices_truncated\": %llu, \"dropped_fraction_sum\": %.12g, \"max_dropped_fraction\": %.12g, \"influences_quantized_to_zero\": %llu, \"rigid_control_points\": %llu},\n", (unsigned long long)m->dropped_weights, (unsigned long long)m->truncated_vertices, m->dropped_mass, m->max_dropped_mass, (unsigned long long)m->quantized_weights, (unsigned long long)m->unweighted_vertices);
    fprintf(f, "  \"normalization\": {\"reference\": \"selected clip first frame, quantized skin\", \"target_axes\": \"right-handed Y-up\", \"target_unit_meters\": 1, \"scale\": %.12g, \"subtract_before_scale\": [%.12g, %.12g, %.12g], \"source_bounds_min\": [%.12g, %.12g, %.12g], \"source_bounds_max\": [%.12g, %.12g, %.12g], \"bounds_min\": [%.12g, %.12g, %.12g], \"bounds_max\": [%.12g, %.12g, %.12g]},\n", m->scale, m->center[0], m->center[1], m->center[2], m->raw_min[0], m->raw_min[1], m->raw_min[2], m->raw_max[0], m->raw_max[1], m->raw_max[2], m->bounds_min[0], m->bounds_min[1], m->bounds_min[2], m->bounds_max[0], m->bounds_max[1], m->bounds_max[2]);
    fprintf(f, "  \"textures\": {\"available_tim2_files\": %zu, \"textured_materials\": %u, \"unresolved\": 0},\n  \"materials\": [\n", m->num_texture_names, textured);
    for (i = 0; i < m->num_materials; ++i) {
        Material *mat = &m->materials[i];
        fputs("    {\"name\": ", f); json_string(f, mat->name);
        fputs(", \"texture\": ", f); json_string(f, mat->filename);
        fputs(", \"match\": ", f); json_string(f, mat->match);
        fprintf(f, ", \"flags\": %u, \"diffuse_rgba\": [%.9g, %.9g, %.9g, %.9g], \"width\": %u, \"height\": %u, \"psm\": %u, \"bytes\": %llu, \"transparent_samples\": %llu, \"triangles\": %zu}%s\n", mat->flags, mat->color[0], mat->color[1], mat->color[2], mat->color[3], mat->width, mat->height, mat->psm, (unsigned long long)mat->bytes, (unsigned long long)mat->transparent_pixels, mat->indices.count / 3, i+1 == m->num_materials ? "" : ",");
    }
    fputs("  ],\n  \"source_stacks\": [", f);
    for (i = 0; i < m->scene->anim_stacks.count; ++i) { if (i) fputs(", ", f); json_string(f, m->scene->anim_stacks.data[i]->name.data); }
    fputs("],\n  \"warnings\": [", f);
    fputs("]\n}\n", f);
    if (ferror(f) || fclose(f)) fail("cannot finish report '%s'", temporary);
    if (rename(temporary, path)) fail("cannot replace '%s': %s", path, strerror(errno));
}

static void print_metadata(Model *m)
{
    uint32_t i;
    printf("Input: %s\nufbx commit: %s\nScene: %zu nodes, %zu meshes, %zu materials, %zu animation stacks\n", m->input, UFBX_PIN, m->scene->nodes.count, m->scene->meshes.count, m->scene->materials.count, m->scene->anim_stacks.count);
    for (i = 0; i < m->scene->anim_stacks.count; ++i) {
        ufbx_anim_stack *a = m->scene->anim_stacks.data[i];
        printf("  Stack '%s': %.9g .. %.9g seconds%s\n", a->name.data, a->time_begin, a->time_end, a == m->clip ? " [REFERENCE]" : "");
    }
    for (i = 0; i < m->num_clips; ++i) printf("Clip %u: '%s', %u frames at %.9g Hz, %.9g seconds\n", i, m->clips[i].name, m->clips[i].frames, m->fps, m->clips[i].duration);
    printf("Geometry: %u instances, %u vertices, %u triangles, %u material parts, %u deformation bindings\n", m->num_instances, m->num_vertices, m->num_indices/3, m->num_parts, m->num_bones);
    printf("Weights: %llu dropped influences on %llu control points; %llu rounded to zero; %llu rigid control points; each vertex sums to 255\n", (unsigned long long)m->dropped_weights, (unsigned long long)m->truncated_vertices, (unsigned long long)m->quantized_weights, (unsigned long long)m->unweighted_vertices);
    printf("Normalized bounds: [%.9g, %.9g, %.9g] .. [%.9g, %.9g, %.9g]; scale %.9g\n", m->bounds_min[0], m->bounds_min[1], m->bounds_min[2], m->bounds_max[0], m->bounds_max[1], m->bounds_max[2], m->scale);
    for (i = 0; i < m->num_materials; ++i) {
        Material *mat = &m->materials[i];
        printf("  Material %u '%s' -> '%s', %zu triangles, flags %u, %ux%u PSM %u, %llu transparent samples\n", i, mat->name, mat->filename[0] ? mat->filename : "diffuse color", mat->indices.count/3, mat->flags, mat->width, mat->height, mat->psm, (unsigned long long)mat->transparent_pixels);
    }
}

static void cleanup(Model *m)
{
    size_t i;
    for (i = 0; i < m->num_instances; ++i) free(m->instances[i].weights);
    for (i = 0; i < m->num_materials; ++i) free(m->materials[i].indices.data);
    for (i = 0; i < m->num_texture_names; ++i) free(m->texture_names[i]);
    free(m->texture_names); free(m->vertices); free(m->hash);
    for (i = 0; i < m->num_clips; ++i) free(m->clips[i].data);
    ufbx_free_scene(m->scene);
}

static void usage(FILE *f)
{
    fputs(
"ps2-model-tool - convert 3D models into PlayStation 2 runtime resources\n"
"\n"
"Usage:\n"
"  ps2-model-tool MODEL.fbx [OUTPUT_DIR] [TEXTURE_FORMAT]\n"
"  ps2-model-tool --input MODEL.fbx [options]\n"
"\n"
"Arguments:\n"
"  MODEL.fbx             source model (FBX binary or ASCII)\n"
"  OUTPUT_DIR            output directory (default: ./<model name>)\n"
"  TEXTURE_FORMAT        extension written for texture names, for example tm2\n"
"                        (default: keep the original image extension)\n"
"\n"
"Options:\n"
"  -i, --input PATH      source model\n"
"  -o, --output DIR      output directory\n"
"  -t, --texture-format EXT\n"
"                        texture extension recorded in the .material file\n"
"      --textures DIR    directory with ready TIM2 files; enables validation and\n"
"                        transparency detection (CT16/CT32, power of two, max 512)\n"
"      --fps N           animation sampling rate, 1-120 (default 30)\n"
"      --clip NAME       reference clip used to normalize size and pose\n"
"                        (default: idle, else the first clip)\n"
"      --double-sided T  mark materials using texture T as double-sided (repeatable)\n"
"      --inspect         validate and print information without writing files\n"
"  -v, --version         print the version\n"
"  -h, --help            show this help\n"
"\n"
"Output (only the files the model needs):\n"
"  NAME.mesh      geometry with 4-bone byte-weighted skinning\n"
"  NAME.skeleton  bindings and bind matrices        (when bones exist)\n"
"  NAME.anim      every animation clip in one file  (when clips exist)\n"
"  NAME.material  materials and texture names\n"
"  report.json    conversion report\n", f);
}

int main(int argc, char **argv)
{
    Model *m = allocate(1, sizeof(Model));
    ufbx_load_opts opts = {0};
    ufbx_error error;
    char message[4096], default_output[PATH_CAP], extension[16];
    const char *clip = NULL, *positional[3] = {0};
    int inspect = 0, i, npos = 0;
    m->fps = 30.0;
    if (sizeof(float) != 4 || sizeof(uint32_t) != 4) fail("host requires IEEE-754 32-bit float and 32-bit uint32_t");
    for (i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (!strcmp(arg, "--help") || !strcmp(arg, "-h")) { usage(stdout); free(m); return 0; }
        if (!strcmp(arg, "--version") || !strcmp(arg, "-v")) { puts("ps2-model-tool " TOOL_VERSION); free(m); return 0; }
        if (!strcmp(arg, "--inspect")) { inspect = 1; continue; }
        if (arg[0] != '-' || !arg[1]) {
            if (npos == 3) fail("too many arguments; use --help");
            positional[npos++] = arg;
            continue;
        }
        if (!strcmp(arg, "--input") || !strcmp(arg, "-i") || !strcmp(arg, "--textures") || !strcmp(arg, "--output") || !strcmp(arg, "-o") ||
            !strcmp(arg, "--clip") || !strcmp(arg, "--fps") || !strcmp(arg, "--texture-format") || !strcmp(arg, "-t") || !strcmp(arg, "--double-sided")) {
            const char *value;
            if (++i == argc || !argv[i][0]) fail("missing value for %s", arg);
            value = argv[i];
            if (!strcmp(arg, "--input") || !strcmp(arg, "-i")) m->input = value;
            else if (!strcmp(arg, "--textures")) m->textures = value;
            else if (!strcmp(arg, "--output") || !strcmp(arg, "-o")) m->output = value;
            else if (!strcmp(arg, "--clip")) clip = value;
            else if (!strcmp(arg, "--texture-format") || !strcmp(arg, "-t")) positional[2] = value;
            else if (!strcmp(arg, "--double-sided")) { if (m->num_double_sided == 16) fail("too many --double-sided options"); m->double_sided[m->num_double_sided++] = value; }
            else { char *end; errno = 0; m->fps = strtod(value, &end); if (errno || *end || !isfinite(m->fps) || m->fps < GR_MIN_FPS || m->fps > GR_MAX_FPS) fail("--fps must be a finite number in [1,120]"); }
            continue;
        }
        fail("unknown option '%s'; use --help", arg);
    }
    if (npos > 0 && !m->input) m->input = positional[0];
    if (npos > 1 && !m->output) m->output = positional[1];
    if (positional[2]) {
        const char *f = positional[2];
        if (*f == '.') ++f;
        if (!*f || strlen(f) > 8) fail("invalid texture format '%s'", positional[2]);
        snprintf(extension, sizeof(extension), ".%s", f);
        m->texture_ext = extension;
    }
    if (!m->input) { usage(stderr); free(m); return 1; }
    stem_name(m->basename, sizeof(m->basename), m->input);
    if (!m->basename[0] || !strcmp(m->basename, ".") || !strcmp(m->basename, "..")) fail("input has an invalid output basename");
    if (!m->output) { snprintf(default_output, sizeof(default_output), "%s", m->basename); m->output = default_output; }
    opts.target_axes = ufbx_axes_right_handed_y_up;
    opts.target_unit_meters = 1.0;
    opts.space_conversion = UFBX_SPACE_CONVERSION_TRANSFORM_ROOT;
    opts.geometry_transform_handling = UFBX_GEOMETRY_TRANSFORM_HANDLING_PRESERVE;
    opts.inherit_mode_handling = UFBX_INHERIT_MODE_HANDLING_PRESERVE;
    opts.generate_missing_normals = true;
    opts.normalize_normals = true;
    opts.ignore_embedded = true;
    opts.node_depth_limit = 512;
    opts.temp_allocator.memory_limit = 512u * 1024u * 1024u;
    opts.result_allocator.memory_limit = 512u * 1024u * 1024u;
    m->scene = ufbx_load_file(m->input, &opts, &error);
    if (!m->scene) { ufbx_format_error(message, sizeof(message), &error); fail("cannot load '%s': %s", m->input, message); }
    if (!m->scene->meshes.count) {
        printf("%s contains no meshes; nothing to convert\n", m->input);
        ufbx_free_scene(m->scene); free(m);
        return 0;
    }
    scan_textures(m);
    select_clip(m, clip);
    gather_bindings(m);
    build_geometry(m);
    normalize_model(m);
    collect_clips(m);
    print_metadata(m);
    if (!inspect) {
        int has_anim = m->scene->anim_stacks.count > 0;
        bake_animation(m);
        make_directory(m->output);
        save_binary(m, "mesh", mesh_binary(m));
        if (m->num_bones) save_binary(m, "skeleton", skeleton_binary(m));
        if (m->num_bones && has_anim) save_binary(m, "anim", animation_binary(m));
        if (m->num_materials) save_binary(m, "material", material_binary(m));
        write_report(m);
    }
    cleanup(m); free(m);
    return 0;
}
