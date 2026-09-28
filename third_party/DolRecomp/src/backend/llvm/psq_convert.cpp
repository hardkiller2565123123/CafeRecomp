#include "backend/llvm/psq_convert.h"

#include <llvm/IR/Constants.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/Module.h>

namespace dolllvm {

using namespace llvm;

Value *extendPSQFloat(IRBuilder<> &builder, Module &module, Value *bits) {
  Type *i32 = Type::getInt32Ty(module.getContext());
  Type *i64 = Type::getInt64Ty(module.getContext());
  Value *x = builder.CreateZExt(bits, i64);
  Value *exponent =
      builder.CreateAnd(builder.CreateLShr(bits, 23), builder.getInt32(0xff));
  Value *fraction = builder.CreateAnd(bits, builder.getInt32(0x007fffff));
  Value *normal =
      builder.CreateAnd(builder.CreateICmpUGT(exponent, builder.getInt32(0)),
                        builder.CreateICmpULT(exponent, builder.getInt32(255)));

  Value *normalY =
      builder.CreateZExt(builder.CreateICmpEQ(builder.CreateLShr(exponent, 7),
                                              builder.getInt32(0)),
                         i64);
  Value *specialY = builder.CreateZExt(builder.CreateLShr(exponent, 7), i64);
  Value *y = builder.CreateSelect(normal, normalY, specialY);
  Value *regular = builder.CreateOr(
      builder.CreateShl(builder.CreateAnd(x, builder.getInt64(0xc0000000ULL)),
                        32),
      builder.CreateOr(
          builder.CreateMul(y, builder.getInt64(0x3800000000000000ULL)),
          builder.CreateShl(
              builder.CreateAnd(x, builder.getInt64(0x3fffffffULL)), 29)));

  Function *ctlz = Intrinsic::getDeclaration(&module, Intrinsic::ctlz, {i32});
  Value *leading = builder.CreateCall(ctlz, {fraction, builder.getInt1(false)});
  Value *shift = builder.CreateSub(leading, builder.getInt32(8));
  Value *shiftedFraction = builder.CreateShl(fraction, shift);
  Value *subnormalExponent = builder.CreateSub(builder.getInt32(897), shift);
  Value *subnormal = builder.CreateOr(
      builder.CreateShl(builder.CreateAnd(x, builder.getInt64(0x80000000ULL)),
                        32),
      builder.CreateOr(
          builder.CreateShl(builder.CreateZExt(subnormalExponent, i64), 52),
          builder.CreateShl(builder.CreateZExt(
                                builder.CreateAnd(shiftedFraction,
                                                  builder.getInt32(0x007fffff)),
                                i64),
                            29)));
  Value *isSubnormal =
      builder.CreateAnd(builder.CreateICmpEQ(exponent, builder.getInt32(0)),
                        builder.CreateICmpNE(fraction, builder.getInt32(0)));
  return builder.CreateBitCast(
      builder.CreateSelect(isSubnormal, subnormal, regular),
      Type::getDoubleTy(module.getContext()));
}

Value *dequantizePSQInteger(IRBuilder<> &builder, Value *value, Value *scale,
                            bool isSigned) {
  Value *single = isSigned ? builder.CreateSIToFP(value, builder.getFloatTy())
                           : builder.CreateUIToFP(value, builder.getFloatTy());
  Value *exponent = builder.CreateSub(builder.getInt32(127), scale);
  Value *power = builder.CreateBitCast(builder.CreateShl(exponent, 23),
                                       builder.getFloatTy());
  return builder.CreateFMul(single, power, "psq.dequantized");
}

Value *truncatePSQFloat(IRBuilder<> &builder, Value *value) {
  Value *single = value;
  if (!value->getType()->isFloatTy()) {
    single = builder.CreateConstrainedFPCast(
        Intrinsic::experimental_constrained_fptrunc, value,
        builder.getFloatTy(), nullptr, "psq.single", nullptr,
        RoundingMode::Dynamic, fp::ebIgnore);
  }
  Value *bits = builder.CreateBitCast(single, builder.getInt32Ty());
  Value *exponent = builder.CreateAnd(bits, builder.getInt32(0x7f800000));
  Value *fraction = builder.CreateAnd(bits, builder.getInt32(0x007fffff));
  Value *denormal =
      builder.CreateAnd(builder.CreateICmpEQ(exponent, builder.getInt32(0)),
                        builder.CreateICmpNE(fraction, builder.getInt32(0)));
  return builder.CreateSelect(denormal, builder.getInt32(0), bits);
}

Value *quantizePSQFloat(IRBuilder<> &builder, Value *value, Value *scale,
                        int minValue, int maxValue) {
  Value *single = value->getType()->isFloatTy()
                      ? value
                      : builder.CreateFPTrunc(value, builder.getFloatTy());
  Value *powerBits =
      builder.CreateShl(builder.CreateAdd(scale, builder.getInt32(127)), 23);
  Value *power = builder.CreateBitCast(powerBits, builder.getFloatTy());
  Value *scaled = builder.CreateFMul(single, power);
  Value *minimum = ConstantFP::get(builder.getFloatTy(), minValue);
  Value *maximum = ConstantFP::get(builder.getFloatTy(), maxValue);
  Value *clamped = builder.CreateSelect(
      builder.CreateFCmpUNO(scaled, scaled),
      ConstantFP::get(builder.getFloatTy(), 0.0),
      builder.CreateSelect(
          builder.CreateFCmpOLE(scaled, minimum), minimum,
          builder.CreateSelect(builder.CreateFCmpOGE(scaled, maximum), maximum,
                               scaled)));
  return builder.CreateFPToSI(clamped, builder.getInt32Ty());
}

} // namespace dolllvm
