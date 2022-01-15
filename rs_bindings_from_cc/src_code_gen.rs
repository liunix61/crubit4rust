// Part of the Crubit project, under the Apache License v2.0 with LLVM
// Exceptions. See /LICENSE for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#[cfg(test)]
#[macro_use]
extern crate static_assertions;

use anyhow::{anyhow, bail, ensure, Context, Result};
use ffi_types::*;
use ir::*;
use itertools::Itertools;
use proc_macro2::{Ident, Literal, TokenStream};
use quote::format_ident;
use quote::quote;
use std::collections::{BTreeSet, HashMap, HashSet};
use std::iter::Iterator;
use std::panic::catch_unwind;
use std::process;
use token_stream_printer::{rs_tokens_to_formatted_string, tokens_to_string};

/// FFI equivalent of `Bindings`.
#[repr(C)]
pub struct FfiBindings {
    rs_api: FfiU8SliceBox,
    rs_api_impl: FfiU8SliceBox,
}

/// Deserializes IR from `json` and generates bindings source code.
///
/// This function panics on error.
///
/// Ownership:
///    * function doesn't take ownership of (in other words it borrows) the
///      param `json`
///    * function passes ownership of the returned value to the caller
///
/// Safety:
///    * function expects that param `json` is a FfiU8Slice for a valid array of
///      bytes with the given size.
///    * function expects that param `json` doesn't change during the call.
#[no_mangle]
pub unsafe extern "C" fn GenerateBindingsImpl(json: FfiU8Slice) -> FfiBindings {
    catch_unwind(|| {
        // It is ok to abort here.
        let Bindings { rs_api, rs_api_impl } = generate_bindings(json.as_slice()).unwrap();

        FfiBindings {
            rs_api: FfiU8SliceBox::from_boxed_slice(rs_api.into_bytes().into_boxed_slice()),
            rs_api_impl: FfiU8SliceBox::from_boxed_slice(
                rs_api_impl.into_bytes().into_boxed_slice(),
            ),
        }
    })
    .unwrap_or_else(|_| process::abort())
}

/// Source code for generated bindings.
struct Bindings {
    // Rust source code.
    rs_api: String,
    // C++ source code.
    rs_api_impl: String,
}

fn generate_bindings(json: &[u8]) -> Result<Bindings> {
    let ir = deserialize_ir(json)?;

    // The code is formatted with a non-default rustfmt configuration. Prevent
    // downstream workflows from reformatting with a different configuration.
    let rs_api =
        format!("#![rustfmt::skip]\n{}", rs_tokens_to_formatted_string(generate_rs_api(&ir)?)?);
    let rs_api_impl = tokens_to_string(generate_rs_api_impl(&ir)?)?;

    Ok(Bindings { rs_api, rs_api_impl })
}

/// Rust source code with attached information about how to modify the parent
/// crate.
///
/// For example, the snippet `vec![].into_raw_parts()` is not valid unless the
/// `vec_into_raw_parts` feature is enabled. So such a snippet should be
/// represented as:
///
/// ```
/// RsSnippet {
///   features: btree_set![make_ident("vec_into_raw_parts")],
///   tokens: quote!{vec![].into_raw_parts()},
/// }
/// ```
struct RsSnippet {
    /// Rust feature flags used by this snippet.
    features: BTreeSet<Ident>,
    /// The snippet itself, as a token stream.
    tokens: TokenStream,
}

impl From<TokenStream> for RsSnippet {
    fn from(tokens: TokenStream) -> Self {
        RsSnippet { features: BTreeSet::new(), tokens }
    }
}

/// If we know the original C++ function is codegenned and already compatible
/// with `extern "C"` calling convention we skip creating/calling the C++ thunk
/// since we can call the original C++ directly.
fn can_skip_cc_thunk(func: &Func) -> bool {
    // ## Inline functions
    //
    // Inline functions may not be codegenned in the C++ library since Clang doesn't
    // know if Rust calls the function or not. Therefore in order to make inline
    // functions callable from Rust we need to generate a C++ file that defines
    // a thunk that delegates to the original inline function. When compiled,
    // Clang will emit code for this thunk and Rust code will call the
    // thunk when the user wants to call the original inline function.
    //
    // This is not great runtime-performance-wise in regular builds (inline function
    // will not be inlined, there will always be a function call), but it is
    // correct. ThinLTO builds will be able to see through the thunk and inline
    // code across the language boundary. For non-ThinLTO builds we plan to
    // implement <internal link> which removes the runtime performance overhead.
    if func.is_inline {
        return false;
    }
    // ## Virtual functions
    //
    // When calling virtual `A::Method()`, it's not necessarily the case that we'll
    // specifically call the concrete `A::Method` impl. For example, if this is
    // called on something whose dynamic type is some subclass `B` with an
    // overridden `B::Method`, then we'll call that.
    //
    // We must reuse the C++ dynamic dispatching system. In this case, the easiest
    // way to do it is by resorting to a C++ thunk, whose implementation will do
    // the lookup.
    //
    // In terms of runtime performance, since this only occurs for virtual function
    // calls, which are already slow, it may not be such a big deal. We can
    // benchmark it later. :)
    if let Some(meta) = &func.member_func_metadata {
        if let Some(inst_meta) = &meta.instance_method_metadata {
            if inst_meta.is_virtual {
                return false;
            }
        }
    }

    true
}

/// Uniquely identifies a generated Rust function.
#[derive(Clone, PartialEq, Eq, Hash)]
struct FunctionId {
    // If the function is on a trait impl, contains the name of the Self type for
    // which the trait is being implemented.
    self_type: Option<syn::Path>,
    // Fully qualified path of the function. For functions in impl blocks, this
    // includes the name of the type or trait on which the function is being
    // implemented, e.g. `Default::default`.
    function_path: syn::Path,
}

/// Returns the name of `func` in C++ synatx.
fn cxx_function_name(func: &Func, ir: &IR) -> Result<String> {
    let record: Option<&str> = func
        .member_func_metadata
        .as_ref()
        .map(|meta| meta.find_record(ir))
        .transpose()?
        .map(|r| &*r.identifier.identifier);

    let func_name = match &func.name {
        UnqualifiedIdentifier::Identifier(id) => id.identifier.clone(),
        UnqualifiedIdentifier::Destructor => {
            format!("~{}", record.expect("destructor must be associated with a record"))
        }
        UnqualifiedIdentifier::Constructor => {
            format!("~{}", record.expect("constructor must be associated with a record"))
        }
    };

    if let Some(record_name) = record {
        Ok(format!("{}::{}", record_name, func_name))
    } else {
        Ok(func_name)
    }
}

/// Generates Rust source code for a given `Func`.
///
/// Returns None if no code was generated for the function; otherwise, returns
/// a tuple containing:
/// - The generated function or trait impl
/// - The thunk
/// - A `FunctionId` identifying the generated Rust function
fn generate_func(func: &Func, ir: &IR) -> Result<Option<(RsSnippet, RsSnippet, FunctionId)>> {
    let mangled_name = &func.mangled_name;
    let thunk_ident = thunk_ident(func);
    let doc_comment = generate_doc_comment(&func.doc_comment);
    let lifetime_to_name = HashMap::<LifetimeId, String>::from_iter(
        func.lifetime_params.iter().map(|l| (l.id, l.name.clone())),
    );
    let return_type_fragment = if func.return_type.rs_type.is_unit_type() {
        quote! {}
    } else {
        let return_type_name = format_rs_type(&func.return_type.rs_type, ir, &lifetime_to_name)
            .with_context(|| format!("Failed to format return type for {:?}", func))?;
        quote! { -> #return_type_name }
    };

    let param_idents =
        func.params.iter().map(|p| make_ident(&p.identifier.identifier)).collect_vec();

    let param_types = func
        .params
        .iter()
        .map(|p| {
            format_rs_type(&p.type_.rs_type, ir, &lifetime_to_name).with_context(|| {
                format!("Failed to format type for parameter {:?} on {:?}", p, func)
            })
        })
        .collect::<Result<Vec<_>>>()?;

    let lifetimes = func
        .lifetime_params
        .iter()
        .map(|l| syn::Lifetime::new(&format!("'{}", l.name), proc_macro2::Span::call_site()));
    let generic_params = format_generic_params(lifetimes);

    let maybe_record: Option<&Record> =
        func.member_func_metadata.as_ref().map(|meta| meta.find_record(ir)).transpose()?;

    // Figure out 1) the name and trait of the API function to generate and 2)
    // whether its first param should be spelled `&self` or `&mut self`.
    enum ImplKind {
        None,               // No `impl` needed
        Struct,             // e.g. `impl SomeStruct { ... }`
        Trait(TokenStream), // e.g. `impl From<int> for SomeStruct { ... }`
    }
    let impl_kind: ImplKind;
    let func_name: syn::Ident;
    let format_first_param_as_self: bool;
    match &func.name {
        UnqualifiedIdentifier::Identifier(id) => {
            impl_kind = match maybe_record {
                None => ImplKind::None,
                Some(_) => ImplKind::Struct,
            };
            func_name = make_ident(&id.identifier);
            format_first_param_as_self = func.is_instance_method();
        }
        UnqualifiedIdentifier::Destructor => {
            // Note: to avoid double-destruction of the fields, they are all wrapped in
            // ManuallyDrop in this case. See `generate_record`.
            let record =
                maybe_record.ok_or_else(|| anyhow!("Destructors must be member functions."))?;
            if !should_implement_drop(record) {
                return Ok(None);
            }
            impl_kind = ImplKind::Trait(quote! {Drop});
            func_name = make_ident("drop");
            format_first_param_as_self = true;
        }
        UnqualifiedIdentifier::Constructor => {
            let record =
                maybe_record.ok_or_else(|| anyhow!("Constructors must be member functions."))?;
            if !record.is_unpin() {
                // TODO: Handle <internal link>
                return Ok(None);
            }
            match func.params.len() {
                0 => bail!("Constructor should have at least 1 parameter (__this)"),
                1 => {
                    impl_kind = ImplKind::Trait(quote! {Default});
                    func_name = make_ident("default");
                    format_first_param_as_self = false;
                }
                2 => {
                    // TODO(lukasza): Do something smart with move constructor.
                    let param_rs_type_kind = RsTypeKind::new(&func.params[1].type_.rs_type, ir)?;
                    if param_rs_type_kind.is_shared_ref_to(record) {
                        // Copy constructor
                        if should_derive_clone(record) {
                            return Ok(None);
                        } else {
                            impl_kind = ImplKind::Trait(quote! { Clone });
                            func_name = make_ident("clone");
                            format_first_param_as_self = true;
                        }
                    } else {
                        let param_type = &param_types[1];
                        impl_kind = ImplKind::Trait(quote! {From< #param_type >});
                        func_name = make_ident("from");
                        format_first_param_as_self = false;
                    }
                }
                _ => {
                    // TODO(b/200066396): Map other constructors to something
                    // (maybe to static method if named via a
                    // bindings-generator-recognized C++ attribute).
                    return Ok(None);
                }
            }
        }
    }

    let api_func_def = {
        let mut return_type_fragment = return_type_fragment.clone();
        let mut thunk_args = param_idents.iter().map(|id| quote! { #id}).collect_vec();
        let mut api_params = param_idents
            .iter()
            .zip(param_types.iter())
            .map(|(ident, type_)| quote! { #ident : #type_ })
            .collect_vec();
        let mut maybe_first_api_param = func.params.get(0);

        if func.name == UnqualifiedIdentifier::Constructor {
            return_type_fragment = quote! { -> Self };

            // Drop `__this` parameter from the public Rust API.
            // TODO(lukasza): Also trim `generic_params` to avoid running into a
            // (future) unused lifetime parameters warning (see also
            // https://github.com/rust-lang/rust/issues/41960).
            api_params.remove(0); // Presence of element #0 is indirectly verified
            thunk_args.remove(0); // by one of `match` statements above.
            maybe_first_api_param = func.params.get(1);
        }

        // Change `__this: &'a SomeStruct` into `&'a self` if needed.
        if format_first_param_as_self {
            let first_api_param = maybe_first_api_param
                .ok_or_else(|| anyhow!("No parameter to format as 'self': {:?}", func))?;
            let self_decl = RsTypeKind::new(&first_api_param.type_.rs_type, ir)?
                .format_as_self_param_for_instance_method(func, ir, &lifetime_to_name)
                .with_context(|| {
                    format!("Failed to format as `self` param: {:?}", first_api_param)
                })?;
            if let Some(new_decl) = self_decl {
                api_params[0] = new_decl; // Presence of element #0 is verified by
                thunk_args[0] = quote! { self }; // `ok_or_else` on `maybe_first_api_param` above.
            }
        }

        let func_body = match &func.name {
            UnqualifiedIdentifier::Identifier(_) | UnqualifiedIdentifier::Destructor => {
                quote! { unsafe { crate::detail::#thunk_ident( #( #thunk_args ),* ) } }
            }
            UnqualifiedIdentifier::Constructor => {
                // SAFETY: A user-defined constructor is not guaranteed to
                // initialize all the fields. To make the `assume_init()` call
                // below safe, the memory is zero-initialized first. This is a
                // bit safer, because zero-initialized memory represents a valid
                // value for the currently supported field types (this may
                // change once the bindings generator starts supporting
                // reference fields). TODO(b/213243309): Double-check if
                // zero-initialization is desirable here.
                quote! {
                    let mut tmp = std::mem::MaybeUninit::<Self>::zeroed();
                    unsafe {
                        crate::detail::#thunk_ident( &mut tmp #( , #thunk_args )* );
                        tmp.assume_init()
                    }
                }
            }
        };

        let pub_ = match impl_kind {
            ImplKind::None | ImplKind::Struct => quote! { pub },
            ImplKind::Trait(_) => quote! {},
        };

        quote! {
            #[inline(always)]
            #pub_ fn #func_name #generic_params( #( #api_params ),* ) #return_type_fragment {
                #func_body
            }
        }
    };

    let api_func: TokenStream;
    let function_id: FunctionId;
    let maybe_record_name = maybe_record.map(|r| make_ident(&r.identifier.identifier));
    match impl_kind {
        ImplKind::None => {
            api_func = quote! { #doc_comment #api_func_def };
            function_id = FunctionId { self_type: None, function_path: func_name.into() };
        }
        ImplKind::Struct => {
            let record_name =
                maybe_record_name.ok_or_else(|| anyhow!("Struct methods must have records"))?;
            api_func = quote! { impl #record_name { #doc_comment #api_func_def } };
            function_id = FunctionId {
                self_type: None,
                function_path: syn::parse2(quote! { #record_name :: #func_name })?,
            };
        }
        ImplKind::Trait(trait_name) => {
            let record_name =
                maybe_record_name.ok_or_else(|| anyhow!("Trait methods must have records"))?;
            api_func = quote! { #doc_comment impl #trait_name for #record_name { #api_func_def } };
            function_id = FunctionId {
                self_type: Some(record_name.into()),
                function_path: syn::parse2(quote! { #trait_name :: #func_name })?,
            };
        }
    }

    let thunk = {
        let thunk_attr = if can_skip_cc_thunk(func) {
            quote! {#[link_name = #mangled_name]}
        } else {
            quote! {}
        };

        // For constructors inject MaybeUninit into the type of `__this_` parameter.
        let mut param_types = param_types;
        if func.name == UnqualifiedIdentifier::Constructor {
            if param_types.is_empty() || func.params.is_empty() {
                bail!("Constructors should have at least one parameter (__this)");
            }
            param_types[0] = RsTypeKind::new(&func.params[0].type_.rs_type, ir)?
                .format_as_this_param_for_constructor_thunk(ir, &lifetime_to_name)
                .with_context(|| {
                    format!("Failed to format `__this` param for a thunk: {:?}", func.params[0])
                })?;
        }

        quote! {
            #thunk_attr
            pub(crate) fn #thunk_ident #generic_params( #( #param_idents: #param_types ),*
            ) #return_type_fragment ;
        }
    };

    Ok(Some((api_func.into(), thunk.into(), function_id)))
}

fn generate_doc_comment(comment: &Option<String>) -> TokenStream {
    match comment {
        Some(text) => {
            // token_stream_printer (and rustfmt) don't put a space between /// and the doc
            // comment, let's add it here so our comments are pretty.
            let doc = format!(" {}", text.replace("\n", "\n "));
            quote! {#[doc=#doc]}
        }
        None => quote! {},
    }
}

fn format_generic_params<T: quote::ToTokens>(params: impl IntoIterator<Item = T>) -> TokenStream {
    let mut params = params.into_iter().peekable();
    if params.peek().is_none() {
        quote! {}
    } else {
        quote! { < #( #params ),* > }
    }
}

fn should_implement_drop(record: &Record) -> bool {
    match record.destructor.definition {
        // TODO(b/202258760): Only omit destructor if `Copy` is specified.
        SpecialMemberDefinition::Trivial => false,

        // TODO(b/212690698): Avoid calling into the C++ destructor (e.g. let
        // Rust drive `drop`-ing) to avoid (somewhat unergonomic) ManuallyDrop
        // if we can ask Rust to preserve C++ field destruction order in
        // NontrivialMembers case.
        SpecialMemberDefinition::NontrivialMembers => true,

        // The `impl Drop` for NontrivialUserDefined needs to call into the
        // user-defined destructor on C++ side.
        SpecialMemberDefinition::NontrivialUserDefined => true,

        // TODO(b/213516512): Today the IR doesn't contain Func entries for
        // deleted functions/destructors/etc. But, maybe we should generate
        // `impl Drop` in this case? With `unreachable!`? With
        // `std::mem::forget`?
        SpecialMemberDefinition::Deleted => false,
    }
}

/// Returns whether fields of type `ty` need to be wrapped in `ManuallyDrop<T>`
/// to prevent the fields from being destructed twice (once by the C++
/// destructor calkled from the `impl Drop` of the struct and once by `drop` on
/// the Rust side).
///
/// A type is safe to destroy twice if it implements `Copy`. Fields of such
/// don't need to be wrapped in `ManuallyDrop<T>` even if the struct
/// containing the fields provides an `impl Drop` that calles into a C++
/// destructor (in addition to dropping the fields on the Rust side).
///
/// Note that it is not enough to just be `!needs_drop<T>()`: Rust only
/// guarantees that it is safe to use-after-destroy for `Copy` types. See
/// e.g. the documentation for
/// [`drop_in_place`](https://doc.rust-lang.org/std/ptr/fn.drop_in_place.html):
///
/// > if `T` is not `Copy`, using the pointed-to value after calling
/// > `drop_in_place` can cause undefined behavior
fn needs_manually_drop(ty: &ir::RsType, ir: &IR) -> Result<bool> {
    let ty_implements_copy = RsTypeKind::new(ty, ir)?.implements_copy();
    Ok(!ty_implements_copy)
}

/// Generates Rust source code for a given `Record` and associated assertions as
/// a tuple.
fn generate_record(record: &Record, ir: &IR) -> Result<(RsSnippet, RsSnippet)> {
    let ident = make_ident(&record.identifier.identifier);
    let doc_comment = generate_doc_comment(&record.doc_comment);
    let field_idents =
        record.fields.iter().map(|f| make_ident(&f.identifier.identifier)).collect_vec();
    let field_doc_coments =
        record.fields.iter().map(|f| generate_doc_comment(&f.doc_comment)).collect_vec();
    let field_types = record
        .fields
        .iter()
        .map(|f| {
            let mut formatted = format_rs_type(&f.type_.rs_type, ir, &HashMap::new())
                .with_context(|| {
                    format!("Failed to format type for field {:?} on record {:?}", f, record)
                })?;
            // TODO(b/212696226): Verify cases where ManuallyDrop<T> is skipped
            // via static asserts in the generated code.
            if should_implement_drop(record) && needs_manually_drop(&f.type_.rs_type, ir)? {
                // TODO(b/212690698): Avoid (somewhat unergonomic) ManuallyDrop
                // if we can ask Rust to preserve field destruction order if the
                // destructor is the SpecialMemberDefinition::NontrivialMembers
                // case.
                formatted = quote! { std::mem::ManuallyDrop<#formatted> }
            };
            Ok(formatted)
        })
        .collect::<Result<Vec<_>>>()?;
    let field_accesses = record
        .fields
        .iter()
        .map(|f| {
            if f.access == AccessSpecifier::Public {
                quote! { pub }
            } else {
                quote! {}
            }
        })
        .collect_vec();
    let size = record.size;
    let alignment = record.alignment;
    let field_assertions =
        record.fields.iter().zip(field_idents.iter()).map(|(field, field_ident)| {
            let offset = field.offset;
            quote! {
                // The IR contains the offset in bits, while offset_of!()
                // returns the offset in bytes, so we need to convert.
                const _: () = assert!(offset_of!(#ident, #field_ident) * 8 == #offset);
            }
        });
    let mut record_features = BTreeSet::new();
    let mut assertion_features = BTreeSet::new();

    // TODO(mboehme): For the time being, we're using unstable features to
    // be able to use offset_of!() in static assertions. This is fine for a
    // prototype, but longer-term we want to either get those features
    // stabilized or find an alternative. For more details, see
    // b/200120034#comment15
    assertion_features.insert(make_ident("const_ptr_offset_from"));

    let derives = generate_derives(record);
    let derives = if derives.is_empty() {
        quote! {}
    } else {
        quote! {#[derive( #(#derives),* )]}
    };
    let unpin_impl;
    if record.is_unpin() {
        unpin_impl = quote! {};
    } else {
        // negative_impls are necessary for universal initialization due to Rust's
        // coherence rules: PhantomPinned isn't enough to prove to Rust that a
        // blanket impl that requires Unpin doesn't apply. See http://<internal link>=h.f6jp8ifzgt3n
        record_features.insert(make_ident("negative_impls"));
        unpin_impl = quote! {
            __NEWLINE__  __NEWLINE__
            impl !Unpin for #ident {}
        };
    }

    let empty_struct_placeholder_field = if record.fields.is_empty() {
        quote! {
          /// Prevent empty C++ struct being zero-size in Rust.
          placeholder: std::mem::MaybeUninit<u8>,
        }
    } else {
        quote! {}
    };

    let record_tokens = quote! {
        #doc_comment
        #derives
        #[repr(C)]
        pub struct #ident {
            #( #field_doc_coments #field_accesses #field_idents: #field_types, )*
            #empty_struct_placeholder_field
        }

        #unpin_impl
    };

    let assertion_tokens = quote! {
        const _: () = assert!(std::mem::size_of::<#ident>() == #size);
        const _: () = assert!(std::mem::align_of::<#ident>() == #alignment);
        #( #field_assertions )*
    };

    Ok((
        RsSnippet { features: record_features, tokens: record_tokens },
        RsSnippet { features: assertion_features, tokens: assertion_tokens },
    ))
}

fn should_derive_clone(record: &Record) -> bool {
    record.is_unpin()
        && record.copy_constructor.access == ir::AccessSpecifier::Public
        && record.copy_constructor.definition == SpecialMemberDefinition::Trivial
}

fn should_derive_copy(record: &Record) -> bool {
    // TODO(b/202258760): Make `Copy` inclusion configurable.
    should_derive_clone(record)
}

fn generate_derives(record: &Record) -> Vec<Ident> {
    let mut derives = vec![];
    if should_derive_clone(record) {
        derives.push(make_ident("Clone"));
    }
    if should_derive_copy(record) {
        derives.push(make_ident("Copy"));
    }
    derives
}

fn generate_type_alias(type_alias: &TypeAlias, ir: &IR) -> Result<TokenStream> {
    let ident = make_ident(&type_alias.identifier.identifier);
    let underlying_type = format_rs_type(&type_alias.underlying_type.rs_type, ir, &HashMap::new())
        .with_context(|| format!("Failed to format underlying type for {:?}", type_alias))?;
    Ok(quote! {pub type #ident = #underlying_type;})
}

/// Generates Rust source code for a given `UnsupportedItem`.
fn generate_unsupported(item: &UnsupportedItem) -> Result<TokenStream> {
    let location = if item.source_loc.filename.is_empty() {
        "<unknown location>".to_string()
    } else {
        // TODO(forster): The "google3" prefix should probably come from a command line
        // argument.
        // TODO(forster): Consider linking to the symbol instead of to the line number
        // to avoid wrong links while generated files have not caught up.
        format!("google3/{};l={}", &item.source_loc.filename, &item.source_loc.line)
    };
    let message = format!(
        "{}\nError while generating bindings for item '{}':\n{}",
        &location, &item.name, &item.message
    );
    Ok(quote! { __COMMENT__ #message })
}

/// Generates Rust source code for a given `Comment`.
fn generate_comment(comment: &Comment) -> Result<TokenStream> {
    let text = &comment.text;
    Ok(quote! { __COMMENT__ #text })
}

fn generate_rs_api(ir: &IR) -> Result<TokenStream> {
    let mut items = vec![];
    let mut thunks = vec![];
    let mut assertions = vec![];

    // We import nullable pointers as an Option<&T> and assume that at the ABI
    // level, None is represented as a zero pointer value whereas Some is
    // represented as as non-zero pointer value. This seems like a pretty safe
    // assumption to make, but to provide some safeguard, assert that
    // `Option<&i32>` and `&i32` have the same size.
    assertions.push(quote! {
        const _: () = assert!(std::mem::size_of::<Option<&i32>>() == std::mem::size_of::<&i32>());
    });

    // TODO(jeanpierreda): Delete has_record, either in favor of using RsSnippet, or not
    // having uses. See https://chat.google.com/room/AAAAnQmj8Qs/6QbkSvWcfhA
    let mut has_record = false;
    let mut features = BTreeSet::new();

    // For #![rustfmt::skip].
    features.insert(make_ident("custom_inner_attributes"));

    // Identify all functions having overloads that we can't import (yet).
    // TODO(b/213280424): Implement support for overloaded functions.
    let mut seen_funcs = HashSet::new();
    let mut overloaded_funcs = HashSet::new();
    for func in ir.functions() {
        if let Some((_, _, function_id)) = generate_func(func, ir)? {
            if !seen_funcs.insert(function_id.clone()) {
                overloaded_funcs.insert(function_id);
            }
        }
    }

    for item in ir.items() {
        match item {
            Item::Func(func) => {
                if let Some((snippet, thunk, function_id)) = generate_func(func, ir)? {
                    if overloaded_funcs.contains(&function_id) {
                        items.push(generate_unsupported(&UnsupportedItem {
                            name: cxx_function_name(func, ir)?,
                            message: "Cannot generate bindings for overloaded function".to_string(),
                            source_loc: func.source_loc.clone(),
                        })?);
                        continue;
                    }
                    features.extend(snippet.features);
                    features.extend(thunk.features);
                    items.push(snippet.tokens);
                    thunks.push(thunk.tokens);
                }
            }
            Item::Record(record) => {
                if !ir.is_current_target(&record.owning_target)
                    && !ir.is_stdlib_target(&record.owning_target)
                {
                    continue;
                }
                let (snippet, assertions_snippet) = generate_record(record, ir)?;
                features.extend(snippet.features);
                features.extend(assertions_snippet.features);
                items.push(snippet.tokens);
                assertions.push(assertions_snippet.tokens);
                has_record = true;
            }
            Item::TypeAlias(type_alias) => {
                if !ir.is_current_target(&type_alias.owning_target)
                    && !ir.is_stdlib_target(&type_alias.owning_target)
                {
                    continue;
                }
                items.push(generate_type_alias(type_alias, ir)?);
            }
            Item::UnsupportedItem(unsupported) => items.push(generate_unsupported(unsupported)?),
            Item::Comment(comment) => items.push(generate_comment(comment)?),
        }
    }

    let mod_detail = if thunks.is_empty() {
        quote! {}
    } else {
        quote! {
            mod detail {
                #[allow(unused_imports)]
                use super::*;
                extern "C" {
                    #( #thunks )*
                }
            }
        }
    };

    let imports = if has_record {
        quote! {
            use memoffset_unstable_const::offset_of;
        }
    } else {
        quote! {}
    };

    let features = if features.is_empty() {
        quote! {}
    } else {
        quote! {
            #![feature( #(#features),* )]
        }
    };

    Ok(quote! {
        #features __NEWLINE__
        #![allow(non_camel_case_types)] __NEWLINE__
        #![allow(non_snake_case)] __NEWLINE__ __NEWLINE__

        #imports __NEWLINE__ __NEWLINE__

        #( #items __NEWLINE__ __NEWLINE__ )*

        #mod_detail __NEWLINE__ __NEWLINE__

         #( #assertions __NEWLINE__ __NEWLINE__ )*
    })
}

fn make_ident(ident: &str) -> Ident {
    format_ident!("{}", ident)
}

fn rs_type_name_for_target_and_identifier(
    owning_target: &BlazeLabel,
    identifier: &ir::Identifier,
    ir: &IR,
) -> Result<TokenStream> {
    let ident = make_ident(identifier.identifier.as_str());

    if ir.is_current_target(owning_target) || ir.is_stdlib_target(owning_target) {
        Ok(quote! {#ident})
    } else {
        let owning_crate = make_ident(owning_target.target_name()?);
        Ok(quote! {#owning_crate::#ident})
    }
}

#[derive(Debug, Eq, PartialEq)]
enum Mutability {
    Const,
    Mut,
}

impl Mutability {
    fn is_mut(&self) -> bool {
        *self == Mutability::Mut
    }

    fn format_for_pointer(&self) -> TokenStream {
        match self {
            Mutability::Mut => quote! {mut},
            Mutability::Const => quote! {const},
        }
    }

    fn format_for_reference(&self) -> TokenStream {
        match self {
            Mutability::Mut => quote! {mut},
            Mutability::Const => quote! {},
        }
    }
}

// TODO(b/213947473): Instead of having a separate RsTypeKind here, consider
// changing ir::RsType into a similar `enum`, with fields that contain
// references (e.g. &'ir Record`) instead of DeclIds.
#[derive(Debug)]
enum RsTypeKind<'ir> {
    Pointer { pointee: Box<RsTypeKind<'ir>>, mutability: Mutability },
    Reference { referent: Box<RsTypeKind<'ir>>, mutability: Mutability, lifetime_id: LifetimeId },
    Record(&'ir Record),
    TypeAlias { type_alias: &'ir TypeAlias, underlying_type: Box<RsTypeKind<'ir>> },
    Unit,
    Other { name: &'ir str, type_args: Vec<RsTypeKind<'ir>> },
}

impl<'ir> RsTypeKind<'ir> {
    pub fn new(ty: &'ir ir::RsType, ir: &'ir IR) -> Result<Self> {
        // The lambdas deduplicate code needed by multiple `match` branches.
        let get_type_args = || -> Result<Vec<RsTypeKind<'ir>>> {
            ty.type_args.iter().map(|type_arg| RsTypeKind::<'ir>::new(type_arg, ir)).collect()
        };
        let get_pointee = || -> Result<Box<RsTypeKind<'ir>>> {
            if ty.type_args.len() != 1 {
                bail!("Missing pointee/referent type (need exactly 1 type argument): {:?}", ty);
            }
            Ok(Box::new(get_type_args()?.remove(0)))
        };
        let get_lifetime = || -> Result<LifetimeId> {
            if ty.lifetime_args.len() != 1 {
                bail!("Missing reference lifetime (need exactly 1 lifetime argument): {:?}", ty);
            }
            Ok(ty.lifetime_args[0])
        };

        let result = match ty.name.as_deref() {
            None => {
                ensure!(
                    ty.type_args.is_empty(),
                    "Type arguments on records nor type aliases are not yet supported: {:?}",
                    ty
                );
                match ir.item_for_type(ty)? {
                    Item::Record(record) => RsTypeKind::Record(record),
                    Item::TypeAlias(type_alias) => RsTypeKind::TypeAlias {
                        type_alias,
                        underlying_type: Box::new(RsTypeKind::new(
                            &type_alias.underlying_type.rs_type,
                            ir,
                        )?),
                    },
                    other_item => bail!("Item does not define a type: {:?}", other_item),
                }
            }
            Some(name) => match name {
                "()" => {
                    if !ty.type_args.is_empty() {
                        bail!("Unit type must not have type arguments: {:?}", ty);
                    }
                    RsTypeKind::Unit
                }
                "*mut" => {
                    RsTypeKind::Pointer { pointee: get_pointee()?, mutability: Mutability::Mut }
                }
                "*const" => {
                    RsTypeKind::Pointer { pointee: get_pointee()?, mutability: Mutability::Const }
                }
                "&mut" => RsTypeKind::Reference {
                    referent: get_pointee()?,
                    mutability: Mutability::Mut,
                    lifetime_id: get_lifetime()?,
                },
                "&" => RsTypeKind::Reference {
                    referent: get_pointee()?,
                    mutability: Mutability::Const,
                    lifetime_id: get_lifetime()?,
                },
                name => RsTypeKind::Other { name, type_args: get_type_args()? },
            },
        };
        Ok(result)
    }

    pub fn format(
        &self,
        ir: &IR,
        lifetime_to_name: &HashMap<LifetimeId, String>,
    ) -> Result<TokenStream> {
        let result = match self {
            RsTypeKind::Pointer { pointee, mutability } => {
                let mutability = mutability.format_for_pointer();
                let nested_type = pointee.format(ir, lifetime_to_name)?;
                quote! {* #mutability #nested_type}
            }
            RsTypeKind::Reference { referent, mutability, lifetime_id } => {
                let mutability = mutability.format_for_reference();
                let lifetime = Self::format_lifetime(lifetime_id, lifetime_to_name)?;
                let nested_type = referent.format(ir, lifetime_to_name)?;
                quote! {& #lifetime #mutability #nested_type}
            }
            RsTypeKind::Record(record) => rs_type_name_for_target_and_identifier(
                &record.owning_target,
                &record.identifier,
                ir,
            )?,
            RsTypeKind::TypeAlias { type_alias, .. } => rs_type_name_for_target_and_identifier(
                &type_alias.owning_target,
                &type_alias.identifier,
                ir,
            )?,
            RsTypeKind::Unit => quote! {()},
            RsTypeKind::Other { name, type_args } => {
                let ident = make_ident(name);
                let generic_params = format_generic_params(
                    type_args
                        .iter()
                        .map(|type_arg| type_arg.format(ir, lifetime_to_name))
                        .collect::<Result<Vec<_>>>()?,
                );
                quote! {#ident #generic_params}
            }
        };
        Ok(result)
    }

    /// Formats the Rust type of `__this` parameter of a constructor - injecting
    /// MaybeUninit to return something like `&'a mut MaybeUninit<SomeStruct>`.
    pub fn format_as_this_param_for_constructor_thunk(
        &self,
        ir: &IR,
        lifetime_to_name: &HashMap<LifetimeId, String>,
    ) -> Result<TokenStream> {
        let nested_type = match self {
            RsTypeKind::Pointer {
                pointee: pointee_or_referent,
                mutability: Mutability::Mut,
                ..
            }
            | RsTypeKind::Reference {
                referent: pointee_or_referent,
                mutability: Mutability::Mut,
                ..
            } => pointee_or_referent.format(ir, lifetime_to_name)?,
            _ => bail!("Unexpected type of `__this` parameter in a constructor: {:?}", self),
        };
        let lifetime = match self {
            RsTypeKind::Pointer { .. } => quote! {},
            RsTypeKind::Reference { lifetime_id, .. } => {
                Self::format_lifetime(lifetime_id, lifetime_to_name)?
            }
            _ => unreachable!(), // Because of the earlier `match`.
        };
        // `mut` can be hardcoded, because of the `match` patterns above.
        Ok(quote! { & #lifetime mut std::mem::MaybeUninit< #nested_type > })
    }

    /// Formats this RsTypeKind as either `&'a self` or `&'a mut self`.
    ///
    /// When this RsTypeKind represents a pointer (without lifetime
    /// annotations), then `Ok(None)` is returned.
    /// TODO(b/214244223): Stop generating bindings when such pointer is used.
    /// (For example in in C++ non-static member functions where (without
    /// lifetime annotations) `__this` will have an `RsType` representing a
    /// pointer (rather than a reference).)
    pub fn format_as_self_param_for_instance_method(
        &self,
        func: &Func,
        ir: &IR,
        lifetime_to_name: &HashMap<LifetimeId, String>,
    ) -> Result<Option<TokenStream>> {
        let record_from_func = func
            .member_func_metadata
            .as_ref()
            .ok_or_else(|| {
                anyhow!(
                    "Unexpectedly formatting `self` parameter in a non-member function: {:?}",
                    func
                )
            })?
            .find_record(ir)?;
        let nested_type = match self {
            RsTypeKind::Pointer { pointee: nested_type, .. }
            | RsTypeKind::Reference { referent: nested_type, .. } => nested_type,
            _ => bail!("Unexpected type of `self` parameter in an instance method: {:?}", self),
        };
        let record_from_self = match **nested_type {
            RsTypeKind::Record(record) => record,
            _ => bail!("`self` reference unexpectedly doesn't point to a Record: {:?}", self),
        };
        if record_from_func != record_from_self {
            bail!(
                "`self` refers to an unexpected record type. \
                Parameter type refers to: {:?}. Function refers to: {:?}.",
                record_from_self,
                record_from_func
            );
        }

        match self {
            RsTypeKind::Pointer { mutability, .. } => {
                if mutability.is_mut() && matches!(func.name, UnqualifiedIdentifier::Destructor) {
                    // Even in C++ it is UB to retain `this` pointer and
                    // dereference it after a destructor runs. Therefore it is
                    // safe to use `&self` or `&mut self` in Rust even if IR
                    // represents `__this` as a Rust pointer (e.g. when lifetime
                    // annotations are missing - lifetime annotations are
                    // required to represent it as a Rust reference).
                    Ok(Some(quote! { &mut self }))
                } else {
                    Ok(None)
                }
            }
            RsTypeKind::Reference { mutability, lifetime_id, .. } => {
                let mutability = mutability.format_for_reference();
                let lifetime = Self::format_lifetime(lifetime_id, lifetime_to_name)?;
                Ok(Some(quote! { & #lifetime #mutability self }))
            }
            _ => unreachable!(), // Because of the the 1st `match` in this function.
        }
    }

    fn format_lifetime(
        lifetime_id: &LifetimeId,
        lifetime_to_name: &HashMap<LifetimeId, String>,
    ) -> Result<TokenStream> {
        let lifetime_name = lifetime_to_name.get(lifetime_id).ok_or_else(|| {
            anyhow!("`lifetime_to_name` doesn't have an entry for {:?}", lifetime_id)
        })?;
        let lifetime =
            syn::Lifetime::new(&format!("'{}", lifetime_name), proc_macro2::Span::call_site());
        Ok(quote! { #lifetime })
    }

    pub fn implements_copy(&self) -> bool {
        // TODO(b/212696226): Verify results of `implements_copy` via static
        // assertions in the generated Rust code (because incorrect results
        // can silently lead to unsafe behavior).
        match self {
            RsTypeKind::Unit => true,
            RsTypeKind::Pointer { .. } => true,
            RsTypeKind::Reference { mutability: Mutability::Const, .. } => true,
            RsTypeKind::Reference { mutability: Mutability::Mut, .. } => false,
            RsTypeKind::Record(record) => should_derive_copy(record),
            RsTypeKind::TypeAlias { underlying_type, .. } => underlying_type.implements_copy(),
            RsTypeKind::Other { .. } => {
                // All "other" primitive types (e.g. i32) implement `Copy`.
                true
            }
        }
    }

    pub fn is_shared_ref_to(&self, expected_record: &Record) -> bool {
        match self {
            RsTypeKind::Reference { referent, mutability: Mutability::Const, .. } => {
                match **referent {
                    RsTypeKind::Record(actual_record) => actual_record.id == expected_record.id,
                    _ => false,
                }
            }
            _ => false,
        }
    }
}

fn format_rs_type(
    ty: &ir::RsType,
    ir: &IR,
    lifetime_to_name: &HashMap<LifetimeId, String>,
) -> Result<TokenStream> {
    RsTypeKind::new(ty, ir)
        .and_then(|kind| kind.format(ir, lifetime_to_name))
        .with_context(|| format!("Failed to format Rust type {:?}", ty))
}

fn cc_type_name_for_item(item: &ir::Item) -> Result<TokenStream> {
    let (disambiguator_fragment, identifier) = match item {
        Item::Record(record) => (quote! { class }, &record.identifier),
        Item::TypeAlias(type_alias) => (quote! {}, &type_alias.identifier),
        _ => bail!("Item does not define a type: {:?}", item),
    };

    let ident = make_ident(identifier.identifier.as_str());
    Ok(quote! { #disambiguator_fragment #ident })
}

fn format_cc_type(ty: &ir::CcType, ir: &IR) -> Result<TokenStream> {
    let const_fragment = if ty.is_const {
        quote! {const}
    } else {
        quote! {}
    };
    if let Some(ref name) = ty.name {
        match name.as_str() {
            "*" => {
                if ty.type_args.len() != 1 {
                    bail!("Invalid pointer type (need exactly 1 type argument): {:?}", ty);
                }
                assert_eq!(ty.type_args.len(), 1);
                let nested_type = format_cc_type(&ty.type_args[0], ir)?;
                Ok(quote! {#nested_type * #const_fragment})
            }
            "&" => {
                if ty.type_args.len() != 1 {
                    bail!("Invalid reference type (need exactly 1 type argument): {:?}", ty);
                }
                let nested_type = format_cc_type(&ty.type_args[0], ir)?;
                Ok(quote! {#nested_type &})
            }
            cc_type_name => {
                if !ty.type_args.is_empty() {
                    bail!("Type not yet supported: {:?}", ty);
                }
                let idents = cc_type_name.split_whitespace().map(make_ident);
                Ok(quote! {#( #idents )* #const_fragment})
            }
        }
    } else {
        let item = ir.item_for_type(ty)?;
        let type_name = cc_type_name_for_item(item)?;
        Ok(quote! {#const_fragment #type_name})
    }
}

fn cc_struct_layout_assertion(record: &Record, ir: &IR) -> TokenStream {
    if !ir.is_current_target(&record.owning_target) && !ir.is_stdlib_target(&record.owning_target) {
        return quote! {};
    }
    let record_ident = make_ident(&record.identifier.identifier);
    let size = Literal::usize_unsuffixed(record.size);
    let alignment = Literal::usize_unsuffixed(record.alignment);
    let field_assertions =
        record.fields.iter().filter(|f| f.access == AccessSpecifier::Public).map(|field| {
            let field_ident = make_ident(&field.identifier.identifier);
            let offset = Literal::usize_unsuffixed(field.offset);
            // The IR contains the offset in bits, while C++'s offsetof()
            // returns the offset in bytes, so we need to convert.
            quote! {
                static_assert(offsetof(class #record_ident, #field_ident) * 8 == #offset);
            }
        });
    quote! {
        static_assert(sizeof(class #record_ident) == #size);
        static_assert(alignof(class #record_ident) == #alignment);
        #( #field_assertions )*
    }
}

fn thunk_ident(func: &Func) -> Ident {
    format_ident!("__rust_thunk__{}", func.mangled_name)
}

fn generate_rs_api_impl(ir: &IR) -> Result<TokenStream> {
    // This function uses quote! to generate C++ source code out of convenience.
    // This is a bold idea so we have to continously evaluate if it still makes
    // sense or the cost of working around differences in Rust and C++ tokens is
    // greather than the value added.
    //
    // See rs_bindings_from_cc/
    // token_stream_printer.rs for a list of supported placeholders.
    let mut thunks = vec![];
    for func in ir.functions() {
        if can_skip_cc_thunk(&func) {
            continue;
        }

        let thunk_ident = thunk_ident(func);
        let implementation_function = match &func.name {
            UnqualifiedIdentifier::Identifier(id) => {
                let fn_ident = make_ident(&id.identifier);
                let static_method_metadata = func
                    .member_func_metadata
                    .as_ref()
                    .filter(|meta| meta.instance_method_metadata.is_none());
                match static_method_metadata {
                    None => quote! {#fn_ident},
                    Some(meta) => {
                        let record_ident = make_ident(&meta.find_record(ir)?.identifier.identifier);
                        quote! { #record_ident :: #fn_ident }
                    }
                }
            }
            // Use `destroy_at` to avoid needing to spell out the class name. Destructor identiifers
            // use the name of the type itself, without namespace qualification, template
            // parameters, or aliases. We do not need to use that naming scheme anywhere else in
            // the bindings, and it can be difficult (impossible?) to spell in the general case. By
            // using destroy_at, we avoid needing to determine or remember what the correct spelling
            // is. Similar arguments apply to `construct_at`.
            UnqualifiedIdentifier::Constructor => {
                quote! { rs_api_impl_support::construct_at }
            }
            UnqualifiedIdentifier::Destructor => quote! {std::destroy_at},
        };
        let return_type_name = format_cc_type(&func.return_type.cc_type, ir)?;
        let return_stmt = if func.return_type.cc_type.is_void() {
            quote! {}
        } else {
            quote! { return }
        };

        let param_idents =
            func.params.iter().map(|p| make_ident(&p.identifier.identifier)).collect_vec();

        let param_types = func
            .params
            .iter()
            .map(|p| format_cc_type(&p.type_.cc_type, ir))
            .collect::<Result<Vec<_>>>()?;

        let needs_this_deref = match &func.member_func_metadata {
            None => false,
            Some(meta) => match &func.name {
                UnqualifiedIdentifier::Constructor | UnqualifiedIdentifier::Destructor => false,
                UnqualifiedIdentifier::Identifier(_) => meta.instance_method_metadata.is_some(),
            },
        };
        let (implementation_function, arg_expressions) = if !needs_this_deref {
            (implementation_function, param_idents.clone())
        } else {
            let this_param = func
                .params
                .first()
                .ok_or_else(|| anyhow!("Instance methods must have `__this` param."))?;
            let this_arg = make_ident(&this_param.identifier.identifier);
            (
                quote! { #this_arg -> #implementation_function},
                param_idents.iter().skip(1).cloned().collect_vec(),
            )
        };

        thunks.push(quote! {
            extern "C" #return_type_name #thunk_ident( #( #param_types #param_idents ),* ) {
                #return_stmt #implementation_function( #( #arg_expressions ),* );
            }
        });
    }

    let layout_assertions = ir.records().map(|record| cc_struct_layout_assertion(record, ir));

    let mut standard_headers = <BTreeSet<Ident>>::new();
    standard_headers.insert(make_ident("memory")); // ubiquitous.
    if ir.records().next().is_some() {
        standard_headers.insert(make_ident("cstddef"));
    };

    let mut includes =
        vec!["rs_bindings_from_cc/support/cxx20_backports.h"];

    // In order to generate C++ thunk in all the cases Clang needs to be able to
    // access declarations from public headers of the C++ library.
    includes.extend(ir.used_headers().map(|i| &i.name as &str));

    Ok(quote! {
        #( __HASH_TOKEN__ include <#standard_headers> __NEWLINE__)*
        #( __HASH_TOKEN__ include #includes __NEWLINE__)* __NEWLINE__

        #( #thunks )* __NEWLINE__ __NEWLINE__

        #( #layout_assertions __NEWLINE__ __NEWLINE__ )*

        // To satisfy http://cs/symbol:devtools.metadata.Presubmit.CheckTerminatingNewline check.
        __NEWLINE__
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use anyhow::anyhow;
    use ir_testing::{ir_from_cc, ir_from_cc_dependency, ir_func, ir_record};
    use token_stream_matchers::{
        assert_cc_matches, assert_cc_not_matches, assert_rs_matches, assert_rs_not_matches,
    };
    use token_stream_printer::tokens_to_string;

    #[test]
    // TODO(hlopko): Move this test to a more principled place where it can access
    // `ir_testing`.
    fn test_duplicate_decl_ids_err() {
        let mut r1 = ir_record("R1");
        r1.id = DeclId(42);
        let mut r2 = ir_record("R2");
        r2.id = DeclId(42);
        let result = make_ir_from_items([r1.into(), r2.into()]);
        assert!(result.is_err());
        assert!(result.unwrap_err().to_string().contains("Duplicate decl_id found in"));
    }

    #[test]
    fn test_simple_function() -> Result<()> {
        let ir = ir_from_cc("int Add(int a, int b);")?;
        let rs_api = generate_rs_api(&ir)?;
        assert_rs_matches!(
            rs_api,
            quote! {
                #[inline(always)]
                pub fn Add(a: i32, b: i32) -> i32 {
                    unsafe { crate::detail::__rust_thunk___Z3Addii(a, b) }
                }
            }
        );
        assert_rs_matches!(
            rs_api,
            quote! {
                mod detail {
                    #[allow(unused_imports)]
                    use super::*;
                    extern "C" {
                        #[link_name = "_Z3Addii"]
                        pub(crate) fn __rust_thunk___Z3Addii(a: i32, b: i32) -> i32;
                    }
                }
            }
        );

        assert_cc_not_matches!(generate_rs_api_impl(&ir)?, quote! {__rust_thunk___Z3Addii});

        Ok(())
    }

    #[test]
    fn test_inline_function() -> Result<()> {
        let ir = ir_from_cc("inline int Add(int a, int b);")?;
        let rs_api = generate_rs_api(&ir)?;
        assert_rs_matches!(
            rs_api,
            quote! {
                #[inline(always)]
                pub fn Add(a: i32, b: i32) -> i32 {
                    unsafe { crate::detail::__rust_thunk___Z3Addii(a, b) }
                }
            }
        );
        assert_rs_matches!(
            rs_api,
            quote! {
                mod detail {
                    #[allow(unused_imports)]
                    use super::*;
                    extern "C" {
                        pub(crate) fn __rust_thunk___Z3Addii(a: i32, b: i32) -> i32;
                    }
                }
            }
        );

        assert_cc_matches!(
            generate_rs_api_impl(&ir)?,
            quote! {
                extern "C" int __rust_thunk___Z3Addii(int a, int b) {
                    return Add(a, b);
                }
            }
        );
        Ok(())
    }

    #[test]
    fn test_simple_function_with_types_from_other_target() -> Result<()> {
        let ir = ir_from_cc_dependency(
            "inline ReturnStruct DoSomething(ParamStruct param);",
            "struct ReturnStruct {}; struct ParamStruct {};",
        )?;

        let rs_api = generate_rs_api(&ir)?;
        assert_rs_matches!(
            rs_api,
            quote! {
                #[inline(always)]
                pub fn DoSomething(param: dependency::ParamStruct)
                    -> dependency::ReturnStruct {
                    unsafe { crate::detail::__rust_thunk___Z11DoSomething11ParamStruct(param) }
                }
            }
        );
        assert_rs_matches!(
            rs_api,
            quote! {
            mod detail {
                #[allow(unused_imports)]
                use super::*;
                extern "C" {
                    pub(crate) fn __rust_thunk___Z11DoSomething11ParamStruct(param: dependency::ParamStruct)
                        -> dependency::ReturnStruct;
                }
            }}
        );

        assert_cc_matches!(
            generate_rs_api_impl(&ir)?,
            quote! {
                extern "C" class ReturnStruct __rust_thunk___Z11DoSomething11ParamStruct(class ParamStruct param) {
                    return DoSomething(param);
                }
            }
        );
        Ok(())
    }

    #[test]
    fn test_simple_struct() -> Result<()> {
        let ir = ir_from_cc(&tokens_to_string(quote! {
            struct SomeStruct final {
                int public_int;
              protected:
                int protected_int;
              private:
               int private_int;
            };
        })?)?;

        let rs_api = generate_rs_api(&ir)?;
        assert_rs_matches!(
            rs_api,
            quote! {
                #[derive(Clone, Copy)]
                #[repr(C)]
                pub struct SomeStruct {
                    pub public_int: i32,
                    protected_int: i32,
                    private_int: i32,
                }
            }
        );
        assert_rs_matches!(
            rs_api,
            quote! {
                const _: () = assert!(std::mem::size_of::<Option<&i32>>() == std::mem::size_of::<&i32>());
                const _: () = assert!(std::mem::size_of::<SomeStruct>() == 12usize);
                const _: () = assert!(std::mem::align_of::<SomeStruct>() == 4usize);
                const _: () = assert!(offset_of!(SomeStruct, public_int) * 8 == 0usize);
                const _: () = assert!(offset_of!(SomeStruct, protected_int) * 8 == 32usize);
                const _: () = assert!(offset_of!(SomeStruct, private_int) * 8 == 64usize);
            }
        );
        let rs_api_impl = generate_rs_api_impl(&ir)?;
        assert_cc_matches!(
            rs_api_impl,
            quote! {
                extern "C" void __rust_thunk___ZN10SomeStructD1Ev(class SomeStruct * __this) {
                    std :: destroy_at (__this) ;
                }
            }
        );
        assert_cc_matches!(
            rs_api_impl,
            quote! {
                static_assert(sizeof(class SomeStruct) == 12);
                static_assert(alignof(class SomeStruct) == 4);
                static_assert(offsetof(class SomeStruct, public_int) * 8 == 0);
            }
        );
        Ok(())
    }

    #[test]
    fn test_ref_to_struct_in_thunk_impls() -> Result<()> {
        let ir = ir_from_cc("struct S{}; inline void foo(class S& s) {} ")?;
        let rs_api_impl = generate_rs_api_impl(&ir)?;
        assert_cc_matches!(
            rs_api_impl,
            quote! {
                extern "C" void __rust_thunk___Z3fooR1S(class S& s) {
                    foo(s);
                }
            }
        );
        Ok(())
    }

    #[test]
    fn test_const_ref_to_struct_in_thunk_impls() -> Result<()> {
        let ir = ir_from_cc("struct S{}; inline void foo(const class S& s) {} ")?;
        let rs_api_impl = generate_rs_api_impl(&ir)?;
        assert_cc_matches!(
            rs_api_impl,
            quote! {
                extern "C" void __rust_thunk___Z3fooRK1S(const class S& s) {
                    foo(s);
                }
            }
        );
        Ok(())
    }

    #[test]
    fn test_unsigned_int_in_thunk_impls() -> Result<()> {
        let ir = ir_from_cc("inline void foo(unsigned int i) {} ")?;
        let rs_api_impl = generate_rs_api_impl(&ir)?;
        assert_cc_matches!(
            rs_api_impl,
            quote! {
                extern "C" void __rust_thunk___Z3fooj(unsigned int i) {
                    foo(i);
                }
            }
        );
        Ok(())
    }

    #[test]
    fn test_record_static_methods_qualify_call_in_thunk() -> Result<()> {
        let ir = ir_from_cc(&tokens_to_string(quote! {
            struct SomeStruct {
                static inline int some_func() { return 42; }
            };
        })?)?;

        assert_cc_matches!(
            generate_rs_api_impl(&ir)?,
            quote! {
                extern "C" int __rust_thunk___ZN10SomeStruct9some_funcEv() {
                    return SomeStruct::some_func();
                }
            }
        );
        Ok(())
    }

    #[test]
    fn test_record_instance_methods_deref_this_in_thunk() -> Result<()> {
        let ir = ir_from_cc(&tokens_to_string(quote! {
            struct SomeStruct {
                inline int some_func(int arg) const { return 42 + arg; }
            };
        })?)?;

        assert_cc_matches!(
            generate_rs_api_impl(&ir)?,
            quote! {
                extern "C" int __rust_thunk___ZNK10SomeStruct9some_funcEi(
                        const class SomeStruct* __this, int arg) {
                    return __this->some_func(arg);
                }
            }
        );
        Ok(())
    }

    #[test]
    fn test_struct_from_other_target() -> Result<()> {
        let ir = ir_from_cc_dependency("// intentionally empty", "struct SomeStruct {};")?;
        assert_rs_not_matches!(generate_rs_api(&ir)?, quote! { SomeStruct });
        assert_cc_not_matches!(generate_rs_api_impl(&ir)?, quote! { SomeStruct });
        Ok(())
    }

    #[test]
    fn test_copy_derives() {
        let record = ir_record("S");
        assert_eq!(generate_derives(&record), &["Clone", "Copy"]);
    }

    #[test]
    fn test_copy_derives_not_is_trivial_abi() {
        let mut record = ir_record("S");
        record.is_trivial_abi = false;
        assert_eq!(generate_derives(&record), &[""; 0]);
    }

    /// Even if it's trivially relocatable, !Unpin C++ type cannot be
    /// cloned/copied or otherwise used by value, because values would allow
    /// assignment into the Pin.
    ///
    /// All !Unpin C++ types, not just non trivially relocatable ones, are
    /// unsafe to assign in the Rust sense.
    #[test]
    fn test_copy_derives_not_final() {
        let mut record = ir_record("S");
        record.is_final = false;
        assert_eq!(generate_derives(&record), &[""; 0]);
    }

    #[test]
    fn test_copy_derives_ctor_nonpublic() {
        let mut record = ir_record("S");
        for access in [ir::AccessSpecifier::Protected, ir::AccessSpecifier::Private] {
            record.copy_constructor.access = access;
            assert_eq!(generate_derives(&record), &[""; 0]);
        }
    }

    #[test]
    fn test_copy_derives_ctor_deleted() {
        let mut record = ir_record("S");
        record.copy_constructor.definition = ir::SpecialMemberDefinition::Deleted;
        assert_eq!(generate_derives(&record), &[""; 0]);
    }

    #[test]
    fn test_copy_derives_ctor_nontrivial_members() {
        let mut record = ir_record("S");
        record.copy_constructor.definition = ir::SpecialMemberDefinition::NontrivialMembers;
        assert_eq!(generate_derives(&record), &[""; 0]);
    }

    #[test]
    fn test_copy_derives_ctor_nontrivial_self() {
        let mut record = ir_record("S");
        record.copy_constructor.definition = ir::SpecialMemberDefinition::NontrivialUserDefined;
        assert_eq!(generate_derives(&record), &[""; 0]);
    }

    #[test]
    fn test_ptr_func() -> Result<()> {
        let ir = ir_from_cc(&tokens_to_string(quote! {
            inline int* Deref(int*const* p);
        })?)?;

        let rs_api = generate_rs_api(&ir)?;
        assert_rs_matches!(
            rs_api,
            quote! {
                #[inline(always)]
                pub fn Deref(p: *const *mut i32) -> *mut i32 {
                    unsafe { crate::detail::__rust_thunk___Z5DerefPKPi(p) }
                }
            }
        );
        assert_rs_matches!(
            rs_api,
            quote! {
                mod detail {
                    #[allow(unused_imports)]
                    use super::*;
                    extern "C" {
                        pub(crate) fn __rust_thunk___Z5DerefPKPi(p: *const *mut i32) -> *mut i32;
                    }
                }
            }
        );

        assert_cc_matches!(
            generate_rs_api_impl(&ir)?,
            quote! {
                extern "C" int* __rust_thunk___Z5DerefPKPi(int* const * p) {
                    return Deref(p);
                }
            }
        );
        Ok(())
    }

    #[test]
    fn test_const_char_ptr_func() -> Result<()> {
        // This is a regression test: We used to include the "const" in the name
        // of the CcType, which caused a panic in the code generator
        // ('"const char" is not a valid Ident').
        // It's therefore important that f() is inline so that we need to
        // generate a thunk for it (where we then process the CcType).
        let ir = ir_from_cc(&tokens_to_string(quote! {
            inline void f(const char *str);
        })?)?;

        let rs_api = generate_rs_api(&ir)?;
        assert_rs_matches!(
            rs_api,
            quote! {
                #[inline(always)]
                pub fn f(str: *const i8) {
                    unsafe { crate::detail::__rust_thunk___Z1fPKc(str) }
                }
            }
        );
        assert_rs_matches!(
            rs_api,
            quote! {
                extern "C" {
                    pub(crate) fn __rust_thunk___Z1fPKc(str: *const i8);
                }
            }
        );

        assert_cc_matches!(
            generate_rs_api_impl(&ir)?,
            quote! {
                extern "C" void __rust_thunk___Z1fPKc(char const * str){ f(str) ; }
            }
        );
        Ok(())
    }

    #[test]
    fn test_item_order() -> Result<()> {
        let ir = ir_from_cc(
            "int first_func();
             struct FirstStruct {};
             int second_func();
             struct SecondStruct {};",
        )?;

        let rs_api = rs_tokens_to_formatted_string(generate_rs_api(&ir)?)?;

        let idx = |s: &str| rs_api.find(s).ok_or(anyhow!("'{}' missing", s));

        let f1 = idx("fn first_func")?;
        let f2 = idx("fn second_func")?;
        let s1 = idx("struct FirstStruct")?;
        let s2 = idx("struct SecondStruct")?;
        let t1 = idx("fn __rust_thunk___Z10first_funcv")?;
        let t2 = idx("fn __rust_thunk___Z11second_funcv")?;

        assert!(f1 < s1);
        assert!(s1 < f2);
        assert!(f2 < s2);
        assert!(s2 < t1);
        assert!(t1 < t2);

        Ok(())
    }

    #[test]
    fn test_doc_comment_func() -> Result<()> {
        let ir = ir_from_cc(
            "
        // Doc Comment
        // with two lines
        int func();",
        )?;

        assert_rs_matches!(
            generate_rs_api(&ir)?,
            // leading space is intentional so there is a space between /// and the text of the
            // comment
            quote! {
                #[doc = " Doc Comment\n with two lines"]
                #[inline(always)]
                pub fn func
            }
        );

        Ok(())
    }

    #[test]
    fn test_doc_comment_record() -> Result<()> {
        let ir = ir_from_cc(
            "// Doc Comment\n\
            //\n\
            //  * with bullet\n\
            struct SomeStruct final {\n\
                // Field doc\n\
                int field;\
            };",
        )?;

        assert_rs_matches!(
            generate_rs_api(&ir)?,
            quote! {
                #[doc = " Doc Comment\n \n  * with bullet"]
                #[derive(Clone, Copy)]
                #[repr(C)]
                pub struct SomeStruct {
                    # [doc = " Field doc"]
                    pub field: i32,
                }
            }
        );
        Ok(())
    }

    #[test]
    fn test_virtual_thunk() -> Result<()> {
        let ir = ir_from_cc("struct Polymorphic { virtual void Foo(); };")?;

        assert_cc_matches!(
            generate_rs_api_impl(&ir)?,
            quote! {
                extern "C" void __rust_thunk___ZN11Polymorphic3FooEv(class Polymorphic * __this)
            }
        );
        Ok(())
    }

    /// A trivially relocatable final struct is safe to use in Rust as normal,
    /// and is Unpin.
    #[test]
    fn test_no_negative_impl_unpin() -> Result<()> {
        let ir = ir_from_cc("struct Trivial final {};")?;
        let rs_api = generate_rs_api(&ir)?;
        assert_rs_not_matches!(rs_api, quote! {impl !Unpin});
        Ok(())
    }

    /// A non-final struct, even if it's trivial, is not usable by mut
    /// reference, and so is !Unpin.
    #[test]
    fn test_negative_impl_unpin_nonfinal() -> Result<()> {
        let ir = ir_from_cc("struct Nonfinal {};")?;
        let rs_api = generate_rs_api(&ir)?;
        assert_rs_matches!(rs_api, quote! {impl !Unpin for Nonfinal {}});
        Ok(())
    }

    /// At the least, a trivial type should have no drop impl if or until we add
    /// empty drop impls.
    #[test]
    fn test_no_impl_drop() -> Result<()> {
        let ir = ir_from_cc("struct Trivial {};")?;
        let rs_api = rs_tokens_to_formatted_string(generate_rs_api(&ir)?)?;
        assert!(!rs_api.contains("impl Drop"));
        Ok(())
    }

    /// User-defined destructors *must* become Drop impls with ManuallyDrop
    /// fields
    #[test]
    fn test_impl_drop_user_defined_destructor() -> Result<()> {
        let ir = ir_from_cc(
            r#" struct NontrivialStruct { ~NontrivialStruct(); };
            struct UserDefinedDestructor {
                ~UserDefinedDestructor();
                int x;
                NontrivialStruct nts;
            };"#,
        )?;
        let rs_api = generate_rs_api(&ir)?;
        assert_rs_matches!(
            rs_api,
            quote! {
                impl Drop for UserDefinedDestructor {
                    #[inline(always)]
                    fn drop(&mut self) {
                        unsafe { crate::detail::__rust_thunk___ZN21UserDefinedDestructorD1Ev(self) }
                    }
                }
            }
        );
        assert_rs_matches!(rs_api, quote! {pub x: i32,});
        assert_rs_matches!(rs_api, quote! {pub nts: std::mem::ManuallyDrop<NontrivialStruct>,});
        Ok(())
    }

    /// nontrivial types without user-defined destructors should invoke
    /// the C++ destructor to preserve the order of field destructions.
    #[test]
    fn test_impl_drop_nontrivial_member_destructor() -> Result<()> {
        // TODO(jeanpierreda): This would be cleaner if the UserDefinedDestructor code were
        // omitted. For example, we simulate it so that UserDefinedDestructor
        // comes from another library.
        let ir = ir_from_cc(
            r#"struct UserDefinedDestructor final {
                ~UserDefinedDestructor();
            };
            struct TrivialStruct final { int i; };
            struct NontrivialMembers final {
                UserDefinedDestructor udd;
                TrivialStruct ts;
                int x;
            };"#,
        )?;
        let rs_api = generate_rs_api(&ir)?;
        assert_rs_matches!(
            rs_api,
            quote! {
                impl Drop for NontrivialMembers {
                    #[inline(always)]
                    fn drop(&mut self) {
                        unsafe { crate::detail::__rust_thunk___ZN17NontrivialMembersD1Ev(self) }
                    }
                }
            }
        );
        assert_rs_matches!(rs_api, quote! {pub x: i32,});
        assert_rs_matches!(rs_api, quote! {pub ts: TrivialStruct,});
        assert_rs_matches!(
            rs_api,
            quote! {pub udd: std::mem::ManuallyDrop<UserDefinedDestructor>,}
        );
        Ok(())
    }

    /// Trivial types (at least those that are mapped to Copy rust types) do not
    /// get a Drop impl.
    #[test]
    fn test_impl_drop_trivial() -> Result<()> {
        let ir = ir_from_cc(
            r#"struct Trivial final {
                ~Trivial() = default;
                int x;
            };"#,
        )?;
        let rs_api = generate_rs_api(&ir)?;
        assert_rs_not_matches!(rs_api, quote! {impl Drop});
        assert_rs_matches!(rs_api, quote! {pub x: i32});
        let rs_api_impl = generate_rs_api_impl(&ir)?;
        // TODO(b/213326125): Avoid generating thunk impls that are never called.
        // (The test assertion below should be reversed once this bug is fixed.)
        assert_cc_matches!(rs_api_impl, quote! { std::destroy_at });
        Ok(())
    }

    #[test]
    fn test_impl_default_explicitly_defaulted_constructor() -> Result<()> {
        let ir = ir_from_cc(
            r#"struct DefaultedConstructor final {
                DefaultedConstructor() = default;
            };"#,
        )?;
        let rs_api = generate_rs_api(&ir)?;
        assert_rs_matches!(
            rs_api,
            quote! {
                impl Default for DefaultedConstructor {
                    #[inline(always)]
                    fn default() -> Self {
                        let mut tmp = std::mem::MaybeUninit::<Self>::zeroed();
                        unsafe {
                            crate::detail::__rust_thunk___ZN20DefaultedConstructorC1Ev(&mut tmp);
                            tmp.assume_init()
                        }
                    }
                }
            }
        );
        let rs_api_impl = generate_rs_api_impl(&ir)?;
        assert_cc_matches!(
            rs_api_impl,
            quote! {
                extern "C" void __rust_thunk___ZN20DefaultedConstructorC1Ev(
                        class DefaultedConstructor* __this) {
                    rs_api_impl_support::construct_at (__this) ;
                }
            }
        );
        Ok(())
    }

    #[test]
    fn test_impl_default_non_trivial_struct() -> Result<()> {
        let ir = ir_from_cc(
            r#"struct NonTrivialStructWithConstructors final {
                NonTrivialStructWithConstructors();
                ~NonTrivialStructWithConstructors();  // Non-trivial
            };"#,
        )?;
        let rs_api = generate_rs_api(&ir)?;
        assert_rs_not_matches!(rs_api, quote! {impl Default});
        Ok(())
    }

    #[test]
    fn test_thunk_ident_function() {
        let func = ir_func("foo");
        assert_eq!(thunk_ident(&func), make_ident("__rust_thunk___Z3foov"));
    }

    #[test]
    fn test_thunk_ident_special_names() {
        let ir = ir_from_cc("struct Class {};").unwrap();

        let destructor =
            ir.functions().find(|f| f.name == UnqualifiedIdentifier::Destructor).unwrap();
        assert_eq!(thunk_ident(&destructor), make_ident("__rust_thunk___ZN5ClassD1Ev"));

        let constructor =
            ir.functions().find(|f| f.name == UnqualifiedIdentifier::Constructor).unwrap();
        assert_eq!(thunk_ident(&constructor), make_ident("__rust_thunk___ZN5ClassC1Ev"));
    }

    #[test]
    fn test_elided_lifetimes() -> Result<()> {
        let ir = ir_from_cc(
            r#"#pragma clang lifetime_elision
          struct S final {
            int& f(int& i);
          };"#,
        )?;
        let rs_api = generate_rs_api(&ir)?;
        assert_rs_matches!(
            rs_api,
            quote! {
                pub fn f<'a, 'b>(&'a mut self, i: &'b mut i32) -> &'a mut i32 { ... }
            }
        );
        assert_rs_matches!(
            rs_api,
            quote! {
                pub(crate) fn __rust_thunk___ZN1S1fERi<'a, 'b>(__this: &'a mut S, i: &'b mut i32)
                    -> &'a mut i32;
            }
        );
        Ok(())
    }

    #[test]
    fn test_format_generic_params() -> Result<()> {
        assert_rs_matches!(format_generic_params(std::iter::empty::<syn::Ident>()), quote! {});

        let idents = ["T1", "T2"].iter().map(|s| make_ident(s));
        assert_rs_matches!(format_generic_params(idents), quote! { < T1, T2 > });

        let lifetimes = ["a", "b"]
            .iter()
            .map(|s| syn::Lifetime::new(&format!("'{}", s), proc_macro2::Span::call_site()));
        assert_rs_matches!(format_generic_params(lifetimes), quote! { < 'a, 'b > });

        Ok(())
    }

    #[test]
    fn test_overloaded_functions() -> Result<()> {
        // TODO(b/213280424): We don't support creating bindings for overloaded
        // functions yet, except in the case of overloaded constructors with a
        // single parameter.
        let ir = ir_from_cc(
            r#"
                void f();
                void f(int i);
                struct S1 final {
                  void f();
                  void f(int i);
                };
                struct S2 final {
                  void f();
                };
                struct S3 final {
                  S3(int i);
                  S3(double d);
                };
            "#,
        )?;
        let rs_api = generate_rs_api(&ir)?;
        let rs_api_str = tokens_to_string(rs_api.clone())?;

        // Cannot overload free functions.
        assert!(rs_api_str.contains("Error while generating bindings for item 'f'"));
        assert_rs_not_matches!(rs_api, quote! {pub fn f()});
        assert_rs_not_matches!(rs_api, quote! {pub fn f(i: i32)});

        // Cannot overload member functions.
        assert!(rs_api_str.contains("Error while generating bindings for item 'S1::f'"));
        assert_rs_not_matches!(rs_api, quote! {pub fn f(... S1 ...)});

        // But we can import member functions that have the same name as a free
        // function.
        assert_rs_matches!(rs_api, quote! {pub fn f(__this: *mut S2)});

        // We can also import overloaded single-parameter constructors.
        assert_rs_matches!(rs_api, quote! {impl From<i32> for S3});
        assert_rs_matches!(rs_api, quote! {impl From<f64> for S3});
        Ok(())
    }

    #[test]
    fn test_type_alias() -> Result<()> {
        let ir = ir_from_cc(
            r#"
                typedef int MyTypedefDecl;
                using MyTypeAliasDecl = int;
                using MyTypeAliasDecl_Alias = MyTypeAliasDecl;

                struct S final {};
                using S_Alias = S;
                using S_Alias_Alias = S_Alias;

                inline void f(MyTypedefDecl t) {}
            "#,
        )?;
        let rs_api = generate_rs_api(&ir)?;
        assert_rs_matches!(rs_api, quote! { pub type MyTypedefDecl = i32; });
        assert_rs_matches!(rs_api, quote! { pub type MyTypeAliasDecl = i32; });
        assert_rs_matches!(rs_api, quote! { pub type MyTypeAliasDecl_Alias = MyTypeAliasDecl; });
        assert_rs_matches!(rs_api, quote! { pub type S_Alias = S; });
        assert_rs_matches!(rs_api, quote! { pub type S_Alias_Alias = S_Alias; });
        assert_rs_matches!(rs_api, quote! { pub fn f(t: MyTypedefDecl) });
        assert_cc_matches!(
            generate_rs_api_impl(&ir)?,
            quote! {
                extern "C" void __rust_thunk___Z1fi(MyTypedefDecl t){ f (t) ; }
            }
        );
        Ok(())
    }

    #[test]
    fn test_rs_type_kind_implements_copy() -> Result<()> {
        let template = r#" #pragma clang lifetime_elision
            struct [[clang::trivial_abi]] TrivialStruct final { int i; };
            struct [[clang::trivial_abi]] UserDefinedCopyConstructor final {
                UserDefinedCopyConstructor(const UserDefinedCopyConstructor&);
            };
            using IntAlias = int;
            using TrivialAlias = TrivialStruct;
            using NonTrivialAlias = UserDefinedCopyConstructor;
            void func(PARAM_TYPE some_param);
        "#;
        assert_impl_all!(i32: Copy);
        assert_impl_all!(&i32: Copy);
        assert_not_impl_all!(&mut i32: Copy);
        assert_impl_all!(*const i32: Copy);
        assert_impl_all!(*mut i32: Copy);
        let tests = vec![
            // Validity of the next few tests is verified via
            // `assert_[not_]impl_all!` static assertions above.
            ("int", true),
            ("const int&", true),
            ("int&", false),
            ("const int*", true),
            ("int*", true),
            // Tests below have been thought-through and verified "manually".
            ("TrivialStruct", true), // Trivial C++ structs are expected to derive Copy.
            ("UserDefinedCopyConstructor", false),
            ("IntAlias", true),
            ("TrivialAlias", true),
            ("NonTrivialAlias", false),
        ];
        for (type_str, is_copy_expected) in tests.iter() {
            let ir = ir_from_cc(&template.replace("PARAM_TYPE", type_str))?;
            let f = ir
                .functions()
                .find(|f| match &f.name {
                    UnqualifiedIdentifier::Identifier(id) => id.identifier == "func",
                    _ => false,
                })
                .expect("IR should contain a function named 'func'");
            let t = RsTypeKind::new(&f.params[0].type_.rs_type, &ir)?;
            assert_eq!(*is_copy_expected, t.implements_copy(), "Testing '{}'", type_str);
        }
        Ok(())
    }

    #[test]
    fn test_rs_type_kind_is_shared_ref_to_with_lifetimes() -> Result<()> {
        let ir = ir_from_cc(
            "#pragma clang lifetime_elision
             struct SomeStruct {};
             void foo(const SomeStruct& foo_param);
             void bar(SomeStruct& bar_param);",
        )?;
        let record = ir.records().next().unwrap();
        let foo_func = ir
            .functions()
            .find(|f| {
                matches!(&f.name, UnqualifiedIdentifier::Identifier(id)
                                  if id.identifier == "foo")
            })
            .unwrap();
        let bar_func = ir
            .functions()
            .find(|f| {
                matches!(&f.name, UnqualifiedIdentifier::Identifier(id)
                                  if id.identifier == "bar")
            })
            .unwrap();

        // const-ref + lifetimes in C++  ===>  shared-ref in Rust
        assert_eq!(foo_func.params.len(), 1);
        let foo_param = &foo_func.params[0];
        assert_eq!(&foo_param.identifier.identifier, "foo_param");
        let foo_type = RsTypeKind::new(&foo_param.type_.rs_type, &ir)?;
        assert!(foo_type.is_shared_ref_to(record));
        assert!(matches!(foo_type, RsTypeKind::Reference { mutability: Mutability::Const, .. }));

        // non-const-ref + lifetimes in C++  ===>  mutable-ref in Rust
        assert_eq!(bar_func.params.len(), 1);
        let bar_param = &bar_func.params[0];
        assert_eq!(&bar_param.identifier.identifier, "bar_param");
        let bar_type = RsTypeKind::new(&bar_param.type_.rs_type, &ir)?;
        assert!(!bar_type.is_shared_ref_to(record));
        assert!(matches!(bar_type, RsTypeKind::Reference { mutability: Mutability::Mut, .. }));

        Ok(())
    }

    #[test]
    fn test_rs_type_kind_is_shared_ref_to_without_lifetimes() -> Result<()> {
        let ir = ir_from_cc(
            "struct SomeStruct {};
             void foo(const SomeStruct& foo_param);",
        )?;
        let record = ir.records().next().unwrap();
        let foo_func = ir
            .functions()
            .find(|f| {
                matches!(&f.name, UnqualifiedIdentifier::Identifier(id)
                                  if id.identifier == "foo")
            })
            .unwrap();

        // const-ref + *no* lifetimes in C++  ===>  const-pointer in Rust
        assert_eq!(foo_func.params.len(), 1);
        let foo_param = &foo_func.params[0];
        assert_eq!(&foo_param.identifier.identifier, "foo_param");
        let foo_type = RsTypeKind::new(&foo_param.type_.rs_type, &ir)?;
        assert!(!foo_type.is_shared_ref_to(record));
        assert!(matches!(foo_type, RsTypeKind::Pointer { mutability: Mutability::Const, .. }));

        Ok(())
    }
}
