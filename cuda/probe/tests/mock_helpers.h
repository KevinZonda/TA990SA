#ifndef TA990SA_MOCK_HELPERS_H
#define TA990SA_MOCK_HELPERS_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int mode(const char *name)
{
    const char *current = getenv("MOCK_CUDA_MODE");
    return current && !strcmp(current, name);
}
static void trace(const char *operation)
{
    const char *path = getenv("MOCK_TRACE_FILE");
    if (path) {
        FILE *f = fopen(path, "a");
        if (f) { fprintf(f, "%s\n", operation); fclose(f); }
    }
}
#endif
