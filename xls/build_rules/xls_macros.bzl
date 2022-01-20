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
This module contains build macros for XLS.
"""

load(
    "//xls/build_rules:xls_codegen_rules.bzl",
    "append_xls_ir_verilog_generated_files",
    "get_xls_ir_verilog_generated_files",
    "validate_verilog_filename",
)
load(
    "//xls/build_rules:xls_config_rules.bzl",
    "enable_generated_file_wrapper",
)
load(
    "//xls/build_rules:xls_ir_rules.bzl",
    "append_xls_dslx_ir_generated_files",
    "append_xls_ir_opt_ir_generated_files",
    "get_xls_dslx_ir_generated_files",
    "get_xls_ir_opt_ir_generated_files",
)
load(
    "//xls/build_rules:xls_rules.bzl",
    "xls_dslx_opt_ir",
    "xls_dslx_verilog",
)

def xls_dslx_verilog_macro(
        name,
        dep,
        verilog_file,
        ir_conv_args = {},
        opt_ir_args = {},
        codegen_args = {},
        enable_generated_file = True,
        enable_presubmit_generated_file = False,
        **kwargs):
    """A macro wrapper for the 'xls_dslx_verilog' rule.

    The macro instantiates the 'xls_dslx_verilog' rule and
    'enable_generated_file_wrapper' function. The generated files of the rule
    are listed in the outs attribute of the rule.

    Args:
      name: The name of the rule.
      dep: The 'xls_dslx_module_library' target used for dependency.
      verilog_file: The filename of Verilog file generated. The filename must
        have a '.v' extension.
      ir_conv_args: Arguments of the IR conversion tool. For details on the
        arguments, refer to the ir_converter_main application at
        //xls/dslx/ir_converter_main.cc. When the default XLS
        toolchain differs from the default toolchain, the application target
        may be different.
      opt_ir_args: Arguments of the IR optimizer tool. For details on the
        arguments, refer to the opt_main application at
        //xls/tools/opt_main.cc. When the default XLS toolchain
        differs from the default toolchain, the application target may be
        different.
      codegen_args: Arguments of the codegen tool. For details on the arguments,
        refer to the codegen_main application at
        //xls/tools/codegen_main.cc. When the default XLS
        toolchain differs from the default toolchain, the application target may
        be different.
      enable_generated_file: See 'enable_generated_file' from
        'enable_generated_file_wrapper' function.
      enable_presubmit_generated_file: See 'enable_presubmit_generated_file'
        from 'enable_generated_file_wrapper' function.
      **kwargs: Keyword arguments. Named arguments.
    """

    # Type check input
    if type(name) != type(""):
        fail("Argument 'name' must be of string type.")
    if type(dep) != type(""):
        fail("Argument 'dep' must be of string type.")
    if type(verilog_file) != type(""):
        fail("Argument 'verilog_file' must be of string type.")
    if type(ir_conv_args) != type({}):
        fail("Argument 'ir_conv_args' must be of dictionary type.")
    if type(opt_ir_args) != type({}):
        fail("Argument 'opt_ir_args' must be of dictionary type.")
    if type(codegen_args) != type({}):
        fail("Argument 'codegen_args' must be of dictionary type.")
    if type(enable_generated_file) != type(True):
        fail("Argument 'enable_generated_file' must be of boolean type.")
    if type(enable_presubmit_generated_file) != type(True):
        fail("Argument 'enable_presubmit_generated_file' must be " +
             "of boolean type.")

    # Append output files to arguments.
    kwargs = append_xls_dslx_ir_generated_files(kwargs, name)
    kwargs = append_xls_ir_opt_ir_generated_files(kwargs, name)
    validate_verilog_filename(verilog_file)
    verilog_basename = verilog_file[:-2]
    kwargs = append_xls_ir_verilog_generated_files(
        kwargs,
        verilog_basename,
        codegen_args,
    )

    xls_dslx_verilog(
        name = name,
        dep = dep,
        verilog_file = verilog_file,
        ir_conv_args = ir_conv_args,
        opt_ir_args = opt_ir_args,
        codegen_args = codegen_args,
        outs = get_xls_dslx_ir_generated_files(kwargs) +
               get_xls_ir_opt_ir_generated_files(kwargs) +
               get_xls_ir_verilog_generated_files(kwargs, codegen_args) +
               [native.package_name() + "/" + verilog_file],
        **kwargs
    )
    enable_generated_file_wrapper(
        wrapped_target = name,
        enable_generated_file = enable_generated_file,
        enable_presubmit_generated_file = enable_presubmit_generated_file,
        **kwargs
    )

def xls_dslx_opt_ir_macro(
        name,
        dep,
        ir_conv_args = {},
        opt_ir_args = {},
        enable_generated_file = True,
        enable_presubmit_generated_file = False,
        **kwargs):
    """A macro wrapper for the 'xls_dslx_opt_ir' rule.

    The macro instantiates the 'xls_dslx_opt_ir' rule that converts a DSLX file
    to an IR, optimizes the IR, and generates a verilog file from the optimized
    IR. The macro also instantiates the 'enable_generated_file_wrapper'
    function. The generated files of the rule are listed in the outs attribute
    of the rule.

    Args:
      name: The name of the rule.
      dep: The 'xls_dslx_module_library' target used for dependency.
      ir_conv_args: Arguments of the IR conversion tool. For details on the
        arguments, refer to the ir_converter_main application at
        //xls/dslx/ir_converter_main.cc. When the default XLS
        toolchain differs from the default toolchain, the application target
        may be different.
      opt_ir_args: Arguments of the IR optimizer tool. For details on the
        arguments, refer to the opt_main application at
        //xls/tools/opt_main.cc. When the default XLS toolchain
        differs from the default toolchain, the application target may be
        different.
      enable_generated_file: See 'enable_generated_file' from
        'enable_generated_file_wrapper' function.
      enable_presubmit_generated_file: See 'enable_presubmit_generated_file'
        from 'enable_generated_file_wrapper' function.
      **kwargs: Keyword arguments. Named arguments.
    """

    # Type check input
    if type(name) != type(""):
        fail("Argument 'name' must be of string type.")
    if type(dep) != type(""):
        fail("Argument 'dep' must be of string type.")
    if type(ir_conv_args) != type({}):
        fail("Argument 'ir_conv_args' must be of dictionary type.")
    if type(opt_ir_args) != type({}):
        fail("Argument 'opt_ir_args' must be of dictionary type.")
    if type(enable_generated_file) != type(True):
        fail("Argument 'enable_generated_file' must be of boolean type.")
    if type(enable_presubmit_generated_file) != type(True):
        fail("Argument 'enable_presubmit_generated_file' must be " +
             "of boolean type.")

    # Append output files to arguments.
    kwargs = append_xls_dslx_ir_generated_files(kwargs, name)
    kwargs = append_xls_ir_opt_ir_generated_files(kwargs, name)

    xls_dslx_opt_ir(
        name = name,
        dep = dep,
        ir_conv_args = ir_conv_args,
        opt_ir_args = opt_ir_args,
        outs = get_xls_dslx_ir_generated_files(kwargs) +
               get_xls_ir_opt_ir_generated_files(kwargs),
        **kwargs
    )
    enable_generated_file_wrapper(
        wrapped_target = name,
        enable_generated_file = enable_generated_file,
        enable_presubmit_generated_file = enable_presubmit_generated_file,
        **kwargs
    )

def xls_dslx_cpp_type_library(
        name,
        src):
    """Creates a cc_library target for transpiled DSLX types.

    This macros invokes the DSLX-to-C++ transpiler and compiles the result as
    a cc_library.

    Args:
      name: The name of the eventual cc_library.
      src: The DSLX file whose types to compile as C++.
    """
    native.genrule(
        name = name + "_generate_sources",
        srcs = [src],
        outs = [
            name + ".h",
            name + ".cc",
        ],
        tools = [
            "//xls/dslx:cpp_transpiler_main",
        ],
        cmd = "$(location //xls/dslx:cpp_transpiler_main) " +
              "--output_header_path=$(@D)/{}.h ".format(name) +
              "--output_source_path=$(@D)/{}.cc ".format(name) +
              "$(location {})".format(src),
    )

    native.cc_library(
        name = name,
        srcs = [":" + name + ".cc"],
        hdrs = [":" + name + ".h"],
        deps = [
            "@com_google_absl//absl/base:core_headers",
            "@com_google_absl//absl/status:status",
            "@com_google_absl//absl/status:statusor",
            "@com_google_absl//absl/types:span",
            "//xls/public:value",
        ],
    )
