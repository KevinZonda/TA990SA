# CUDA Driver API probe

Small C diagnostic for the ARM64 DRIVE Orin Linux guest. It dynamically loads
the existing `/usr/lib/libcuda.so.1`; CUDA headers, `nvcc`, Python, and the CUDA
Runtime API are not build dependencies. The executable uses the target's glibc
and libdl rather than a static musl loader.

## Build on Windows

From this directory, with Docker Desktop running:

```powershell
.\build.ps1 -BuildImage
```

The ARM64 Ubuntu 20.04 image builds against glibc 2.31 and runs mock-driver
tests. The executable is exported to `build/cuda-probe`. Logs, SHA256, ELF
inspection, and build metadata are in `artifacts/`. No real GPU is used during
the container tests.

## Run on the board

Deploy `build/cuda-probe` to an executable filesystem, for example the existing
`/var/lib/fake_usr/bin/cuda-probe`, and set mode 755. `/opt/m` and `/opt/m0` were
mounted `noexec` when inspected; copying there alone does not make it runnable.

Start with device queries:

```sh
/var/lib/fake_usr/bin/cuda-probe --query-only
```

This initializes the CUDA driver, queries its API version, lists GPU names,
compute capabilities and total memory, and checks the selected device ordinal.
It creates no CUDA context or device allocation.

Then run the default 1 MiB allocation/copy verification:

```sh
/var/lib/fake_usr/bin/cuda-probe
```

This creates a temporary context, reports its free/total memory, allocates device
memory, copies a deterministic byte pattern to the device and back, verifies
every byte, then frees the allocation and destroys the context. Allocation size
is bounded to 16 MiB. Cleanup failures produce a failing exit status.

Options: `--library PATH`, `--device N`, `--bytes N`, `--query-only`, `--help`.
Exit codes: 0 = pass; 2 = invalid arguments; 3 = library/symbol problem;
4 = driver/device query failure; 5 = context or memory test failure;
6 = cleanup failure after an otherwise successful test.

This probe does not launch GPU kernels, benchmark performance, or validate
CUDA Runtime/cuBLAS/cuDNN/PyTorch compatibility. Successful mock tests do not
establish that the real Orin GPU or driver is working.

## API references

- https://docs.nvidia.com/cuda/archive/12.1.0/cuda-driver-api/group__CUDA__DEVICE.html
- https://docs.nvidia.com/cuda/archive/12.1.0/cuda-driver-api/group__CUDA__DEVICE__DEPRECATED.html
- https://docs.nvidia.com/cuda/archive/12.1.0/cuda-driver-api/group__CUDA__CTX.html
- https://docs.nvidia.com/cuda/archive/12.1.0/cuda-driver-api/group__CUDA__MEM.html
