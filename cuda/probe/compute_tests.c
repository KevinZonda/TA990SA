#include "compute_tests.h"

#include <dlfcn.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

struct nvrtc_api {
    int (*version)(int *, int *);
    int (*arch_count)(int *);
    int (*archs)(int *);
    int (*create)(nvrtcProgram *, const char *, const char *, int,
                  const char **, const char **);
    int (*compile)(nvrtcProgram, int, const char **);
    int (*log_size)(nvrtcProgram, size_t *);
    int (*log)(nvrtcProgram, char *);
    int (*ptx_size)(nvrtcProgram, size_t *);
    int (*ptx)(nvrtcProgram, char *);
    int (*destroy)(nvrtcProgram *);
    const char *(*error)(int);
};

struct module_api {
    CUresult (*load)(CUmodule *, const void *);
    CUresult (*function)(CUfunction *, CUmodule, const char *);
    CUresult (*launch)(CUfunction, unsigned int, unsigned int, unsigned int,
                       unsigned int, unsigned int, unsigned int, unsigned int,
                       CUstream, void **, void **);
    CUresult (*unload)(CUmodule);
};

struct blas_api {
    int (*create)(cublasHandle_t *);
    int (*destroy)(cublasHandle_t);
    int (*version)(cublasHandle_t, int *);
    int (*pointer_mode)(cublasHandle_t, int);
    int (*sgemm)(cublasHandle_t, int, int, int, int, int, const float *,
                 const float *, int, const float *, int, const float *, float *, int);
};

#define SYMBOL(lib, table, member, name) \
    load_symbol(lib, &(table).member, sizeof((table).member), name, 1)
#define GPU(call) cuda_ok(api, (call), #call)

static int nvrtc_ok(const struct nvrtc_api *nv, int result, const char *operation)
{
    if (!result) return 1;
    fprintf(stderr, "%s failed: %s (%d)\n", operation, nv->error(result), result);
    return 0;
}
#define NV(call) nvrtc_ok(&nv, (call), #call)

static int blas_ok(int result, const char *operation)
{
    if (!result) return 1;
    fprintf(stderr, "%s failed: cuBLAS status %d\n", operation, result);
    return 0;
}
#define BLAS(call) blas_ok((call), #call)

static int close_library(void *library, int status)
{
    if (library && dlclose(library)) {
        fprintf(stderr, "Compute library dlclose failed: %s\n", dlerror());
        if (!status) status = 6;
    }
    return status;
}

int test_kernel(void *driver, const struct cuda_api *api, CUdevice device,
                const char *nvrtc_path)
{
    static const char source[] =
        "extern \"C\" __global__ void vector_add(const float *a, const float *b, "
        "float *out, unsigned int n) {\n"
        " unsigned int i = blockIdx.x * blockDim.x + threadIdx.x;\n"
        " if (i < n) out[i] = a[i] + b[i];\n"
        "}\n";
    enum { ELEMENTS = 4099, BLOCK = 128, MAX_ARCHS = 256 };
    float a[ELEMENTS], b[ELEMENTS], out[ELEMENTS];
    const size_t bytes = sizeof(a);
    CUdeviceptr buffers[3] = {0};
    unsigned int n = ELEMENTS;
    void *arguments[] = { &buffers[0], &buffers[1], &buffers[2], &n };
    void *library = NULL;
    struct nvrtc_api nv = {0};
    struct module_api module_api = {0};
    nvrtcProgram program = NULL;
    CUmodule module = NULL;
    CUfunction function = NULL;
    int status = 7, allocated = 0, launched = 0;
    int major = 0, minor = 0, nvmajor = 0, nvminor = 0, arch_count = 0, target = 0;
    int archs[MAX_ARCHS];
    size_t ptx_size = 0;
    char architecture[64], *ptx = NULL;
    const char *options[] = { architecture, "--std=c++11" };

    printf("NVRTC library: %s\n", nvrtc_path);
    library = dlopen(nvrtc_path, RTLD_NOW | RTLD_LOCAL);
    if (!library) {
        fprintf(stderr, "Cannot load NVRTC: %s\n", dlerror());
        return status;
    }
    if (!SYMBOL(library, nv, error, "nvrtcGetErrorString") ||
        !SYMBOL(library, nv, version, "nvrtcVersion") ||
        !SYMBOL(library, nv, arch_count, "nvrtcGetNumSupportedArchs") ||
        !SYMBOL(library, nv, archs, "nvrtcGetSupportedArchs") ||
        !SYMBOL(library, nv, create, "nvrtcCreateProgram") ||
        !SYMBOL(library, nv, compile, "nvrtcCompileProgram") ||
        !SYMBOL(library, nv, log_size, "nvrtcGetProgramLogSize") ||
        !SYMBOL(library, nv, log, "nvrtcGetProgramLog") ||
        !SYMBOL(library, nv, ptx_size, "nvrtcGetPTXSize") ||
        !SYMBOL(library, nv, ptx, "nvrtcGetPTX") ||
        !SYMBOL(library, nv, destroy, "nvrtcDestroyProgram") ||
        !SYMBOL(driver, module_api, load, "cuModuleLoadData") ||
        !SYMBOL(driver, module_api, function, "cuModuleGetFunction") ||
        !SYMBOL(driver, module_api, launch, "cuLaunchKernel") ||
        !SYMBOL(driver, module_api, unload, "cuModuleUnload")) goto cleanup;
    if (!GPU(api->device_capability(&major, &minor, device)) ||
        !NV(nv.version(&nvmajor, &nvminor)) ||
        !NV(nv.arch_count(&arch_count))) goto cleanup;
    if (arch_count < 1 || arch_count > MAX_ARCHS) {
        fprintf(stderr, "Invalid NVRTC architecture count: %d\n", arch_count);
        goto cleanup;
    }
    if (!NV(nv.archs(archs))) goto cleanup;
    for (int i = 0; i < arch_count; ++i)
        if (archs[i] <= major * 10 + minor && archs[i] > target) target = archs[i];
    if (!target) {
        fprintf(stderr, "NVRTC has no PTX target compatible with sm_%d%d\n", major, minor);
        goto cleanup;
    }
    snprintf(architecture, sizeof(architecture), "--gpu-architecture=compute_%d", target);
    printf("NVRTC version: %d.%d; PTX target: compute_%d (GPU sm_%d%d)\n",
           nvmajor, nvminor, target, major, minor);
    if (!NV(nv.create(&program, source, "vector_add.cu", 0, NULL, NULL))) goto cleanup;
    {
        int result = nv.compile(program, 2, options);
        size_t log_size = 0;
        if (!NV(nv.log_size(program, &log_size))) goto cleanup;
        if (log_size > 1) {
            char *log = log_size <= 16 * 1024 * 1024 ? calloc(log_size, 1) : NULL;
            if (!log) { fprintf(stderr, "NVRTC log allocation failed\n"); goto cleanup; }
            int log_ok = NV(nv.log(program, log));
            log[log_size - 1] = '\0';
            fprintf(stderr, "NVRTC compiler log:\n%s\n", log);
            free(log);
            if (!log_ok) goto cleanup;
        }
        if (!nvrtc_ok(&nv, result, "nvrtcCompileProgram")) goto cleanup;
    }
    if (!NV(nv.ptx_size(program, &ptx_size))) goto cleanup;
    if (!ptx_size || ptx_size > 16 * 1024 * 1024 || !(ptx = calloc(ptx_size, 1))) {
        fprintf(stderr, "Invalid PTX size or allocation failure: %zu\n", ptx_size);
        goto cleanup;
    }
    if (!NV(nv.ptx(program, ptx))) goto cleanup;
    ptx[ptx_size - 1] = '\0';
    if (!GPU(module_api.load(&module, ptx)) ||
        !GPU(module_api.function(&function, module, "vector_add"))) goto cleanup;
    for (unsigned int i = 0; i < n; ++i) {
        a[i] = (float)((int)(i % 127) - 63) * 0.25f;
        b[i] = (float)((int)(i % 31) - 15) * 0.125f;
        out[i] = NAN;
    }
    for (int i = 0; i < 3; ++i) {
        if (!GPU(api->memory_allocate(&buffers[i], bytes))) goto cleanup;
        ++allocated;
    }
    if (!GPU(api->copy_host_to_device(buffers[0], a, bytes)) ||
        !GPU(api->copy_host_to_device(buffers[1], b, bytes)) ||
        !GPU(api->copy_host_to_device(buffers[2], out, bytes))) goto cleanup;
    launched = 1;
    if (!GPU(module_api.launch(function, (n + BLOCK - 1) / BLOCK, 1, 1,
                              BLOCK, 1, 1, 0, NULL, arguments, NULL))) goto cleanup;
    if (!GPU(api->context_sync())) goto cleanup;
    launched = 0;
    if (!GPU(api->copy_device_to_host(out, buffers[2], bytes))) goto cleanup;
    for (unsigned int i = 0; i < n; ++i) {
        float expected = a[i] + b[i];
        if (!isfinite(out[i]) || out[i] != expected) {
            fprintf(stderr, "Kernel mismatch at element %u: expected %.9g, got %.9g\n",
                    i, (double)expected, (double)out[i]);
            goto cleanup;
        }
    }
    printf("GPU vector addition: PASS (%u FP32 results verified)\n", n);
    status = 0;

cleanup:
    if (launched && !GPU(api->context_sync()) && !status) status = 6;
    for (int i = allocated - 1; i >= 0; --i)
        if (!GPU(api->memory_free(buffers[i])) && !status) status = 6;
    if (module && !GPU(module_api.unload(module)) && !status) status = 6;
    if (program && !NV(nv.destroy(&program)) && !status) status = 6;
    free(ptx);
    return close_library(library, status);
}

int test_cublas(const struct cuda_api *api, const char *cublas_path)
{
    enum { M = 17, N = 13, K = 19 };
    float a[M * K], b[K * N], c[M * N], out[M * N];
    const float alpha = 0.75f, beta = 0.25f;
    const size_t sizes[] = { sizeof(a), sizeof(b), sizeof(c) };
    const void *inputs[] = { a, b, c };
    CUdeviceptr buffers[3] = {0};
    void *library = NULL;
    struct blas_api blas = {0};
    cublasHandle_t handle = NULL;
    int status = 8, allocated = 0, submitted = 0, version = 0;
    double max_error = 0;

    printf("cuBLAS library: %s\n", cublas_path);
    library = dlopen(cublas_path, RTLD_NOW | RTLD_LOCAL);
    if (!library) {
        fprintf(stderr, "Cannot load cuBLAS: %s\n", dlerror());
        return status;
    }
    if (!SYMBOL(library, blas, create, "cublasCreate_v2") ||
        !SYMBOL(library, blas, destroy, "cublasDestroy_v2") ||
        !SYMBOL(library, blas, version, "cublasGetVersion_v2") ||
        !SYMBOL(library, blas, pointer_mode, "cublasSetPointerMode_v2") ||
        !SYMBOL(library, blas, sgemm, "cublasSgemm_v2")) goto cleanup;
    if (!BLAS(blas.create(&handle)) || !BLAS(blas.version(handle, &version)) ||
        !BLAS(blas.pointer_mode(handle, 0))) goto cleanup;
    printf("cuBLAS version: %d\n", version);
    for (int i = 0; i < M * K; ++i) a[i] = (float)(i % 23 - 11) * 0.125f;
    for (int i = 0; i < K * N; ++i) b[i] = (float)(i % 17 - 8) * 0.25f;
    for (int i = 0; i < M * N; ++i) c[i] = (float)(i % 7 - 3) * 0.5f;
    for (int i = 0; i < 3; ++i) {
        if (!GPU(api->memory_allocate(&buffers[i], sizes[i]))) goto cleanup;
        ++allocated;
        if (!GPU(api->copy_host_to_device(buffers[i], inputs[i], sizes[i]))) goto cleanup;
    }
    /* cuBLAS uses column-major storage. The deliberately rectangular matrices
     * and nonzero beta check layout, leading dimensions and accumulation. */
    submitted = 1;
    if (!BLAS(blas.sgemm(handle, 0, 0, M, N, K, &alpha,
                         (const float *)(uintptr_t)buffers[0], M,
                         (const float *)(uintptr_t)buffers[1], K, &beta,
                         (float *)(uintptr_t)buffers[2], M))) goto cleanup;
    if (!GPU(api->context_sync())) goto cleanup;
    submitted = 0;
    if (!GPU(api->copy_device_to_host(out, buffers[2], sizeof(out)))) goto cleanup;
    for (int col = 0; col < N; ++col) {
        for (int row = 0; row < M; ++row) {
            double sum = 0;
            for (int k = 0; k < K; ++k) sum += (double)a[row + k * M] * b[k + col * K];
            double expected = alpha * sum + beta * c[row + col * M];
            double actual = out[row + col * M];
            double error = fabs(actual - expected);
            if (!isfinite(actual) || error > 1e-5 * (1.0 + fabs(expected))) {
                fprintf(stderr, "cuBLAS mismatch at (%d,%d): expected %.9g, got %.9g\n",
                        row, col, expected, actual);
                goto cleanup;
            }
            if (error > max_error) max_error = error;
        }
    }
    printf("cuBLAS SGEMM: PASS (%dx%d * %dx%d, %d FP32 results verified; max abs error %.9g)\n",
           M, K, K, N, M * N, max_error);
    status = 0;

cleanup:
    if (submitted && !GPU(api->context_sync()) && !status) status = 6;
    if (handle && !BLAS(blas.destroy(handle)) && !status) status = 6;
    for (int i = allocated - 1; i >= 0; --i)
        if (!GPU(api->memory_free(buffers[i])) && !status) status = 6;
    return close_library(library, status);
}
