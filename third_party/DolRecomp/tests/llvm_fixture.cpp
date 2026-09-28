#include "backend/llvm/llvm_backend.h"
#include "cpu/cpu.h"
#include "ir/dolir_builder.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      std::fprintf(stderr, "check failed: %s:%d: %s\n", __FILE__, __LINE__,    \
                   #x);                                                        \
      return 1;                                                                \
    }                                                                          \
  } while (0)

static u32 encode_spr(u16 spr) {
  return ((spr & 31u) << 5) | ((spr >> 5) & 31u);
}

static u32 mfspr(u8 destination, u16 spr) {
  return 0x7C0002A6u | (u32(destination) << 21) | (encode_spr(spr) << 11);
}

static u32 mtspr(u8 source, u16 spr) {
  return 0x7C0003A6u | (u32(source) << 21) | (encode_spr(spr) << 11);
}

static u32 xform(u32 xo, u8 destination, u8 a, u8 b) {
  return (31u << 26) | (u32(destination) << 21) | (u32(a) << 16) |
         (u32(b) << 11) | (xo << 1);
}

static u32 paired_aform(u8 xo, u8 destination, u8 a, u8 b, u8 c) {
  return (4u << 26) | (u32(destination) << 21) | (u32(a) << 16) |
         (u32(b) << 11) | (u32(c) << 6) | (u32(xo) << 1);
}

static u32 psq_dform(bool store, u8 reg, u8 base, u16 displacement) {
  return ((store ? 60u : 56u) << 26) | (u32(reg) << 21) | (u32(base) << 16) |
         (displacement & 0x0fffu);
}

static u32 branch(bool link, u32 delta) {
  return 0x48000000u | (delta & 0x03fffffcu) | (link ? 1u : 0u);
}

static bool add_chunk(DolIRModule *module, const u32 *words, u32 count,
                      u32 address) {
  PPCInst *instructions = new PPCInst[count];
  for (u32 i = 0; i < count; i++)
    instructions[i] = ppc_decode(words[i], address + i * 4u);
  const bool result = dolir_build_chunk(module, instructions, count, address);
  delete[] instructions;
  return result;
}

static std::vector<unsigned char> read_file(const std::string &path) {
  FILE *file = std::fopen(path.c_str(), "rb");
  if (!file)
    return {};
  std::vector<unsigned char> bytes;
  unsigned char buffer[4096];
  size_t count;
  while ((count = std::fread(buffer, 1, sizeof(buffer), file)) != 0)
    bytes.insert(bytes.end(), buffer, buffer + count);
  std::fclose(file);
  return bytes;
}

int main(int argc, char **argv) {
  CHECK(argc == 3);
  DolLLVMOptions fingerprintOptions{};
  char x86Fingerprint[1024];
  char armFingerprint[1024];
  fingerprintOptions.target_profile = DOLLLVM_TARGET_X86_64_V3;
  CHECK(dolllvm_codegen_fingerprint(&fingerprintOptions, x86Fingerprint,
                                    sizeof(x86Fingerprint)));
  fingerprintOptions.target_profile = DOLLLVM_TARGET_AARCH64_A57;
  CHECK(dolllvm_codegen_fingerprint(&fingerprintOptions, armFingerprint,
                                    sizeof(armFingerprint)));
  CHECK(std::strcmp(x86Fingerprint, armFingerprint) != 0);
  CHECK(std::strstr(x86Fingerprint, "native-abi=2") != nullptr);
  DolIRModule module;
  dolir_module_init(&module);

  const u32 main_words[] = {
      0x38600000u, 0x00000000u, 0x38800000u, 0x7C841A14u, 0x90610000u,
      0x38630001u, 0x2C03000Au, 0x4180FFF4u, 0xEE32A4FAu, 0x4E800020u,
  };
  CHECK(add_chunk(&module, main_words, 10, 0x80001000u));

  const u32 spr_words[] = {
      mtspr(3, 273),
      mfspr(4, 273),
      0x4E800020u,
  };
  CHECK(add_chunk(&module, spr_words, 3, 0x80002000u));

  const u32 segment_words[] = {
      0x7DC401A4u, 0x7D6304A6u, 0x7DE081E4u, 0x7D806D26u, 0x4E800020u,
  };
  CHECK(add_chunk(&module, segment_words, 5, 0x80002100u));

  const u32 fpscr_words[] = {
      0xFFE0004Cu,
      0xFDA0048Eu,
      0x4E800020u,
  };
  CHECK(add_chunk(&module, fpscr_words, 3, 0x80002200u));

  const u32 lswx_words[] = {
      0x7D34AC2Au,
      0x4E800020u,
  };
  CHECK(add_chunk(&module, lswx_words, 2, 0x80002300u));

  const u32 cache_words[] = {
      0x7C11906Cu,
      0x4E800020u,
  };
  CHECK(add_chunk(&module, cache_words, 2, 0x80002400u));

  const u32 trap_words[] = {
      0x0C85FFFEu,
      0x38630001u,
      0x4E800020u,
  };
  CHECK(add_chunk(&module, trap_words, 3, 0x80002500u));

  const u32 sc_words[] = {0x44000002u};
  CHECK(add_chunk(&module, sc_words, 1, 0x80002600u));

  const u32 rfi_words[] = {0x4C000064u};
  CHECK(add_chunk(&module, rfi_words, 1, 0x80002700u));

  const u32 dcbz_l_words[] = {
      0x100537ECu,
      0x4E800020u,
  };
  CHECK(add_chunk(&module, dcbz_l_words, 2, 0x80002800u));

  const u32 psq_words[] = {
      0xE0240000u,
      0xF0240008u,
      0x4E800020u,
  };
  CHECK(add_chunk(&module, psq_words, 3, 0x80002880u));

  const u32 ecowx_words[] = {
      0x7D6C6B6Cu,
      0x4E800020u,
  };
  CHECK(add_chunk(&module, ecowx_words, 2, 0x80002900u));

  const u32 float_words[] = {
      0xEC22182Au, 0xEC853028u, 0xECE80272u, 0xED4B6024u, 0xFDAE782Au,
      0xFE119028u, 0xFE740572u, 0xFED7C024u, 0x4E800020u,
  };
  CHECK(add_chunk(&module, float_words, 9, 0x80002A00u));

  const u32 paired_words[] = {
      0x1022182Au, 0x10E80272u, 0x11AE83FAu, 0x10A03030u,
      0x110D7000u, 0x10853460u, 0x4E800020u,
  };
  CHECK(add_chunk(&module, paired_words, 7, 0x80002B00u));

  // Runtime boundaries must not reset the dispatcher budget.
  const u32 budget_words[] = {
      0x38630001u, 0x00000000u, 0x2C032710u, 0x4180FFF4u, 0x4E800020u,
  };
  CHECK(add_chunk(&module, budget_words, 5, 0x80002C00u));

  // External tail branches share the same budget across chunks.
  const u32 cross_chunk_a[] = {0x48000100u};
  const u32 cross_chunk_b[] = {0x4BFFFF00u};
  CHECK(add_chunk(&module, cross_chunk_a, 1, 0x80002D00u));
  CHECK(add_chunk(&module, cross_chunk_b, 1, 0x80002E00u));

  const u32 property_words[] = {
      xform(266, 3, 4, 5),   xform(40, 6, 7, 8),     xform(235, 9, 10, 11),
      xform(28, 13, 12, 14), xform(444, 16, 15, 17), xform(316, 19, 18, 20),
      xform(24, 22, 21, 23), xform(536, 25, 24, 26), xform(26, 28, 27, 0),
      0x4E800020u,
  };
  CHECK(add_chunk(&module, property_words, 10, 0x80003000u));

  const u32 memory_words[] = {
      0x80640000u,
      0x90640004u,
      0x38A50001u,
      0x4E800020u,
  };
  CHECK(add_chunk(&module, memory_words, 4, 0x80003100u));

  const u32 reverse_words[] = {
      xform(534, 3, 0, 4),
      xform(662, 3, 0, 5),
      0x4E800020u,
  };
  CHECK(add_chunk(&module, reverse_words, 3, 0x80003200u));

  const u32 constant_memory_words[] = {
      0x3C808000u,
      0x80640600u,
      0x90640604u,
      0x4E800020u,
  };
  CHECK(add_chunk(&module, constant_memory_words, 4, 0x80003300u));

  const u32 icbi_words[] = {
      xform(982, 0, 17, 18),
      0x4E800020u,
  };
  CHECK(add_chunk(&module, icbi_words, 2, 0x80003400u));

  const u32 call_words[] = {
      mfspr(0, 8), 0x480000FDu, mtspr(0, 8), 0x38630001u, 0x4E800020u,
  };
  const u32 callee_words[] = {
      0x38840002u,
      0x4E800020u,
  };
  CHECK(add_chunk(&module, call_words, 5, 0x80003500u));
  CHECK(add_chunk(&module, callee_words, 2, 0x80003600u));

  // Packed CR is an architectural boundary; normal lowering tracks fields.
  const u32 mfcr_words[] = {0x7D400026u, 0x4E800020u};
  const u32 mtcrf_words[] = {0x7D4FF120u, 0x7D600026u, 0x4E800020u};
  const u32 compare_mfcr_words[] = {
      0x2C030000u,
      0x7D400026u,
      0x4E800020u,
  };
  CHECK(add_chunk(&module, mfcr_words, 2, 0x80003700u));
  CHECK(add_chunk(&module, mtcrf_words, 3, 0x80003710u));
  CHECK(add_chunk(&module, compare_mfcr_words, 3, 0x80003720u));
  const u32 xer_roundtrip_words[] = {
      mtspr(10, 1),
      mfspr(11, 1),
      0x4E800020u,
  };
  const u32 carry_chain_words[] = {
      xform(10, 5, 3, 4),
      xform(138, 6, 7, 8),
      mfspr(10, 1),
      0x4E800020u,
  };
  CHECK(add_chunk(&module, xer_roundtrip_words, 3, 0x80003740u));
  CHECK(add_chunk(&module, carry_chain_words, 4, 0x80003750u));
  const u32 mcrxr_words[] = {
      0x7D000400u,
      0x7D400026u,
      mfspr(11, 1),
      0x4E800020u,
  };
  CHECK(add_chunk(&module, mcrxr_words, 4, 0x80003760u));
  const u32 cr_loop_words[] = {
      0x38600000u, 0x38630001u, 0x2C030064u, 0x4180FFF8u, 0x4E800020u,
  };
  CHECK(add_chunk(&module, cr_loop_words, 5, 0x80003800u));

  const u32 paired_add_words[] = {
      paired_aform(21, 1, 2, 3, 0),
      0x4E800020u,
  };
  CHECK(add_chunk(&module, paired_add_words, 2, 0x80003900u));

  const u32 paired_chain_words[] = {
      paired_aform(21, 1, 2, 3, 0),
      paired_aform(25, 4, 1, 0, 5),
      paired_aform(21, 6, 4, 7, 0),
      paired_aform(20, 8, 6, 9, 0),
      0x4E800020u,
  };
  CHECK(add_chunk(&module, paired_chain_words, 5, 0x80003920u));

  const u32 paired_memory_words[] = {
      psq_dform(false, 2, 4, 0),    psq_dform(false, 3, 4, 8),
      paired_aform(25, 1, 2, 0, 3), paired_aform(21, 1, 1, 5, 0),
      psq_dform(true, 1, 4, 16),    0x4E800020u,
  };
  CHECK(add_chunk(&module, paired_memory_words, 6, 0x80003940u));

  const u32 paired_fma_words[] = {
      paired_aform(29, 10, 2, 4, 3),
      paired_aform(28, 11, 2, 4, 3),
      paired_aform(31, 12, 2, 4, 3),
      paired_aform(30, 13, 2, 4, 3),
      paired_aform(14, 14, 2, 4, 3),
      paired_aform(15, 15, 2, 4, 3),
      0x4E800020u,
  };
  CHECK(add_chunk(&module, paired_fma_words, 7, 0x80003980u));

  const u32 paired_ssa_words[] = {
      paired_aform(21, 1, 2, 3, 0),
      paired_aform(25, 4, 1, 0, 1),
      paired_aform(21, 6, 4, 1, 0),
      paired_aform(20, 8, 6, 4, 0),
      0x4E800020u,
  };
  CHECK(add_chunk(&module, paired_ssa_words, 5, 0x800039C0u));

  const u32 paired_fma_one_words[] = {
      paired_aform(29, 10, 2, 4, 3),
      0x4E800020u,
  };
  CHECK(add_chunk(&module, paired_fma_one_words, 2, 0x80003A00u));

  const u32 paired_fma_chain_words[] = {
      paired_aform(21, 1, 2, 3, 0), paired_aform(29, 4, 1, 1, 1),
      paired_aform(28, 5, 4, 1, 1), paired_aform(31, 6, 5, 1, 1),
      paired_aform(30, 7, 6, 1, 1), paired_aform(14, 8, 7, 1, 1),
      paired_aform(15, 9, 8, 1, 1), 0x4E800020u,
  };
  CHECK(add_chunk(&module, paired_fma_chain_words, 8, 0x80003A20u));

  const u32 psq_fma_words[] = {
      psq_dform(false, 2, 4, 0),
      psq_dform(false, 3, 4, 8),
      paired_aform(29, 1, 2, 2, 3),
      psq_dform(true, 1, 4, 16),
      0x4E800020u,
  };
  CHECK(add_chunk(&module, psq_fma_words, 5, 0x80003A60u));

  const u32 fixed_gqr_fma_words[] = {
      0x3D400004u,
      0x614A0004u,
      mtspr(10, 912),
      psq_dform(false, 2, 4, 0),
      psq_dform(false, 3, 4, 2),
      paired_aform(29, 1, 2, 2, 3),
      psq_dform(true, 1, 4, 4),
      0x4E800020u,
  };
  CHECK(add_chunk(&module, fixed_gqr_fma_words, 8, 0x80003AA0u));

  const u32 return_call_words[] = {
      mfspr(0, 8),
      0x480000FDu,
  };
  const u32 return_continuation_words[] = {
      mtspr(0, 8),
      0x38A50007u,
      0x4E800020u,
  };
  const u32 return_callee_words[] = {
      0x38C60001u,
      0x4E800020u,
  };
  CHECK(add_chunk(&module, return_call_words, 2, 0x80003B00u));
  CHECK(add_chunk(&module, return_continuation_words, 3, 0x80003B08u));
  CHECK(add_chunk(&module, return_callee_words, 2, 0x80003C00u));

  // Both indirect returns can dispatch to the linked branch's fallback
  // continuation. The mtctr/bctr path also carries modified state into it.
  const u32 indirect_fallback_words[] = {
      branch(true, 0x10u),
      0x00000000u,
      mtspr(3, 9),
      0x4E800420u,
      0x4E800020u,
  };
  CHECK(add_chunk(&module, indirect_fallback_words, 5, 0x80003D00u));

  const u32 mtmsr_words[] = {
      xform(146, 3, 0, 0),
      0x4E800020u,
  };
  CHECK(add_chunk(&module, mtmsr_words, 2, 0x80003D20u));

  CHECK(dolir_verify(&module, stderr));
  DolLLVMOptions options{};
  options.optimization_level = 2;
  options.verify = 1;
  options.emit_ir = 1;
  options.ir_path = argv[2];
  options.fixed_memory_layout = 1;
  options.ram_size = GC_MAIN_RAM_SIZE;
  options.mem2_size = WII_MEM2_SIZE;
  DolLLVMFunctionRange ranges[8]{};
  const u32 range_bounds[8][2] = {
      {0x80002D00u, 0x80002D04u}, {0x80002E00u, 0x80002E04u},
      {0x80003300u, 0x80003310u},
      {0x80003500u, 0x80003514u}, {0x80003600u, 0x80003608u},
      {0x80003B00u, 0x80003B08u}, {0x80003B08u, 0x80003B14u},
      {0x80003C00u, 0x80003C08u},
  };
  for (u32 index = 0; index < 8; index++) {
    ranges[index].start = range_bounds[index][0];
    ranges[index].end = range_bounds[index][1];
  }
  options.function_ranges = ranges;
  options.function_range_count = (u32)(sizeof(ranges) / sizeof(ranges[0]));
  CHECK(dolllvm_emit_object(&module, argv[1], &options, stderr));
  CHECK(dolllvm_object_matches_options(argv[1], &options));

  const std::string memoryStateObject = std::string(argv[1]) + ".state-memory";
  const std::string memoryStateIR = std::string(argv[2]) + ".state-memory";
  options.state_in_memory = 1;
  options.ir_path = memoryStateIR.c_str();
  CHECK(dolllvm_emit_object(&module, memoryStateObject.c_str(), &options,
                            stderr));
  options.state_in_memory = 0;
  options.ir_path = argv[2];

  DolIRModule splitCaller;
  DolIRModule splitCallee;
  dolir_module_init(&splitCaller);
  dolir_module_init(&splitCallee);
  const u32 split_caller_words[] = {
      mfspr(0, 8), 0x38630001u, 0x480000F9u, mtspr(0, 8), 0x4E800020u,
  };
  const u32 split_callee_words[] = {0x38840002u, 0x4E800020u};
  const u32 split_memory_caller_words[] = {
      mfspr(0, 8), 0x480000FDu, mtspr(0, 8), 0x38A50001u, 0x4E800020u,
  };
  const u32 split_memory_callee_words[] = {
      0x3C808000u, 0x80640600u, 0x90640604u, 0x4E800020u,
  };
  CHECK(add_chunk(&splitCaller, split_caller_words, 5, 0x80004000u));
  CHECK(add_chunk(&splitCallee, split_callee_words, 2, 0x80004100u));
  CHECK(add_chunk(&splitCaller, split_memory_caller_words, 5, 0x80004500u));
  CHECK(add_chunk(&splitCallee, split_memory_callee_words, 4, 0x80004600u));
  DolLLVMFunctionRange splitRanges[4]{};
  splitRanges[0].start = 0x80004000u;
  splitRanges[0].end = 0x80004014u;
  splitRanges[1].start = 0x80004100u;
  splitRanges[1].end = 0x80004108u;
  splitRanges[2].start = 0x80004500u;
  splitRanges[2].end = 0x80004514u;
  splitRanges[3].start = 0x80004600u;
  splitRanges[3].end = 0x80004610u;
  CHECK(
      dolllvm_analyze_function_abi(&splitCaller.functions[0], &splitRanges[0]));
  CHECK(
      dolllvm_analyze_function_abi(&splitCallee.functions[0], &splitRanges[1]));
  CHECK(
      dolllvm_analyze_function_abi(&splitCaller.functions[1], &splitRanges[2]));
  CHECK(
      dolllvm_analyze_function_abi(&splitCallee.functions[1], &splitRanges[3]));
  CHECK((splitRanges[3].abi_flags &
         DOLLLVM_FUNCTION_ABI_NATIVE_MEMORY) != 0);
  const DolLLVMCallEdge splitEdges[] = {
      {0x80004000u, 0x80004100u},
      {0x80004500u, 0x80004600u},
  };
  CHECK(dolllvm_propagate_function_abis(splitRanges, 4, splitEdges, 2));
  DolLLVMOptions splitOptions{};
  splitOptions.optimization_level = 2;
  splitOptions.verify = 1;
  splitOptions.fixed_memory_layout = 1;
  splitOptions.ram_size = GC_MAIN_RAM_SIZE;
  splitOptions.mem2_size = WII_MEM2_SIZE;
  splitOptions.function_ranges = splitRanges;
  splitOptions.function_range_count = 4;
  const std::string splitCallerObject = std::string(argv[1]) + ".abi.caller";
  const std::string splitCalleeObject = std::string(argv[1]) + ".abi.callee";
  const std::string splitCallerIR = std::string(argv[2]) + ".abi.caller";
  splitOptions.emit_ir = 1;
  splitOptions.ir_path = splitCallerIR.c_str();
  CHECK(dolllvm_emit_object(&splitCaller, splitCallerObject.c_str(),
                            &splitOptions, stderr));
  splitOptions.emit_ir = 0;
  splitOptions.ir_path = nullptr;
  CHECK(dolllvm_emit_object(&splitCallee, splitCalleeObject.c_str(),
                            &splitOptions, stderr));
  dolir_module_free(&splitCaller);
  dolir_module_free(&splitCallee);

  DolIRModule loopCaller;
  DolIRModule loopCallee;
  dolir_module_init(&loopCaller);
  dolir_module_init(&loopCallee);
  const u32 loop_caller_words[] = {
      mfspr(0, 8), mtspr(5, 9), 0x480000F9u,
      0x4200FFFCu, mtspr(0, 8), 0x4E800020u,
  };
  CHECK(add_chunk(&loopCaller, loop_caller_words, 6, 0x80004200u));
  CHECK(add_chunk(&loopCallee, split_callee_words, 2, 0x80004300u));
  DolLLVMFunctionRange loopRanges[2]{};
  loopRanges[0].start = 0x80004200u;
  loopRanges[0].end = 0x80004218u;
  loopRanges[1].start = 0x80004300u;
  loopRanges[1].end = 0x80004308u;
  CHECK(dolllvm_analyze_function_abi(&loopCaller.functions[0], &loopRanges[0]));
  CHECK(dolllvm_analyze_function_abi(&loopCallee.functions[0], &loopRanges[1]));
  const DolLLVMCallEdge loopEdge = {0x80004200u, 0x80004300u};
  CHECK(dolllvm_propagate_function_abis(loopRanges, 2, &loopEdge, 1));
  splitOptions.function_ranges = loopRanges;
  const std::string loopCallerObject =
      std::string(argv[1]) + ".abi.loop.caller";
  const std::string loopCalleeObject =
      std::string(argv[1]) + ".abi.loop.callee";
  CHECK(dolllvm_emit_object(&loopCaller, loopCallerObject.c_str(),
                            &splitOptions, stderr));
  CHECK(dolllvm_emit_object(&loopCallee, loopCalleeObject.c_str(),
                            &splitOptions, stderr));
  dolir_module_free(&loopCaller);
  dolir_module_free(&loopCallee);

  const std::string v2_path = std::string(argv[1]) + ".v2";
  const std::string v2_copy = std::string(argv[1]) + ".v2.copy";
  options.emit_ir = 0;
  options.target_profile = DOLLLVM_TARGET_X86_64_V2;
  options.symbol_suffix = "__x86_64_v2";
  const std::string v2_bitcode = v2_path + ".bc";
  const std::string v2_copy_bitcode = v2_copy + ".bc";
  options.emit_thinlto = 1;
  options.thinlto_path = v2_bitcode.c_str();
  CHECK(dolllvm_emit_object(&module, v2_path.c_str(), &options, stderr));
  options.thinlto_path = v2_copy_bitcode.c_str();
  CHECK(dolllvm_emit_object(&module, v2_copy.c_str(), &options, stderr));
  CHECK(dolllvm_object_matches_options(v2_path.c_str(), &options));
  CHECK(read_file(v2_path) == read_file(v2_copy));
  CHECK(read_file(v2_bitcode) == read_file(v2_copy_bitcode));
  options.emit_thinlto = 0;
  options.thinlto_path = nullptr;

  const std::string v3_path = std::string(argv[1]) + ".v3";
  options.target_profile = DOLLLVM_TARGET_X86_64_V3;
  options.symbol_suffix = "__x86_64_v3";
  CHECK(dolllvm_emit_object(&module, v3_path.c_str(), &options, stderr));
  CHECK(dolllvm_object_matches_options(v3_path.c_str(), &options));

  const std::string arm_path = std::string(argv[1]) + ".arm64";
  options.target_profile = DOLLLVM_TARGET_AARCH64_GENERIC;
  options.symbol_suffix = "__aarch64";
  CHECK(dolllvm_emit_object(&module, arm_path.c_str(), &options, stderr));
  CHECK(dolllvm_object_matches_options(arm_path.c_str(), &options));

  const std::string a57_path = std::string(argv[1]) + ".a57";
  const std::string a57_ir = std::string(argv[2]) + ".a57";
  options.target_profile = DOLLLVM_TARGET_AARCH64_A57;
  options.symbol_suffix = "__aarch64_a57";
  options.emit_ir = 1;
  options.ir_path = a57_ir.c_str();
  CHECK(dolllvm_emit_object(&module, a57_path.c_str(), &options, stderr));
  CHECK(dolllvm_object_matches_options(a57_path.c_str(), &options));

  const std::string instrumented_path = std::string(argv[1]) + ".instrumented";
  const std::string instrumented_ir = std::string(argv[2]) + ".instrumented";
  options.target_profile = DOLLLVM_TARGET_HOST;
  options.symbol_suffix = "__instrumented";
  options.instrumentation = DOLLLVM_INSTRUMENTATION_LOCKSTEP;
  options.emit_ir = 1;
  options.ir_path = instrumented_ir.c_str();
  CHECK(dolllvm_emit_object(&module, instrumented_path.c_str(), &options,
                            stderr));

  const std::string pgo_path = std::string(argv[1]) + ".pgo";
  const std::string pgo_ir = std::string(argv[2]) + ".pgo";
  options.symbol_suffix = "__pgo";
  options.instrumentation = DOLLLVM_INSTRUMENTATION_NONE;
  options.profile_generate_path = "%m.profraw";
  options.emit_ir = 1;
  options.ir_path = pgo_ir.c_str();
  CHECK(dolllvm_emit_object(&module, pgo_path.c_str(), &options, stderr));
  options.profile_generate_path = nullptr;
  const std::string fast_path = std::string(argv[1]) + ".fast";
  const std::string fast_ir = std::string(argv[2]) + ".fast";
  options.target_profile = DOLLLVM_TARGET_X86_64_V3;
  options.symbol_suffix = "__fast";
  options.semantics = DOLLLVM_SEMANTICS_FAST;
  options.ir_path = fast_ir.c_str();
  CHECK(dolllvm_emit_object(&module, fast_path.c_str(), &options, stderr));
  options.semantics = DOLLLVM_SEMANTICS_EXACT;
  options.emit_ir = 0;

  options.target_profile = DOLLLVM_TARGET_X86_64_V3;
  options.target_triple = "aarch64-unknown-linux-gnu";
  CHECK(!dolllvm_emit_object(&module, v3_path.c_str(), &options, stderr));
  dolir_module_free(&module);
  return 0;
}
