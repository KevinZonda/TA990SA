CC ?= cc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra -Wpedantic -Werror -fstack-protector-strong
CPPFLAGS += -D_POSIX_C_SOURCE=200809L
LDFLAGS ?= -Wl,-z,relro,-z,now -pie

.PHONY: all check
all: build/cuda-probe

build:
	mkdir -p $@

build/cuda-probe: probe.c cuda_driver_minimal.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIE probe.c -o $@ $(LDFLAGS) -ldl

build/mock-cuda.so: tests/mock_cuda.c cuda_driver_minimal.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -shared tests/mock_cuda.c -o $@

build/mock-cuda-no-alloc.so: tests/mock_cuda.c cuda_driver_minimal.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -shared -DOMIT_MEM_ALLOC tests/mock_cuda.c -o $@

check: build/cuda-probe build/mock-cuda.so build/mock-cuda-no-alloc.so
	sh tests/test-probe.sh ./build/cuda-probe ./build/mock-cuda.so ./build/mock-cuda-no-alloc.so
