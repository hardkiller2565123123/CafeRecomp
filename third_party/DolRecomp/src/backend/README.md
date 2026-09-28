# Backends

DolRecomp has a portable C backend and an LLVM object backend. Both consume
DolIR and use the same CPU state and runtime helpers at architectural
boundaries.

The C backend emits split source files and a manifest. `emitter.c` handles code
generation, while `dispatch.c` emits the runtime dispatch tables.

The LLVM backend lives in `llvm/`:

- `emitter.cpp` owns function construction and the public entry wrapper.
- `register_state.cpp` promotes architectural state to SSA.
- `control_flow.cpp` and `branch_targets.cpp` lower guest control flow.
- `memory.cpp` lowers RAM accesses and external-memory fallbacks.
- `floating_point.cpp`, `paired_single.cpp`, `paired_fma.cpp`, and `psq.cpp`
  lower floating-point and paired-single operations.
- `fp_fixups.cpp` contains exact exceptional-value repair paths.
- `target.cpp` and `passes.cpp` configure target machines and LLVM's
  optimization pipeline.
- `exits.cpp` materializes architectural state at runtime boundaries.

Generated native regions enter through a cheap interception guard. With no
guest hooks active, execution stays native. A patch-bearing region exits with
architectural state materialized so the runtime can dispatch the hook.

`module_abi.*` defines variant selection and `variant_output.*` emits the module
table used by runtimes. Public module ABI changes must be versioned. Internal
LLVM function signatures are not part of that ABI.
