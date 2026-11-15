#include "../compute_tests.h"
#include "mock_helpers.h"

struct _nvrtcProgram { int unused; };
static struct _nvrtcProgram instance;
int nvrtcVersion(int *major, int *minor) { *major = 11; *minor = 4; return 0; }
int nvrtcGetNumSupportedArchs(int *count) { *count = mode("arch_count_bad") ? 257 : 2; return 0; }
int nvrtcGetSupportedArchs(int *archs)
{
    archs[0] = mode("arch_none") ? 90 : 80;
    archs[1] = mode("arch_none") ? 100 : 86;
    return 0;
}
const char *nvrtcGetErrorString(int result) { (void)result; return "NVRTC mock error"; }
int nvrtcCreateProgram(nvrtcProgram *program, const char *source, const char *name,
                       int headers, const char **text, const char **names)
{
    (void)source; (void)name; (void)headers; (void)text; (void)names;
    trace("nvrtc_create");
    *program = &instance;
    return 0;
}
int nvrtcCompileProgram(nvrtcProgram program, int count, const char **options)
{
    (void)program;
    trace("nvrtc_compile");
    if (count != 2 || strcmp(options[0], "--gpu-architecture=compute_86")) return 5;
    return mode("compile_fail") ? 6 : 0;
}
int nvrtcGetProgramLogSize(nvrtcProgram program, size_t *bytes)
{
    (void)program;
    *bytes = mode("compile_fail") ? sizeof("simulated compilation failure") : 1;
    return 0;
}
int nvrtcGetProgramLog(nvrtcProgram program, char *log)
{
    (void)program;
    strcpy(log, mode("compile_fail") ? "simulated compilation failure" : "");
    return 0;
}
int nvrtcGetPTXSize(nvrtcProgram program, size_t *bytes)
{ (void)program; *bytes = mode("ptx_size_bad") ? 0 : sizeof("mock PTX"); return 0; }
int nvrtcGetPTX(nvrtcProgram program, char *ptx)
{ (void)program; strcpy(ptx, "mock PTX"); return 0; }
int nvrtcDestroyProgram(nvrtcProgram *program)
{
    trace("nvrtc_destroy");
    *program = NULL;
    return mode("nvrtc_destroy_fail") ? 11 : 0;
}
