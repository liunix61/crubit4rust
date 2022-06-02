// Part of the Crubit project, under the Apache License v2.0 with LLVM
// Exceptions. See /LICENSE for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef THIRD_PARTY_CRUBIT_RS_BINDINGS_FROM_CC_GENERATE_BINDINGS_AND_METADATA_H_
#define THIRD_PARTY_CRUBIT_RS_BINDINGS_FROM_CC_GENERATE_BINDINGS_AND_METADATA_H_

#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "rs_bindings_from_cc/cmdline.h"
#include "rs_bindings_from_cc/ir.h"

namespace crubit {
// Contains generated bindings and all related metadata, such as the IR.
struct BindingsAndMetadata {
  // Intermediate representation of the Clang AST from which we generated
  // bindings.
  IR ir;
  // Generated Rust source code.
  std::string rs_api;
  // Generated C++ source code.
  std::string rs_api_impl;
};

// Returns `BindingsAndMetadata` as requested by the user on the command line.
absl::StatusOr<BindingsAndMetadata> GenerateBindingsAndMetadata(
    Cmdline& cmdline, std::vector<std::string> clang_args);

}  // namespace crubit

#endif  // THIRD_PARTY_CRUBIT_RS_BINDINGS_FROM_CC_GENERATE_BINDINGS_AND_METADATA_H_
