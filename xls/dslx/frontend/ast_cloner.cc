// Copyright 2022 The XLS Authors
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
#include "xls/dslx/frontend/ast_cloner.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/types/span.h"
#include "absl/types/variant.h"
#include "xls/common/casts.h"
#include "xls/common/status/status_macros.h"
#include "xls/common/visitor.h"
#include "xls/dslx/frontend/ast.h"
#include "xls/dslx/frontend/ast_utils.h"
#include "xls/dslx/frontend/module.h"
#include "xls/dslx/frontend/pos.h"
#include "xls/dslx/frontend/proc.h"
#include "xls/ir/format_strings.h"

namespace xls::dslx {
namespace {

class AstCloner : public AstNodeVisitor {
 public:
  explicit AstCloner(Module* module, CloneReplacer replacer)
      : module_(module), replacer_(std::move(replacer)) {}

  absl::Status HandleArray(const Array* n) override {
    XLS_RETURN_IF_ERROR(VisitChildren(n));

    std::vector<Expr*> new_members;
    new_members.reserve(n->members().size());
    for (const Expr* old_member : n->members()) {
      new_members.push_back(down_cast<Expr*>(old_to_new_.at(old_member)));
    }

    Array* array =
        module_->Make<Array>(n->span(), new_members, n->has_ellipsis());
    if (n->type_annotation() != nullptr) {
      array->set_type_annotation(
          down_cast<TypeAnnotation*>(old_to_new_.at(n->type_annotation())));
    }
    old_to_new_[n] = array;
    return absl::OkStatus();
  }

  absl::Status HandleArrayTypeAnnotation(
      const ArrayTypeAnnotation* n) override {
    XLS_RETURN_IF_ERROR(VisitChildren(n));

    old_to_new_[n] = module_->Make<ArrayTypeAnnotation>(
        n->span(),
        down_cast<TypeAnnotation*>(old_to_new_.at(n->element_type())),
        down_cast<Expr*>(old_to_new_.at(n->dim())));
    return absl::OkStatus();
  }

  absl::Status HandleAttr(const Attr* n) override {
    XLS_RETURN_IF_ERROR(VisitChildren(n));

    // We must've seen the NameDef here, so we don't need to visit it.
    old_to_new_[n] = module_->Make<Attr>(
        n->span(), down_cast<Expr*>(old_to_new_.at(n->lhs())),
        std::string{n->attr()});
    return absl::OkStatus();
  }

  absl::Status HandleBinop(const Binop* n) override {
    XLS_RETURN_IF_ERROR(VisitChildren(n));
    auto* new_binop = module_->Make<Binop>(
        n->span(), n->binop_kind(), down_cast<Expr*>(old_to_new_.at(n->lhs())),
        down_cast<Expr*>(old_to_new_.at(n->rhs())));
    // TODO(cdleary): 2023-05-30 The in_parens arguments should be given to the
    // Expr base class so we don't need to remember to clone it explicitly here
    // (i.e. it's given at construction naturally).
    new_binop->set_in_parens(n->in_parens());
    old_to_new_[n] = new_binop;
    return absl::OkStatus();
  }

  absl::Status HandleStatementBlock(const StatementBlock* n) override {
    XLS_RETURN_IF_ERROR(VisitChildren(n));
    std::vector<Statement*> new_statements;
    new_statements.reserve(n->statements().size());
    for (Statement* old : n->statements()) {
      if (!old_to_new_.contains(old)) {
        return absl::InternalError(absl::StrCat(
            "Statement not found in old_to_new_: ", old->ToString()));
      }
      auto new_stmt = old_to_new_.at(old);
      if (auto* verbatim = dynamic_cast<VerbatimNode*>(new_stmt)) {
        if (verbatim->IsEmpty()) {
          // Intentionally skip empty verbatim nodes.
          continue;
        }
        new_statements.push_back(module_->Make<Statement>(verbatim));
      } else {
        new_statements.push_back(down_cast<Statement*>(new_stmt));
      }
    }
    old_to_new_[n] = module_->Make<StatementBlock>(
        n->span(), std::move(new_statements), n->trailing_semi());
    return absl::OkStatus();
  }

  absl::Status HandleConstAssert(const ConstAssert* n) override {
    XLS_RETURN_IF_ERROR(VisitChildren(n));
    old_to_new_[n] = module_->Make<ConstAssert>(
        n->span(), down_cast<Expr*>(old_to_new_.at(n->arg())));
    return absl::OkStatus();
  }

  absl::Status HandleStatement(const Statement* n) override {
    XLS_RETURN_IF_ERROR(VisitChildren(n));
    XLS_ASSIGN_OR_RETURN(
        auto new_wrapped,
        Statement::NodeToWrapped(old_to_new_.at(ToAstNode(n->wrapped()))));
    old_to_new_[n] = module_->Make<Statement>(new_wrapped);
    return absl::OkStatus();
  }

  absl::Status HandleBuiltinNameDef(const BuiltinNameDef* n) override {
    if (!old_to_new_.contains(n)) {
      old_to_new_[n] = module_->GetOrCreateBuiltinNameDef(n->identifier());
    }
    return absl::OkStatus();
  }

  absl::Status HandleBuiltinTypeAnnotation(
      const BuiltinTypeAnnotation* n) override {
    XLS_RETURN_IF_ERROR(ReplaceOrVisit(n->builtin_name_def()));
    old_to_new_[n] = module_->Make<BuiltinTypeAnnotation>(
        n->span(), n->builtin_type(),
        down_cast<BuiltinNameDef*>(old_to_new_.at(n->builtin_name_def())));
    return absl::OkStatus();
  }

  absl::Status HandleCast(const Cast* n) override {
    XLS_RETURN_IF_ERROR(VisitChildren(n));
    old_to_new_[n] = module_->Make<Cast>(
        n->span(), down_cast<Expr*>(old_to_new_.at(n->expr())),
        down_cast<TypeAnnotation*>(old_to_new_.at(n->type_annotation())));
    return absl::OkStatus();
  }

  absl::Status HandleChannelDecl(const ChannelDecl* n) override {
    XLS_RETURN_IF_ERROR(VisitChildren(n));

    std::optional<std::vector<Expr*>> new_dims;
    if (n->dims().has_value()) {
      std::vector<Expr*> old_dims_vector = n->dims().value();
      std::vector<Expr*> new_dims_vector;
      new_dims_vector.reserve(old_dims_vector.size());
      for (const Expr* expr : old_dims_vector) {
        new_dims_vector.push_back(down_cast<Expr*>(old_to_new_.at(expr)));
      }
      new_dims = std::move(new_dims_vector);
    }

    std::optional<Expr*> new_fifo_depth;
    if (n->fifo_depth().has_value()) {
      new_fifo_depth =
          down_cast<Expr*>(old_to_new_.at(n->fifo_depth().value()));
    }

    old_to_new_[n] = module_->Make<ChannelDecl>(
        n->span(), down_cast<TypeAnnotation*>(old_to_new_.at(n->type())),
        new_dims, new_fifo_depth,
        *down_cast<Expr*>(old_to_new_.at(&n->channel_name_expr())));
    return absl::OkStatus();
  }

  absl::Status HandleChannelTypeAnnotation(
      const ChannelTypeAnnotation* n) override {
    XLS_RETURN_IF_ERROR(VisitChildren(n));

    std::optional<std::vector<Expr*>> new_dims;
    if (n->dims().has_value()) {
      std::vector<Expr*> old_dims_vector = n->dims().value();
      std::vector<Expr*> new_dims_vector;
      new_dims_vector.reserve(old_dims_vector.size());
      for (const Expr* expr : old_dims_vector) {
        new_dims_vector.push_back(down_cast<Expr*>(old_to_new_.at(expr)));
      }
      new_dims = new_dims_vector;
    }

    old_to_new_[n] = module_->Make<ChannelTypeAnnotation>(
        n->span(), n->direction(),
        down_cast<TypeAnnotation*>(old_to_new_.at(n->payload())), new_dims);
    return absl::OkStatus();
  }

  absl::Status HandleColonRef(const ColonRef* n) override {
    XLS_RETURN_IF_ERROR(VisitChildren(n));

    ColonRef::Subject new_subject;
    if (std::holds_alternative<NameRef*>(n->subject())) {
      new_subject =
          down_cast<NameRef*>(old_to_new_.at(std::get<NameRef*>(n->subject())));
    } else {
      new_subject = down_cast<ColonRef*>(
          old_to_new_.at(std::get<ColonRef*>(n->subject())));
    }

    old_to_new_[n] = module_->Make<ColonRef>(n->span(), new_subject, n->attr());
    return absl::OkStatus();
  }

  absl::Status HandleConstantArray(const ConstantArray* n) override {
    XLS_RETURN_IF_ERROR(VisitChildren(n));

    std::vector<Expr*> new_members;
    for (const Expr* old_member : n->members()) {
      new_members.push_back(down_cast<Expr*>(old_to_new_.at(old_member)));
    }
    ConstantArray* array =
        module_->Make<ConstantArray>(n->span(), new_members, n->has_ellipsis());
    if (n->type_annotation() != nullptr) {
      array->set_type_annotation(
          down_cast<TypeAnnotation*>(old_to_new_.at(n->type_annotation())));
    }
    old_to_new_[n] = array;
    return absl::OkStatus();
  }

  absl::Status HandleConstantDef(const ConstantDef* n) override {
    XLS_RETURN_IF_ERROR(VisitChildren(n));
    TypeAnnotation* new_type_annotation = nullptr;
    if (n->type_annotation() != nullptr) {
      new_type_annotation =
          down_cast<TypeAnnotation*>(old_to_new_.at(n->type_annotation()));
    }
    old_to_new_[n] = module_->Make<ConstantDef>(
        n->span(), down_cast<NameDef*>(old_to_new_.at(n->name_def())),
        new_type_annotation, down_cast<Expr*>(old_to_new_.at(n->value())),
        n->is_public());
    return absl::OkStatus();
  }

  absl::Status HandleConstRef(const ConstRef* n) override {
    old_to_new_[n] =
        module_->Make<ConstRef>(n->span(), n->identifier(), n->name_def());
    return absl::OkStatus();
  }

  absl::Status HandleEnumDef(const EnumDef* n) override {
    XLS_RETURN_IF_ERROR(VisitChildren(n));

    NameDef* new_name_def = down_cast<NameDef*>(old_to_new_.at(n->name_def()));
    TypeAnnotation* new_type_annotation = nullptr;
    if (n->type_annotation() != nullptr) {
      new_type_annotation =
          down_cast<TypeAnnotation*>(old_to_new_.at(n->type_annotation()));
    }

    std::vector<EnumMember> new_values;
    for (const auto& member : n->values()) {
      new_values.push_back(EnumMember{
          .name_def = down_cast<NameDef*>(old_to_new_.at(member.name_def)),
          .value = down_cast<Expr*>(old_to_new_.at(member.value))});
    }

    EnumDef* new_enum_def =
        module_->Make<EnumDef>(n->span(), new_name_def, new_type_annotation,
                               new_values, n->is_public());
    new_name_def->set_definer(new_enum_def);
    old_to_new_[n] = new_enum_def;

    return absl::OkStatus();
  }

  absl::Status HandleFor(const For* n) override {
    XLS_RETURN_IF_ERROR(VisitChildren(n));

    old_to_new_[n] = module_->Make<For>(
        n->span(), down_cast<NameDefTree*>(old_to_new_.at(n->names())),
        down_cast<TypeAnnotation*>(old_to_new_.at(n->type_annotation())),
        down_cast<Expr*>(old_to_new_.at(n->iterable())),
        down_cast<StatementBlock*>(old_to_new_.at(n->body())),
        down_cast<Expr*>(old_to_new_.at(n->init())));
    return absl::OkStatus();
  }

  absl::Status HandleFormatMacro(const FormatMacro* n) override {
    XLS_RETURN_IF_ERROR(VisitChildren(n));

    std::vector<Expr*> new_args;
    new_args.reserve(n->args().size());
    for (const Expr* arg : n->args()) {
      new_args.push_back(down_cast<Expr*>(old_to_new_.at(arg)));
    }

    old_to_new_[n] = module_->Make<FormatMacro>(
        n->span(), n->macro(),
        std::vector<FormatStep>(n->format().begin(), n->format().end()),
        new_args);
    return absl::OkStatus();
  }

  absl::Status HandleFunctionRef(const FunctionRef* n) override {
    XLS_RETURN_IF_ERROR(VisitChildren(n));
    old_to_new_[n] = module_->Make<FunctionRef>(
        n->span(), down_cast<Expr*>(old_to_new_.at(n->callee())),
        CloneParametrics(n->explicit_parametrics()));
    return absl::OkStatus();
  }

  absl::Status HandleZeroMacro(const ZeroMacro* n) override {
    XLS_RETURN_IF_ERROR(VisitChildren(n));

    ExprOrType new_eot = ToExprOrType(old_to_new_.at(ToAstNode(n->type())));
    old_to_new_[n] = module_->Make<ZeroMacro>(n->span(), new_eot);
    return absl::OkStatus();
  }

  absl::Status HandleAllOnesMacro(const AllOnesMacro* n) override {
    XLS_RETURN_IF_ERROR(VisitChildren(n));

    ExprOrType new_eot = ToExprOrType(old_to_new_.at(ToAstNode(n->type())));
    old_to_new_[n] = module_->Make<AllOnesMacro>(n->span(), new_eot);
    return absl::OkStatus();
  }

  absl::Status HandleFunction(const Function* n) override {
    XLS_RETURN_IF_ERROR(VisitChildren(n));
    NameDef* new_name_def = down_cast<NameDef*>(old_to_new_.at(n->name_def()));

    std::vector<ParametricBinding*> new_parametric_bindings;
    new_parametric_bindings.reserve(n->parametric_bindings().size());
    for (const ParametricBinding* pb : n->parametric_bindings()) {
      XLS_RETURN_IF_ERROR(ReplaceOrVisit(pb));
      new_parametric_bindings.push_back(
          down_cast<ParametricBinding*>(old_to_new_.at(pb)));
    }

    std::vector<Param*> new_params;
    new_params.reserve(n->params().size());
    for (const auto* param : n->params()) {
      new_params.push_back(down_cast<Param*>(old_to_new_.at(param)));
    }

    TypeAnnotation* new_return_type = nullptr;
    if (n->return_type() != nullptr) {
      new_return_type =
          down_cast<TypeAnnotation*>(old_to_new_.at(n->return_type()));
    }
    old_to_new_[n] = module_->Make<Function>(
        n->span(), new_name_def, new_parametric_bindings, new_params,
        new_return_type, down_cast<StatementBlock*>(old_to_new_.at(n->body())),
        n->tag(), n->is_public());
    new_name_def->set_definer(old_to_new_.at(n));
    return absl::OkStatus();
  }

  absl::Status HandleImport(const Import* n) override {
    XLS_RETURN_IF_ERROR(VisitChildren(n));

    old_to_new_[n] = module_->Make<Import>(
        n->span(), n->subject(),
        *down_cast<NameDef*>(old_to_new_.at(&n->name_def())), n->alias());
    return absl::OkStatus();
  }

  absl::Status HandleIndex(const Index* n) override {
    XLS_RETURN_IF_ERROR(VisitChildren(n));

    IndexRhs new_rhs =
        absl::visit(Visitor{[&](auto* expr) -> IndexRhs {
                      return down_cast<decltype(expr)>(old_to_new_.at(expr));
                    }},
                    n->rhs());

    old_to_new_[n] = module_->Make<Index>(
        n->span(), down_cast<Expr*>(old_to_new_.at(n->lhs())), new_rhs);
    return absl::OkStatus();
  }

  absl::Status HandleInvocation(const Invocation* n) override {
    XLS_RETURN_IF_ERROR(VisitChildren(n));

    std::vector<Expr*> new_args;
    new_args.reserve(n->args().size());
    for (const Expr* arg : n->args()) {
      new_args.push_back(down_cast<Expr*>(old_to_new_.at(arg)));
    }

    old_to_new_[n] = module_->Make<Invocation>(
        n->span(), down_cast<Expr*>(old_to_new_.at(n->callee())), new_args,
        CloneParametrics(n->explicit_parametrics()));
    return absl::OkStatus();
  }

  absl::Status HandleLet(const Let* n) override {
    XLS_RETURN_IF_ERROR(VisitChildren(n));

    TypeAnnotation* new_type = nullptr;
    if (n->type_annotation() != nullptr) {
      new_type =
          down_cast<TypeAnnotation*>(old_to_new_.at(n->type_annotation()));
    }

    old_to_new_[n] = module_->Make<Let>(
        n->span(), down_cast<NameDefTree*>(old_to_new_.at(n->name_def_tree())),
        new_type, down_cast<Expr*>(old_to_new_.at(n->rhs())), n->is_const());
    return absl::OkStatus();
  }

  absl::Status HandleMatch(const Match* n) override {
    XLS_RETURN_IF_ERROR(VisitChildren(n));

    std::vector<MatchArm*> new_arms;
    new_arms.reserve(n->arms().size());
    for (const MatchArm* arm : n->arms()) {
      new_arms.push_back(down_cast<MatchArm*>(old_to_new_.at(arm)));
    }

    old_to_new_[n] = module_->Make<Match>(
        n->span(), down_cast<Expr*>(old_to_new_.at(n->matched())), new_arms);
    return absl::OkStatus();
  }

  absl::Status HandleMatchArm(const MatchArm* n) override {
    XLS_RETURN_IF_ERROR(VisitChildren(n));

    std::vector<NameDefTree*> new_patterns;
    new_patterns.reserve(n->patterns().size());
    for (const NameDefTree* pattern : n->patterns()) {
      new_patterns.push_back(down_cast<NameDefTree*>(old_to_new_.at(pattern)));
    }

    old_to_new_[n] = module_->Make<MatchArm>(
        n->span(), new_patterns, down_cast<Expr*>(old_to_new_.at(n->expr())));

    return absl::OkStatus();
  }

  // If `replacement` is a `VerbatimNode`, return it. Otherwise, return the
  // `replacement` cast to the given type.
  template <typename T>
  static ModuleMember CastOrVerbatim(AstNode* replacement) {
    if (auto* verbatim = dynamic_cast<VerbatimNode*>(replacement)) {
      return verbatim;
    }
    return down_cast<T>(replacement);
  }

  absl::Status HandleModule(const Module* n) override {
    for (const ModuleMember member : n->top()) {
      ModuleMember new_member;
      XLS_RETURN_IF_ERROR(absl::visit(
          Visitor{
              [&](auto* old_member) -> absl::Status {
                XLS_RETURN_IF_ERROR(ReplaceOrVisit(old_member));
                new_member = CastOrVerbatim<decltype(old_member)>(
                    old_to_new().at(old_member));
                return absl::OkStatus();
              },
          },
          member));
      XLS_RETURN_IF_ERROR(
          module_->AddTop(new_member, /*make_collision_error=*/nullptr));
    }

    return absl::OkStatus();
  }

  absl::Status HandleNameDef(const NameDef* n) override {
    if (!old_to_new_.contains(n)) {
      // We need to set the definer in the definer itself, not here, to avoid
      // looping (Function -> NameDef -> Function -> NameDef -> ...).
      old_to_new_[n] =
          module_->Make<NameDef>(n->span(), n->identifier(), nullptr);
    }

    return absl::OkStatus();
  }

  absl::Status HandleNameDefTree(const NameDefTree* n) override {
    XLS_RETURN_IF_ERROR(VisitChildren(n));

    if (n->is_leaf()) {
      NameDefTree::Leaf leaf;
      absl::visit(
          [&](auto* x) { leaf = down_cast<decltype(x)>(old_to_new_.at(x)); },
          n->leaf());
      old_to_new_[n] = module_->Make<NameDefTree>(n->span(), leaf);
      return absl::OkStatus();
    }

    NameDefTree::Nodes nodes;
    nodes.reserve(n->nodes().size());
    for (const auto& node : n->nodes()) {
      nodes.push_back(down_cast<NameDefTree*>(old_to_new_.at(node)));
    }
    old_to_new_[n] = module_->Make<NameDefTree>(n->span(), nodes);
    return absl::OkStatus();
  }

  absl::Status HandleNameRef(const NameRef* n) override {
    // If it's a ref to a cloned def, then point it to the cloned def.
    // Otherwise, it may be a ref to a def that is outside the scope being
    // cloned.
    auto it = old_to_new_.end();
    if (std::holds_alternative<const NameDef*>(n->name_def())) {
      it = old_to_new_.find(std::get<const NameDef*>(n->name_def()));
    }
    old_to_new_[n] = module_->Make<NameRef>(
        n->span(), n->identifier(),
        it == old_to_new_.end() ? n->name_def()
                                : down_cast<NameDef*>(it->second));
    return absl::OkStatus();
  }

  absl::Status HandleNumber(const Number* n) override {
    XLS_RETURN_IF_ERROR(VisitChildren(n));

    TypeAnnotation* new_type = nullptr;
    if (n->type_annotation() != nullptr) {
      new_type =
          down_cast<TypeAnnotation*>(old_to_new_.at(n->type_annotation()));
    }
    old_to_new_[n] =
        module_->Make<Number>(n->span(), n->text(), n->number_kind(), new_type);
    return absl::OkStatus();
  }

  absl::Status HandleParam(const Param* n) override {
    XLS_RETURN_IF_ERROR(VisitChildren(n));
    old_to_new_[n] = module_->Make<Param>(
        down_cast<NameDef*>(old_to_new_.at(n->name_def())),
        down_cast<TypeAnnotation*>(old_to_new_.at(n->type_annotation())));
    return absl::OkStatus();
  }

  absl::Status HandleProcMember(const ProcMember* n) override {
    XLS_RETURN_IF_ERROR(VisitChildren(n));
    old_to_new_[n] = module_->Make<ProcMember>(
        down_cast<NameDef*>(old_to_new_.at(n->name_def())),
        down_cast<TypeAnnotation*>(old_to_new_.at(n->type_annotation())));
    return absl::OkStatus();
  }

  absl::Status HandleParametricBinding(const ParametricBinding* n) override {
    XLS_RETURN_IF_ERROR(VisitChildren(n));

    Expr* new_expr = nullptr;
    if (n->expr() != nullptr) {
      new_expr = down_cast<Expr*>(old_to_new_.at(n->expr()));
    }
    old_to_new_[n] = module_->Make<ParametricBinding>(
        down_cast<NameDef*>(old_to_new_.at(n->name_def())),
        down_cast<TypeAnnotation*>(old_to_new_.at(n->type_annotation())),
        new_expr);
    return absl::OkStatus();
  }

  absl::Status HandleProc(const Proc* n) override {
    XLS_RETURN_IF_ERROR(VisitChildren(n));

    std::vector<ParametricBinding*> new_parametric_bindings;
    new_parametric_bindings.reserve(n->parametric_bindings().size());
    for (const ParametricBinding* pb : n->parametric_bindings()) {
      new_parametric_bindings.push_back(
          down_cast<ParametricBinding*>(old_to_new_.at(pb)));
    }

    std::vector<ProcMember*> new_members;
    new_members.reserve(n->members().size());
    for (const ProcMember* member : n->members()) {
      new_members.push_back(down_cast<ProcMember*>(old_to_new_.at(member)));
    }

    std::vector<ProcStmt> new_stmts;
    new_stmts.reserve(n->stmts().size());
    for (const ProcStmt& stmt : n->stmts()) {
      XLS_ASSIGN_OR_RETURN(ProcStmt new_stmt,
                           ToProcStmt(old_to_new_.at(ToAstNode(stmt))));
      new_stmts.push_back(new_stmt);
    }

    NameDef* new_name_def = down_cast<NameDef*>(old_to_new_.at(n->name_def()));
    ProcLikeBody new_body = {
        .stmts = new_stmts,
        .config = down_cast<Function*>(old_to_new_.at(&n->config())),
        .next = down_cast<Function*>(old_to_new_.at(&n->next())),
        .init = down_cast<Function*>(old_to_new_.at(&n->init())),
        .members = new_members,
    };
    Proc* p =
        module_->Make<Proc>(n->span(), n->body_span(), new_name_def,
                            new_parametric_bindings, new_body, n->is_public());
    new_name_def->set_definer(p);
    old_to_new_[n] = p;
    return absl::OkStatus();
  }

  absl::Status HandleQuickCheck(const QuickCheck* n) override {
    XLS_RETURN_IF_ERROR(VisitChildren(n));
    old_to_new_[n] = module_->Make<QuickCheck>(
        n->GetSpan().value(), down_cast<Function*>(old_to_new_.at(n->fn())),
        n->test_count());
    return absl::OkStatus();
  }

  absl::Status HandleRange(const Range* n) override {
    XLS_RETURN_IF_ERROR(VisitChildren(n));
    old_to_new_[n] = module_->Make<Range>(
        n->span(), down_cast<Expr*>(old_to_new_.at(n->start())),
        down_cast<Expr*>(old_to_new_.at(n->end())));
    return absl::OkStatus();
  }

  absl::Status HandleSlice(const Slice* n) override {
    XLS_RETURN_IF_ERROR(VisitChildren(n));

    Expr* new_start = n->start() == nullptr
                          ? nullptr
                          : down_cast<Expr*>(old_to_new_.at(n->start()));
    Expr* new_limit = n->limit() == nullptr
                          ? nullptr
                          : down_cast<Expr*>(old_to_new_.at(n->limit()));

    old_to_new_[n] =
        module_->Make<Slice>(n->GetSpan().value(), new_start, new_limit);
    return absl::OkStatus();
  }

  absl::Status HandleSpawn(const Spawn* n) override {
    XLS_RETURN_IF_ERROR(VisitChildren(n));
    XLS_RETURN_IF_ERROR(ReplaceOrVisit(n->callee()));
    old_to_new_[n] = module_->Make<Spawn>(
        n->span(), down_cast<Expr*>(old_to_new_.at(n->callee())),
        down_cast<Invocation*>(old_to_new_.at(n->config())),
        down_cast<Invocation*>(old_to_new_.at(n->next())),
        CloneParametrics(n->explicit_parametrics()));
    return absl::OkStatus();
  }

  absl::Status HandleSplatStructInstance(
      const SplatStructInstance* n) override {
    XLS_RETURN_IF_ERROR(VisitChildren(n));

    // Have to explicitly visit struct ref, since it's not a child.
    XLS_RETURN_IF_ERROR(ReplaceOrVisit(n->struct_ref()));
    TypeAnnotation* new_struct_ref =
        down_cast<TypeAnnotation*>(old_to_new_.at(n->struct_ref()));

    const std::vector<std::pair<std::string, Expr*>>& old_members =
        n->members();
    std::vector<std::pair<std::string, Expr*>> new_members;
    new_members.reserve(old_members.size());
    for (const auto& member : old_members) {
      new_members.push_back(std::make_pair(
          member.first, down_cast<Expr*>(old_to_new_.at(member.second))));
    }

    old_to_new_[n] = module_->Make<SplatStructInstance>(
        n->span(), new_struct_ref, new_members,
        down_cast<Expr*>(old_to_new_.at(n->splatted())));
    return absl::OkStatus();
  }

  absl::Status HandleString(const String* n) override {
    old_to_new_[n] = module_->Make<String>(n->span(), n->text());
    return absl::OkStatus();
  }

  // Helper function to clone a node of type `T` where `T` is a concrete
  // subclass of `StructDefBase`.
  template <typename T>
  absl::Status HandleStructDefBaseInternal(const T* n) {
    XLS_RETURN_IF_ERROR(VisitChildren(n));

    std::vector<ParametricBinding*> new_parametric_bindings;
    new_parametric_bindings.reserve(n->parametric_bindings().size());
    for (const auto* pb : n->parametric_bindings()) {
      new_parametric_bindings.push_back(
          down_cast<ParametricBinding*>(old_to_new_.at(pb)));
    }

    std::vector<StructMember> new_members;
    for (const StructMember& member : n->members()) {
      new_members.push_back(StructMember{
          .name_span = member.name_span,
          .name = member.name,
          .type = down_cast<TypeAnnotation*>(old_to_new_.at(member.type))});
    }

    auto* new_name_def = down_cast<NameDef*>(old_to_new_.at(n->name_def()));
    auto* new_struct_def =
        module_->Make<T>(n->span(), new_name_def, new_parametric_bindings,
                         new_members, n->is_public());
    old_to_new_[n] = new_struct_def;

    if (n->impl().has_value()) {
      if (!old_to_new_.contains(n->impl().value())) {
        XLS_RETURN_IF_ERROR(ReplaceOrVisit(n->impl().value()));
      }
      auto* new_impl = down_cast<Impl*>(old_to_new_.at(n->impl().value()));
      new_struct_def->set_impl(new_impl);
    }
    new_name_def->set_definer(new_struct_def);
    return absl::OkStatus();
  }

  absl::Status HandleStructDef(const StructDef* n) override {
    return HandleStructDefBaseInternal(n);
  }

  absl::Status HandleProcDef(const ProcDef* n) override {
    return HandleStructDefBaseInternal(n);
  }

  absl::Status HandleImpl(const Impl* n) override {
    XLS_RETURN_IF_ERROR(VisitChildren(n));

    std::vector<ImplMember> new_members;
    new_members.reserve(n->members().size());
    for (const auto& member : n->members()) {
      AstNode* new_node = old_to_new_.at(ToAstNode(member));
      if (std::holds_alternative<ConstantDef*>(member)) {
        new_members.push_back(down_cast<ConstantDef*>(new_node));
      } else {
        new_members.push_back(down_cast<Function*>(new_node));
      }
    }

    Impl* new_impl =
        module_->Make<Impl>(n->span(), nullptr, new_members, n->is_public());
    old_to_new_[n] = new_impl;
    if (!old_to_new_.contains(n->struct_ref())) {
      XLS_RETURN_IF_ERROR(ReplaceOrVisit(n->struct_ref()));
    }
    TypeAnnotation* new_struct_ref =
        down_cast<TypeAnnotation*>(old_to_new_.at(n->struct_ref()));
    new_impl->set_struct_ref(new_struct_ref);
    return absl::OkStatus();
  }

  absl::Status HandleStructInstance(const StructInstance* n) override {
    XLS_RETURN_IF_ERROR(VisitChildren(n));

    // Have to explicitly visit struct ref, since it's not a child.
    XLS_RETURN_IF_ERROR(ReplaceOrVisit(n->struct_ref()));
    TypeAnnotation* new_struct_ref =
        down_cast<TypeAnnotation*>(old_to_new_.at(n->struct_ref()));

    absl::Span<const std::pair<std::string, Expr*>> old_members =
        n->GetUnorderedMembers();
    std::vector<std::pair<std::string, Expr*>> new_members;
    new_members.reserve(old_members.size());
    for (const auto& member : old_members) {
      new_members.push_back(std::make_pair(
          member.first, down_cast<Expr*>(old_to_new_.at(member.second))));
    }

    old_to_new_[n] =
        module_->Make<StructInstance>(n->span(), new_struct_ref, new_members);
    return absl::OkStatus();
  }

  absl::Status HandleConditional(const Conditional* n) override {
    XLS_RETURN_IF_ERROR(VisitChildren(n));

    std::variant<StatementBlock*, Conditional*> new_alternate;
    AstNode* new_alternate_node = old_to_new_.at(ToAstNode(n->alternate()));
    if (auto* block = down_cast<StatementBlock*>(new_alternate_node)) {
      new_alternate = block;
    } else if (auto* cond = down_cast<Conditional*>(new_alternate_node)) {
      new_alternate = cond;
    } else {
      return absl::InternalError("Unexpected Conditional alternate node type.");
    }

    old_to_new_[n] = module_->Make<Conditional>(
        n->span(), down_cast<Expr*>(old_to_new_.at(n->test())),
        down_cast<StatementBlock*>(old_to_new_.at(n->consequent())),
        new_alternate);
    return absl::OkStatus();
  }

  absl::Status HandleTestFunction(const TestFunction* n) override {
    XLS_RETURN_IF_ERROR(VisitChildren(n));

    XLS_RETURN_IF_ERROR(ReplaceOrVisit(&n->fn()));
    old_to_new_[n] = module_->Make<TestFunction>(
        n->span(), *down_cast<Function*>(old_to_new_.at(&n->fn())));
    return absl::OkStatus();
  }

  absl::Status HandleTestProc(const TestProc* n) override {
    XLS_RETURN_IF_ERROR(VisitChildren(n));

    old_to_new_[n] =
        module_->Make<TestProc>(down_cast<Proc*>(old_to_new_.at(n->proc())));
    return absl::OkStatus();
  }

  absl::Status HandleTupleIndex(const TupleIndex* n) override {
    XLS_RETURN_IF_ERROR(VisitChildren(n));
    old_to_new_[n] = module_->Make<TupleIndex>(
        n->span(), down_cast<Expr*>(old_to_new_.at(n->lhs())),
        down_cast<Number*>(old_to_new_.at(n->index())));
    return absl::OkStatus();
  }

  absl::Status HandleTupleTypeAnnotation(
      const TupleTypeAnnotation* n) override {
    XLS_RETURN_IF_ERROR(VisitChildren(n));

    std::vector<TypeAnnotation*> new_members;
    new_members.reserve(n->members().size());
    for (const auto* member : n->members()) {
      new_members.push_back(down_cast<TypeAnnotation*>(old_to_new_.at(member)));
    }
    old_to_new_[n] = module_->Make<TupleTypeAnnotation>(n->span(), new_members);
    return absl::OkStatus();
  }

  absl::Status HandleTypeAlias(const TypeAlias* n) override {
    XLS_RETURN_IF_ERROR(VisitChildren(n));

    NameDef* new_name_def = down_cast<NameDef*>(old_to_new_.at(&n->name_def()));
    TypeAlias* new_td = module_->Make<TypeAlias>(
        n->span(), *new_name_def,
        *down_cast<TypeAnnotation*>(old_to_new_.at(&n->type_annotation())),
        n->is_public());
    new_name_def->set_definer(new_td);
    old_to_new_[n] = new_td;
    return absl::OkStatus();
  }

  absl::Status HandleTypeRef(const TypeRef* n) override {
    TypeDefinition new_type_definition = n->type_definition();

    // A TypeRef doesn't own its referenced type definition, so we have to
    // explicitly visit it.
    XLS_RETURN_IF_ERROR(absl::visit(
        Visitor{[&](auto* ref) -> absl::Status {
          XLS_RETURN_IF_ERROR(ReplaceOrVisit(ref));
          new_type_definition = down_cast<decltype(ref)>(old_to_new_.at(ref));
          return absl::OkStatus();
        }},
        n->type_definition()));

    old_to_new_[n] = module_->Make<TypeRef>(n->span(), new_type_definition);
    return absl::OkStatus();
  }

  absl::Status HandleTypeRefTypeAnnotation(
      const TypeRefTypeAnnotation* n) override {
    XLS_RETURN_IF_ERROR(VisitChildren(n));
    old_to_new_[n] = module_->Make<TypeRefTypeAnnotation>(
        n->span(), down_cast<TypeRef*>(old_to_new_.at(n->type_ref())),
        CloneParametrics(n->parametrics()));
    return absl::OkStatus();
  }

  absl::Status HandleUnop(const Unop* n) override {
    XLS_RETURN_IF_ERROR(VisitChildren(n));
    auto* new_op =
        module_->Make<Unop>(n->span(), n->unop_kind(),
                            down_cast<Expr*>(old_to_new_.at(n->operand())));
    // TODO(cdleary): 2023-05-30 The in_parens arguments should be given to the
    // Expr base class so we don't need to remember to clone it explicitly here
    // (i.e. it's given at construction naturally).
    new_op->set_in_parens(n->in_parens());
    old_to_new_[n] = new_op;
    return absl::OkStatus();
  }

  absl::Status HandleUnrollFor(const UnrollFor* n) override {
    XLS_RETURN_IF_ERROR(VisitChildren(n));

    UnrollFor* new_unroll_for = module_->Make<UnrollFor>(
        n->span(), down_cast<NameDefTree*>(old_to_new_.at(n->names())),
        down_cast<TypeAnnotation*>(old_to_new_.at(n->type_annotation())),
        down_cast<Expr*>(old_to_new_.at(n->iterable())),
        down_cast<StatementBlock*>(old_to_new_.at(n->body())),
        down_cast<Expr*>(old_to_new_.at(n->init())));
    old_to_new_[n] = new_unroll_for;
    return absl::OkStatus();
  }

  absl::Status HandleWidthSlice(const WidthSlice* n) override {
    XLS_RETURN_IF_ERROR(VisitChildren(n));
    old_to_new_[n] = module_->Make<WidthSlice>(
        n->GetSpan().value(), down_cast<Expr*>(old_to_new_.at(n->start())),
        down_cast<TypeAnnotation*>(old_to_new_.at(n->width())));
    return absl::OkStatus();
  }

  absl::Status HandleWildcardPattern(const WildcardPattern* n) override {
    old_to_new_[n] = module_->Make<WildcardPattern>(n->span());
    return absl::OkStatus();
  }

  absl::Status HandleRestOfTuple(const RestOfTuple* n) override {
    old_to_new_[n] = module_->Make<RestOfTuple>(n->span());
    return absl::OkStatus();
  }

  absl::Status HandleXlsTuple(const XlsTuple* n) override {
    XLS_RETURN_IF_ERROR(VisitChildren(n));

    std::vector<Expr*> members;
    members.reserve(n->members().size());
    for (const auto* member : n->members()) {
      members.push_back(down_cast<Expr*>(old_to_new_.at(member)));
    }

    old_to_new_[n] =
        module_->Make<XlsTuple>(n->span(), members, n->has_trailing_comma());
    return absl::OkStatus();
  }

  absl::Status HandleVerbatimNode(const VerbatimNode* n) override {
    old_to_new_[n] = module_->Make<VerbatimNode>(n->span(), n->text());
    return absl::OkStatus();
  }

  const absl::flat_hash_map<const AstNode*, AstNode*>& old_to_new() {
    return old_to_new_;
  }

 private:
  // Visits all the children of the given node, skipping those that have
  // already been processed.
  absl::Status VisitChildren(const AstNode* node) {
    for (const auto& child : node->GetChildren(/*want_types=*/true)) {
      if (!old_to_new_.contains(child)) {
        XLS_RETURN_IF_ERROR(ReplaceOrVisit(child));
      }
    }
    return absl::OkStatus();
  }

  absl::Status ReplaceOrVisit(const AstNode* node) {
    XLS_ASSIGN_OR_RETURN(std::optional<AstNode*> replacement, replacer_(node));
    if (replacement.has_value()) {
      old_to_new_[node] = *replacement;
      return absl::OkStatus();
    }
    return node->Accept(this);
  }

  std::vector<ExprOrType> CloneParametrics(
      const std::vector<ExprOrType>& parametrics) {
    std::vector<ExprOrType> new_parametrics;
    new_parametrics.reserve(parametrics.size());
    for (const ExprOrType& parametric : parametrics) {
      AstNode* new_node = old_to_new_.at(ToAstNode(parametric));
      if (std::holds_alternative<Expr*>(parametric)) {
        new_parametrics.push_back(down_cast<Expr*>(new_node));
      } else {
        new_parametrics.push_back(down_cast<TypeAnnotation*>(new_node));
      }
    }
    return new_parametrics;
  }

  Module* const module_;
  CloneReplacer replacer_;
  absl::flat_hash_map<const AstNode*, AstNode*> old_to_new_;
};

}  // namespace

std::optional<AstNode*> PreserveTypeDefinitionsReplacer(const AstNode* node) {
  if (const auto* type_ref = dynamic_cast<const TypeRef*>(node); type_ref) {
    return node->owner()->Make<TypeRef>(type_ref->span(),
                                        type_ref->type_definition());
  }
  return std::nullopt;
}

CloneReplacer NameRefReplacer(const NameDef* def, Expr* replacement) {
  return [=](const AstNode* node) -> std::optional<AstNode*> {
    if (const auto* name_ref = dynamic_cast<const NameRef*>(node);
        name_ref != nullptr &&
        std::holds_alternative<const NameDef*>(name_ref->name_def()) &&
        std::get<const NameDef*>(name_ref->name_def()) == def) {
      return replacement;
    }
    return std::nullopt;
  };
}

absl::StatusOr<AstNode*> CloneAst(const AstNode* root, CloneReplacer replacer) {
  if (dynamic_cast<const Module*>(root) != nullptr) {
    return absl::InvalidArgumentError("Clone a module via 'CloneModule'.");
  }
  AstCloner cloner(root->owner(), std::move(replacer));
  XLS_RETURN_IF_ERROR(root->Accept(&cloner));
  return cloner.old_to_new().at(root);
}

absl::StatusOr<std::unique_ptr<Module>> CloneModule(const Module& module,
                                                    CloneReplacer replacer) {
  auto new_module = std::make_unique<Module>(module.name(), module.fs_path(),
                                             *module.file_table());
  AstCloner cloner(new_module.get(), std::move(replacer));
  XLS_RETURN_IF_ERROR(module.Accept(&cloner));
  return new_module;
}

// Verifies that `node` consists solely of "new" AST nodes and none that are
// in the "old" set.
absl::Status VerifyClone(const AstNode* old_root, const AstNode* new_root,
                         const FileTable& file_table) {
  absl::flat_hash_set<const AstNode*> old_nodes = FlattenToSet(old_root);
  absl::flat_hash_set<const AstNode*> new_nodes = FlattenToSet(new_root);
  for (const AstNode* new_node : new_nodes) {
    if (old_nodes.contains(new_node)) {
      std::optional<Span> span = new_node->GetSpan();
      return absl::InvalidArgumentError(absl::StrFormat(
          "Node \"%s\" (%s; %s) was found in both the old set and "
          "new translation!",
          new_node->ToString(),
          span.has_value() ? span.value().ToString(file_table) : "<no span>",
          new_node->GetNodeTypeName()));
    }
  }
  return absl::OkStatus();
}

}  // namespace xls::dslx
