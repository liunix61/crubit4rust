# Part of the Crubit project, under the Apache License v2.0 with LLVM
# Exceptions. See /LICENSE for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Utility module for sharing logic between rules and aspects that generate Rust bindings from C++.
"""

# buildifier: disable=bzl-visibility
load("@rules_rust//rust/private:providers.bzl", "DepVariantInfo")

# buildifier: disable=bzl-visibility
load(
    "@rules_rust//rust/private:rustc.bzl",
    "ExtraRustcFlagsInfo",
    "rustc_compile_action",
)

def _get_crate_info(providers):
    for provider in providers:
        if hasattr(provider, "name"):
            return provider
    fail("Couldn't find a CrateInfo in the list of providers")

def _get_dep_info(providers):
    for provider in providers:
        if hasattr(provider, "direct_crates"):
            return provider
    fail("Couldn't find a DepInfo in the list of providers")

def _get_cc_info(providers):
    for provider in providers:
        if hasattr(provider, "linking_context"):
            return provider
    fail("Couldn't find a CcInfo in the list of providers")

def escape_cpp_target_name(package_name, crate_name):
    # Crubit generates assertions with `::core`, which would resolve to current crate, if current
    # crate (i.e., cc_library) is named 'core'.
    if crate_name == "core":
        _, _, last_path_component = package_name.rpartition("/")
        crate_name = "core_" + last_path_component

    # b/216587072: Sync the escaping logic with bazel_rules/rules_rust/rust/private:utils.bzl's
    # encode_label_as_crate_name. Currently, the encoding contains a (escaped and hence longer) copy
    # of both the package name _and_ the target name, which causes "File name too long" error.
    return "".join([char if char.isalnum() else "_" for char in crate_name.elems()])

def compile_rust(ctx, attr, src, extra_srcs, deps):
    """Compiles a Rust source file.

    Args:
      ctx: The rule context.
      attr: The current rule's attributes.
      src: The source file to be compiled.
      extra_srcs: Additional source files to include in the crate.
      deps: List[DepVariantInfo]: A list of dependencies needed.

    Returns:
      A DepVariantInfo provider.
    """
    toolchain = ctx.toolchains["@rules_rust//rust:toolchain_type"]

    output_hash = repr(hash(src.path))

    # TODO(b/216587072): Remove this hacky escaping and use the import! macro once available
    crate_name = escape_cpp_target_name(ctx.label.package, ctx.label.name)

    lib_name = "{prefix}{name}-{lib_hash}{extension}".format(
        prefix = "lib",
        name = crate_name,
        lib_hash = output_hash,
        extension = ".rlib",
    )

    rmeta_name = "{prefix}{name}-{lib_hash}{extension}".format(
        prefix = "lib",
        name = crate_name,
        lib_hash = output_hash,
        extension = ".rmeta",
    )

    lib = ctx.actions.declare_file(lib_name)
    rmeta = ctx.actions.declare_file(rmeta_name)

    providers = rustc_compile_action(
        ctx = ctx,
        attr = attr,
        toolchain = toolchain,
        crate_info_dict = dict(
            name = crate_name,
            type = "rlib",
            root = src,
            srcs = depset([src] + extra_srcs),
            deps = depset(deps),
            proc_macro_deps = depset([]),
            aliases = {},
            output = lib,
            metadata = rmeta,
            edition = "2018",
            is_test = False,
            rustc_env = {},
            compile_data = depset([]),
            compile_data_targets = depset([]),
            owner = ctx.label,
        ),
        rust_flags = ctx.attr._extra_rustc_flags[ExtraRustcFlagsInfo].extra_rustc_flags,
        output_hash = output_hash,
        force_all_deps_direct = True,
    )

    return DepVariantInfo(
        crate_info = _get_crate_info(providers),
        dep_info = _get_dep_info(providers),
        cc_info = _get_cc_info(providers),
        build_info = None,
    )
