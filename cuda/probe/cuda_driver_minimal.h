#ifndef TA990SA_CUDA_DRIVER_MINIMAL_H
#define TA990SA_CUDA_DRIVER_MINIMAL_H

#include <stddef.h>

/* Small, stable Linux Driver API ABI subset; no CUDA SDK is needed to build.
 * The *_v2 exports use size_t and a 64-bit CUdeviceptr on the target LP64 ABI.
 * References: NVIDIA CUDA 12.1 Driver API, device/context/memory management.
 */
typedef int CUresult;
typedef int CUdevice;
typedef unsigned long long CUdeviceptr;
typedef struct CUctx_st *CUcontext;
typedef struct CUmod_st *CUmodule;
typedef struct CUfunc_st *CUfunction;
typedef struct CUstream_st *CUstream;

enum { CUDA_SUCCESS = 0 };

_Static_assert(sizeof(void *) == 8, "This probe requires a 64-bit Linux ABI");
_Static_assert(sizeof(size_t) == 8, "CUDA memory sizes require LP64");
_Static_assert(sizeof(CUdeviceptr) == 8, "CUdeviceptr must be 64 bits");

struct cuda_api {
    CUresult (*init)(unsigned int);
    CUresult (*driver_version)(int *);
    CUresult (*device_count)(int *);
    CUresult (*device_get)(CUdevice *, int);
    CUresult (*device_name)(char *, int, CUdevice);
    CUresult (*device_capability)(int *, int *, CUdevice);
    CUresult (*device_total_memory)(size_t *, CUdevice);
    CUresult (*error_name)(CUresult, const char **);
    CUresult (*error_string)(CUresult, const char **);
    CUresult (*context_create)(CUcontext *, unsigned int, CUdevice);
    CUresult (*context_destroy)(CUcontext);
    CUresult (*context_sync)(void);
    CUresult (*memory_info)(size_t *, size_t *);
    CUresult (*memory_allocate)(CUdeviceptr *, size_t);
    CUresult (*memory_free)(CUdeviceptr);
    CUresult (*copy_host_to_device)(CUdeviceptr, const void *, size_t);
    CUresult (*copy_device_to_host)(void *, CUdeviceptr, size_t);
};

int load_symbol(void *library, void *destination, size_t size,
                const char *name, int required);
int cuda_ok(const struct cuda_api *api, CUresult result, const char *operation);

#endif
