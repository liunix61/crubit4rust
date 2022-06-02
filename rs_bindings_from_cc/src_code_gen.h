// Part of the Crubit project, under the Apache License v2.0 with LLVM
// Exceptions. See /LICENSE for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef CRUBIT_RS_BINDINGS_FROM_CC_SRC_CODE_GEN_H_
#define CRUBIT_RS_BINDINGS_FROM_CC_SRC_CODE_GEN_H_

#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "rs_bindings_from_cc/ir.h"

namespace crubit {

// Source code for generated bindings.
struct Bindings {
  // Rust source code.
  std::string rs_api;
  // C++ source code.
  std::string rs_api_impl;
};

// Generates bindings from the given `IR`.
absl::StatusOr<Bindings> GenerateBindings(
    const IR& ir, absl::string_view crubit_support_path,
    absl::string_view rustfmt_exe_path, absl::string_view rustfmt_config_path);

}  // namespace crubit

#endif  // CRUBIT_RS_BINDINGS_FROM_CC_SRC_CODE_GEN_H_
