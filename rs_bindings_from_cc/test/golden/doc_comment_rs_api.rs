// Part of the Crubit project, under the Apache License v2.0 with LLVM
// Exceptions. See /LICENSE for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Automatically @generated Rust bindings for the following C++ target:
// //rs_bindings_from_cc/test/golden:doc_comment_cc
// Features: experimental, extern_c, supported

#![rustfmt::skip]
#![feature(custom_inner_attributes, negative_impls, register_tool)]
#![allow(stable_features)]
#![no_std]
#![register_tool(__crubit)]
#![allow(improper_ctypes)]
#![allow(nonstandard_style)]
#![deny(warnings)]

/// Doc comment
///
///  * with three slashes
#[derive(Clone, Copy)]
#[repr(C)]
#[__crubit::annotate(cc_type = "DocCommentSlashes")]
pub struct DocCommentSlashes {
    __non_field_data: [::core::mem::MaybeUninit<u8>; 0],
    /// A field.
    pub i: ::core::ffi::c_int,
}
impl !Send for DocCommentSlashes {}
impl !Sync for DocCommentSlashes {}
forward_declare::unsafe_define!(
    forward_declare::symbol!("DocCommentSlashes"),
    crate::DocCommentSlashes
);

impl<'b> From<::ctor::RvalueReference<'b, Self>> for DocCommentSlashes {
    #[inline(always)]
    fn from(__param_0: ::ctor::RvalueReference<'b, Self>) -> Self {
        let mut tmp = ::core::mem::MaybeUninit::<Self>::zeroed();
        unsafe {
            crate::detail::__rust_thunk___ZN17DocCommentSlashesC1EOS_(&mut tmp, __param_0);
            tmp.assume_init()
        }
    }
}

impl<'b> ::ctor::UnpinAssign<&'b Self> for DocCommentSlashes {
    #[inline(always)]
    fn unpin_assign<'a>(&'a mut self, __param_0: &'b Self) {
        unsafe {
            crate::detail::__rust_thunk___ZN17DocCommentSlashesaSERKS_(self, __param_0);
        }
    }
}

impl<'b> ::ctor::UnpinAssign<::ctor::RvalueReference<'b, Self>> for DocCommentSlashes {
    #[inline(always)]
    fn unpin_assign<'a>(&'a mut self, __param_0: ::ctor::RvalueReference<'b, Self>) {
        unsafe {
            crate::detail::__rust_thunk___ZN17DocCommentSlashesaSEOS_(self, __param_0);
        }
    }
}

/// The default constructor which will get translated into
/// `impl Default for DocCommentSlashes`.
impl Default for DocCommentSlashes {
    #[inline(always)]
    fn default() -> Self {
        let mut tmp = ::core::mem::MaybeUninit::<Self>::zeroed();
        unsafe {
            crate::detail::__rust_thunk___ZN17DocCommentSlashesC1Ev(&mut tmp);
            tmp.assume_init()
        }
    }
}

/// An implicit conversion constructor which will get translated into `impl
/// From<int> for DocCommentSlashes`.
impl From<::core::ffi::c_int> for DocCommentSlashes {
    #[inline(always)]
    fn from(__param_0: ::core::ffi::c_int) -> Self {
        let mut tmp = ::core::mem::MaybeUninit::<Self>::zeroed();
        unsafe {
            crate::detail::__rust_thunk___ZN17DocCommentSlashesC1Ei(&mut tmp, __param_0);
            tmp.assume_init()
        }
    }
}

impl DocCommentSlashes {
    /// A non-static member function (`const` flavor).
    #[inline(always)]
    pub fn get_field_value<'a>(&'a self) -> ::core::ffi::c_int {
        unsafe { crate::detail::__rust_thunk___ZNK17DocCommentSlashes15get_field_valueEv(self) }
    }
}

impl DocCommentSlashes {
    /// A non-static member function (non-`const` flavor).
    #[inline(always)]
    pub fn set_field_value<'a>(&'a mut self, new_value: ::core::ffi::c_int) {
        unsafe {
            crate::detail::__rust_thunk___ZN17DocCommentSlashes15set_field_valueEi(self, new_value)
        }
    }
}

impl DocCommentSlashes {
    /// A static method.
    #[inline(always)]
    pub fn static_method() -> ::core::ffi::c_int {
        unsafe { crate::detail::__rust_thunk___ZN17DocCommentSlashes13static_methodEv() }
    }
}

/// Doc comment
///
///  * with slashes and bang
#[derive(Clone, Copy)]
#[repr(C)]
#[__crubit::annotate(cc_type = "DocCommentBang")]
pub struct DocCommentBang {
    /// A field
    pub i: ::core::ffi::c_int,
}
impl !Send for DocCommentBang {}
impl !Sync for DocCommentBang {}
forward_declare::unsafe_define!(forward_declare::symbol!("DocCommentBang"), crate::DocCommentBang);

impl Default for DocCommentBang {
    #[inline(always)]
    fn default() -> Self {
        let mut tmp = ::core::mem::MaybeUninit::<Self>::zeroed();
        unsafe {
            crate::detail::__rust_thunk___ZN14DocCommentBangC1Ev(&mut tmp);
            tmp.assume_init()
        }
    }
}

impl<'b> From<::ctor::RvalueReference<'b, Self>> for DocCommentBang {
    #[inline(always)]
    fn from(__param_0: ::ctor::RvalueReference<'b, Self>) -> Self {
        let mut tmp = ::core::mem::MaybeUninit::<Self>::zeroed();
        unsafe {
            crate::detail::__rust_thunk___ZN14DocCommentBangC1EOS_(&mut tmp, __param_0);
            tmp.assume_init()
        }
    }
}

impl<'b> ::ctor::UnpinAssign<&'b Self> for DocCommentBang {
    #[inline(always)]
    fn unpin_assign<'a>(&'a mut self, __param_0: &'b Self) {
        unsafe {
            crate::detail::__rust_thunk___ZN14DocCommentBangaSERKS_(self, __param_0);
        }
    }
}

impl<'b> ::ctor::UnpinAssign<::ctor::RvalueReference<'b, Self>> for DocCommentBang {
    #[inline(always)]
    fn unpin_assign<'a>(&'a mut self, __param_0: ::ctor::RvalueReference<'b, Self>) {
        unsafe {
            crate::detail::__rust_thunk___ZN14DocCommentBangaSEOS_(self, __param_0);
        }
    }
}

/// Multiline comment
///
///  with two stars
#[derive(Clone, Copy)]
#[repr(C)]
#[__crubit::annotate(cc_type = "MultilineCommentTwoStars")]
pub struct MultilineCommentTwoStars {
    /// A field
    pub i: ::core::ffi::c_int,
}
impl !Send for MultilineCommentTwoStars {}
impl !Sync for MultilineCommentTwoStars {}
forward_declare::unsafe_define!(
    forward_declare::symbol!("MultilineCommentTwoStars"),
    crate::MultilineCommentTwoStars
);

impl Default for MultilineCommentTwoStars {
    #[inline(always)]
    fn default() -> Self {
        let mut tmp = ::core::mem::MaybeUninit::<Self>::zeroed();
        unsafe {
            crate::detail::__rust_thunk___ZN24MultilineCommentTwoStarsC1Ev(&mut tmp);
            tmp.assume_init()
        }
    }
}

impl<'b> From<::ctor::RvalueReference<'b, Self>> for MultilineCommentTwoStars {
    #[inline(always)]
    fn from(__param_0: ::ctor::RvalueReference<'b, Self>) -> Self {
        let mut tmp = ::core::mem::MaybeUninit::<Self>::zeroed();
        unsafe {
            crate::detail::__rust_thunk___ZN24MultilineCommentTwoStarsC1EOS_(&mut tmp, __param_0);
            tmp.assume_init()
        }
    }
}

impl<'b> ::ctor::UnpinAssign<&'b Self> for MultilineCommentTwoStars {
    #[inline(always)]
    fn unpin_assign<'a>(&'a mut self, __param_0: &'b Self) {
        unsafe {
            crate::detail::__rust_thunk___ZN24MultilineCommentTwoStarsaSERKS_(self, __param_0);
        }
    }
}

impl<'b> ::ctor::UnpinAssign<::ctor::RvalueReference<'b, Self>> for MultilineCommentTwoStars {
    #[inline(always)]
    fn unpin_assign<'a>(&'a mut self, __param_0: ::ctor::RvalueReference<'b, Self>) {
        unsafe {
            crate::detail::__rust_thunk___ZN24MultilineCommentTwoStarsaSEOS_(self, __param_0);
        }
    }
}

/// Line comment
///
///  * with two slashes
#[derive(Clone, Copy)]
#[repr(C)]
#[__crubit::annotate(cc_type = "LineComment")]
pub struct LineComment {
    /// A field
    pub i: ::core::ffi::c_int,
}
impl !Send for LineComment {}
impl !Sync for LineComment {}
forward_declare::unsafe_define!(forward_declare::symbol!("LineComment"), crate::LineComment);

impl Default for LineComment {
    #[inline(always)]
    fn default() -> Self {
        let mut tmp = ::core::mem::MaybeUninit::<Self>::zeroed();
        unsafe {
            crate::detail::__rust_thunk___ZN11LineCommentC1Ev(&mut tmp);
            tmp.assume_init()
        }
    }
}

impl<'b> From<::ctor::RvalueReference<'b, Self>> for LineComment {
    #[inline(always)]
    fn from(__param_0: ::ctor::RvalueReference<'b, Self>) -> Self {
        let mut tmp = ::core::mem::MaybeUninit::<Self>::zeroed();
        unsafe {
            crate::detail::__rust_thunk___ZN11LineCommentC1EOS_(&mut tmp, __param_0);
            tmp.assume_init()
        }
    }
}

impl<'b> ::ctor::UnpinAssign<&'b Self> for LineComment {
    #[inline(always)]
    fn unpin_assign<'a>(&'a mut self, __param_0: &'b Self) {
        unsafe {
            crate::detail::__rust_thunk___ZN11LineCommentaSERKS_(self, __param_0);
        }
    }
}

impl<'b> ::ctor::UnpinAssign<::ctor::RvalueReference<'b, Self>> for LineComment {
    #[inline(always)]
    fn unpin_assign<'a>(&'a mut self, __param_0: ::ctor::RvalueReference<'b, Self>) {
        unsafe {
            crate::detail::__rust_thunk___ZN11LineCommentaSEOS_(self, __param_0);
        }
    }
}

/// Multiline comment
///
///  with one star
#[derive(Clone, Copy)]
#[repr(C)]
#[__crubit::annotate(cc_type = "MultilineOneStar")]
pub struct MultilineOneStar {
    /// A field
    pub i: ::core::ffi::c_int,
}
impl !Send for MultilineOneStar {}
impl !Sync for MultilineOneStar {}
forward_declare::unsafe_define!(
    forward_declare::symbol!("MultilineOneStar"),
    crate::MultilineOneStar
);

impl Default for MultilineOneStar {
    #[inline(always)]
    fn default() -> Self {
        let mut tmp = ::core::mem::MaybeUninit::<Self>::zeroed();
        unsafe {
            crate::detail::__rust_thunk___ZN16MultilineOneStarC1Ev(&mut tmp);
            tmp.assume_init()
        }
    }
}

impl<'b> From<::ctor::RvalueReference<'b, Self>> for MultilineOneStar {
    #[inline(always)]
    fn from(__param_0: ::ctor::RvalueReference<'b, Self>) -> Self {
        let mut tmp = ::core::mem::MaybeUninit::<Self>::zeroed();
        unsafe {
            crate::detail::__rust_thunk___ZN16MultilineOneStarC1EOS_(&mut tmp, __param_0);
            tmp.assume_init()
        }
    }
}

impl<'b> ::ctor::UnpinAssign<&'b Self> for MultilineOneStar {
    #[inline(always)]
    fn unpin_assign<'a>(&'a mut self, __param_0: &'b Self) {
        unsafe {
            crate::detail::__rust_thunk___ZN16MultilineOneStaraSERKS_(self, __param_0);
        }
    }
}

impl<'b> ::ctor::UnpinAssign<::ctor::RvalueReference<'b, Self>> for MultilineOneStar {
    #[inline(always)]
    fn unpin_assign<'a>(&'a mut self, __param_0: ::ctor::RvalueReference<'b, Self>) {
        unsafe {
            crate::detail::__rust_thunk___ZN16MultilineOneStaraSEOS_(self, __param_0);
        }
    }
}

/// A function
#[inline(always)]
pub fn foo() -> ::core::ffi::c_int {
    unsafe { crate::detail::__rust_thunk___Z3foov() }
}

/// A type alias
pub type MyTypeAlias = crate::DocCommentSlashes;

// Error while generating bindings for item 'MyTemplate':
// Class templates are not supported yet

// Class template specialization.

// A non-static member function in a specialization.

// Data member in a specialization.

/// Type alias to template instantiation.
pub type MyInstantiation = crate::__CcTemplateInst10MyTemplateIiE;

/// Type alias to instantiation of a template specialization.
pub type MySpecializedInstantiation = crate::__CcTemplateInst10MyTemplateIfE;

// Error while generating bindings for item 'OuterTemplate':
// Class templates are not supported yet

// Error while generating bindings for item 'ConcreteNestedStruct':
// Unsupported type 'struct OuterTemplate<int>::NestedStruct': No generated bindings found for 'NestedStruct'

/// Class template.
#[derive(Clone, Copy)]
#[repr(C)]
#[__crubit::annotate(cc_type = "MyTemplate < int >")]
pub struct __CcTemplateInst10MyTemplateIiE {
    /// Data member.
    pub value: ::core::ffi::c_int,
}
impl !Send for __CcTemplateInst10MyTemplateIiE {}
impl !Sync for __CcTemplateInst10MyTemplateIiE {}
forward_declare::unsafe_define!(
    forward_declare::symbol!("MyTemplate < int >"),
    crate::__CcTemplateInst10MyTemplateIiE
);

impl Default for __CcTemplateInst10MyTemplateIiE {
    #[inline(always)]
    fn default() -> Self {
        let mut tmp = ::core::mem::MaybeUninit::<Self>::zeroed();
        unsafe {
            crate::detail::__rust_thunk___ZN10MyTemplateIiEC1Ev__2f_2fthird_5fparty_2fcrubit_2frs_5fbindings_5ffrom_5fcc_2ftest_2fgolden_3adoc_5fcomment_5fcc(&mut tmp);
            tmp.assume_init()
        }
    }
}

impl<'b> From<::ctor::RvalueReference<'b, Self>> for __CcTemplateInst10MyTemplateIiE {
    #[inline(always)]
    fn from(__param_0: ::ctor::RvalueReference<'b, Self>) -> Self {
        let mut tmp = ::core::mem::MaybeUninit::<Self>::zeroed();
        unsafe {
            crate::detail::__rust_thunk___ZN10MyTemplateIiEC1EOS0___2f_2fthird_5fparty_2fcrubit_2frs_5fbindings_5ffrom_5fcc_2ftest_2fgolden_3adoc_5fcomment_5fcc(&mut tmp,__param_0);
            tmp.assume_init()
        }
    }
}

impl<'b> ::ctor::UnpinAssign<&'b Self> for __CcTemplateInst10MyTemplateIiE {
    #[inline(always)]
    fn unpin_assign<'a>(&'a mut self, __param_0: &'b Self) {
        unsafe {
            crate::detail::__rust_thunk___ZN10MyTemplateIiEaSERKS0___2f_2fthird_5fparty_2fcrubit_2frs_5fbindings_5ffrom_5fcc_2ftest_2fgolden_3adoc_5fcomment_5fcc(self,__param_0);
        }
    }
}

impl<'b> ::ctor::UnpinAssign<::ctor::RvalueReference<'b, Self>>
    for __CcTemplateInst10MyTemplateIiE
{
    #[inline(always)]
    fn unpin_assign<'a>(&'a mut self, __param_0: ::ctor::RvalueReference<'b, Self>) {
        unsafe {
            crate::detail::__rust_thunk___ZN10MyTemplateIiEaSEOS0___2f_2fthird_5fparty_2fcrubit_2frs_5fbindings_5ffrom_5fcc_2ftest_2fgolden_3adoc_5fcomment_5fcc(self,__param_0);
        }
    }
}

// A non-static member function.

impl __CcTemplateInst10MyTemplateIiE {
    /// A non-static member function.
    #[inline(always)]
    pub fn get_field_value<'a>(&'a self) -> &'a ::core::ffi::c_int {
        unsafe {
            crate::detail::__rust_thunk___ZNK10MyTemplateIiE15get_field_valueEv__2f_2fthird_5fparty_2fcrubit_2frs_5fbindings_5ffrom_5fcc_2ftest_2fgolden_3adoc_5fcomment_5fcc(self)
        }
    }
}

/// Class template specialization.
#[derive(Clone, Copy)]
#[repr(C)]
#[__crubit::annotate(cc_type = "MyTemplate < float >")]
pub struct __CcTemplateInst10MyTemplateIfE {
    /// Data member in a specialization.
    pub value: f32,
}
impl !Send for __CcTemplateInst10MyTemplateIfE {}
impl !Sync for __CcTemplateInst10MyTemplateIfE {}
forward_declare::unsafe_define!(
    forward_declare::symbol!("MyTemplate < float >"),
    crate::__CcTemplateInst10MyTemplateIfE
);

impl Default for __CcTemplateInst10MyTemplateIfE {
    #[inline(always)]
    fn default() -> Self {
        let mut tmp = ::core::mem::MaybeUninit::<Self>::zeroed();
        unsafe {
            crate::detail::__rust_thunk___ZN10MyTemplateIfEC1Ev__2f_2fthird_5fparty_2fcrubit_2frs_5fbindings_5ffrom_5fcc_2ftest_2fgolden_3adoc_5fcomment_5fcc(&mut tmp);
            tmp.assume_init()
        }
    }
}

impl<'b> From<::ctor::RvalueReference<'b, Self>> for __CcTemplateInst10MyTemplateIfE {
    #[inline(always)]
    fn from(__param_0: ::ctor::RvalueReference<'b, Self>) -> Self {
        let mut tmp = ::core::mem::MaybeUninit::<Self>::zeroed();
        unsafe {
            crate::detail::__rust_thunk___ZN10MyTemplateIfEC1EOS0___2f_2fthird_5fparty_2fcrubit_2frs_5fbindings_5ffrom_5fcc_2ftest_2fgolden_3adoc_5fcomment_5fcc(&mut tmp,__param_0);
            tmp.assume_init()
        }
    }
}

impl<'b> ::ctor::UnpinAssign<&'b Self> for __CcTemplateInst10MyTemplateIfE {
    #[inline(always)]
    fn unpin_assign<'a>(&'a mut self, __param_0: &'b Self) {
        unsafe {
            crate::detail::__rust_thunk___ZN10MyTemplateIfEaSERKS0___2f_2fthird_5fparty_2fcrubit_2frs_5fbindings_5ffrom_5fcc_2ftest_2fgolden_3adoc_5fcomment_5fcc(self,__param_0);
        }
    }
}

impl<'b> ::ctor::UnpinAssign<::ctor::RvalueReference<'b, Self>>
    for __CcTemplateInst10MyTemplateIfE
{
    #[inline(always)]
    fn unpin_assign<'a>(&'a mut self, __param_0: ::ctor::RvalueReference<'b, Self>) {
        unsafe {
            crate::detail::__rust_thunk___ZN10MyTemplateIfEaSEOS0___2f_2fthird_5fparty_2fcrubit_2frs_5fbindings_5ffrom_5fcc_2ftest_2fgolden_3adoc_5fcomment_5fcc(self,__param_0);
        }
    }
}

impl __CcTemplateInst10MyTemplateIfE {
    /// A non-static member function in a specialization.
    #[inline(always)]
    pub fn get_field_value<'a>(&'a self) -> &'a f32 {
        unsafe {
            crate::detail::__rust_thunk___ZNK10MyTemplateIfE15get_field_valueEv__2f_2fthird_5fparty_2fcrubit_2frs_5fbindings_5ffrom_5fcc_2ftest_2fgolden_3adoc_5fcomment_5fcc(self)
        }
    }
}

mod detail {
    #[allow(unused_imports)]
    use super::*;
    extern "C" {
        pub(crate) fn __rust_thunk___ZN17DocCommentSlashesC1EOS_<'a, 'b>(
            __this: &'a mut ::core::mem::MaybeUninit<crate::DocCommentSlashes>,
            __param_0: ::ctor::RvalueReference<'b, crate::DocCommentSlashes>,
        );
        pub(crate) fn __rust_thunk___ZN17DocCommentSlashesaSERKS_<'a, 'b>(
            __this: &'a mut crate::DocCommentSlashes,
            __param_0: &'b crate::DocCommentSlashes,
        ) -> &'a mut crate::DocCommentSlashes;
        pub(crate) fn __rust_thunk___ZN17DocCommentSlashesaSEOS_<'a, 'b>(
            __this: &'a mut crate::DocCommentSlashes,
            __param_0: ::ctor::RvalueReference<'b, crate::DocCommentSlashes>,
        ) -> &'a mut crate::DocCommentSlashes;
        #[link_name = "_ZN17DocCommentSlashesC1Ev"]
        pub(crate) fn __rust_thunk___ZN17DocCommentSlashesC1Ev<'a>(
            __this: &'a mut ::core::mem::MaybeUninit<crate::DocCommentSlashes>,
        );
        #[link_name = "_ZN17DocCommentSlashesC1Ei"]
        pub(crate) fn __rust_thunk___ZN17DocCommentSlashesC1Ei<'a>(
            __this: &'a mut ::core::mem::MaybeUninit<crate::DocCommentSlashes>,
            __param_0: ::core::ffi::c_int,
        );
        #[link_name = "_ZNK17DocCommentSlashes15get_field_valueEv"]
        pub(crate) fn __rust_thunk___ZNK17DocCommentSlashes15get_field_valueEv<'a>(
            __this: &'a crate::DocCommentSlashes,
        ) -> ::core::ffi::c_int;
        #[link_name = "_ZN17DocCommentSlashes15set_field_valueEi"]
        pub(crate) fn __rust_thunk___ZN17DocCommentSlashes15set_field_valueEi<'a>(
            __this: &'a mut crate::DocCommentSlashes,
            new_value: ::core::ffi::c_int,
        );
        #[link_name = "_ZN17DocCommentSlashes13static_methodEv"]
        pub(crate) fn __rust_thunk___ZN17DocCommentSlashes13static_methodEv() -> ::core::ffi::c_int;
        pub(crate) fn __rust_thunk___ZN14DocCommentBangC1Ev<'a>(
            __this: &'a mut ::core::mem::MaybeUninit<crate::DocCommentBang>,
        );
        pub(crate) fn __rust_thunk___ZN14DocCommentBangC1EOS_<'a, 'b>(
            __this: &'a mut ::core::mem::MaybeUninit<crate::DocCommentBang>,
            __param_0: ::ctor::RvalueReference<'b, crate::DocCommentBang>,
        );
        pub(crate) fn __rust_thunk___ZN14DocCommentBangaSERKS_<'a, 'b>(
            __this: &'a mut crate::DocCommentBang,
            __param_0: &'b crate::DocCommentBang,
        ) -> &'a mut crate::DocCommentBang;
        pub(crate) fn __rust_thunk___ZN14DocCommentBangaSEOS_<'a, 'b>(
            __this: &'a mut crate::DocCommentBang,
            __param_0: ::ctor::RvalueReference<'b, crate::DocCommentBang>,
        ) -> &'a mut crate::DocCommentBang;
        pub(crate) fn __rust_thunk___ZN24MultilineCommentTwoStarsC1Ev<'a>(
            __this: &'a mut ::core::mem::MaybeUninit<crate::MultilineCommentTwoStars>,
        );
        pub(crate) fn __rust_thunk___ZN24MultilineCommentTwoStarsC1EOS_<'a, 'b>(
            __this: &'a mut ::core::mem::MaybeUninit<crate::MultilineCommentTwoStars>,
            __param_0: ::ctor::RvalueReference<'b, crate::MultilineCommentTwoStars>,
        );
        pub(crate) fn __rust_thunk___ZN24MultilineCommentTwoStarsaSERKS_<'a, 'b>(
            __this: &'a mut crate::MultilineCommentTwoStars,
            __param_0: &'b crate::MultilineCommentTwoStars,
        ) -> &'a mut crate::MultilineCommentTwoStars;
        pub(crate) fn __rust_thunk___ZN24MultilineCommentTwoStarsaSEOS_<'a, 'b>(
            __this: &'a mut crate::MultilineCommentTwoStars,
            __param_0: ::ctor::RvalueReference<'b, crate::MultilineCommentTwoStars>,
        ) -> &'a mut crate::MultilineCommentTwoStars;
        pub(crate) fn __rust_thunk___ZN11LineCommentC1Ev<'a>(
            __this: &'a mut ::core::mem::MaybeUninit<crate::LineComment>,
        );
        pub(crate) fn __rust_thunk___ZN11LineCommentC1EOS_<'a, 'b>(
            __this: &'a mut ::core::mem::MaybeUninit<crate::LineComment>,
            __param_0: ::ctor::RvalueReference<'b, crate::LineComment>,
        );
        pub(crate) fn __rust_thunk___ZN11LineCommentaSERKS_<'a, 'b>(
            __this: &'a mut crate::LineComment,
            __param_0: &'b crate::LineComment,
        ) -> &'a mut crate::LineComment;
        pub(crate) fn __rust_thunk___ZN11LineCommentaSEOS_<'a, 'b>(
            __this: &'a mut crate::LineComment,
            __param_0: ::ctor::RvalueReference<'b, crate::LineComment>,
        ) -> &'a mut crate::LineComment;
        pub(crate) fn __rust_thunk___ZN16MultilineOneStarC1Ev<'a>(
            __this: &'a mut ::core::mem::MaybeUninit<crate::MultilineOneStar>,
        );
        pub(crate) fn __rust_thunk___ZN16MultilineOneStarC1EOS_<'a, 'b>(
            __this: &'a mut ::core::mem::MaybeUninit<crate::MultilineOneStar>,
            __param_0: ::ctor::RvalueReference<'b, crate::MultilineOneStar>,
        );
        pub(crate) fn __rust_thunk___ZN16MultilineOneStaraSERKS_<'a, 'b>(
            __this: &'a mut crate::MultilineOneStar,
            __param_0: &'b crate::MultilineOneStar,
        ) -> &'a mut crate::MultilineOneStar;
        pub(crate) fn __rust_thunk___ZN16MultilineOneStaraSEOS_<'a, 'b>(
            __this: &'a mut crate::MultilineOneStar,
            __param_0: ::ctor::RvalueReference<'b, crate::MultilineOneStar>,
        ) -> &'a mut crate::MultilineOneStar;
        pub(crate) fn __rust_thunk___Z3foov() -> ::core::ffi::c_int;
        pub(crate) fn __rust_thunk___ZN10MyTemplateIiEC1Ev__2f_2fthird_5fparty_2fcrubit_2frs_5fbindings_5ffrom_5fcc_2ftest_2fgolden_3adoc_5fcomment_5fcc<
            'a,
        >(
            __this: &'a mut ::core::mem::MaybeUninit<crate::__CcTemplateInst10MyTemplateIiE>,
        );
        pub(crate) fn __rust_thunk___ZN10MyTemplateIiEC1EOS0___2f_2fthird_5fparty_2fcrubit_2frs_5fbindings_5ffrom_5fcc_2ftest_2fgolden_3adoc_5fcomment_5fcc<
            'a,
            'b,
        >(
            __this: &'a mut ::core::mem::MaybeUninit<crate::__CcTemplateInst10MyTemplateIiE>,
            __param_0: ::ctor::RvalueReference<'b, crate::__CcTemplateInst10MyTemplateIiE>,
        );
        pub(crate) fn __rust_thunk___ZN10MyTemplateIiEaSERKS0___2f_2fthird_5fparty_2fcrubit_2frs_5fbindings_5ffrom_5fcc_2ftest_2fgolden_3adoc_5fcomment_5fcc<
            'a,
            'b,
        >(
            __this: &'a mut crate::__CcTemplateInst10MyTemplateIiE,
            __param_0: &'b crate::__CcTemplateInst10MyTemplateIiE,
        ) -> &'a mut crate::__CcTemplateInst10MyTemplateIiE;
        pub(crate) fn __rust_thunk___ZN10MyTemplateIiEaSEOS0___2f_2fthird_5fparty_2fcrubit_2frs_5fbindings_5ffrom_5fcc_2ftest_2fgolden_3adoc_5fcomment_5fcc<
            'a,
            'b,
        >(
            __this: &'a mut crate::__CcTemplateInst10MyTemplateIiE,
            __param_0: ::ctor::RvalueReference<'b, crate::__CcTemplateInst10MyTemplateIiE>,
        ) -> &'a mut crate::__CcTemplateInst10MyTemplateIiE;
        pub(crate) fn __rust_thunk___ZNK10MyTemplateIiE15get_field_valueEv__2f_2fthird_5fparty_2fcrubit_2frs_5fbindings_5ffrom_5fcc_2ftest_2fgolden_3adoc_5fcomment_5fcc<
            'a,
        >(
            __this: &'a crate::__CcTemplateInst10MyTemplateIiE,
        ) -> &'a ::core::ffi::c_int;
        pub(crate) fn __rust_thunk___ZN10MyTemplateIfEC1Ev__2f_2fthird_5fparty_2fcrubit_2frs_5fbindings_5ffrom_5fcc_2ftest_2fgolden_3adoc_5fcomment_5fcc<
            'a,
        >(
            __this: &'a mut ::core::mem::MaybeUninit<crate::__CcTemplateInst10MyTemplateIfE>,
        );
        pub(crate) fn __rust_thunk___ZN10MyTemplateIfEC1EOS0___2f_2fthird_5fparty_2fcrubit_2frs_5fbindings_5ffrom_5fcc_2ftest_2fgolden_3adoc_5fcomment_5fcc<
            'a,
            'b,
        >(
            __this: &'a mut ::core::mem::MaybeUninit<crate::__CcTemplateInst10MyTemplateIfE>,
            __param_0: ::ctor::RvalueReference<'b, crate::__CcTemplateInst10MyTemplateIfE>,
        );
        pub(crate) fn __rust_thunk___ZN10MyTemplateIfEaSERKS0___2f_2fthird_5fparty_2fcrubit_2frs_5fbindings_5ffrom_5fcc_2ftest_2fgolden_3adoc_5fcomment_5fcc<
            'a,
            'b,
        >(
            __this: &'a mut crate::__CcTemplateInst10MyTemplateIfE,
            __param_0: &'b crate::__CcTemplateInst10MyTemplateIfE,
        ) -> &'a mut crate::__CcTemplateInst10MyTemplateIfE;
        pub(crate) fn __rust_thunk___ZN10MyTemplateIfEaSEOS0___2f_2fthird_5fparty_2fcrubit_2frs_5fbindings_5ffrom_5fcc_2ftest_2fgolden_3adoc_5fcomment_5fcc<
            'a,
            'b,
        >(
            __this: &'a mut crate::__CcTemplateInst10MyTemplateIfE,
            __param_0: ::ctor::RvalueReference<'b, crate::__CcTemplateInst10MyTemplateIfE>,
        ) -> &'a mut crate::__CcTemplateInst10MyTemplateIfE;
        pub(crate) fn __rust_thunk___ZNK10MyTemplateIfE15get_field_valueEv__2f_2fthird_5fparty_2fcrubit_2frs_5fbindings_5ffrom_5fcc_2ftest_2fgolden_3adoc_5fcomment_5fcc<
            'a,
        >(
            __this: &'a crate::__CcTemplateInst10MyTemplateIfE,
        ) -> &'a f32;
    }
}

const _: () = {
    assert!(::core::mem::size_of::<crate::DocCommentSlashes>() == 4);
    assert!(::core::mem::align_of::<crate::DocCommentSlashes>() == 4);
    static_assertions::assert_impl_all!(crate::DocCommentSlashes: Clone);
    static_assertions::assert_impl_all!(crate::DocCommentSlashes: Copy);
    static_assertions::assert_not_impl_any!(crate::DocCommentSlashes: Drop);
    assert!(::core::mem::offset_of!(crate::DocCommentSlashes, i) == 0);

    assert!(::core::mem::size_of::<crate::DocCommentBang>() == 4);
    assert!(::core::mem::align_of::<crate::DocCommentBang>() == 4);
    static_assertions::assert_impl_all!(crate::DocCommentBang: Clone);
    static_assertions::assert_impl_all!(crate::DocCommentBang: Copy);
    static_assertions::assert_not_impl_any!(crate::DocCommentBang: Drop);
    assert!(::core::mem::offset_of!(crate::DocCommentBang, i) == 0);

    assert!(::core::mem::size_of::<crate::MultilineCommentTwoStars>() == 4);
    assert!(::core::mem::align_of::<crate::MultilineCommentTwoStars>() == 4);
    static_assertions::assert_impl_all!(crate::MultilineCommentTwoStars: Clone);
    static_assertions::assert_impl_all!(crate::MultilineCommentTwoStars: Copy);
    static_assertions::assert_not_impl_any!(crate::MultilineCommentTwoStars: Drop);
    assert!(::core::mem::offset_of!(crate::MultilineCommentTwoStars, i) == 0);

    assert!(::core::mem::size_of::<crate::LineComment>() == 4);
    assert!(::core::mem::align_of::<crate::LineComment>() == 4);
    static_assertions::assert_impl_all!(crate::LineComment: Clone);
    static_assertions::assert_impl_all!(crate::LineComment: Copy);
    static_assertions::assert_not_impl_any!(crate::LineComment: Drop);
    assert!(::core::mem::offset_of!(crate::LineComment, i) == 0);

    assert!(::core::mem::size_of::<crate::MultilineOneStar>() == 4);
    assert!(::core::mem::align_of::<crate::MultilineOneStar>() == 4);
    static_assertions::assert_impl_all!(crate::MultilineOneStar: Clone);
    static_assertions::assert_impl_all!(crate::MultilineOneStar: Copy);
    static_assertions::assert_not_impl_any!(crate::MultilineOneStar: Drop);
    assert!(::core::mem::offset_of!(crate::MultilineOneStar, i) == 0);

    assert!(::core::mem::size_of::<crate::__CcTemplateInst10MyTemplateIiE>() == 4);
    assert!(::core::mem::align_of::<crate::__CcTemplateInst10MyTemplateIiE>() == 4);
    static_assertions::assert_impl_all!(crate::__CcTemplateInst10MyTemplateIiE: Clone);
    static_assertions::assert_impl_all!(crate::__CcTemplateInst10MyTemplateIiE: Copy);
    static_assertions::assert_not_impl_any!(crate::__CcTemplateInst10MyTemplateIiE: Drop);
    assert!(::core::mem::offset_of!(crate::__CcTemplateInst10MyTemplateIiE, value) == 0);

    assert!(::core::mem::size_of::<crate::__CcTemplateInst10MyTemplateIfE>() == 4);
    assert!(::core::mem::align_of::<crate::__CcTemplateInst10MyTemplateIfE>() == 4);
    static_assertions::assert_impl_all!(crate::__CcTemplateInst10MyTemplateIfE: Clone);
    static_assertions::assert_impl_all!(crate::__CcTemplateInst10MyTemplateIfE: Copy);
    static_assertions::assert_not_impl_any!(crate::__CcTemplateInst10MyTemplateIfE: Drop);
    assert!(::core::mem::offset_of!(crate::__CcTemplateInst10MyTemplateIfE, value) == 0);
};
