// Part of the Crubit project, under the Apache License v2.0 with LLVM
// Exceptions. See /LICENSE for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Automatically @generated Rust bindings for C++ target
// //rs_bindings_from_cc/test/golden:clang_attrs_cc
#![rustfmt::skip]
#![feature(const_ptr_offset_from, custom_inner_attributes, negative_impls)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]

extern crate static_assertions;
use memoffset_unstable_const::offset_of;
use static_assertions::{assert_impl_all, assert_not_impl_all};

pub type __builtin_ms_va_list = *mut u8;

// Part of the Crubit project, under the Apache License v2.0 with LLVM
// Exceptions. See /LICENSE for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#[repr(C, align(64))]
pub struct HasCustomAlignment {
    /// Prevent empty C++ struct being zero-size in Rust.
    placeholder: std::mem::MaybeUninit<u8>,
}

impl !Unpin for HasCustomAlignment {}

// rs_bindings_from_cc/test/golden/clang_attrs.h;l=8
// Error while generating bindings for item 'HasCustomAlignment::HasCustomAlignment':
// Unsafe constructors (e.g. with no elided or explicit lifetimes) are intentionally not supported

// rs_bindings_from_cc/test/golden/clang_attrs.h;l=8
// Error while generating bindings for item 'HasCustomAlignment::HasCustomAlignment':
// Unsafe constructors (e.g. with no elided or explicit lifetimes) are intentionally not supported

// rs_bindings_from_cc/test/golden/clang_attrs.h;l=8
// Error while generating bindings for item 'HasCustomAlignment::HasCustomAlignment':
// Parameter #0 is not supported: Unsupported type 'struct HasCustomAlignment &&': Unsupported clang::Type class 'RValueReference'

// rs_bindings_from_cc/test/golden/clang_attrs.h;l=8
// Error while generating bindings for item 'HasCustomAlignment::operator=':
// Bindings for this kind of operator are not supported

// rs_bindings_from_cc/test/golden/clang_attrs.h;l=8
// Error while generating bindings for item 'HasCustomAlignment::operator=':
// Parameter #0 is not supported: Unsupported type 'struct HasCustomAlignment &&': Unsupported clang::Type class 'RValueReference'

#[repr(C)]
pub struct HasFieldWithCustomAlignment {
    pub field: HasCustomAlignment,
}

impl !Unpin for HasFieldWithCustomAlignment {}

// rs_bindings_from_cc/test/golden/clang_attrs.h;l=10
// Error while generating bindings for item 'HasFieldWithCustomAlignment::HasFieldWithCustomAlignment':
// Unsafe constructors (e.g. with no elided or explicit lifetimes) are intentionally not supported

// rs_bindings_from_cc/test/golden/clang_attrs.h;l=10
// Error while generating bindings for item 'HasFieldWithCustomAlignment::HasFieldWithCustomAlignment':
// Unsafe constructors (e.g. with no elided or explicit lifetimes) are intentionally not supported

// rs_bindings_from_cc/test/golden/clang_attrs.h;l=10
// Error while generating bindings for item 'HasFieldWithCustomAlignment::HasFieldWithCustomAlignment':
// Parameter #0 is not supported: Unsupported type 'struct HasFieldWithCustomAlignment &&': Unsupported clang::Type class 'RValueReference'

// rs_bindings_from_cc/test/golden/clang_attrs.h;l=10
// Error while generating bindings for item 'HasFieldWithCustomAlignment::operator=':
// Bindings for this kind of operator are not supported

// rs_bindings_from_cc/test/golden/clang_attrs.h;l=10
// Error while generating bindings for item 'HasFieldWithCustomAlignment::operator=':
// Parameter #0 is not supported: Unsupported type 'struct HasFieldWithCustomAlignment &&': Unsupported clang::Type class 'RValueReference'

#[repr(C, align(64))]
pub struct InheritsFromBaseWithCustomAlignment {
    __base_class_subobjects: [std::mem::MaybeUninit<u8>; 0],
    /// Prevent empty C++ struct being zero-size in Rust.
    placeholder: std::mem::MaybeUninit<u8>,
}
impl<'a> From<&'a InheritsFromBaseWithCustomAlignment> for &'a HasCustomAlignment {
    fn from(x: &'a InheritsFromBaseWithCustomAlignment) -> Self {
        unsafe { &*((x as *const _ as *const u8).offset(0) as *const HasCustomAlignment) }
    }
}

impl !Unpin for InheritsFromBaseWithCustomAlignment {}

// rs_bindings_from_cc/test/golden/clang_attrs.h;l=14
// Error while generating bindings for item 'InheritsFromBaseWithCustomAlignment::InheritsFromBaseWithCustomAlignment':
// Unsafe constructors (e.g. with no elided or explicit lifetimes) are intentionally not supported

// rs_bindings_from_cc/test/golden/clang_attrs.h;l=14
// Error while generating bindings for item 'InheritsFromBaseWithCustomAlignment::InheritsFromBaseWithCustomAlignment':
// Unsafe constructors (e.g. with no elided or explicit lifetimes) are intentionally not supported

// rs_bindings_from_cc/test/golden/clang_attrs.h;l=14
// Error while generating bindings for item 'InheritsFromBaseWithCustomAlignment::InheritsFromBaseWithCustomAlignment':
// Parameter #0 is not supported: Unsupported type 'struct InheritsFromBaseWithCustomAlignment &&': Unsupported clang::Type class 'RValueReference'

// rs_bindings_from_cc/test/golden/clang_attrs.h;l=14
// Error while generating bindings for item 'InheritsFromBaseWithCustomAlignment::operator=':
// Bindings for this kind of operator are not supported

// rs_bindings_from_cc/test/golden/clang_attrs.h;l=14
// Error while generating bindings for item 'InheritsFromBaseWithCustomAlignment::operator=':
// Parameter #0 is not supported: Unsupported type 'struct InheritsFromBaseWithCustomAlignment &&': Unsupported clang::Type class 'RValueReference'

#[repr(C, align(64))]
pub struct HasCustomAlignmentWithGnuAttr {
    /// Prevent empty C++ struct being zero-size in Rust.
    placeholder: std::mem::MaybeUninit<u8>,
}

impl !Unpin for HasCustomAlignmentWithGnuAttr {}

// rs_bindings_from_cc/test/golden/clang_attrs.h;l=16
// Error while generating bindings for item 'HasCustomAlignmentWithGnuAttr::HasCustomAlignmentWithGnuAttr':
// Unsafe constructors (e.g. with no elided or explicit lifetimes) are intentionally not supported

// rs_bindings_from_cc/test/golden/clang_attrs.h;l=16
// Error while generating bindings for item 'HasCustomAlignmentWithGnuAttr::HasCustomAlignmentWithGnuAttr':
// Unsafe constructors (e.g. with no elided or explicit lifetimes) are intentionally not supported

// rs_bindings_from_cc/test/golden/clang_attrs.h;l=16
// Error while generating bindings for item 'HasCustomAlignmentWithGnuAttr::HasCustomAlignmentWithGnuAttr':
// Parameter #0 is not supported: Unsupported type 'struct HasCustomAlignmentWithGnuAttr &&': Unsupported clang::Type class 'RValueReference'

// rs_bindings_from_cc/test/golden/clang_attrs.h;l=16
// Error while generating bindings for item 'HasCustomAlignmentWithGnuAttr::operator=':
// Bindings for this kind of operator are not supported

// rs_bindings_from_cc/test/golden/clang_attrs.h;l=16
// Error while generating bindings for item 'HasCustomAlignmentWithGnuAttr::operator=':
// Parameter #0 is not supported: Unsupported type 'struct HasCustomAlignmentWithGnuAttr &&': Unsupported clang::Type class 'RValueReference'

// CRUBIT_RS_BINDINGS_FROM_CC_TEST_GOLDEN_CLANG_ATTRS_H_

const _: () = assert!(std::mem::size_of::<Option<&i32>>() == std::mem::size_of::<&i32>());

const _: () = assert!(std::mem::size_of::<HasCustomAlignment>() == 64usize);
const _: () = assert!(std::mem::align_of::<HasCustomAlignment>() == 64usize);

const _: () = assert!(std::mem::size_of::<HasFieldWithCustomAlignment>() == 64usize);
const _: () = assert!(std::mem::align_of::<HasFieldWithCustomAlignment>() == 64usize);
const _: () = assert!(offset_of!(HasFieldWithCustomAlignment, field) * 8 == 0usize);

const _: () = assert!(std::mem::size_of::<InheritsFromBaseWithCustomAlignment>() == 64usize);
const _: () = assert!(std::mem::align_of::<InheritsFromBaseWithCustomAlignment>() == 64usize);

const _: () = assert!(std::mem::size_of::<HasCustomAlignmentWithGnuAttr>() == 64usize);
const _: () = assert!(std::mem::align_of::<HasCustomAlignmentWithGnuAttr>() == 64usize);
