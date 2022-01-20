# Copyright 2021 The XLS Authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""
This module contains build rules for XLS.
"""

load("@bazel_skylib//lib:dicts.bzl", "dicts")
load("//xls/build_rules:xls_config_rules.bzl", "CONFIG")
load(
    "//xls/build_rules:xls_providers.bzl",
    "ConvIRInfo",
    "DslxModuleInfo",
    "OptIRInfo",
)
load(
    "//xls/build_rules:xls_dslx_rules.bzl",
    "get_dslx_test_cmd",
    "xls_dslx_test_common_attrs",
)
load(
    "//xls/build_rules:xls_ir_rules.bzl",
    "get_benchmark_ir_cmd",
    "get_eval_ir_test_cmd",
    "get_ir_equivalence_test_cmd",
    "xls_benchmark_ir_attrs",
    "xls_dslx_ir_attrs",
    "xls_dslx_ir_impl",
    "xls_entry_attrs",
    "xls_eval_ir_test_attrs",
    "xls_ir_equivalence_test_attrs",
    "xls_ir_opt_ir_attrs",
    "xls_ir_opt_ir_impl",
)
load(
    "//xls/build_rules:xls_codegen_rules.bzl",
    "xls_ir_verilog_attrs",
    "xls_ir_verilog_impl",
)
load("//xls/build_rules:xls_toolchains.bzl", "xls_toolchain_attr")

def _xls_dslx_opt_ir_impl(ctx, src, dep_src_list):
    """The implementation of the 'xls_dslx_opt_ir' rule.

    Converts a DSLX file to an IR and optimizes the IR.

    Args:
      ctx: The current rule's context object.
      src: The source file.
      dep_src_list: A list of source file dependencies.
    Returns:
      DslxModuleInfo provider.
      DslxInfo provider.
      ConvIRInfo provider.
      OptIRInfo provider.
      DefaultInfo provider.
    """
    dslx_module_info, ir_conv_info, ir_conv_default_info = (
        xls_dslx_ir_impl(ctx, src, dep_src_list)
    )
    ir_opt_info, ir_opt_default_info = xls_ir_opt_ir_impl(
        ctx,
        ir_conv_info.conv_ir_file,
    )
    return [
        dslx_module_info,
        ir_conv_info,
        ir_opt_info,
        DefaultInfo(
            files = depset(
                ir_conv_default_info.files.to_list() +
                ir_opt_default_info.files.to_list(),
            ),
            # TODO(vmirian) 06-18-2021 Add transitive files.
        ),
    ]

_xls_dslx_opt_ir_attrs = dicts.add(
    xls_dslx_ir_attrs,
    xls_ir_opt_ir_attrs,
    CONFIG["xls_outs_attrs"],
    xls_toolchain_attr,
)

def _xls_dslx_opt_ir_impl_wrapper(ctx):
    """The implementation of the 'xls_dslx_opt_ir' rule.

    Wrapper for _xls_dslx_opt_ir_impl. See: _xls_dslx_opt_ir_impl.

    Args:
      ctx: The current rule's context object.
    Returns:
      See: _xls_dslx_opt_ir_impl.
    """
    src = ctx.attr.dep[DslxModuleInfo].dslx_source_module_file
    dep_src_list = ctx.attr.dep[DslxModuleInfo].dslx_source_files
    return _xls_dslx_opt_ir_impl(ctx, src, dep_src_list)

xls_dslx_opt_ir = rule(
    doc = """A build rule that generates an optimized IR file from a DSLX source file.

        Example:

        Generate optimized IR from a xls_dslx_module_library target.

        ```
            xls_dslx_module_library(
                name = "a_dslx_module",
                src = "a.x",
            )

            xls_dslx_opt_ir(
                name = "a_opt_ir",
                dep = ":a_dslx_module",
            )
        ```
    """,
    implementation = _xls_dslx_opt_ir_impl_wrapper,
    attrs = _xls_dslx_opt_ir_attrs,
)

def _xls_dslx_opt_ir_test_impl(ctx):
    """The implementation of the 'xls_dslx_opt_ir_test' rule.

    Executes the commands in the order presented in the list for the following
    rules:
      1) xls_dslx_test
      2) xls_ir_equivalence_test
      3) xls_eval_ir_test
      4) xls_benchmark_ir

    Args:
      ctx: The current rule's context object.
    Returns:
      DefaultInfo provider
    """
    dslx_test_file = ctx.attr.dep[DslxModuleInfo].dslx_source_module_file
    dslx_source_files = ctx.attr.dep[DslxModuleInfo].dslx_source_files
    conv_ir_file = ctx.attr.dep[ConvIRInfo].conv_ir_file
    opt_ir_file = ctx.attr.dep[OptIRInfo].opt_ir_file
    opt_ir_args = ctx.attr.dep[OptIRInfo].opt_ir_args
    entry = opt_ir_args.get("entry", None)
    runfiles = list(dslx_source_files)

    # xls_dslx_test
    my_runfiles, dslx_test_cmd = get_dslx_test_cmd(ctx, dslx_test_file)
    runfiles += my_runfiles

    # xls_ir_equivalence_test
    my_runfiles, ir_equivalence_test_cmd = get_ir_equivalence_test_cmd(
        ctx,
        conv_ir_file,
        opt_ir_file,
        entry,
    )
    runfiles += my_runfiles

    # xls_eval_ir_test
    my_runfiles, eval_ir_test_cmd = get_eval_ir_test_cmd(
        ctx,
        conv_ir_file,
        entry,
    )
    runfiles += my_runfiles

    # xls_benchmark_ir
    my_runfiles, benchmark_ir_cmd = get_benchmark_ir_cmd(
        ctx,
        conv_ir_file,
        entry,
    )
    runfiles += my_runfiles

    executable_file = ctx.actions.declare_file(ctx.label.name + ".sh")
    ctx.actions.write(
        output = executable_file,
        content = "\n".join([
            "#!/bin/bash",
            "set -e",
            dslx_test_cmd,
            ir_equivalence_test_cmd,
            eval_ir_test_cmd,
            benchmark_ir_cmd,
            "exit 0",
        ]),
        is_executable = True,
    )
    return [
        DefaultInfo(
            runfiles = ctx.runfiles(files = runfiles),
            files = depset([executable_file]),
            executable = executable_file,
        ),
    ]

_xls_dslx_opt_ir_test_impl_attrs = {
    "dep": attr.label(
        doc = "The xls_dslx_opt_ir target to test.",
        providers = [
            DslxModuleInfo,
            ConvIRInfo,
            OptIRInfo,
        ],
    ),
}

xls_dslx_opt_ir_test = rule(
    doc = """A build rule that tests a xls_dslx_opt_ir target.

        Example:
            xls_dslx_module_library(
                name = "a_dslx_module",
                src = "a.x",
            )

            xls_dslx_opt_ir(
                name = "a_opt_ir",
                dep = ":a_dslx_module",
            )

            xls_dslx_opt_ir_test(
                name = "a_opt_ir_test",
                dep = ":a_opt_ir",
            )
    """,
    implementation = _xls_dslx_opt_ir_test_impl,
    attrs = dicts.add(
        _xls_dslx_opt_ir_test_impl_attrs,
        xls_dslx_test_common_attrs,
        xls_ir_equivalence_test_attrs,
        xls_eval_ir_test_attrs,
        xls_benchmark_ir_attrs,
        xls_entry_attrs,
        xls_toolchain_attr,
    ),
    test = True,
)

def _xls_dslx_verilog_impl(ctx):
    """The implementation of the 'xls_dslx_verilog' rule.

    Converts a DSLX file to an IR, optimizes the IR, and generates a verilog
    file from the optimized IR.

    Args:
      ctx: The current rule's context object.
    Returns:
      DslxModuleInfo provider.
      DslxInfo provider.
      ConvIRInfo provider.
      OptIRInfo provider.
      CodegenInfo provider.
      DefaultInfo provider.
    """
    src = ctx.attr.dep[DslxModuleInfo].dslx_source_module_file
    dep_src_list = ctx.attr.dep[DslxModuleInfo].dslx_source_files
    (
        dslx_module_info,
        ir_conv_info,
        ir_opt_info,
        dslx_ir_default_info,
    ) = _xls_dslx_opt_ir_impl(ctx, src, dep_src_list)
    codegen_info, codegen_default_info = xls_ir_verilog_impl(
        ctx,
        ir_opt_info.opt_ir_file,
    )
    return [
        dslx_module_info,
        ir_conv_info,
        ir_opt_info,
        codegen_info,
        DefaultInfo(
            files = depset(
                dslx_ir_default_info.files.to_list() +
                codegen_default_info.files.to_list(),
            ),
            # TODO(vmirian) 06-18-2021 Add transitive files.
        ),
    ]

_dslx_verilog_attrs = dicts.add(
    xls_dslx_ir_attrs,
    xls_ir_opt_ir_attrs,
    xls_ir_verilog_attrs,
    CONFIG["xls_outs_attrs"],
    xls_toolchain_attr,
)

xls_dslx_verilog = rule(
    doc = """A build rule that generates a Verilog file from a DSLX source file.

        Examples:

        ```
            xls_dslx_module_library(
                name = "a_dslx_module",
                src = "a.x",
            )

            xls_dslx_verilog(
                name = "a_verilog",
                codegen_args = {
                    "pipeline_stages": "1",
                },
                dep = ":a_dslx_module",
            )
        ```
    """,
    implementation = _xls_dslx_verilog_impl,
    attrs = _dslx_verilog_attrs,
)

# TODO(vmirian) 2021-05-20 When https://github.com/google/xls/issues/418 and
# https://github.com/google/xls/issues/419 are resolved:
# implement xls_dslx_verilog_test
