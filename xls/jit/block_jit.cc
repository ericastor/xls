// Copyright 2023 The XLS Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "xls/jit/block_jit.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/types/span.h"
#include "xls/codegen/block_inlining_pass.h"
#include "xls/codegen/codegen_pass.h"
#include "xls/codegen/module_signature.pb.h"
#include "xls/common/status/ret_check.h"
#include "xls/common/status/status_macros.h"
#include "xls/interpreter/block_evaluator.h"
#include "xls/ir/block.h"
#include "xls/ir/block_elaboration.h"
#include "xls/ir/clone_package.h"
#include "xls/ir/elaboration.h"
#include "xls/ir/events.h"
#include "xls/ir/instantiation.h"
#include "xls/ir/nodes.h"
#include "xls/ir/package.h"
#include "xls/ir/register.h"
#include "xls/ir/type.h"
#include "xls/ir/value.h"
#include "xls/ir/value_utils.h"
#include "xls/jit/function_base_jit.h"
#include "xls/jit/jit_buffer.h"
#include "xls/jit/jit_callbacks.h"
#include "xls/jit/jit_runtime.h"
#include "xls/jit/orc_jit.h"

namespace xls {

namespace {

class CheckNoInstantiationsOnTop : public verilog::CodegenPass {
 public:
  CheckNoInstantiationsOnTop()
      : verilog::CodegenPass(
            "check_no_instantiations",
            "Check no instantiations remain on the top block") {}

 protected:
  absl::StatusOr<bool> RunInternal(
      verilog::CodegenPassUnit* unit,
      const verilog::CodegenPassOptions& options,
      verilog::CodegenPassResults* results) const final {
    XLS_RET_CHECK(unit->top_block->GetInstantiations().empty())
        << "Jit is unable to implement instantiations.";
    return false;
  }
};

std::unique_ptr<verilog::CodegenCompoundPass> PrepareForJitPassPipeline() {
  auto passes = std::make_unique<verilog::CodegenCompoundPass>(
      "prepare_for_jit", "Process the IR to make it compatible with the jit");
  passes->Add<verilog::BlockInliningPass>();
  passes->Add<CheckNoInstantiationsOnTop>();
  return passes;
}

// Helper override for BlockJitContinuation that just adds renames for the
// registers.
//
// Note this relies on the reg_rename_map being generated by the
// PrepareForJitPassPipeline to ensure only one rename was performed.
class ElaboratedBlockJitContinuation : public BlockJitContinuation {
 public:
  ElaboratedBlockJitContinuation(
      Block* blk, BlockJit* jit, const JittedFunctionBase& jit_func,
      const absl::flat_hash_map<std::string, std::string>& reg_rename_map)
      : BlockJitContinuation(blk, jit, jit_func),
        reg_rename_map_(reg_rename_map) {}

  absl::flat_hash_map<std::string, Value> GetRegistersMap() const override {
    absl::flat_hash_map<std::string, Value> base =
        BlockJitContinuation::GetRegistersMap();
    for (const auto& [orig, rename] : reg_rename_map_) {
      if (base.contains(rename)) {
        base[orig] = base.extract(rename).mapped();
      }
    }
    return base;
  }
  absl::flat_hash_map<std::string, int64_t> GetRegisterIndices()
      const override {
    auto base = BlockJitContinuation::GetRegisterIndices();
    for (const auto& [orig, rename] : reg_rename_map_) {
      if (base.contains(rename)) {
        base[orig] = base.extract(rename).mapped();
      }
    }
    return base;
  }
  absl::Status SetRegisters(
      const absl::flat_hash_map<std::string, Value>& regs) override {
    absl::flat_hash_map<std::string, Value> translated_regs = regs;
    for (const auto& [orig, rename] : reg_rename_map_) {
      if (translated_regs.contains(orig)) {
        translated_regs[rename] = translated_regs.extract(orig).mapped();
      }
    }
    return BlockJitContinuation::SetRegisters(translated_regs);
  }

 private:
  // Map from path::style::name -> real_flat_style_name
  const absl::flat_hash_map<std::string, std::string>& reg_rename_map_;
};

// Specialized block-jit that intercepts and renames some registers to match
// elaboration behavior.
class ElaboratedBlockJit : public BlockJit {
 public:
  std::unique_ptr<BlockJitContinuation> NewContinuation() override {
    return std::make_unique<ElaboratedBlockJitContinuation>(
        block_, this, function_, reg_rename_map_);
  }

 private:
  ElaboratedBlockJit(
      std::unique_ptr<Package> jit_pkg, Block* block,
      absl::flat_hash_map<std::string, std::string> reg_rename_map,
      std::unique_ptr<JitRuntime> runtime, std::unique_ptr<OrcJit> jit,
      JittedFunctionBase function)
      : BlockJit(block, std::move(runtime), std::move(jit),
                 std::move(function)),
        jit_pkg_(std::move(jit_pkg)),
        reg_rename_map_(std::move(reg_rename_map)) {}

  std::unique_ptr<Package> jit_pkg_;
  absl::flat_hash_map<std::string, std::string> reg_rename_map_;

  friend class BlockJit;
};
}  // namespace

absl::StatusOr<std::unique_ptr<BlockJit>> BlockJit::Create(Block* block) {
  XLS_ASSIGN_OR_RETURN(BlockElaboration elab,
                       BlockElaboration::Elaborate(block));
  return BlockJit::Create(elab);
}

absl::StatusOr<std::unique_ptr<BlockJit>> BlockJit::Create(
    const BlockElaboration& elab) {
  Block* block;
  absl::flat_hash_map<std::string, std::string> reg_aliases;
  XLS_ASSIGN_OR_RETURN(std::unique_ptr<OrcJit> orc_jit, OrcJit::Create());
  XLS_ASSIGN_OR_RETURN(auto data_layout, orc_jit->CreateDataLayout());
  auto jit_runtime = std::make_unique<JitRuntime>(data_layout);
  if (elab.top()->block() &&
      (*elab.top()->block())->GetInstantiations().empty()) {
    block = elab.blocks().front();
    XLS_ASSIGN_OR_RETURN(auto function,
                         JittedFunctionBase::Build(block, *orc_jit));
    return std::unique_ptr<BlockJit>(new BlockJit(block, std::move(jit_runtime),
                                                  std::move(orc_jit),
                                                  std::move(function)));
  }
  XLS_RET_CHECK(elab.top()->block())
      << "Top block of elaboration must be an XLS 'block' in order to use JIT";
  XLS_ASSIGN_OR_RETURN(
      std::unique_ptr<Package> jit_package,
      ClonePackage(elab.package(),
                   absl::StrFormat("jit_clone_of_%s", elab.package()->name())));
  XLS_ASSIGN_OR_RETURN(
      Block * cloned_top,
      jit_package->GetBlock(elab.top()->block().value()->name()));
  verilog::CodegenPassUnit pass_unit(jit_package.get(), cloned_top);
  verilog::CodegenPassResults results;
  verilog::CodegenPassOptions opts;
  XLS_RETURN_IF_ERROR(
      PrepareForJitPassPipeline()->Run(&pass_unit, opts, &results).status());
  Block* jit_block = pass_unit.top_block;
  XLS_ASSIGN_OR_RETURN(JittedFunctionBase jit_entrypoint,
                       JittedFunctionBase::Build(jit_block, *orc_jit));
  return std::unique_ptr<BlockJit>(new ElaboratedBlockJit(
      std::move(jit_package), jit_block, std::move(results.register_renames),
      std::move(jit_runtime), std::move(orc_jit), std::move(jit_entrypoint)));
}

std::unique_ptr<BlockJitContinuation> BlockJit::NewContinuation() {
  return std::unique_ptr<BlockJitContinuation>(
      new BlockJitContinuation(block_, this, function_));
}

absl::Status BlockJit::RunOneCycle(BlockJitContinuation& continuation) {
  function_.RunJittedFunction(
      continuation.input_buffers_.current(),
      continuation.output_buffers_.current(), continuation.temp_buffer_,
      &continuation.GetEvents(), /*instance_context=*/&continuation.callbacks_,
      runtime_.get(),
      /*continuation_point=*/0);
  continuation.SwapRegisters();
  return absl::OkStatus();
}

absl::StatusOr<JitArgumentSet> BlockJitContinuation::CombineBuffers(
    const JittedFunctionBase& jit_func, const JitArgumentSet& left,
    int64_t left_count, const JitArgumentSet& rest, int64_t rest_start,
    bool is_inputs) {
  XLS_RET_CHECK_EQ(left.source(), &jit_func);
  XLS_RET_CHECK_EQ(rest.source(), &jit_func);
  const auto& final_sizes = is_inputs ? jit_func.input_buffer_sizes()
                                      : jit_func.output_buffer_sizes();
  const auto& final_aligns =
      is_inputs ? jit_func.input_buffer_preferred_alignments()
                : jit_func.output_buffer_preferred_alignments();
  const absl::Span<int64_t const> left_sizes =
      left.is_inputs() ? left.source()->input_buffer_sizes()
                       : left.source()->output_buffer_sizes();
  const absl::Span<int64_t const> left_aligns =
      left.is_inputs() ? left.source()->input_buffer_preferred_alignments()
                       : left.source()->output_buffer_preferred_alignments();
  const absl::Span<int64_t const> rest_sizes =
      rest.is_inputs() ? rest.source()->input_buffer_sizes()
                       : rest.source()->output_buffer_sizes();
  const absl::Span<int64_t const> rest_aligns =
      rest.is_inputs() ? rest.source()->input_buffer_preferred_alignments()
                       : rest.source()->output_buffer_preferred_alignments();
  std::vector<uint8_t*> final_ptrs;
  XLS_RET_CHECK_LE(left_count, final_sizes.size());
  XLS_RET_CHECK_LE(left_count, left.pointers().size());
  final_ptrs.reserve(final_sizes.size());
  for (int64_t i = 0; i < left_count; ++i) {
    XLS_RET_CHECK_EQ(final_sizes[i], left_sizes[i]) << i;
    XLS_RET_CHECK_EQ(final_aligns[i], left_aligns[i]) << i;
    final_ptrs.push_back(left.pointers()[i]);
  }
  for (int64_t i = left_count; i < final_sizes.size(); ++i) {
    XLS_RET_CHECK_EQ(final_sizes[i], rest_sizes[rest_start])
        << i << " rest: " << rest_start;
    XLS_RET_CHECK_EQ(final_aligns[i], rest_aligns[rest_start])
        << i << " rest: " << rest_start;
    final_ptrs.push_back(rest.pointers()[rest_start++]);
  }
  return JitArgumentSet(&jit_func, /*data=*/nullptr, std::move(final_ptrs),
                        /*is_inputs=*/is_inputs, /*is_outputs=*/!is_inputs);
}

BlockJitContinuation::IOSpace BlockJitContinuation::MakeCombinedBuffers(
    const JittedFunctionBase& jit_func, const Block* block,
    const JitArgumentSet& ports, const BlockJitContinuation::BufferPair& regs,
    bool input) {
  int64_t num_ports =
      input ? block->GetInputPorts().size() : block->GetOutputPorts().size();
  // Registers use the input port offsets.
  int64_t num_input_ports = block->GetInputPorts().size();
  return IOSpace(CombineBuffers(jit_func, ports, num_ports, regs[0],
                                num_input_ports, input)
                     .value(),
                 CombineBuffers(jit_func, ports, num_ports, regs[1],
                                num_input_ports, input)
                     .value());
}

BlockJitContinuation::BlockJitContinuation(Block* block, BlockJit* jit,
                                           const JittedFunctionBase& jit_func)
    : block_(block),
      block_jit_(jit),
      register_buffers_memory_{jit_func.CreateInputBuffer(),
                               jit_func.CreateInputBuffer()},
      input_port_buffers_memory_(jit_func.CreateInputBuffer()),
      output_port_buffers_memory_(jit_func.CreateOutputBuffer()),
      input_buffers_(MakeCombinedBuffers(jit_func, block_,
                                         input_port_buffers_memory_,
                                         register_buffers_memory_,
                                         /*input=*/true)),
      output_buffers_(MakeCombinedBuffers(jit_func, block_,
                                          output_port_buffers_memory_,
                                          register_buffers_memory_,
                                          /*input=*/false)),
      temp_buffer_(jit_func.CreateTempBuffer()),
      callbacks_(InstanceContext::CreateForBlock()) {
  // since input and output share the same register pointers they need to use
  // different sides at all times.
  input_buffers_.SetActive(IOSpace::RegisterSpace::kLeft);
  output_buffers_.SetActive(IOSpace::RegisterSpace::kRight);
}

absl::Status BlockJitContinuation::SetInputPorts(
    absl::Span<const Value> values) {
  XLS_RET_CHECK_EQ(block_->GetInputPorts().size(), values.size());
  std::vector<Type*> types;
  types.reserve(values.size());
  auto it = values.cbegin();
  for (auto ip : block_->GetInputPorts()) {
    types.push_back(ip->GetType());
    XLS_RET_CHECK(ValueConformsToType(*it, ip->GetType()))
        << "input port " << ip->name() << " cannot be set to value of " << *it
        << " due to type mismatch with input port type of "
        << ip->GetType()->ToString();
    ++it;
  }
  return block_jit_->runtime()->PackArgs(values, types, input_port_pointers());
}

absl::Status BlockJitContinuation::SetInputPorts(
    absl::Span<const uint8_t* const> inputs) {
  XLS_RET_CHECK_EQ(block_->GetInputPorts().size(), inputs.size());
  // TODO(allight): This is a lot of copying. We could do this more efficiently
  for (int i = 0; i < inputs.size(); ++i) {
    memcpy(input_port_pointers()[i], inputs[i],
           block_jit_->input_port_sizes()[i]);
  }
  return absl::OkStatus();
}

absl::Status BlockJitContinuation::SetInputPorts(
    const absl::flat_hash_map<std::string, Value>& inputs) {
  std::vector<Value> values(block_->GetInputPorts().size());
  auto input_indices = GetInputPortIndices();
  for (const auto& [name, value] : inputs) {
    if (!input_indices.contains(name)) {
      return absl::InvalidArgumentError(
          absl::StrFormat("Block has no input port '%s'", name));
    }
    values[input_indices.at(name)] = value;
  }
  if (block_->GetInputPorts().size() != inputs.size()) {
    std::ostringstream oss;
    for (auto p : block_->GetInputPorts()) {
      if (!inputs.contains(p->name())) {
        oss << "\n\tMissing input for port '" << p->name() << "'";
      }
    }
    return absl::InvalidArgumentError(
        absl::StrFormat("Expected %d input port values but only got %d:%s",
                        values.size(), inputs.size(), oss.str()));
  }
  return SetInputPorts(values);
}

absl::Status BlockJitContinuation::SetRegisters(
    absl::Span<const Value> values) {
  XLS_RET_CHECK_EQ(block_->GetRegisters().size(), values.size());
  std::vector<Type*> types;
  types.reserve(values.size());
  auto it = values.cbegin();
  for (auto reg : block_->GetRegisters()) {
    types.push_back(reg->type());
    XLS_RET_CHECK(ValueConformsToType(*it, reg->type()))
        << "register " << reg->name() << " cannot be set to value of " << *it
        << " due to type mismatch with register type of "
        << reg->type()->ToString();
    ++it;
  }
  return block_jit_->runtime()->PackArgs(values, types, register_pointers());
}

absl::Status BlockJitContinuation::SetRegisters(
    absl::Span<const uint8_t* const> regs) {
  XLS_RET_CHECK_EQ(block_->GetRegisters().size(), regs.size());
  // TODO(allight): This is a lot of copying. We could do this more efficiently
  for (int i = 0; i < regs.size(); ++i) {
    memcpy(register_pointers()[i], regs[i], block_jit_->register_sizes()[i]);
  }
  return absl::OkStatus();
}

absl::Status BlockJitContinuation::SetRegisters(
    const absl::flat_hash_map<std::string, Value>& regs) {
  auto reg_indices = BlockJitContinuation::GetRegisterIndices();
  std::vector<Value> values(reg_indices.size());
  for (const auto& [name, value] : regs) {
    if (!reg_indices.contains(name)) {
      return absl::InvalidArgumentError(
          absl::StrFormat("Block has no register '%s'", name));
    }
    values[reg_indices.at(name)] = value;
  }

  if (block_->GetRegisters().size() != regs.size()) {
    std::ostringstream oss;
    for (auto p : block_->GetRegisters()) {
      if (!regs.contains(p->name())) {
        oss << "\n\tMissing value for port '" << p->name() << "'";
      }
    }
    return absl::InvalidArgumentError(
        absl::StrFormat("Expected %d register values but only got %d:%s",
                        reg_indices.size(), regs.size(), oss.str()));
  }
  return SetRegisters(values);
}

std::vector<Value> BlockJitContinuation::GetOutputPorts() const {
  std::vector<Value> result;
  result.reserve(output_port_pointers().size());
  int i = 0;
  for (auto ptr : output_port_pointers()) {
    result.push_back(block_jit_->runtime()->UnpackBuffer(
        ptr, block_->GetOutputPorts()[i++]->operand(0)->GetType()));
  }
  return result;
}

absl::flat_hash_map<std::string, int64_t>
BlockJitContinuation::GetInputPortIndices() const {
  absl::flat_hash_map<std::string, int64_t> ret;
  int i = 0;
  for (auto v : block_->GetInputPorts()) {
    ret[v->name()] = i++;
  }
  return ret;
}

absl::flat_hash_map<std::string, int64_t>
BlockJitContinuation::GetOutputPortIndices() const {
  absl::flat_hash_map<std::string, int64_t> ret;
  int i = 0;
  for (auto v : block_->GetOutputPorts()) {
    ret[v->name()] = i++;
  }
  return ret;
}

absl::flat_hash_map<std::string, int64_t>
BlockJitContinuation::GetRegisterIndices() const {
  absl::flat_hash_map<std::string, int64_t> ret;
  int i = 0;
  for (auto v : block_->GetRegisters()) {
    ret[v->name()] = i++;
  }
  return ret;
}

absl::flat_hash_map<std::string, Value>
BlockJitContinuation::GetOutputPortsMap() const {
  absl::flat_hash_map<std::string, Value> result;
  result.reserve(output_port_pointers().size());
  auto regs = GetOutputPorts();
  for (const auto& [name, off] : GetOutputPortIndices()) {
    result[name] = regs[off];
  }
  return result;
}

std::vector<Value> BlockJitContinuation::GetRegisters() const {
  std::vector<Value> result;
  result.reserve(register_pointers().size());
  int i = 0;
  for (auto ptr : register_pointers()) {
    result.push_back(block_jit_->runtime()->UnpackBuffer(
        ptr, block_->GetRegisters()[i++]->type()));
  }
  return result;
}

absl::flat_hash_map<std::string, Value> BlockJitContinuation::GetRegistersMap()
    const {
  absl::flat_hash_map<std::string, Value> result;
  result.reserve(register_pointers().size());
  auto regs = GetRegisters();
  for (const auto& [name, off] : BlockJitContinuation::GetRegisterIndices()) {
    result[name] = regs[off];
  }
  return result;
}

namespace {
absl::StatusOr<absl::flat_hash_map<std::string, Value>> GetZeroRegisterValues(
    const BlockElaboration& elab) {
  absl::flat_hash_map<std::string, Value> regs;
  for (BlockInstance* inst : elab.instances()) {
    if (!inst->block()) {
      continue;
    }
    for (Register* reg : inst->block().value()->GetRegisters()) {
      regs[absl::StrCat(inst->RegisterPrefix(), reg->name())] =
          ZeroOfType(reg->type());
    }
  }
  return regs;
}
}  // namespace

namespace {
// Helper adapter to implement the interpreter-focused block-continuation api
// used by eval_proc_main. This holds live all the values needed to run the
// block-jit.
class BlockContinuationJitWrapper final : public BlockContinuation {
 public:
  BlockContinuationJitWrapper(std::unique_ptr<BlockJitContinuation>&& cont,
                              std::unique_ptr<BlockJit>&& jit)
      : continuation_(std::move(cont)), jit_(std::move(jit)) {}
  const absl::flat_hash_map<std::string, Value>& output_ports() final {
    if (!temporary_outputs_) {
      temporary_outputs_.emplace(continuation_->GetOutputPortsMap());
    }
    return *temporary_outputs_;
  }
  const absl::flat_hash_map<std::string, Value>& registers() final {
    if (!temporary_regs_) {
      temporary_regs_.emplace(continuation_->GetRegistersMap());
    }
    return *temporary_regs_;
  }
  const InterpreterEvents& events() final { return continuation_->GetEvents(); }
  absl::Status RunOneCycle(
      const absl::flat_hash_map<std::string, Value>& inputs) final {
    temporary_outputs_.reset();
    temporary_regs_.reset();
    continuation_->ClearEvents();
    XLS_RETURN_IF_ERROR(continuation_->SetInputPorts(inputs));
    return jit_->RunOneCycle(*continuation_);
  }
  absl::Status SetRegisters(
      const absl::flat_hash_map<std::string, Value>& regs) final {
    return continuation_->SetRegisters(regs);
  }

 private:
  std::unique_ptr<BlockJitContinuation> continuation_;
  std::unique_ptr<BlockJit> jit_;
  // Holder for the data we return out of output_ports so that we can reduce
  // copying.
  std::optional<absl::flat_hash_map<std::string, Value>> temporary_outputs_;
  // Holder for the data we return out of registers so that we can reduce
  // copying.
  std::optional<absl::flat_hash_map<std::string, Value>> temporary_regs_;
};
}  // namespace

absl::StatusOr<std::unique_ptr<BlockContinuation>>
StreamingJitBlockEvaluator::MakeNewContinuation(
    BlockElaboration&& elaboration,
    const absl::flat_hash_map<std::string, Value>& initial_registers) const {
  XLS_ASSIGN_OR_RETURN(auto jit, BlockJit::Create(elaboration));
  auto jit_cont = jit->NewContinuation();
  XLS_RETURN_IF_ERROR(jit_cont->SetRegisters(initial_registers));
  return std::make_unique<BlockContinuationJitWrapper>(std::move(jit_cont),
                                                       std::move(jit));
}

}  // namespace xls
