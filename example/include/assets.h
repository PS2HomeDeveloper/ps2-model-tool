#ifndef GRANNY_ASSETS_H
#define GRANNY_ASSETS_H

#include <stdint.h>

typedef struct {
    const char *name;
    const unsigned char *data;
    uint32_t size;
} GrAsset;

int assets_init_embedded(void);
const GrAsset *assets_find(const char *name);
const char *assets_error(void);

#endif
