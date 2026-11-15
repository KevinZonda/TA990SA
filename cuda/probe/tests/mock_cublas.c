#include "../compute_tests.h"
#include "mock_helpers.h"
#include <math.h>

struct cublasContext { int unused; };
static struct cublasContext instance;
int cublasCreate_v2(cublasHandle_t *handle)
{
    trace("blas_create");
    if (mode("blas_create_fail")) return 1;
    *handle = &instance;
    return 0;
}
int cublasDestroy_v2(cublasHandle_t handle)
{ (void)handle; trace("blas_destroy"); return mode("blas_destroy_fail") ? 14 : 0; }
int cublasGetVersion_v2(cublasHandle_t handle, int *version)
{ (void)handle; *version = 11606; return 0; }
int cublasSetPointerMode_v2(cublasHandle_t handle, int pointer_mode)
{ (void)handle; return pointer_mode == 0 ? 0 : 7; }
int cublasSgemm_v2(cublasHandle_t handle, int transa, int transb,
                   int m, int n, int k, const float *alpha, const float *a, int lda,
                   const float *b, int ldb, const float *beta, float *c, int ldc)
{
    (void)handle;
    trace("blas_sgemm");
    if (mode("blas_sgemm_fail")) return 13;
    if (transa || transb || lda < m || ldb < k || ldc < m) return 7;
    if (mode("blas_noop")) return 0;
    for (int col = 0; col < n; ++col) {
        for (int row = 0; row < m; ++row) {
            float sum = 0;
            for (int t = 0; t < k; ++t) sum += a[row + t * lda] * b[t + col * ldb];
            c[row + col * ldc] = *alpha * sum + *beta * c[row + col * ldc];
        }
    }
    if (mode("blas_corrupt")) c[(n - 1) * ldc + m - 1] += 1;
    if (mode("blas_nan")) c[(n - 1) * ldc + m - 1] = NAN;
    return 0;
}
