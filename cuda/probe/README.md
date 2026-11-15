# CUDA probe

Small C diagnostic for the ARM64 DRIVE Orin Linux guest. It dynamically loads
the existing `/usr/lib/libcuda.so.1`. The default test checks device memory;
optional tests execute a GPU kernel and cuBLAS SGEMM, using the board's existing
NVRTC and cuBLAS libraries. CUDA headers, `nvcc`, Python, and CUDA development
packages are not build dependencies. The executable uses the target's glibc and
libdl rather than a static musl loader.

## Build on Windows

From this directory, with Docker Desktop running:

```powershell
.\build.ps1 -BuildImage
```

The ARM64 Ubuntu 20.04 image builds against glibc 2.31 and runs 49 mock tests
covering Driver API, NVRTC and cuBLAS paths. These include corrupted final
elements, NaNs, operations that return success without computing, compilation
and execution failures, partial allocation and cleanup failures.
The executable is exported to `build/cuda-probe`. Logs, SHA256, ELF
inspection, and build metadata are in `artifacts/`. No real GPU is used during
the container tests.

## Run on the board

Deploy `build/cuda-probe` to an executable filesystem, for example the existing
`/var/lib/fake_usr/lib/cuda-probe`, and set mode 755. `/opt/m` and `/opt/m0` were
mounted `noexec` when inspected; copying there alone does not make it runnable.

Start with device queries:

```sh
/var/lib/fake_usr/lib/cuda-probe --query-only
```

This initializes the CUDA driver, queries its API version, lists GPU names,
compute capabilities and total memory, and checks the selected device ordinal.
It creates no CUDA context or device allocation.

Then run the default 1 MiB allocation/copy verification:

```sh
/var/lib/fake_usr/lib/cuda-probe
```

This creates a temporary context, reports its free/total memory, allocates device
memory, copies a deterministic byte pattern to the device and back, verifies
every byte, then frees the allocation and destroys the context. Allocation size
is bounded to 16 MiB. Cleanup failures produce a failing exit status.

Run both compute tests in addition to the memory test:

```sh
CUDA_CACHE_DISABLE=1 timeout 60 /var/lib/fake_usr/lib/cuda-probe --all-tests
```

`CUDA_CACHE_DISABLE=1` disables the driver's on-disk JIT cache for this process.
`timeout` bounds the command's elapsed time. The program performs one small
calculation per test, with no benchmark loop:

- `--kernel-test`: NVRTC compiles the embedded vector-add kernel into PTX.
  The program chooses the highest NVRTC-supported virtual architecture no
  greater than the selected GPU's compute capability and prints that target.
  Driver API loads the PTX and launches 128-thread blocks for 4099 FP32 elements.
  Every output must exactly equal the CPU sum; NaNs and infinities fail.
- `--cublas-test`: cuBLAS computes `C = 0.75*A*B + 0.25*C`, with column-major
  matrices of sizes 17x19 and 19x13. All 221 FP32 outputs are checked against
  a double-precision CPU reference with tolerance `1e-5 * (1 + abs(expected))`.
  The maximum absolute error is reported. NaNs and infinities fail.

`--all-tests` selects both. Compute flags cannot be combined with `--query-only`.
Device allocations, modules, NVRTC programs, cuBLAS handles and the CUDA context
are released before exit. Compute-library symbols are loaded only when their
test is selected, so the default memory test does not require NVRTC or cuBLAS.

Default compute libraries on this DRIVE installation:

```text
/usr/local/cuda-11.4/targets/aarch64-linux/lib/libnvrtc.so.11.2
/usr/local/cuda-11.4/targets/aarch64-linux/lib/libcublas.so.11
```

Options: `--library PATH`, `--device N`, `--bytes N`, `--query-only`,
`--kernel-test`, `--cublas-test`, `--all-tests`, `--nvrtc-library PATH`,
`--cublas-library PATH`, `--help`.
Exit codes: 0 = pass; 2 = invalid arguments; 3 = library/symbol problem;
4 = driver/device query failure; 5 = context or memory test failure;
6 = cleanup failure after an otherwise successful test;
7 = kernel/NVRTC test failure, including its library/symbol problems;
8 = cuBLAS test failure, including its library/symbol problems.

## Target verification

The exported executable was uploaded to `/var/lib/fake_usr/lib/cuda-probe` on
`192.168.15.111`. The previous memory-only probe was retained alongside it as
`cuda-probe.pre-compute-20261115T185735Z`. Uploaded and local SHA256 values matched.
One `--all-tests` run completed with exit 0 in approximately 2.2 seconds:

```text
Driver API version: 12.1 (raw 12010)
Device 0: Orin
Compute capability: 8.7 (sm_87)
Memory round-trip: PASS (1048576 bytes verified)
NVRTC version: 11.4; PTX target: compute_87 (GPU sm_87)
GPU vector addition: PASS (4099 FP32 results verified)
cuBLAS version: 11606
cuBLAS SGEMM: PASS (17x19 * 19x13, 221 FP32 results verified; max abs error 0)
Result: PASS
```

Full command output, elapsed time, boot ID, SHA256 and post-test temperatures are
in `artifacts/hardware-compute-run.json`. Driver API 12.1 and NVRTC 11.4 are
versions of different components. No driver, firmware or system configuration
was replaced. This checks the tested FP32 paths; it does not establish cuDNN,
PyTorch, every CUDA Runtime API, or sustained-load stability.

## API references

- https://docs.nvidia.com/cuda/archive/12.1.0/cuda-driver-api/group__CUDA__DEVICE.html
- https://docs.nvidia.com/cuda/archive/12.1.0/cuda-driver-api/group__CUDA__DEVICE__DEPRECATED.html
- https://docs.nvidia.com/cuda/archive/12.1.0/cuda-driver-api/group__CUDA__CTX.html
- https://docs.nvidia.com/cuda/archive/12.1.0/cuda-driver-api/group__CUDA__MEM.html
- https://docs.nvidia.com/cuda/archive/12.1.0/cuda-driver-api/group__CUDA__MODULE.html
- https://docs.nvidia.com/cuda/archive/12.1.0/cuda-driver-api/group__CUDA__EXEC.html
- https://docs.nvidia.com/cuda/archive/11.4.4/nvrtc/index.html
- https://docs.nvidia.com/cuda/archive/11.4.4/cublas/index.html
