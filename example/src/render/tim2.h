#ifndef GR_TIM2_H
#define GR_TIM2_H

#include <stddef.h>
#include <stdint.h>

typedef struct GrTim2Image {
    unsigned char *pixels;
    uint32_t width, height, psm, byte_size, allocation_size;
} GrTim2Image;

int gr_tim2_load(const unsigned char *data, size_t size, GrTim2Image *image,
                 char *error, size_t error_size);
void gr_tim2_free(GrTim2Image *image);

#endif
