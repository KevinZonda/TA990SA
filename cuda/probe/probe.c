#include "cuda_driver_minimal.h"
#include "compute_tests.h"

#include <dlfcn.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    EXIT_USAGE = 2,
    EXIT_LIBRARY = 3,
    EXIT_DEVICE = 4,
    EXIT_MEMORY_TEST = 5,
    EXIT_CLEANUP = 6
};

#define DEFAULT_BYTES ((size_t)1024 * 1024)
#define MAX_BYTES ((size_t)16 * 1024 * 1024)

static void usage(FILE *out, const char *program)
{
    fprintf(out,
            "Usage: %s [--library PATH] [--device N] [--bytes N] [--query-only]\n"
            "       [--kernel-test] [--cublas-test] [--all-tests]\n"
            "       [--nvrtc-library PATH] [--cublas-library PATH]\n"
            "       %s --help\n\n"
            "Default library: /usr/lib/libcuda.so.1\n"
            "Default device: 0. Default test size: 1048576 bytes (1 MiB).\n"
            "--bytes accepts 1..16777216 bytes (16 MiB maximum).\n"
            "--query-only initializes the driver and queries devices without\n"
            "creating a context or allocating device memory.\n"
            "The memory test performs a byte-for-byte HtoD/DtoH round trip.\n"
            "--kernel-test compiles and runs a 4099-element vector addition.\n"
            "--cublas-test verifies a small FP32 matrix multiplication.\n"
            "--all-tests enables both; every result is checked on the CPU.\n"
            "Compute tests cannot be combined with --query-only.\n"
            "Default: memory test only. No cuDNN or performance test.\n",
            program, program);
}

static int number(const char *text, unsigned long long max,
                  unsigned long long *value)
{
    char *end = NULL;
    if (!text[0]) return 0;
    for (const char *p = text; *p; ++p) {
        if (*p < '0' || *p > '9') return 0;
    }
    errno = 0;
    *value = strtoull(text, &end, 10);
    return !errno && end && !*end && *value <= max;
}

int load_symbol(void *library, void *destination, size_t size,
                       const char *name, int required)
{
    void *symbol;
    const char *error;
    if (size != sizeof(symbol)) {
        fprintf(stderr, "Unsupported function-pointer ABI: %s\n", name);
        return 0;
    }
    dlerror();
    symbol = dlsym(library, name);
    error = dlerror();
    if (error || !symbol) {
        if (required) {
            fprintf(stderr, "Missing CUDA symbol %s: %s\n", name,
                    error ? error : "null address");
        }
        return !required;
    }
    /* POSIX dlsym returns a callable address; memcpy avoids an ISO C cast. */
    memcpy(destination, &symbol, sizeof(symbol));
    return 1;
}

#define LOAD(member, name) \
    load_symbol(library, &api.member, sizeof(api.member), name, 1)
#define OPTIONAL(member, name) \
    ((void)load_symbol(library, &api.member, sizeof(api.member), name, 0))

int cuda_ok(const struct cuda_api *api, CUresult result,
                   const char *operation)
{
    const char *name = "CUDA_ERROR_UNKNOWN";
    const char *description = "No error description available";
    const char *value = NULL;
    if (result == CUDA_SUCCESS) return 1;
    if (api->error_name && api->error_name(result, &value) == CUDA_SUCCESS && value)
        name = value;
    value = NULL;
    if (api->error_string && api->error_string(result, &value) == CUDA_SUCCESS && value)
        description = value;
    fprintf(stderr, "%s failed: %s (%d): %s\n", operation, name, result,
            description);
    return 0;
}

int main(int argc, char **argv)
{
    const char *library_path = "/usr/lib/libcuda.so.1";
    const char *nvrtc_path = DEFAULT_NVRTC_LIBRARY;
    const char *cublas_path = DEFAULT_CUBLAS_LIBRARY;
    int kernel_test = 0, cublas_test = 0;
    int ordinal = 0, query_only = 0, status = EXIT_DEVICE;
    int count = 0, driver_version = 0;
    size_t bytes = DEFAULT_BYTES;
    void *library = NULL;
    struct cuda_api api = {0};
    CUdevice selected_device = 0;
    CUcontext context = NULL;
    CUdeviceptr device_memory = 0;
    int context_created = 0, memory_allocated = 0;
    unsigned char *input = NULL, *output = NULL;

    for (int i = 1; i < argc; ++i) {
        unsigned long long parsed = 0;
        if (!strcmp(argv[i], "--help")) {
            usage(stdout, argv[0]);
            return 0;
        } else if (!strcmp(argv[i], "--query-only")) {
            query_only = 1;
        } else if (!strcmp(argv[i], "--kernel-test")) {
            kernel_test = 1;
        } else if (!strcmp(argv[i], "--cublas-test")) {
            cublas_test = 1;
        } else if (!strcmp(argv[i], "--all-tests")) {
            kernel_test = cublas_test = 1;
        } else if (!strcmp(argv[i], "--nvrtc-library")) {
            if (++i == argc || !argv[i][0]) goto invalid_usage;
            nvrtc_path = argv[i];
        } else if (!strcmp(argv[i], "--cublas-library")) {
            if (++i == argc || !argv[i][0]) goto invalid_usage;
            cublas_path = argv[i];
        } else if (!strcmp(argv[i], "--library")) {
            if (++i == argc || !argv[i][0]) goto invalid_usage;
            library_path = argv[i];
        } else if (!strcmp(argv[i], "--device")) {
            if (++i == argc || !number(argv[i], INT_MAX, &parsed)) goto invalid_usage;
            ordinal = (int)parsed;
        } else if (!strcmp(argv[i], "--bytes")) {
            if (++i == argc || !number(argv[i], MAX_BYTES, &parsed) || !parsed)
                goto invalid_usage;
            bytes = (size_t)parsed;
        } else {
            goto invalid_usage;
        }
    }

    if (query_only && (kernel_test || cublas_test)) goto invalid_usage;
    printf("CUDA probe (Driver API; compute tests are opt-in)\n");
    printf("Library: %s\n", library_path);
    library = dlopen(library_path, RTLD_NOW | RTLD_LOCAL);
    if (!library) {
        fprintf(stderr, "Cannot load CUDA driver: %s\n", dlerror());
        return EXIT_LIBRARY;
    }
    OPTIONAL(error_name, "cuGetErrorName");
    OPTIONAL(error_string, "cuGetErrorString");
    if (!LOAD(init, "cuInit") ||
        !LOAD(driver_version, "cuDriverGetVersion") ||
        !LOAD(device_count, "cuDeviceGetCount") ||
        !LOAD(device_get, "cuDeviceGet") ||
        !LOAD(device_name, "cuDeviceGetName") ||
        !LOAD(device_capability, "cuDeviceComputeCapability") ||
        !LOAD(device_total_memory, "cuDeviceTotalMem_v2")) {
        status = EXIT_LIBRARY;
        goto cleanup;
    }
    if (!cuda_ok(&api, api.init(0), "cuInit") ||
        !cuda_ok(&api, api.driver_version(&driver_version), "cuDriverGetVersion") ||
        !cuda_ok(&api, api.device_count(&count), "cuDeviceGetCount"))
        goto cleanup;

    printf("Driver API version: %d.%d (raw %d)\n", driver_version / 1000,
           (driver_version % 1000) / 10, driver_version);
    printf("CUDA devices: %d\n", count);
    if (count <= 0) {
        fprintf(stderr, "No CUDA-capable devices reported.\n");
        goto cleanup;
    }
    for (int i = 0; i < count; ++i) {
        CUdevice device;
        char name[256] = {0};
        int major = 0, minor = 0;
        size_t total = 0;
        if (!cuda_ok(&api, api.device_get(&device, i), "cuDeviceGet") ||
            !cuda_ok(&api, api.device_name(name, sizeof(name), device), "cuDeviceGetName") ||
            !cuda_ok(&api, api.device_capability(&major, &minor, device), "cuDeviceComputeCapability") ||
            !cuda_ok(&api, api.device_total_memory(&total, device), "cuDeviceTotalMem_v2"))
            goto cleanup;
        name[sizeof(name) - 1] = '\0';
        printf("Device %d: %s\n", i, name);
        printf("  Compute capability: %d.%d (sm_%d%d)\n", major, minor, major, minor);
        printf("  Reported total memory: %zu bytes (%.2f GiB)\n", total,
               (double)total / 1073741824.0);
        if (i == ordinal) selected_device = device;
    }
    if (ordinal >= count) {
        fprintf(stderr, "Requested device %d, but only %d device(s) exist.\n", ordinal, count);
        goto cleanup;
    }
    printf("Selected device: %d\n", ordinal);
    if (query_only) {
        status = 0;
        goto cleanup;
    }

    if (!LOAD(context_create, "cuCtxCreate_v2") ||
        !LOAD(context_destroy, "cuCtxDestroy_v2") ||
        !LOAD(context_sync, "cuCtxSynchronize") ||
        !LOAD(memory_info, "cuMemGetInfo_v2") ||
        !LOAD(memory_allocate, "cuMemAlloc_v2") ||
        !LOAD(memory_free, "cuMemFree_v2") ||
        !LOAD(copy_host_to_device, "cuMemcpyHtoD_v2") ||
        !LOAD(copy_device_to_host, "cuMemcpyDtoH_v2")) {
        status = EXIT_LIBRARY;
        goto cleanup;
    }
    status = EXIT_MEMORY_TEST;
    if (!cuda_ok(&api, api.context_create(&context, 0, selected_device), "cuCtxCreate_v2"))
        goto cleanup;
    context_created = 1;
    {
        size_t free_bytes = 0, total_bytes = 0;
        if (!cuda_ok(&api, api.memory_info(&free_bytes, &total_bytes), "cuMemGetInfo_v2"))
            goto cleanup;
        printf("Context memory: %zu free / %zu total bytes\n", free_bytes, total_bytes);
    }
    input = malloc(bytes);
    output = malloc(bytes);
    if (!input || !output) {
        fprintf(stderr, "Host memory allocation failed for %zu-byte buffers.\n", bytes);
        goto cleanup;
    }
    for (size_t i = 0; i < bytes; ++i)
        input[i] = (unsigned char)((i * 131u + (i >> 8) + 17u) & 255u);
    memset(output, 0xa5, bytes);
    if (!cuda_ok(&api, api.memory_allocate(&device_memory, bytes), "cuMemAlloc_v2"))
        goto cleanup;
    memory_allocated = 1;
    printf("Device allocation: %zu bytes\n", bytes);
    if (!cuda_ok(&api, api.copy_host_to_device(device_memory, input, bytes), "cuMemcpyHtoD_v2") ||
        !cuda_ok(&api, api.copy_device_to_host(output, device_memory, bytes), "cuMemcpyDtoH_v2") ||
        !cuda_ok(&api, api.context_sync(), "cuCtxSynchronize"))
        goto cleanup;
    for (size_t i = 0; i < bytes; ++i) {
        if (input[i] != output[i]) {
            fprintf(stderr, "Round-trip mismatch at byte %zu: expected %02x, got %02x\n",
                    i, (unsigned int)input[i], (unsigned int)output[i]);
            goto cleanup;
        }
    }
    printf("Memory round-trip: PASS (%zu bytes verified)\n", bytes);
    if (kernel_test) {
        status = test_kernel(library, &api, selected_device, nvrtc_path);
        if (status) goto cleanup;
    }
    if (cublas_test) {
        status = test_cublas(&api, cublas_path);
        if (status) goto cleanup;
    }
    status = 0;

cleanup:
    if (memory_allocated &&
        !cuda_ok(&api, api.memory_free(device_memory), "cuMemFree_v2") && !status)
        status = EXIT_CLEANUP;
    if (context_created &&
        !cuda_ok(&api, api.context_destroy(context), "cuCtxDestroy_v2") && !status)
        status = EXIT_CLEANUP;
    free(output);
    free(input);
    if (dlclose(library)) {
        fprintf(stderr, "dlclose failed: %s\n", dlerror());
        if (!status) status = EXIT_CLEANUP;
    }
    if (!status)
        printf("Result: PASS%s\n", query_only ? " (query-only; memory test skipped)" : "");
    else
        fprintf(stderr, "Result: FAIL (exit %d)\n", status);
    return status;

invalid_usage:
    usage(stderr, argv[0]);
    return EXIT_USAGE;
}
