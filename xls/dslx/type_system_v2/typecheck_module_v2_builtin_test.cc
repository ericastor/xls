// Copyright 2024 The XLS Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "xls/dslx/type_system/typecheck_test_utils.h"
#include "xls/dslx/type_system_v2/matchers.h"

namespace xls::dslx {
namespace {

using ::testing::AllOf;
using ::testing::HasSubstr;

TEST(TypecheckV2BuiltinTest, AndReduce) {
  EXPECT_THAT("const Y = and_reduce(u6:3);",
              TypecheckSucceeds(HasNodeWithType("Y", "uN[1]")));
}

TEST(TypecheckV2BuiltinTest, ArrayRevExplicit) {
  EXPECT_THAT("const Y = array_rev<u6, 4>([1, 2, 3, 4]);",
              TypecheckSucceeds(HasNodeWithType("Y", "uN[6][4]")));
}

TEST(TypecheckV2BuiltinTest, ArrayRevSizeMismatch) {
  EXPECT_THAT("const Y = array_rev<u6, 3>([u6:1, u6:2, u6:3, u6:4]);",
              TypecheckFails(HasSizeMismatch("u6[3]", "uN[6][4]")));
}

TEST(TypecheckV2BuiltinTest, ArrayRevElementMismatch) {
  EXPECT_THAT("const Y = array_rev<u6, 3>([1, 2, 3, 4]);",
              TypecheckFails(HasSizeMismatch("u6[3]", "uN[3][4]")));
}

TEST(TypecheckV2BuiltinTest, ArrayRevImplicitElementType) {
  EXPECT_THAT("const Y = array_rev([1, 2, 3, 4]);",
              TypecheckSucceeds(HasNodeWithType("Y", "uN[3][4]")));
}

TEST(TypecheckV2BuiltinTest, ArrayRevImplicit) {
  EXPECT_THAT("const Y = array_rev([u32:1, u32:2, u32:3, u32:4]);",
              TypecheckSucceeds(HasNodeWithType("Y", "uN[32][4]")));
}

TEST(TypecheckV2BuiltinTest, ArraySizeExplicit) {
  EXPECT_THAT("const Y = array_size<u32, 4>([u32:1, u32:2, u32:3, u32:4]);",
              TypecheckSucceeds(HasNodeWithType("Y", "uN[32]")));
}

TEST(TypecheckV2BuiltinTest, ArraySizeArraySizeMismatch) {
  EXPECT_THAT("const Y = array_size<u32, 3>([u32:1, u32:2, u32:3, u32:4]);",
              TypecheckFails(HasSizeMismatch("u32[3]", "uN[32][4]")));
}

TEST(TypecheckV2BuiltinTest, ArraySizeElementBitsMismatch) {
  EXPECT_THAT("const Y = array_size<u31, 4>([u32:1, u32:2, u32:3, u32:4]);",
              TypecheckFails(HasSizeMismatch("u31", "uN[32]")));
}

TEST(TypecheckV2BuiltinTest, ArraySizeImplicit) {
  EXPECT_THAT("const Y = array_size([1, 2, 3, 4]);",
              TypecheckSucceeds(HasNodeWithType("Y", "uN[32]")));
}

TEST(TypecheckV2BuiltinTest, ArraySliceAllImplicitSizes) {
  EXPECT_THAT(R"(
  const TM = [u16:1, u16:2, u16:3, u16:4];
  const TP = [u16:0, u16:0, u16:0];
  const Y = array_slice(TM, u32:10, TP);
  )",
              TypecheckSucceeds(HasNodeWithType("Y", "uN[16][3]")));
}

TEST(TypecheckV2BuiltinTest, ArraySliceSomeImplicitSizes) {
  EXPECT_THAT(R"(
  const TM = [u16:1, u16:2, u16:3, u16:4];
  const TP = [u16:0, u16:0, u16:0];
  const Y = array_slice<u16>(TM, u32:10, TP);
  )",
              TypecheckSucceeds(HasNodeWithType("Y", "uN[16][3]")));
}

TEST(TypecheckV2BuiltinTest, ArraySliceExplicitSizes) {
  EXPECT_THAT(R"(
  const TM = [u16:1, u16:2, u16:3, u16:4];
  const TP = [u16:0, u16:0, u16:0];
  const Y = array_slice<u16, 4, 32, 3>(TM, u32:10, TP);
  )",
              TypecheckSucceeds(HasNodeWithType("Y", "uN[16][3]")));
}

TEST(TypecheckV2BuiltinTest, AssertEqExplicitParametricType) {
  EXPECT_THAT(R"(
fn f(x: u32, y: u32) {
  assert_eq<u32>(x, y);
}
)",
              TypecheckSucceeds(HasNodeWithType("x", "uN[32]")));
}

TEST(TypecheckV2BuiltinTest, AssertEqExplicitParametricTypeConstants) {
  EXPECT_THAT(R"(
fn f() {
  assert_eq<u32>(u32:1, u32:2);
}
)",
              TypecheckSucceeds(HasNodeWithType("u32:1", "uN[32]")));
}

TEST(TypecheckV2BuiltinTest, AssertEqExplicitParametricTypeSecondMismatch) {
  EXPECT_THAT(R"(
fn f(x: u32, y: u31) {
  assert_eq<u32>(x, y);
}
)",
              TypecheckFails(HasSizeMismatch("u31", "u32")));
}

TEST(TypecheckV2BuiltinTest, AssertEqExplicitParametricTypeFirstMismatch) {
  EXPECT_THAT(R"(
fn f(x: u31, y: u32) {
  assert_eq<u32>(x, y);
}
)",
              TypecheckFails(HasSizeMismatch("u31", "u32")));
}

TEST(TypecheckV2BuiltinTest, AssertEqExplicitParametricTypeBothMismatch) {
  EXPECT_THAT(R"(
fn f(x: u31, y: u31) {
  assert_eq<u32>(x, y);
}
)",
              TypecheckFails(HasSizeMismatch("u31", "u32")));
}

TEST(TypecheckV2BuiltinTest, AssertEqExplicitParametricTypeNotAtype) {
  EXPECT_THAT(
      R"(
fn f(x: u32, y: u32) {
  assert_eq<u32:33>(x, y);
}
)",
      TypecheckFails(HasSubstr("Expected parametric type, saw `u32:33`")));
}

TEST(TypecheckV2BuiltinTest, AssertEqImplicitParametricType) {
  EXPECT_THAT(R"(
fn f(x: u32, y: u32) {
  assert_eq(x, y);
}
)",
              TypecheckSucceeds(HasNodeWithType("x", "uN[32]")));
}

TEST(TypecheckV2BuiltinTest, AssertEqImplicitParametricTypeTuple) {
  EXPECT_THAT(R"(
fn f(x: (u32), y: (u32)) {
  assert_eq(x, y);
}
)",
              TypecheckSucceeds(HasNodeWithType("x", "(uN[32])")));
}

TEST(TypecheckV2BuiltinTest, AssertEqImplicitParametricTypeMismatch) {
  EXPECT_THAT(R"(
fn f(x: u32, y: u31) {
  assert_eq(x, y);
}
)",
              TypecheckFails(HasSizeMismatch("u31", "u32")));
}

TEST(TypecheckV2BuiltinTest, AssertEqImplicitParametricTypeMismatchTuple) {
  EXPECT_THAT(R"(
fn f(x: u32, y: (u31)) {
  assert_eq(x, y);
}
)",
              TypecheckFails(HasTypeMismatch("(u31,)", "u32")));
}
TEST(TypecheckV2BuiltinTest, AssertLt) {
  EXPECT_THAT(
      R"(
fn foo(x: u10) -> u10 {
  assert_lt(x, 25);
  x
}

const X = foo(10);
)",
      TypecheckSucceeds(HasNodeWithType("25", "uN[10]")));
}

TEST(TypecheckV2BuiltinTest, AssertWithArray) {
  EXPECT_THAT(
      R"(
fn foo(x:u32) -> u32 {
  assert!(x>32, [1,2,3]);
  x
}

const X = foo(10);
)",
      TypecheckSucceeds(HasNodeWithType("[1, 2, 3]", "uN[8][3]")));
}

TEST(TypecheckV2BuiltinTest, Assert) {
  EXPECT_THAT(
      R"(
fn foo(x:u32) -> u32 {
  assert!(x>32, "Failed");
  x
}

const X = foo(10);
)",
      TypecheckSucceeds(AllOf(HasNodeWithType("X", "uN[32]"),
                              HasNodeWithType("\"Failed\"", "uN[8][6]"))));
}

TEST(TypecheckV2BuiltinTest, BitCount) {
  EXPECT_THAT(R"(
struct MyPoint { x: u32, y: u32 }

fn test_bit_count_size() {
    const x = bit_count<u32[4]>();
    const y = bit_count<bool>();
    const z = bit_count<MyPoint>();
})",
              TypecheckSucceeds(AllOf(HasNodeWithType("x", "uN[32]"),
                                      HasNodeWithType("y", "uN[32]"),
                                      HasNodeWithType("z", "uN[32]"))));
}

TEST(TypecheckV2BuiltinTest, BitCountMismatch) {
  EXPECT_THAT(R"(
const x: u31 = bit_count<u32[4]>();
)",
              TypecheckFails(HasSizeMismatch("u32", "u31")));
}

TEST(TypecheckV2BuiltinTest, BitCountImplicitIsIllegal) {
  EXPECT_THAT(R"(
const x: u31 = bit_count();
)",
              TypecheckFails(HasSubstr("Could not infer parametric")));
}

TEST(TypecheckV2BuiltinTest, BitSliceUpdate) {
  EXPECT_THAT(R"(const Y = bit_slice_update(u32:10, u33:11, u34:12);)",
              TypecheckSucceeds(HasNodeWithType("Y", "uN[32]")));
}

TEST(TypecheckV2BuiltinTest, BitSliceUpdateError) {
  EXPECT_THAT(R"(const Y: u64 = bit_slice_update(u32:10, u33:11, u34:12);)",
              TypecheckFails(HasSizeMismatch("uN[32]", "u64")));
}

TEST(TypecheckV2BuiltinTest, CheckedCast) {
  EXPECT_THAT(R"(const Y = checked_cast<u32>(u31:3);)",
              TypecheckSucceeds(HasNodeWithType("Y", "uN[32]")));
}

TEST(TypecheckV2BuiltinTest, CheckedCastExplicit) {
  EXPECT_THAT(R"(const Y = checked_cast<u32, u31>(u31:3);)",
              TypecheckSucceeds(HasNodeWithType("Y", "uN[32]")));
}

TEST(TypecheckV2BuiltinTest, CheckedCastMismatch) {
  EXPECT_THAT(R"(const Y = checked_cast<u32, u31>(u32:3);)",
              TypecheckFails(HasSizeMismatch("u32", "u31")));
}

TEST(TypecheckV2BuiltinTest, CheckedCastImplicitDestinationFails) {
  // This (intentionally) fails the same way as v1.
  EXPECT_THAT(R"(const Y: u32 = checked_cast(u31:3);)",
              TypecheckFails(HasSubstr("Could not infer parametric(s): DEST")));
}

TEST(TypecheckV2BuiltinTest, Clz) {
  EXPECT_THAT(R"(const Y = clz(u8:3);)",
              TypecheckSucceeds(HasNodeWithType("Y", "uN[8]")));
}

TEST(TypecheckV2BuiltinTest, Ctz) {
  EXPECT_THAT(R"(const Y = ctz(u8:3);)",
              TypecheckSucceeds(HasNodeWithType("Y", "uN[8]")));
}

TEST(TypecheckV2BuiltinTest, Decode) {
  EXPECT_THAT(R"(const Y = decode<u2, u32:5>(u5:1);)",
              TypecheckSucceeds(HasNodeWithType("Y", "uN[2]")));
}

TEST(TypecheckV2BuiltinTest, DecodeImplicitSize) {
  EXPECT_THAT(R"(const Y = decode<u2>(u5:1);)",
              TypecheckSucceeds(HasNodeWithType("Y", "uN[2]")));
}

TEST(TypecheckV2BuiltinTest, DecodeXn) {
  EXPECT_THAT(R"(const Y = decode<xN[bool:0x0][2]>(u1:1);)",
              TypecheckSucceeds(HasNodeWithType("Y", "uN[2]")));
}

TEST(TypecheckV2BuiltinTest, DecodeSizeMismatch) {
  EXPECT_THAT(R"(const Y: u3 = decode<u3, 2>(u1:1);)",
              TypecheckFails(HasSizeMismatch("u1", "uN[2]")));
}

TEST(TypecheckV2BuiltinTest, DecodeNotParametricType) {
  EXPECT_THAT(
      R"(
  const Y = decode<u32:31>(u5:1);)",
      TypecheckFails(HasSubstr("Expected parametric type, saw `u32:31`")));
}

TEST(TypecheckV2BuiltinTest, DecodeSignMismatch) {
  EXPECT_THAT(R"(const Y = decode<s3>(u1:1);)",
              TypecheckFails(HasSubstr(
                  "`decode` return type must be unsigned, saw `sN[3]`")));
}

TEST(TypecheckV2BuiltinTest, DecodeSignMismatchTypeAlias) {
  EXPECT_THAT(R"(
  type SIGNED=s3;
  const Y = decode<SIGNED>(u1:1);
  )",
              TypecheckFails(HasSubstr(
                  "`decode` return type must be unsigned, saw `sN[3]`")));
}

TEST(TypecheckV2BuiltinTest, DecodeRequiresParametric) {
  EXPECT_THAT(R"(const Y = decode(u1:1);)",
              TypecheckFails(HasSubstr("Could not infer parametric")));
}

TEST(TypecheckV2BuiltinTest, DecodeRequiresParametricEvenWithTarget) {
  // TIV2 can't use return types to infer parametrics.
  EXPECT_THAT(R"(const Y: u32 = decode(u1:1);)",
              TypecheckFails(HasSubstr("Could not infer parametric")));
}

TEST(TypecheckV2BuiltinTest, DecodeToStructError) {
  EXPECT_THAT(R"(
  struct S {}
  const Y = decode<S>(u5:1);)",
              TypecheckFails(HasSubstr(
                  "`decode` return type must be a bits type, saw `S {}`")));
}

TEST(TypecheckV2BuiltinTest, ElementCountArray) {
  EXPECT_THAT(R"(
const Y = element_count<u32[4]>();
)",
              TypecheckSucceeds(HasNodeWithType("Y", "uN[32]")));
}

TEST(TypecheckV2BuiltinTest, ElementCountIsSetCorrectly) {
  EXPECT_THAT(R"(
const Y = element_count<u32[4]>();
const Z: u32[Y] = [1, ...];
)",
              TypecheckSucceeds(AllOf(HasNodeWithType("Y", "uN[32]"),
                                      HasNodeWithType("Z", "uN[32][4]"))));
}

TEST(TypecheckV2BuiltinTest, ElementCount2DArray) {
  EXPECT_THAT(R"(
const Y = element_count<u32[4][u32:5]>();
)",
              TypecheckSucceeds(HasNodeWithType("Y", "uN[32]")));
}

TEST(TypecheckV2BuiltinTest, ElementCountBool) {
  EXPECT_THAT(R"(
const Y = element_count<bool>();
)",
              TypecheckSucceeds(HasNodeWithType("Y", "uN[32]")));
}

TEST(TypecheckV2BuiltinTest, ElementCountUN) {
  EXPECT_THAT(R"(
const Y = element_count<uN[64]>();
)",
              TypecheckSucceeds(HasNodeWithType("Y", "uN[32]")));
}

TEST(TypecheckV2BuiltinTest, ElementCountBits) {
  EXPECT_THAT(R"(
const Y = element_count<u32>();
)",
              TypecheckSucceeds(HasNodeWithType("Y", "uN[32]")));
}

TEST(TypecheckV2BuiltinTest, ElementCountStruct) {
  EXPECT_THAT(R"(
struct MyPoint { x: u32, y: u32 }
const Y = element_count<MyPoint>();
)",
              TypecheckSucceeds(HasNodeWithType("Y", "uN[32]")));
}

TEST(TypecheckV2BuiltinTest, ElementCountStructArray) {
  EXPECT_THAT(R"(
struct MyPoint { x: u32, y: u32 }
const Y = element_count<MyPoint[2]>();
)",
              TypecheckSucceeds(HasNodeWithType("Y", "uN[32]")));
}

TEST(TypecheckV2BuiltinTest, ElementCountParametricArg) {
  EXPECT_THAT(R"(
fn f<N:u32>() -> u32 {
  element_count<u32[N]>()
}
const Y = f<10>();
)",
              TypecheckSucceeds(HasNodeWithType("Y", "uN[32]")));
}

TEST(TypecheckV2BuiltinTest, ElementCountMissingParametric) {
  EXPECT_THAT(
      R"(
fn f<N:u32>() -> u32 {
  element_count<u32[M]>()
}
const Y = f<10>();
)",
      TypecheckFails(HasSubstr(R"(Cannot find a definition for name: "M")")));
}

TEST(TypecheckV2BuiltinTest, ElementCountMissingParam) {
  EXPECT_THAT(R"(const Y: u32 = element_count();)",
              TypecheckFails(HasSubstr(R"(Could not infer parametric(s): T)")));
}

TEST(TypecheckV2BuiltinTest, ElementCountParametricStruct) {
  EXPECT_THAT(R"(
struct T<N: u32> {
  a: uN[N],
  b: u32
}
const Y = element_count<T<u32:4>>();
)",
              TypecheckSucceeds(HasNodeWithType("Y", "uN[32]")));
}

TEST(TypecheckV2BuiltinTest, ElementCountAlias) {
  EXPECT_THAT(R"(
struct S {
  b: u32
}
type A=S;

const Y = element_count<A>();
)",
              TypecheckSucceeds(HasNodeWithType("Y", "uN[32]")));
}

// This fails with nullptr in Concretize
TEST(TypecheckV2BuiltinTest, DISABLED_ElementCountParametricAlias) {
  EXPECT_THAT(R"(
struct T<N: u32> {
  a: uN[N],
  b: u32
}
type A=T;

const Y = element_count<A<u32:4>>();
)",
              TypecheckSucceeds(HasNodeWithType("Y", "uN[32]")));
}

TEST(TypecheckV2BuiltinTest, ElementCountInstantiatedAlias) {
  EXPECT_THAT(R"(
struct T<N: u32> {
  a: uN[N],
  b: u32
}
type A=T<u32:4>;

const Y = element_count<A>();
)",
              TypecheckSucceeds(HasNodeWithType("Y", "uN[32]")));
}

TEST(TypecheckV2BuiltinTest, ElementCountTuple) {
  EXPECT_THAT(R"(const Y = element_count<(u32, bool)>();)",
              TypecheckSucceeds(HasNodeWithType("Y", "uN[32]")));
}

TEST(TypecheckV2BuiltinTest, ElementCountUnit) {
  EXPECT_THAT(R"(const Y = element_count<()>();)",
              TypecheckSucceeds(HasNodeWithType("Y", "uN[32]")));
}

TEST(TypecheckV2BuiltinTest, Enumerate) {
  EXPECT_THAT(R"(const Y = enumerate<u16, u32:3>([u16:1, u16:2, u16:3]);)",
              TypecheckSucceeds(HasNodeWithType("Y", "(uN[32], uN[16])[3]")));
}

TEST(TypecheckV2BuiltinTest, EnumerateImplicitSize) {
  EXPECT_THAT(R"(const Y = enumerate<u16>([u16:1, u16:2, u16:3]);)",
              TypecheckSucceeds(HasNodeWithType("Y", "(uN[32], uN[16])[3]")));
}

TEST(TypecheckV2BuiltinTest, EnumerateImplicitType) {
  EXPECT_THAT(R"(const Y = enumerate([u16:1, u16:2, u16:3]);)",
              TypecheckSucceeds(HasNodeWithType("Y", "(uN[32], uN[16])[3]")));
}

TEST(TypecheckV2BuiltinTest, Fail) {
  EXPECT_THAT(
      R"(
fn f() {
  let true_result = fail!("t", true);
  let false_result = fail!("f", u2:0);
}
)",
      TypecheckSucceeds(AllOf(HasNodeWithType("true_result", "uN[1]"),
                              HasNodeWithType("false_result", "uN[2]"))));
}

TEST(TypecheckV2BuiltinTest, FailExplicitParametrics) {
  EXPECT_THAT(
      R"(
fn f() {
  let true_result = fail!<1, u1>("t", true);
  let false_result = fail!<1, u2>("f", u2:0);
}
)",
      TypecheckSucceeds(AllOf(HasNodeWithType("true_result", "uN[1]"),
                              HasNodeWithType("false_result", "uN[2]"))));
}

TEST(TypecheckV2BuiltinTest, FailConstExpr) {
  EXPECT_THAT(
      R"(
fn f() -> bool {
  fail!("Fail", 1 > 0)
}
)",
      TypecheckSucceeds(HasNodeWithType("f", "() -> uN[1]")));
}

TEST(TypecheckV2BuiltinTest, Gate) {
  EXPECT_THAT(R"(const Y = gate!(true, u32:123);)",
              TypecheckSucceeds(HasNodeWithType("Y", "uN[32]")));
}

TEST(TypecheckV2BuiltinTest, GateMismatch) {
  EXPECT_THAT(R"(const Y = gate!<u31>(true, u32:123);)",
              TypecheckFails(HasSizeMismatch("u31", "u32")));
}

TEST(TypecheckV2BuiltinTest, GateReturnTypeMismatch) {
  EXPECT_THAT(R"(const Y:u31 = gate!(true, u32:123);)",
              TypecheckFails(HasSizeMismatch("u31", "u32")));
}

TEST(TypecheckV2BuiltinTest, DISABLED_OneHot) {
  // This doesn't work yet. It gives an error in GenerateTypeInfo,
  // probably because it needs the parametric environment in context of the main
  // module at the invocation site at the same time it needs the function
  // signature from the builtins module.
  EXPECT_THAT(R"(const Y = one_hot(u32:2, true);)",
              TypecheckSucceeds(HasNodeWithType("Y", "uN[33]")));
}

TEST(TypecheckV2BuiltinTest, MyOneHot) {
  EXPECT_THAT(R"(
fn my_one_hot<N: u32, M:u32={N+1}>(x: uN[N], y: u1) -> uN[M] {
  zero!<uN[M]>()
}

const Y = my_one_hot(u32:2, true);
)",
              TypecheckSucceeds(HasNodeWithType("Y", "uN[33]")));
}

TEST(TypecheckV2BuiltinTest, OneHotSel) {
  EXPECT_THAT(R"(const Y = one_hot_sel(2, [s10:1, s10:2]);)",
              TypecheckSucceeds(HasNodeWithType("Y", "sN[10]")));
}

TEST(TypecheckV2BuiltinTest, OrReduce) {
  EXPECT_THAT(R"(const Y = or_reduce(u6:3);)",
              TypecheckSucceeds(HasNodeWithType("Y", "uN[1]")));
}

TEST(TypecheckV2BuiltinTest, OrReduceError) {
  EXPECT_THAT(R"(const Y: u32 = or_reduce(u6:3);)",
              TypecheckFails(HasSizeMismatch("u1", "u32")));
}

TEST(TypecheckV2BuiltinTest, PrioritySel) {
  EXPECT_THAT(R"(const Y = priority_sel(2, [s10:1, s10:2], s10:3);)",
              TypecheckSucceeds(HasNodeWithType("Y", "sN[10]")));
}

TEST(TypecheckV2BuiltinTest, RevWithParametric) {
  EXPECT_THAT(R"(const Y = rev<u32:8>(u8:3);)",
              TypecheckSucceeds(HasNodeWithType("Y", "uN[8]")));
}

TEST(TypecheckV2BuiltinTest, RevWithParametricMismatch) {
  EXPECT_THAT(R"(const Y = rev<u32:8>(u6:3);)",
              TypecheckFails(HasSizeMismatch("uN[8]", "u6")));
}

TEST(TypecheckV2BuiltinTest, RevMismatch) {
  EXPECT_THAT(R"(const Y:u32 = rev(u6:3);)",
              TypecheckFails(HasSizeMismatch("u32", "uN[6]")));
}

TEST(TypecheckV2BuiltinTest, RevWithoutParametric) {
  EXPECT_THAT(R"(
const X = rev(u32:3);
const Y = rev(u8:3);
)",
              TypecheckSucceeds(AllOf(HasNodeWithType("X", "uN[32]"),
                                      HasNodeWithType("Y", "uN[8]"))));
}

TEST(TypecheckV2BuiltinTest, RevWithArithmetic) {
  EXPECT_THAT(R"(
const Y = rev(u8:3);
const Z = rev(Y) + 1;
)",
              TypecheckSucceeds(HasNodeWithType("rev(Y) + 1", "uN[8]")));
}

TEST(TypecheckV2BuiltinTest, RevIndex) {
  EXPECT_THAT(R"(
const X:uN[32][4] = [1,2,3,4];
const Y = X[rev(u32:0)];
)",
              TypecheckSucceeds(AllOf(HasNodeWithType("X", "uN[32][4]"),
                                      HasNodeWithType("Y", "uN[32]"))));
}

TEST(TypecheckV2BuiltinTest, RevArraySizeMismatch) {
  EXPECT_THAT(R"(const X:uN[rev(u2:1)][4] = [1,2,1,2];)",
              TypecheckFails(HasSizeMismatch("u32", "uN[2]")));
}

TEST(TypecheckV2BuiltinTest, RevArraySizeOK) {
  EXPECT_THAT(R"(
  // Should reverse to u32:2
const X:uN[rev(u32:0b1000000000000000000000000000000)][4] = [1,2,1,2];
)",
              TypecheckSucceeds(HasNodeWithType("X", "uN[2][4]")));
}

TEST(TypecheckV2BuiltinTest, RevTwiceArraySizeOK) {
  EXPECT_THAT(R"(
const X:uN[rev(rev(u32:2))][4] = [1,2,1,2];
)",
              TypecheckSucceeds(HasNodeWithType("X", "uN[2][4]")));
}

TEST(TypecheckV2BuiltinTest, SignEx) {
  EXPECT_THAT(R"(
const X = signex(s16:10, s16:0);
const Y = signex(u16:10, s32:0);
)",
              TypecheckSucceeds(AllOf(HasNodeWithType("X", "sN[16]"),
                                      HasNodeWithType("Y", "sN[32]"))));
}

TEST(TypecheckV2BuiltinTest, Smulp) {
  EXPECT_THAT(R"(const Y = smulp(s16:10, s16:20);)",
              TypecheckSucceeds(HasNodeWithType("Y", "(uN[16], uN[16])")));
}

TEST(TypecheckV2BuiltinTest, Trace) {
  EXPECT_THAT(R"(const Y = trace!(u32:123);)",
              TypecheckSucceeds(HasNodeWithType("Y", "uN[32]")));
}

TEST(TypecheckV2BuiltinTest, TraceMismatchedParamType) {
  EXPECT_THAT(R"(const Y = trace!<u31>(u32:123);)",
              TypecheckFails(HasSizeMismatch("u31", "u32")));
}

TEST(TypecheckV2BuiltinTest, TraceMismatchedReturnType) {
  EXPECT_THAT(R"(const Y: u31 = trace!(u32:123);)",
              TypecheckFails(HasSizeMismatch("u31", "u32")));
}

TEST(TypecheckV2BuiltinTest, Umulp) {
  EXPECT_THAT(R"(const Y = umulp(u16:10, u16:20);)",
              TypecheckSucceeds(HasNodeWithType("Y", "(uN[16], uN[16])")));
}

TEST(TypecheckV2BuiltinTest, WideningCast) {
  EXPECT_THAT("const Y = widening_cast<u31>(u30:3);", TopNodeHasType("uN[31]"));
}

TEST(TypecheckV2BuiltinTest, WideningCastWithNarrowingTypeFails) {
  EXPECT_THAT(
      R"(const Y = widening_cast<u31>(u32:3);)",
      TypecheckFails(HasSubstr("Cannot cast from type `uN[32]` (32 bits) to "
                               "`uN[31]` (31 bits) with widening_cast")));
}

TEST(TypecheckV2BuiltinTest, WideningCastToSameSizeSignedFails) {
  EXPECT_THAT(
      R"(const Y = widening_cast<s31>(u31:3);)",
      TypecheckFails(HasSubstr("Cannot cast from type `uN[31]` (31 bits) to "
                               "`sN[31]` (31 bits) with widening_cast")));
}

TEST(TypecheckV2BuiltinTest, WideningCastExplicit) {
  EXPECT_THAT(R"(const Y = widening_cast<u31, u30>(u30:3);)",
              TypecheckSucceeds(HasNodeWithType("Y", "uN[31]")));
}

TEST(TypecheckV2BuiltinTest, WideningCastMismatch) {
  EXPECT_THAT(R"(const Y = widening_cast<u31, u32>(u31:3);)",
              TypecheckFails(HasSizeMismatch("u32", "u31")));
}

TEST(TypecheckV2BuiltinTest, WideningCastImplicitDestinationFails) {
  // This (intentionally) fails the same way as v1.
  EXPECT_THAT(R"(const Y: u32 = widening_cast(u31:3);)",
              TypecheckFails(HasSubstr("Could not infer parametric(s): DEST")));
}

TEST(TypecheckV2BuiltinTest, ZipExplicitSizes) {
  EXPECT_THAT(
      R"(const Y = zip<u16, 3, u32>([u16:1, u16:2, u16:3], [u32:1, u32:2, u32:3]);)",
      TypecheckSucceeds(HasNodeWithType("Y", "(uN[16], uN[32])[3]")));
}

TEST(TypecheckV2BuiltinTest, ZipImplicitSizes) {
  EXPECT_THAT(R"(const Y = zip([u16:1, u16:2, u16:3], [u32:1, u32:2, u32:3]);)",
              TypecheckSucceeds(HasNodeWithType("Y", "(uN[16], uN[32])[3]")));
}

TEST(TypecheckV2BuiltinTest, TokenFnImplicit) {
  EXPECT_THAT(R"(
  const Y = token();
)",
              TypecheckSucceeds(HasNodeWithType("Y", "token")));
}

TEST(TypecheckV2BuiltinTest, TokenFnExplicit) {
  EXPECT_THAT(R"(
  const Y:token = token();
)",
              TypecheckSucceeds(HasNodeWithType("Y", "token")));
}

TEST(TypecheckV2BuiltinTest, TokenReturnedFromFn) {
  EXPECT_THAT(R"(
fn f() -> token { token() }
const Y = f();
)",
              TypecheckSucceeds(HasNodeWithType("Y", "token")));
}

TEST(TypecheckV2BuiltinTest, TokenReturnedFromParametricFn) {
  EXPECT_THAT(R"(
fn f<N: u32>() -> token { token() }
const Y = f<0>();
)",
              TypecheckSucceeds(HasNodeWithType("Y", "token")));
}

TEST(TypecheckV2BuiltinTest, TokenParam) {
  EXPECT_THAT(R"(
fn f(t: token) -> token { t }
const Y = f(token());
)",
              TypecheckSucceeds(HasNodeWithType("Y", "token")));
}

TEST(TypecheckV2BuiltinTest, TokenFnMismatch) {
  EXPECT_THAT(R"(
  const Y: u32 = token();
)",
              TypecheckFails(HasTypeMismatch("u32", "token")));
}

TEST(TypecheckV2BuiltinTest, TokenDeclarationMismatch) {
  EXPECT_THAT(R"(
  const Y = u32:0;
  const X: token = Y;
)",
              TypecheckFails(HasTypeMismatch("u32", "token")));
}

TEST(TypecheckV2BuiltinTest, TokenFnAssignmentDeclarationMismatch) {
  EXPECT_THAT(R"(
  const Y = token();
  const X: u32 = Y;
)",
              TypecheckFails(HasTypeMismatch("u32", "token")));
}

TEST(TypecheckV2BuiltinTest, TokenAssignmentMismatch) {
  EXPECT_THAT(R"(
  const X: token = u32:0;
)",
              TypecheckFails(HasTypeMismatch("u32", "token")));
}

TEST(TypecheckV2BuiltinTest, Join) {
  EXPECT_THAT(R"(
  const T = token();
  const Y = join(T);
)",
              TypecheckSucceeds(AllOf(HasNodeWithType("T", "token"),
                                      HasNodeWithType("Y", "token"))));
}

TEST(TypecheckV2BuiltinTest, Join2) {
  EXPECT_THAT(R"(
  const T = token();
  const Y = join(T, T);
)",
              TypecheckSucceeds(AllOf(HasNodeWithType("T", "token"),
                                      HasNodeWithType("Y", "token"))));
}

TEST(TypecheckV2BuiltinTest, Join0) {
  EXPECT_THAT(R"(
  const T = token();
  const Y = join();
)",
              TypecheckSucceeds(AllOf(HasNodeWithType("T", "token"),
                                      HasNodeWithType("Y", "token"))));
}

TEST(TypecheckV2BuiltinTest, Join012) {
  EXPECT_THAT(
      R"(
  const T = token();
  const X = join();
  const Y = join(T);
  const Z = join(T, T);
)",
      TypecheckSucceeds(
          AllOf(HasNodeWithType("T", "token"), HasNodeWithType("X", "token"),
                HasNodeWithType("Y", "token"), HasNodeWithType("Z", "token"))));
}

TEST(TypecheckV2BuiltinTest, JoinMismatchReturnType) {
  EXPECT_THAT(
      R"(
  const X:u32 = join();
)",
      TypecheckFails(HasTypeMismatch("u32", "token")));
}

TEST(TypecheckV2BuiltinTest, JoinMismatchParamType) {
  EXPECT_THAT(
      R"(
  const X = join(u32:0);
)",
      TypecheckFails(HasTypeMismatch("u32", "token")));
}

TEST(TypecheckV2BuiltinTest, JoinMismatchSecondParamType) {
  EXPECT_THAT(
      R"(
  const X = join(token(), u32:0);
)",
      TypecheckFails(HasTypeMismatch("u32", "token")));
}

TEST(TypecheckV2BuiltinTest, Send) {
  EXPECT_THAT(
      R"(
proc P {
  c: chan<u32> out;
  v: u32;

  init { join() }
  config(c: chan<u32> out, v: u32) { (c, v) }

  next(state: token) {
    send(state, c, v)
  }
}
)",
      TypecheckSucceeds(HasOneLineBlockWithType("send(state, c, v)", "token")));
}

TEST(TypecheckV2BuiltinTest, SendWithDataMismatchFails) {
  EXPECT_THAT(
      R"(
proc P {
  c: chan<u32> out;
  v: u64;

  init { join() }
  config(c: chan<u32> out, v: u64) { (c, v) }

  next(state: token) {
    send(state, c, v)
  }
}
)",
      TypecheckFails(HasSizeMismatch("uN[32]", "uN[64]")));
}

TEST(TypecheckV2BuiltinTest, SendWithChannelDirectionMismatchFails) {
  EXPECT_THAT(
      R"(
proc P {
  c: chan<u32> in;
  v: u32;

  init { join() }
  config(c: chan<u32> in, v: u32) { (c, v) }

  next(state: token) {
    send(state, c, v)
  }
}
)",
      TypecheckFails(HasTypeMismatch("chan<uN[32]> out", "chan<uN[32]> in")));
}

TEST(TypecheckV2BuiltinTest, SendWithChannelDimMismatchFails) {
  EXPECT_THAT(
      R"(
proc P {
  c: chan<u32>[2] out;
  v: u32;

  init { join() }
  config(c: chan<u32>[2] out, v: u32) { (c, v) }

  next(state: token) {
    send(state, c, v)
  }
}
)",
      TypecheckFails(HasTypeMismatch("chan<uN[32]>[2] out", "chan<Any> out")));
}

TEST(TypecheckV2BuiltinTest, SendIf) {
  EXPECT_THAT(
      R"(
proc P {
  c: chan<u32> out;
  v: u32;

  init { join() }
  config(c: chan<u32> out, v: u32) { (c, v) }

  next(state: token) {
    send_if(state, c, false, v)
  }
}
)",
      TypecheckSucceeds(
          HasOneLineBlockWithType("send_if(state, c, false, v)", "token")));
}

TEST(TypecheckV2BuiltinTest, SendIfWithPredicateTypeMismatchFails) {
  EXPECT_THAT(
      R"(
proc P {
  c: chan<u32> out;
  v: u32;

  init { join() }
  config(c: chan<u32> out, v: u32) { (c, v) }

  next(state: token) {
    send_if(state, c, u32:50, v)
  }
}
)",
      TypecheckFails(HasTypeMismatch("bool", "u32")));
}

TEST(TypecheckV2BuiltinTest, Recv) {
  EXPECT_THAT(
      R"(
proc P {
  c: chan<u32> in;

  init { (join(), 0) }
  config(c: chan<u32> in) { (c,) }

  next(state: (token, u32)) {
    recv(state.0, c)
  }
}
)",
      TypecheckSucceeds(
          HasOneLineBlockWithType("recv(state.0, c)", "(token, uN[32])")));
}

TEST(TypecheckV2BuiltinTest, RecvWithDataMismatchFails) {
  EXPECT_THAT(
      R"(
proc P {
  c: chan<u32> in;

  init { (join(), 0) }
  config(c: chan<u32> in) { (c,) }

  next(state: (token, u64)) {
    recv(state.0, c)
  }
}
)",
      TypecheckFails(HasSizeMismatch("uN[32]", "u64")));
}

TEST(TypecheckV2BuiltinTest, RecvWithChannelDirectionMismatchFails) {
  EXPECT_THAT(
      R"(
proc P {
  c: chan<u32> out;

  init { (join(), 0) }
  config(c: chan<u32> out) { (c,) }

  next(state: (token, u32)) {
    recv(state.0, c)
  }
}
)",
      TypecheckFails(HasTypeMismatch("chan<uN[32]> in", "chan<uN[32]> out")));
}

TEST(TypecheckV2BuiltinTest, RecvWithChannelDimMismatchFails) {
  EXPECT_THAT(
      R"(
proc P {
  c: chan<u32>[2] in;

  init { (join(), 0) }
  config(c: chan<u32>[2] in) { (c,) }

  next(state: (token, u32)) {
    recv(state.0, c)
  }
}
)",
      TypecheckFails(HasTypeMismatch("chan<uN[32]>[2] in", "chan<Any> in")));
}

TEST(TypecheckV2BuiltinTest, RecvIf) {
  EXPECT_THAT(
      R"(
proc P {
  c: chan<u32> in;
  v: u32;

  init { (join(), 0) }
  config(c: chan<u32> in, v: u32) { (c, v) }

  next(state: (token, u32)) {
    recv_if(state.0, c, false, v)
  }
}
)",
      TypecheckSucceeds(HasOneLineBlockWithType("recv_if(state.0, c, false, v)",
                                                "(token, uN[32])")));
}

TEST(TypecheckV2BuiltinTest, RecvIfWithPredicateTypeMismatchFails) {
  EXPECT_THAT(
      R"(
proc P {
  c: chan<u32> in;
  v: u32;

  init { (join(), 0) }
  config(c: chan<u32> in, v: u32) { (c, v) }

  next(state: (token, u32)) {
    recv_if(state.0, c, u32:50, v)
  }
}
)",
      TypecheckFails(HasTypeMismatch("u32", "bool")));
}

TEST(TypecheckV2BuiltinTest, RecvIfWithDefaultValueTypeMismatchFails) {
  EXPECT_THAT(
      R"(
proc P {
  c: chan<u32> in;
  v: u64;

  init { (join(), 0) }
  config(c: chan<u32> in, v: u64) { (c, v) }

  next(state: (token, u32)) {
    recv_if(state.0, c, false, v)
  }
}
)",
      TypecheckFails(HasTypeMismatch("uN[32]", "uN[64]")));
}

TEST(TypecheckV2BuiltinTest, RecvNonBlocking) {
  EXPECT_THAT(
      R"(
proc P {
  c: chan<u32> in;
  v: u32;

  init { (join(), 0, false) }
  config(c: chan<u32> in, v: u32) { (c, v) }

  next(state: (token, u32, bool)) {
    recv_non_blocking(state.0, c, v)
  }
}
)",
      TypecheckSucceeds(HasOneLineBlockWithType(
          "recv_non_blocking(state.0, c, v)", "(token, uN[32], uN[1])")));
}

TEST(TypecheckV2BuiltinTest, RecvNonBlockingWithDefaultValueMismatchFails) {
  EXPECT_THAT(
      R"(
proc P {
  c: chan<u32> in;
  v: u64;

  init { (join(), 0, false) }
  config(c: chan<u32> in, v: u64) { (c, v) }

  next(state: (token, u32, bool)) {
    recv_non_blocking(state.0, c, v)
  }
}
)",
      TypecheckFails(HasTypeMismatch("uN[32]", "uN[64]")));
}

TEST(TypecheckV2BuiltinTest, RecvIfNonBlocking) {
  EXPECT_THAT(
      R"(
proc P {
  c: chan<u32> in;
  v: u32;

  init { (join(), 0, false) }
  config(c: chan<u32> in, v: u32) { (c, v) }

  next(state: (token, u32, bool)) {
    recv_if_non_blocking(state.0, c, false, v)
  }
}
)",
      TypecheckSucceeds(
          HasOneLineBlockWithType("recv_if_non_blocking(state.0, c, false, v)",
                                  "(token, uN[32], uN[1])")));
}

TEST(TypecheckV2BuiltinTest, RecvIfNonBlockingWithPredicateTypeMismatchFails) {
  EXPECT_THAT(
      R"(
proc P {
  c: chan<u32> in;
  v: u32;

  init { (join(), 0, false) }
  config(c: chan<u32> in, v: u32) { (c, v) }

  next(state: (token, u32, bool)) {
    recv_if_non_blocking(state.0, c, u32:50, v)
  }
}
)",
      TypecheckFails(HasTypeMismatch("u32", "bool")));
}

TEST(TypecheckV2BuiltinTest,
     RecvIfNonBlockingWithDefaultValueTypeMismatchFails) {
  EXPECT_THAT(
      R"(
proc P {
  c: chan<u32> in;
  v: u64;

  init { (join(), 0, false) }
  config(c: chan<u32> in, v: u64) { (c, v) }

  next(state: (token, u32, bool)) {
    recv_if_non_blocking(state.0, c, false, v)
  }
}
)",
      TypecheckFails(HasTypeMismatch("uN[32]", "uN[64]")));
}

TEST(TypecheckV2BuiltinTest, TraceFmt) {
  EXPECT_THAT(R"(
fn f() { trace_fmt!("foo"); }
)",
              TypecheckSucceeds(HasNodeWithType(R"(trace_fmt!("foo"))", "()")));
}

TEST(TypecheckV2BuiltinTest, VtraceFmt) {
  EXPECT_THAT(
      R"(
fn f() { vtrace_fmt!(1, "foo"); }
)",
      TypecheckSucceeds(
          AllOf(HasNodeWithType("1", "uN[1]"),
                HasNodeWithType(R"(vtrace_fmt!(1, "foo"))", "()"))));
}

TEST(TypecheckV2BuiltinTest, TraceFmtInteger) {
  EXPECT_THAT(R"(
fn f(a: u32) { trace_fmt!("a is {}", 2); }
)",
              TypecheckSucceeds(HasNodeWithType("2", "uN[2]")));
}

TEST(TypecheckV2BuiltinTest, TraceTraceFmtWithTooFewArgsFails) {
  EXPECT_THAT(R"(
fn f(a: u32) { trace_fmt!("a is {}"); }
)",
              TypecheckFails(HasSubstr("trace_fmt! macro expects 1 argument(s) "
                                       "from format but has 0 argument(s)")));
}

TEST(TypecheckV2BuiltinTest, TraceTraceFmtWithTooManyArgsFails) {
  EXPECT_THAT(R"(
fn f(a: u32) { trace_fmt!("a is {}", a, a); }
)",
              TypecheckFails(HasSubstr("trace_fmt! macro expects 1 argument(s) "
                                       "from format but has 2 argument(s)")));
}

TEST(TypecheckV2BuiltinTest, VtraceFmtWithWrongTypeVerbosityFails) {
  EXPECT_THAT(R"(
fn f() { vtrace_fmt!((u32:1,), "foobar"); }
)",
              TypecheckFails(HasSubstr("vtrace_fmt! verbosity values must be "
                                       "positive integers; got `(u32:1,)`")));
}

TEST(TypecheckV2BuiltinTest, VtraceFmtWithNegativeVerbosityFails) {
  EXPECT_THAT(R"(
fn f() { vtrace_fmt!(-1, "foobar"); }
)",
              TypecheckFails(HasSubstr("vtrace_fmt! verbosity values must be "
                                       "positive integers; got `-1`")));
}

TEST(TypecheckV2BuiltinTest, VtraceFmtWithNonConstantVerbosityFails) {
  EXPECT_THAT(R"(
fn f(v: u32) { vtrace_fmt!(v, "foobar"); }
)",
              TypecheckFails(HasSubstr("vtrace_fmt! verbosity values must be "
                                       "compile-time constants; got `v`")));
}

TEST(TypecheckV2BuiltinTest, VtraceTraceFmtInteger) {
  EXPECT_THAT(R"(
fn f(a: u32) { vtrace_fmt!(4, "a is {}", 2); }
)",
              TypecheckSucceeds(AllOf(HasNodeWithType("2", "uN[2]"),
                                      HasNodeWithType("4", "uN[3]"))));
}

TEST(TypecheckV2BuiltinTest, TraceFmtFunctionCallResult) {
  EXPECT_THAT(R"(
fn g() -> u32 { 2 }
fn f(a: u32) { trace_fmt!("a is {}", g()); }
)",
              TypecheckSucceeds(HasNodeWithType("g()", "uN[32]")));
}

TEST(TypecheckV2BuiltinTest, TraceFmtStruct) {
  EXPECT_THAT(
      R"(
struct S {
 a: u32,
 b: u16
}

fn f(a: S) { trace_fmt!("a is {}", a); }
)",
      TypecheckSucceeds(HasNodeWithType(R"(trace_fmt!("a is {}", a))", "()")));
}

TEST(TypecheckV2BuiltinTest, TraceFmtFunctionFails) {
  EXPECT_THAT(R"(
fn g() -> u32 { 2 }
fn f(a: u32) { trace_fmt!("a is {}", g); }
)",
              TypecheckFails(
                  HasSubstr("Cannot format an expression with function type")));
}

}  // namespace
}  // namespace xls::dslx
