// Part of the Crubit project, under the Apache License v2.0 with LLVM
// Exceptions. See /LICENSE for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#ifndef CRUBIT_RS_BINDINGS_FROM_CC_TEST_CRUBIT_FEATURES_WRAPPER_STRUCT_ENABLED_H_
#define CRUBIT_RS_BINDINGS_FROM_CC_TEST_CRUBIT_FEATURES_WRAPPER_STRUCT_ENABLED_H_

#include "rs_bindings_from_cc/test/crubit_features/definition_disabled.h"

struct EnabledStructWithDisabledField final {
  DisabledStruct x;
  char y;
};

#endif  // CRUBIT_RS_BINDINGS_FROM_CC_TEST_CRUBIT_FEATURES_WRAPPER_STRUCT_ENABLED_H_
