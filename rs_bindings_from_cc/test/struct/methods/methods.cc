// Part of the Crubit project, under the Apache License v2.0 with LLVM
// Exceptions. See /LICENSE for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "rs_bindings_from_cc/test/struct/methods/methods.h"

// static
SomeStruct SomeStruct::static_factory_method(int int_var_initial_value) {
  return SomeStruct{.int_var = int_var_initial_value};
}

// static
int SomeStruct::static_method_that_multiplies_its_args(int x, int y) {
  return x * y;
}
