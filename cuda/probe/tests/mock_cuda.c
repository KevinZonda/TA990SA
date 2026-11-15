#include "../cuda_driver_minimal.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct CUctx_st { int unused; };
static struct CUctx_st mock_context;

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

CUresult cuGetErrorName(CUresult error, const char **name)
{
    *name = error == 2 ? "CUDA_ERROR_OUT_OF_MEMORY" : "CUDA_ERROR_UNKNOWN";
    return 0;
}

CUresult cuGetErrorString(CUresult error, const char **description)
{
    (void)error;
    *description = "simulated test failure";
    return 0;
}

CUresult cuInit(unsigned int flags)
{
    (void)flags;
    return mode("init_fail") ? 999 : 0;
}

CUresult cuDriverGetVersion(int *version) { *version = 12010; return 0; }
CUresult cuDeviceGetCount(int *count) { *count = mode("no_devices") ? 0 : 1; return 0; }
CUresult cuDeviceGet(CUdevice *device, int ordinal) { *device = ordinal; return 0; }
CUresult cuDeviceGetName(char *name, int len, CUdevice device)
{
    (void)device;
    snprintf(name, (size_t)len, "Mock Orin (test only)");
    return 0;
}
CUresult cuDeviceComputeCapability(int *major, int *minor, CUdevice device)
{
    (void)device;
    *major = 8; *minor = 7;
    return 0;
}
CUresult cuDeviceTotalMem_v2(size_t *bytes, CUdevice device)
{
    (void)device;
    *bytes = (size_t)32 * 1024 * 1024 * 1024;
    return 0;
}
CUresult cuCtxCreate_v2(CUcontext *context, unsigned int flags, CUdevice device)
{
    (void)flags; (void)device;
    trace("context_create");
    if (mode("context_fail")) return 999;
    *context = &mock_context;
    return 0;
}
CUresult cuCtxDestroy_v2(CUcontext context)
{
    (void)context;
    trace("context_destroy");
    return mode("destroy_fail") ? 999 : 0;
}
CUresult cuCtxSynchronize(void) { return mode("sync_fail") ? 999 : 0; }
CUresult cuMemGetInfo_v2(size_t *free_bytes, size_t *total_bytes)
{
    *total_bytes = (size_t)32 * 1024 * 1024 * 1024;
    *free_bytes = (size_t)28 * 1024 * 1024 * 1024;
    return 0;
}
#ifndef OMIT_MEM_ALLOC
CUresult cuMemAlloc_v2(CUdeviceptr *pointer, size_t bytes)
{
    void *memory;
    trace("memory_allocate");
    if (mode("alloc_fail")) return 2;
    memory = malloc(bytes);
    if (!memory) return 2;
    *pointer = (CUdeviceptr)(uintptr_t)memory;
    return 0;
}
#endif
CUresult cuMemFree_v2(CUdeviceptr pointer)
{
    trace("memory_free");
    free((void *)(uintptr_t)pointer);
    return mode("free_fail") ? 999 : 0;
}
CUresult cuMemcpyHtoD_v2(CUdeviceptr destination, const void *source, size_t bytes)
{
    memcpy((void *)(uintptr_t)destination, source, bytes);
    return 0;
}
CUresult cuMemcpyDtoH_v2(void *destination, CUdeviceptr source, size_t bytes)
{
    memcpy(destination, (void *)(uintptr_t)source, bytes);
    if (mode("corrupt_copy")) ((unsigned char *)destination)[bytes - 1] ^= 1;
    return 0;
}
