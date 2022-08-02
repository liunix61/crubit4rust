// Part of the Crubit project, under the Apache License v2.0 with LLVM
// Exceptions. See /LICENSE for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "nullability_verification/pointer_nullability_analysis.h"

#include <string>

#include "common/check.h"
#include "nullability_verification/pointer_nullability.h"
#include "nullability_verification/pointer_nullability_matchers.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Expr.h"
#include "clang/AST/OperationKinds.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/Type.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Analysis/FlowSensitive/DataflowEnvironment.h"
#include "clang/Analysis/FlowSensitive/MatchSwitch.h"
#include "clang/Analysis/FlowSensitive/NoopLattice.h"
#include "clang/Analysis/FlowSensitive/Value.h"
#include "clang/Basic/LLVM.h"

namespace clang {
namespace tidy {
namespace nullability {

using ast_matchers::MatchFinder;
using dataflow::BoolValue;
using dataflow::Environment;
using dataflow::MatchSwitchBuilder;
using dataflow::NoopLattice;
using dataflow::SkipPast;
using dataflow::TransferState;
using dataflow::Value;

namespace {

void transferInitNotNullPointer(const Expr* NotNullPointer,
                                const MatchFinder::MatchResult&,
                                TransferState<NoopLattice>& State) {
  initPointerNullState(NotNullPointer, State.Env,
                       /*Known=*/&State.Env.getBoolLiteralValue(true),
                       /*NotNull=*/&State.Env.getBoolLiteralValue(true));
}

void transferInitNullPointer(const Expr* NullPointer,
                             const MatchFinder::MatchResult&,
                             TransferState<NoopLattice>& State) {
  initPointerNullState(NullPointer, State.Env,
                       /*Known=*/&State.Env.getBoolLiteralValue(true),
                       /*NotNull=*/&State.Env.getBoolLiteralValue(false));
}

void transferInitNullablePointer(const Expr* NullablePointer,
                                 TransferState<NoopLattice>& State) {
  initPointerNullState(NullablePointer, State.Env,
                       /*Known=*/&State.Env.getBoolLiteralValue(true));
}

void transferInitPointerFromDecl(const Expr* PointerExpr,
                                 const MatchFinder::MatchResult&,
                                 TransferState<NoopLattice>& State) {
  // TODO(b/233582219): Implement processing of nullability annotations. The
  // current implementation treats unnannotated pointers as nullable.
  transferInitNullablePointer(PointerExpr, State);
}

// TODO(b/233582219): Implement promotion of nullability knownness for initially
// unknown pointers when there is evidence that it is nullable, for example
// when the pointer is compared to nullptr, or casted to boolean.
void transferNullCheckComparison(const BinaryOperator* BinaryOp,
                                 const MatchFinder::MatchResult& result,
                                 TransferState<NoopLattice>& State) {
  // Boolean representing the comparison between the two pointer values,
  // automatically created by the dataflow framework
  auto& PointerComparison =
      *cast<BoolValue>(State.Env.getValue(*BinaryOp, SkipPast::None));

  CHECK(BinaryOp->getOpcode() == BO_EQ || BinaryOp->getOpcode() == BO_NE);
  auto& PointerEQ = BinaryOp->getOpcode() == BO_EQ
                        ? PointerComparison
                        : State.Env.makeNot(PointerComparison);
  auto& PointerNE = BinaryOp->getOpcode() == BO_EQ
                        ? State.Env.makeNot(PointerComparison)
                        : PointerComparison;

  auto [LHSKnown, LHSNotNull] =
      getPointerNullState(BinaryOp->getLHS(), State.Env);
  auto [RHSKnown, RHSNotNull] =
      getPointerNullState(BinaryOp->getRHS(), State.Env);
  auto& LHSKnownNotNull = State.Env.makeAnd(LHSKnown, LHSNotNull);
  auto& RHSKnownNotNull = State.Env.makeAnd(RHSKnown, RHSNotNull);
  auto& LHSKnownNull =
      State.Env.makeAnd(LHSKnown, State.Env.makeNot(LHSNotNull));
  auto& RHSKnownNull =
      State.Env.makeAnd(RHSKnown, State.Env.makeNot(RHSNotNull));

  // nullptr == nullptr
  State.Env.addToFlowCondition(State.Env.makeImplication(
      State.Env.makeAnd(LHSKnownNull, RHSKnownNull), PointerEQ));
  // nullptr != notnull
  State.Env.addToFlowCondition(State.Env.makeImplication(
      State.Env.makeAnd(LHSKnownNull, RHSKnownNotNull), PointerNE));
  // notnull != nullptr
  State.Env.addToFlowCondition(State.Env.makeImplication(
      State.Env.makeAnd(LHSKnownNotNull, RHSKnownNull), PointerNE));
}

void transferNullCheckImplicitCastPtrToBool(const Expr* CastExpr,
                                            const MatchFinder::MatchResult&,
                                            TransferState<NoopLattice>& State) {
  auto [PointerKnown, PointerNotNull] =
      getPointerNullState(CastExpr->IgnoreImplicit(), State.Env);
  auto& CastExprLoc = State.Env.createStorageLocation(*CastExpr);
  State.Env.setValue(CastExprLoc, PointerNotNull);
  State.Env.setStorageLocation(*CastExpr, CastExprLoc);
}

auto buildTransferer() {
  return MatchSwitchBuilder<TransferState<NoopLattice>>()
      // Handles initialization of the null states of pointers
      .CaseOf<Expr>(isPointerVariableReference(), transferInitPointerFromDecl)
      .CaseOf<Expr>(isCXXThisExpr(), transferInitNotNullPointer)
      .CaseOf<Expr>(isAddrOf(), transferInitNotNullPointer)
      .CaseOf<Expr>(isNullPointerLiteral(), transferInitNullPointer)
      .CaseOf<MemberExpr>(isMemberOfPointerType(), transferInitPointerFromDecl)
      // Handles comparison between 2 pointers
      .CaseOf<BinaryOperator>(isPointerCheckBinOp(),
                              transferNullCheckComparison)
      // Handles checking of pointer as boolean
      .CaseOf<Expr>(isImplicitCastPointerToBool(),
                    transferNullCheckImplicitCastPtrToBool)
      .Build();
}
}  // namespace

PointerNullabilityAnalysis::PointerNullabilityAnalysis(ASTContext& Context)
    : DataflowAnalysis<PointerNullabilityAnalysis, NoopLattice>(Context),
      Transferer(buildTransferer()) {}

void PointerNullabilityAnalysis::transfer(const Stmt* Stmt,
                                          NoopLattice& Lattice,
                                          Environment& Env) {
  TransferState<NoopLattice> State(Lattice, Env);
  Transferer(*Stmt, getASTContext(), State);
}

bool PointerNullabilityAnalysis::merge(QualType Type, const Value& Val1,
                                       const Environment& Env1,
                                       const Value& Val2,
                                       const Environment& Env2,
                                       Value& MergedVal,
                                       Environment& MergedEnv) {
  return false;
}
}  // namespace nullability
}  // namespace tidy
}  // namespace clang
