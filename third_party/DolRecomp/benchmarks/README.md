# LLVM backend benchmarks

Configure a release build with LLVM enabled:

```sh
cmake -S . -B build-llvm -DCMAKE_BUILD_TYPE=Release \
  -DDOLRECOMP_ENABLE_LLVM=ON \
  -DLLVM_DIR=/path/to/llvm/lib/cmake/llvm
cmake --build build-llvm --target llvm_backend_bench
```

Run every kernel or select one by name:

```sh
./build-llvm/llvm_backend_bench 1000000
./build-llvm/llvm_backend_bench 1000000 psq_fma
```

The first argument is the iteration count. Available kernels are listed in
`benchmarks/llvm_backend_bench.c`.

For repeatable measurements, use a release build, pin the process to one CPU,
and run several repetitions. On Linux:

```sh
taskset -c 2 perf stat -r 7 \
  -e cycles,instructions,branches,branch-misses \
  ./build-llvm/llvm_backend_bench 50000000 call_nomod
```

Keep before and after measurements on the same machine, target profile, CPU,
and power configuration. Record wall time, counters, code size, and relevant
disassembly together.
