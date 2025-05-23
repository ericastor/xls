# Copyright 2020 The XLS Authors
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

package(
    default_applicable_licenses = ["//:license"],
    default_visibility = ["//xls:xls_internal"],
    features = [
        "layering_check",
        "parse_headers",
    ],
    licenses = ["notice"],  # Apache 2.0
)

cc_library(
    name = "stage_conversion",
    srcs = ["stage_conversion.cc"],
    hdrs = ["stage_conversion.h"],
    deps = [
        "//xls/codegen:codegen_options",
        "//xls/common/status:ret_check",
        "//xls/common/status:status_macros",
        "//xls/ir",
        "//xls/ir:channel",
        "//xls/ir:function_builder",
        "//xls/ir:name_uniquer",
        "//xls/ir:op",
        "//xls/ir:source_location",
        "//xls/ir:type",
        "//xls/ir:value",
        "//xls/scheduling:pipeline_schedule",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/types:span",
    ],
)

cc_test(
    name = "stage_conversion_test",
    srcs = ["stage_conversion_test.cc"],
    deps = [
        ":stage_conversion",
        "//xls/codegen:codegen_options",
        "//xls/common:xls_gunit_main",
        "//xls/common/logging:log_lines",
        "//xls/common/status:matchers",
        "//xls/interpreter:channel_queue",
        "//xls/interpreter:evaluator_options",
        "//xls/interpreter:interpreter_proc_runtime",
        "//xls/interpreter:proc_runtime",
        "//xls/ir",
        "//xls/ir:bits",
        "//xls/ir:channel",
        "//xls/ir:function_builder",
        "//xls/ir:ir_test_base",
        "//xls/ir:proc_elaboration",
        "//xls/ir:value",
        "//xls/ir:verifier",
        "//xls/scheduling:pipeline_schedule",
        "//xls/scheduling:run_pipeline_schedule",
        "//xls/scheduling:scheduling_options",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings:str_format",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "stage_conversion_pass",
    srcs = ["stage_conversion_pass.cc"],
    hdrs = ["stage_conversion_pass.h"],
    deps = [
        ":stage_conversion",
        "//xls/codegen:codegen_pass",
        "//xls/common/logging:log_lines",
        "//xls/common/status:status_macros",
        "//xls/ir",
        "//xls/ir:xls_ir_interface_cc_proto",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/status:statusor",
    ],
)

cc_library(
    name = "stage_conversion_pass_pipeline",
    srcs = ["stage_conversion_pass_pipeline.cc"],
    hdrs = ["stage_conversion_pass_pipeline.h"],
    deps = [
        ":stage_conversion_pass",
        "//xls/codegen:codegen_options",
        "//xls/codegen:codegen_pass",
        "//xls/passes:optimization_pass",
    ],
)

cc_test(
    name = "stage_conversion_pass_pipeline_test",
    srcs = ["stage_conversion_pass_pipeline_test.cc"],
    deps = [
        ":stage_conversion",
        ":stage_conversion_pass_pipeline",
        "//xls/codegen:codegen_options",
        "//xls/codegen:codegen_pass",
        "//xls/common:xls_gunit_main",
        "//xls/common/logging:log_lines",
        "//xls/common/status:matchers",
        "//xls/interpreter:channel_queue",
        "//xls/interpreter:evaluator_options",
        "//xls/interpreter:interpreter_proc_runtime",
        "//xls/interpreter:proc_runtime",
        "//xls/ir",
        "//xls/ir:bits",
        "//xls/ir:channel",
        "//xls/ir:function_builder",
        "//xls/ir:ir_test_base",
        "//xls/ir:proc_elaboration",
        "//xls/ir:value",
        "//xls/ir:verifier",
        "//xls/passes:optimization_pass",
        "//xls/scheduling:pipeline_schedule",
        "//xls/scheduling:run_pipeline_schedule",
        "//xls/scheduling:scheduling_options",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings:str_format",
        "@googletest//:gtest",
    ],
)
