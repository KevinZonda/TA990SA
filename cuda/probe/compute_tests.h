#ifndef TA990SA_COMPUTE_TESTS_H
#define TA990SA_COMPUTE_TESTS_H

#include "cuda_driver_minimal.h"

#define DEFAULT_NVRTC_LIBRARY "/usr/local/cuda-11.4/targets/aarch64-linux/lib/libnvrtc.so.11.2"
#define DEFAULT_CUBLAS_LIBRARY "/usr/local/cuda-11.4/targets/aarch64-linux/lib/libcublas.so.11"

/* Public Linux ABI subsets from CUDA 11.4 NVRTC and cuBLAS documentation. */
typedef struct _nvrtcProgram *nvrtcProgram;
typedef struct cublasContext *cublasHandle_t;

/* A CUDA context must be current. Returns 0, 6 (cleanup), 7 (kernel), 8 (BLAS). */
int test_kernel(void *driver, const struct cuda_api *api, CUdevice device,
                const char *nvrtc_path);
int test_cublas(const struct cuda_api *api, const char *cublas_path);

#endif
