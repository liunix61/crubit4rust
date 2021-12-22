// Part of the Crubit project, under the Apache License v2.0 with LLVM
// Exceptions. See /LICENSE for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef CRUBIT_RS_BINDINGS_FROM_CC_TEST_GOLDEN_UNSUPPORTED_H_
#define CRUBIT_RS_BINDINGS_FROM_CC_TEST_GOLDEN_UNSUPPORTED_H_

struct NontrivialCustomType final {
  NontrivialCustomType(NontrivialCustomType&&);

  int i;
};

void UnsupportedParamType(NontrivialCustomType n);
NontrivialCustomType UnsupportedReturnType();

NontrivialCustomType MultipleReasons(NontrivialCustomType n, int);

namespace ns {
void FunctionInNamespace();
struct StructInNamespace final {};
}  // namespace ns

struct ContainingStruct final {
  struct NestedStruct final {};
};

#endif  // CRUBIT_RS_BINDINGS_FROM_CC_TEST_GOLDEN_UNSUPPORTED_H_
