#include "render/tim2.h"
#include "core/gr_limits.h"

#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t read_u16(const unsigned char *p)
{
    return (uint16_t)((uint16_t)p[0] | (uint16_t)p[1] << 8);
}

static uint32_t read_u32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int failure(char *error, size_t error_size, const char *message)
{
    if (error && error_size)
        snprintf(error, error_size, "%s", message);
    return -1;
}

int gr_tim2_load(const unsigned char *data, size_t size, GrTim2Image *image,
                 char *error, size_t error_size)
{
    const unsigned char *picture;
    uint32_t total, image_size, header_size, width, height, psm, bytes;
    uint32_t padded_width, padded_height, allocation;
    size_t offset;
    unsigned char *pixels;

    if (!image)
        return failure(error, error_size, "TIM2: null output image");
    memset(image, 0, sizeof(*image));
    if (!data || size < 64 || memcmp(data, "TIM2", 4))
        return failure(error, error_size, "TIM2: missing or truncated header");
    if (data[4] != 4 || data[5] != 0 || read_u16(data + 6) != 1)
        return failure(error, error_size, "TIM2: require version 4, linear, one picture");
    picture = data + 16;
    total = read_u32(picture);
    image_size = read_u32(picture + 8);
    header_size = read_u16(picture + 12);
    if (header_size != 48 || picture[19] != 0)
        return failure(error, error_size, "TIM2: unsupported custom picture header");
    if (total > size - 16 || total < header_size || image_size > total - header_size)
        return failure(error, error_size, "TIM2: picture block exceeds input bounds");
    if (read_u32(picture + 4) || read_u16(picture + 14) || picture[17])
        return failure(error, error_size, "TIM2: paletted textures are not supported");
    if (picture[16] != 1)
        return failure(error, error_size, "TIM2: require one raw mip level");
    width = read_u16(picture + 20);
    height = read_u16(picture + 22);
    psm = (read_u32(picture + 24) >> 20) & 63u;
    if (!width || !height || width > GR_MAX_TEXTURE_SIZE || height > GR_MAX_TEXTURE_SIZE ||
        (width & (width - 1)) || (height & (height - 1)))
        return failure(error, error_size, "TIM2: dimensions must be powers of two, 1..512");
    if (psm != 0 && psm != 2)
        return failure(error, error_size, "TIM2: require raw PSMCT32 or PSMCT16 in GsTex0");
    bytes = width * height * (psm == 0 ? 4u : 2u);
    if (image_size != bytes)
        return failure(error, error_size, "TIM2: image byte count disagrees with GsTex0 PSM");
    offset = 16u + header_size;
    if (offset > size || bytes > size - offset)
        return failure(error, error_size, "TIM2: truncated raw pixel data");
    padded_width = (width + 63u) & ~63u;
    padded_height = (height + (psm == 0 ? 31u : 63u)) & ~(psm == 0 ? 31u : 63u);
    allocation = padded_width * padded_height * (psm == 0 ? 4u : 2u);
    allocation = (allocation + 127u) & ~127u;
    pixels = memalign(128, allocation);
    if (!pixels)
        return failure(error, error_size, "TIM2: aligned pixel allocation failed");
    memset(pixels, 0, allocation);
    memcpy(pixels, data + offset, bytes);
    image->pixels = pixels;
    image->width = width;
    image->height = height;
    image->psm = psm;
    image->byte_size = bytes;
    image->allocation_size = allocation;
    if (error && error_size)
        error[0] = '\0';
    return 0;
}

void gr_tim2_free(GrTim2Image *image)
{
    if (image) {
        free(image->pixels);
        memset(image, 0, sizeof(*image));
    }
}
