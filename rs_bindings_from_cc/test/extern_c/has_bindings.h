// Part of the Crubit project, under the Apache License v2.0 with LLVM
// Exceptions. See /LICENSE for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef THIRD_PARTY_CRUBIT_RS_BINDINGS_FROM_CC_TEST_EXTERN_C_ALLOWED_H_
#define THIRD_PARTY_CRUBIT_RS_BINDINGS_FROM_CC_TEST_EXTERN_C_ALLOWED_H_

namespace crubit::has_bindings {
extern "C" {

struct Struct final {
  int* x;
  float y;
  Struct* z;
};

using StructAlias = Struct;

enum Enum {
  kEnumerator = 0,
  // This doesn't receive bindings, because the enumerator has an unrecognized
  // attribute.
  kUnkownAttrEnumerator [[deprecated]] = 1,
};

inline void crubit_void_function() {}
inline const void* crubit_void_ptr_identity(const void* x) { return x; }
inline int crubit_add(int x, int y) { return x + y; }
inline Struct crubit_anystruct(Struct x, const StructAlias*) { return x; }
inline Enum crubit_enum_function(Enum x) { return x; }

// Note the use of references, rather than pointers. A rust function pointer
// corresponds to a C++ function reference, more or less.
typedef void (&Callback)(int* x);
inline void crubit_invoke_callback(void (&f)(int* x), int* x) { f(x); }
}
}  // namespace crubit::has_bindings
#endif  // THIRD_PARTY_CRUBIT_RS_BINDINGS_FROM_CC_TEST_EXTERN_C_ALLOWED_H_
