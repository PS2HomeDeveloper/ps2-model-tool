#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "dbglog.h"


static char g_log_path[256] = "mass:/debug_log.txt";

void debug_init(int argc, char *argv[])
{
    if (argc > 0 && argv[0] != NULL && argv[0][0] != '\0') {
        strncpy(g_log_path, argv[0], sizeof(g_log_path) - 1);
        g_log_path[sizeof(g_log_path) - 1] = '\0';

        
        char *slash = strrchr(g_log_path, '/');
        if (slash != NULL) {
            strcpy(slash + 1, "debug_log.txt");
        }
    }

    
    FILE *f = fopen(g_log_path, "w");
    if (f != NULL) {
        fprintf(f, "=== Rebax Engine — Debug Log ===\n");
        fprintf(f, "log path: %s\n", g_log_path);
        fprintf(f, "argc=%d\n\n", argc);
        fclose(f);
    }
    
}

void debug_log(const char *fmt, ...)
{
    FILE *f = fopen(g_log_path, "a");
    if (f == NULL) return;

    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);

    fclose(f); 
}
