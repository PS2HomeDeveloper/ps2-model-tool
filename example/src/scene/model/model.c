#include "model.h"
#include "model_internal.h"

#include <stdlib.h>
#include <string.h>

char gr_model_error[192];

const char *model_error(void)
{
    return gr_model_error;
}

void model_destroy(GrModel *model)
{
    if (!model) return;
    free(model->source_vertices);
    free(model->vertices);
    free(model->indices);
    free(model->parts);
    free(model->materials);
    free(model->inverse_bind);
    free(model->palette);
    free(model->normal_palette);
    memset(model, 0, sizeof(*model));
}
