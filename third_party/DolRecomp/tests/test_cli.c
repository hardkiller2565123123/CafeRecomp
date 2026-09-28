#include "app/cli.h"

#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "check failed: %s:%d: %s\n", \
    __FILE__, __LINE__, #x); return 1; } } while (0)

int main(void) {
    char* valid[] = {
        "dolrecomp", "--gamecube", "--backend=llvm",
        "--targets=x86-64-v2,x86-64-v3", "--semantics=fast",
        "--state-in-memory",
        "--instrumentation=lockstep", "--profile-use=profile.profdata",
        "--partition-instructions", "512", "--partition-seed", "42",
        "input.dol", "output",
    };
    CliOptions options;
#ifdef DOLRECOMP_ENABLE_LLVM
    CHECK(parse_cli((int)(sizeof(valid) / sizeof(valid[0])), valid, &options));
    CHECK(options.backend == DOLRECOMP_BACKEND_LLVM);
    CHECK(strcmp(options.llvm_targets, "x86-64-v2,x86-64-v3") == 0);
    CHECK(options.fast_semantics && options.lockstep_instrumentation);
    CHECK(options.state_in_memory);
    CHECK(strcmp(options.profile_use_path, "profile.profdata") == 0);
    CHECK(options.partition_instructions == 512u);
    CHECK(options.partition_seed == 42u);
#else
    (void)valid;
#endif

    char* bad_target[] = {
        "dolrecomp", "--gamecube", "--targets=x86-64-v4", "input.dol",
    };
    CHECK(!parse_cli((int)(sizeof(bad_target) / sizeof(bad_target[0])),
                     bad_target, &options));

    char* conflicting_profile[] = {
        "dolrecomp", "--gamecube", "--profile-use=one",
        "--profile-generate=two", "input.dol",
    };
    CHECK(!parse_cli(
        (int)(sizeof(conflicting_profile) / sizeof(conflicting_profile[0])),
        conflicting_profile, &options));
    return 0;
}
