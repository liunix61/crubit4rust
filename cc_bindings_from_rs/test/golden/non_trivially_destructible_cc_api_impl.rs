// Part of the Crubit project, under the Apache License v2.0 with LLVM
// Exceptions. See /LICENSE for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Automatically @generated C++ bindings for the following Rust crate:
// non_trivially_destructible_rust_golden
// Features: experimental, supported

#![allow(improper_ctypes_definitions)]

const _: () = assert!(
    ::std::mem::size_of::<::non_trivially_destructible_rust_golden::NonTriviallyDestructable>()
        == 4
);
const _: () = assert!(
    ::std::mem::align_of::<::non_trivially_destructible_rust_golden::NonTriviallyDestructable>()
        == 4
);
#[unsafe(no_mangle)]
extern "C" fn __crubit_thunk_default(
    __ret_slot: &mut ::core::mem::MaybeUninit<
        ::non_trivially_destructible_rust_golden::NonTriviallyDestructable,
    >,
) -> () {
    __ret_slot.write(<::non_trivially_destructible_rust_golden::NonTriviallyDestructable as::core::default::Default>::default());
}
#[unsafe(no_mangle)]
extern "C" fn __crubit_thunk_drop(
    __self: &mut ::core::mem::MaybeUninit<
        ::non_trivially_destructible_rust_golden::NonTriviallyDestructable,
    >,
) {
    unsafe { __self.assume_init_drop() };
}
#[unsafe(no_mangle)]
extern "C" fn __crubit_thunk_clone<'__anon1>(
    __self: &'__anon1 ::non_trivially_destructible_rust_golden::NonTriviallyDestructable,
    __ret_slot: &mut ::core::mem::MaybeUninit<
        ::non_trivially_destructible_rust_golden::NonTriviallyDestructable,
    >,
) -> () {
    __ret_slot.write(<::non_trivially_destructible_rust_golden::NonTriviallyDestructable as::core::clone::Clone>::clone(__self));
}
#[unsafe(no_mangle)]
extern "C" fn __crubit_thunk_clone_ufrom<'__anon1, '__anon2>(
    __self: &'__anon1 mut ::non_trivially_destructible_rust_golden::NonTriviallyDestructable,
    source: &'__anon2 ::non_trivially_destructible_rust_golden::NonTriviallyDestructable,
) -> () {
    <::non_trivially_destructible_rust_golden::NonTriviallyDestructable as::core::clone::Clone>::clone_from(__self,source)
}
const _: () = assert!(
    ::core::mem::offset_of!(
        ::non_trivially_destructible_rust_golden::NonTriviallyDestructable,
        field
    ) == 0
);
#[unsafe(no_mangle)]
extern "C" fn __crubit_thunk_take_uby_uvalue(
    _x: &mut ::core::mem::MaybeUninit<
        ::non_trivially_destructible_rust_golden::NonTriviallyDestructable,
    >,
) -> () {
    ::non_trivially_destructible_rust_golden::take_by_value(unsafe { _x.assume_init_read() })
}
#[unsafe(no_mangle)]
extern "C" fn __crubit_thunk_return_uby_uvalue(
    __ret_slot: &mut ::core::mem::MaybeUninit<
        ::non_trivially_destructible_rust_golden::NonTriviallyDestructable,
    >,
) -> () {
    __ret_slot.write(::non_trivially_destructible_rust_golden::return_by_value());
}
