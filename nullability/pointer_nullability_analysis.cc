// Part of the Crubit project, under the Apache License v2.0 with LLVM
// Exceptions. See /LICENSE for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "nullability/pointer_nullability_analysis.h"

#include <cassert>
#include <functional>
#include <optional>
#include <vector>

#include "absl/log/check.h"
#include "nullability/pointer_nullability.h"
#include "nullability/pointer_nullability_lattice.h"
#include "nullability/pointer_nullability_matchers.h"
#include "nullability/type_nullability.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/OperationKinds.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/Type.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Analysis/CFG.h"
#include "clang/Analysis/FlowSensitive/Arena.h"
#include "clang/Analysis/FlowSensitive/CFGMatchSwitch.h"
#include "clang/Analysis/FlowSensitive/DataflowAnalysis.h"
#include "clang/Analysis/FlowSensitive/DataflowAnalysisContext.h"
#include "clang/Analysis/FlowSensitive/DataflowEnvironment.h"
#include "clang/Analysis/FlowSensitive/StorageLocation.h"
#include "clang/Analysis/FlowSensitive/Value.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/Specifiers.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

namespace clang::tidy::nullability {

using ast_matchers::MatchFinder;
using dataflow::Arena;
using dataflow::BoolValue;
using dataflow::CFGMatchSwitchBuilder;
using dataflow::ComparisonResult;
using dataflow::DataflowAnalysisContext;
using dataflow::Environment;
using dataflow::Formula;
using dataflow::PointerValue;
using dataflow::RecordStorageLocation;
using dataflow::StorageLocation;
using dataflow::TransferState;
using dataflow::Value;

#define DEBUG_TYPE "pointer_nullability_analysis.cc"

namespace {

TypeNullability prepend(NullabilityKind Head, const TypeNullability &Tail) {
  TypeNullability Result = {Head};
  Result.insert(Result.end(), Tail.begin(), Tail.end());
  return Result;
}

void computeNullability(const Expr *E,
                        TransferState<PointerNullabilityLattice> &State,
                        std::function<TypeNullability()> Compute) {
  (void)State.Lattice.insertExprNullabilityIfAbsent(E, [&] {
    auto Nullability = Compute();
    if (unsigned ExpectedSize = countPointersInType(E);
        ExpectedSize != Nullability.size()) {
      // A nullability vector must have one entry per pointer in the type.
      // If this is violated, we probably failed to handle some AST node.
      LLVM_DEBUG({
        llvm::dbgs()
            << "=== Nullability vector has wrong number of entries: ===\n";
        llvm::dbgs() << "Expression: \n";
        dump(E, llvm::dbgs());
        llvm::dbgs() << "\nNullability (" << Nullability.size()
                     << " pointers): " << nullabilityToString(Nullability)
                     << "\n";
        llvm::dbgs() << "\nType (" << ExpectedSize << " pointers): \n";
        dump(exprType(E), llvm::dbgs());
        llvm::dbgs() << "=================================\n";
      });

      // We can't meaningfully interpret the vector, so discard it.
      // TODO: fix all broken cases and upgrade to CHECK or DCHECK or so.
      Nullability.assign(ExpectedSize, NullabilityKind::Unspecified);
    }
    return Nullability;
  });
}

// Returns the computed nullability for a subexpr of the current expression.
// This is always available as we compute bottom-up.
const TypeNullability &getNullabilityForChild(
    const Expr *E, TransferState<PointerNullabilityLattice> &State) {
  return State.Lattice.insertExprNullabilityIfAbsent(E, [&] {
    // Since we process child nodes before parents, we should already have
    // computed the child nullability. However, this is not true in all test
    // cases. So, we return unspecified nullability annotations.
    // TODO: fix this issue, and CHECK() instead.
    LLVM_DEBUG({
      llvm::dbgs() << "=== Missing child nullability: ===\n";
      dump(E, llvm::dbgs());
      llvm::dbgs() << "==================================\n";
    });

    return unspecifiedNullability(E);
  });
}

/// Compute the nullability annotation of type `T`, which contains types
/// originally written as a class template type parameter.
///
/// Example:
///
/// \code
///   template <typename F, typename S>
///   struct pair {
///     S *_Nullable getNullablePtrToSecond();
///   };
/// \endcode
///
/// Consider the following member call:
///
/// \code
///   pair<int *, int *_Nonnull> x;
///   x.getNullablePtrToSecond();
/// \endcode
///
/// The class template specialization `x` has the following substitutions:
///
///   F=int *, whose nullability is [_Unspecified]
///   S=int * _Nonnull, whose nullability is [_Nonnull]
///
/// The return type of the member call `x.getNullablePtrToSecond()` is
/// S * _Nullable.
///
/// When we call `substituteNullabilityAnnotationsInClassTemplate` with the type
/// `S * _Nullable` and the `base` node of the member call (in this case, a
/// `DeclRefExpr`), it returns the nullability of the given type after applying
/// substitutions, which in this case is [_Nullable, _Nonnull].
TypeNullability substituteNullabilityAnnotationsInClassTemplate(
    QualType T, const TypeNullability &BaseNullabilityAnnotations,
    QualType BaseType) {
  return getNullabilityAnnotationsFromType(
      T,
      [&](const SubstTemplateTypeParmType *ST)
          -> std::optional<TypeNullability> {
        // The class specialization that is BaseType and owns ST.
        const ClassTemplateSpecializationDecl *Specialization = nullptr;
        if (const auto *RT = BaseType->getAs<RecordType>())
          Specialization =
              dyn_cast<ClassTemplateSpecializationDecl>(RT->getDecl());
        // TODO: handle nested templates, where associated decl != base type
        // (e.g. PointerNullabilityTest.MemberFunctionTemplateOfTemplateStruct)
        if (!Specialization || Specialization != ST->getAssociatedDecl())
          return std::nullopt;
        // TODO: The code below does not deal correctly with partial
        // specializations. We should eventually handle these, but for now, just
        // bail out.
        if (isa<ClassTemplatePartialSpecializationDecl>(
                ST->getReplacedParameter()->getDeclContext()))
          return std::nullopt;

        unsigned ArgIndex = ST->getIndex();
        auto TemplateArgs = Specialization->getTemplateArgs().asArray();

        // TODO: If the type was substituted from a pack template argument,
        // we must find the slice that pertains to this particular type.
        // For now, just give up on resugaring this type.
        if (ST->getPackIndex().has_value()) return std::nullopt;

        unsigned PointerCount =
            countPointersInType(Specialization->getDeclContext());
        for (auto TA : TemplateArgs.take_front(ArgIndex)) {
          PointerCount += countPointersInType(TA);
        }

        unsigned SliceSize = countPointersInType(TemplateArgs[ArgIndex]);
        return ArrayRef(BaseNullabilityAnnotations)
            .slice(PointerCount, SliceSize)
            .vec();
      });
}

/// Compute nullability annotations of `T`, which might contain template type
/// variable substitutions bound by the call `CE`.
///
/// Example:
///
/// \code
///   template<typename F, typename S>
///   std::pair<S, F> flip(std::pair<F, S> p);
/// \endcode
///
/// Consider the following CallExpr:
///
/// \code
///   flip<int * _Nonnull, int * _Nullable>(std::make_pair(&x, &y));
/// \endcode
///
/// This CallExpr has the following substitutions:
///   F=int * _Nonnull, whose nullability is [_Nonnull]
///   S=int * _Nullable, whose nullability is [_Nullable]
///
/// The return type of this CallExpr is `std::pair<S, F>`.
///
/// When we call `substituteNullabilityAnnotationsInFunctionTemplate` with the
/// type `std::pair<S, F>` and the above CallExpr, it returns the nullability
/// the given type after applying substitutions, which in this case is
/// [_Nullable, _Nonnull].
TypeNullability substituteNullabilityAnnotationsInFunctionTemplate(
    QualType T, const CallExpr *CE) {
  return getNullabilityAnnotationsFromType(
      T,
      [&](const SubstTemplateTypeParmType *ST)
          -> std::optional<TypeNullability> {
        auto *DRE = dyn_cast<DeclRefExpr>(CE->getCallee()->IgnoreImpCasts());
        if (DRE == nullptr) return std::nullopt;

        // TODO: Handle calls that use template argument deduction.

        // Does this refer to a parameter of the function template?
        // If not (e.g. nested templates, template specialization types in the
        // return value), we handle the desugaring elsewhere.
        auto *ReferencedFunction = dyn_cast<FunctionDecl>(DRE->getDecl());
        if (!ReferencedFunction) return std::nullopt;
        if (ReferencedFunction->getPrimaryTemplate() != ST->getAssociatedDecl())
          return std::nullopt;

        // Some or all of the template arguments may be deduced, and we won't
        // see those on the `DeclRefExpr`. If the template argument was deduced,
        // we don't have any sugar for it.
        // TODO(b/268348533): Can we somehow obtain it from the function param
        // it was deduced from?
        // TODO(b/268345783): This check, as well as the index into
        // `template_arguments` below, may be incorrect in the presence of
        // parameters packs.  In function templates, parameter packs may appear
        // anywhere in the parameter list. The index may therefore refer to one
        // of the pack arguments, but we might incorrectly interpret it as
        // referring to an argument that follows the pack.
        if (ST->getIndex() >= DRE->template_arguments().size())
          return std::nullopt;

        TypeSourceInfo *TSI =
            DRE->template_arguments()[ST->getIndex()].getTypeSourceInfo();
        if (TSI == nullptr) return std::nullopt;
        return getNullabilityAnnotationsFromType(TSI->getType());
      });
}

PointerTypeNullability getPointerTypeNullability(
    const Expr *E, PointerNullabilityAnalysis::Lattice &L) {
  // TODO: handle this in non-flow-sensitive transfer instead
  if (auto FromClang = E->getType()->getNullability();
      FromClang && *FromClang != NullabilityKind::Unspecified)
    return *FromClang;

  if (const auto *NonFlowSensitive = L.getExprNullability(E)) {
    if (!NonFlowSensitive->empty())
      // Return the nullability of the topmost pointer in the type.
      return NonFlowSensitive->front();
  }

  return NullabilityKind::Unspecified;
}

void initPointerFromTypeNullability(
    PointerValue &PointerVal, const Expr *E,
    TransferState<PointerNullabilityLattice> &State) {
  initPointerNullState(PointerVal, State.Env.getDataflowAnalysisContext(),
                       getPointerTypeNullability(E, State.Lattice));
}

/// Returns a new pointer value referencing the same location as `PointerVal`
/// but with any "top" nullability properties unpacked into fresh atoms.
/// This is analogous to the unpacking done on `TopBoolValue`s in the framework.
/// TODO(mboehme): When we add support for smart pointers, this function will
/// also need to be called when accessing the `PointerValue` that underlies the
/// smart pointer.
PointerValue *unpackPointerValue(PointerValue &PointerVal, Environment &Env) {
  auto [FromNullable, Null] = getPointerNullState(PointerVal);
  if (FromNullable && Null) return nullptr;

  auto &A = Env.getDataflowAnalysisContext().arena();

  auto &NewPointerVal = Env.create<PointerValue>(PointerVal.getPointeeLoc());
  initPointerNullState(NewPointerVal, Env.getDataflowAnalysisContext());
  auto NewNullability = getPointerNullState(NewPointerVal);
  assert(NewNullability.FromNullable != nullptr);
  assert(NewNullability.IsNull != nullptr);

  if (FromNullable != nullptr)
    Env.assume(A.makeEquals(*NewNullability.FromNullable, *FromNullable));
  if (Null != nullptr) Env.assume(A.makeEquals(*NewNullability.IsNull, *Null));

  return &NewPointerVal;
}

void setToNonNullPointer(StorageLocation &PtrLoc, Environment &Env) {
  auto &Val = *cast<PointerValue>(Env.createValue(PtrLoc.getType()));
  initPointerNullState(Val, Env.getDataflowAnalysisContext(),
                       NullabilityKind::NonNull);
  Env.setValue(PtrLoc, Val);
}

void transferValue_NullPointer(
    const Expr *NullPointer, const MatchFinder::MatchResult &,
    TransferState<PointerNullabilityLattice> &State) {
  if (auto *PointerVal = getPointerValueFromExpr(NullPointer, State.Env)) {
    initNullPointer(*PointerVal, State.Env.getDataflowAnalysisContext());
  }
}

void transferValue_NotNullPointer(
    const Expr *NotNullPointer, const MatchFinder::MatchResult &,
    TransferState<PointerNullabilityLattice> &State) {
  if (auto *PointerVal = getPointerValueFromExpr(NotNullPointer, State.Env)) {
    initPointerNullState(*PointerVal, State.Env.getDataflowAnalysisContext(),
                         NullabilityKind::NonNull);
  }
}

bool isStdWeakPtrType(QualType Ty) {
  const CXXRecordDecl *RD = Ty.getCanonicalType()->getAsCXXRecordDecl();
  if (RD == nullptr) return false;

  if (!RD->getDeclContext()->isStdNamespace()) return false;

  const IdentifierInfo *ID = RD->getIdentifier();
  if (ID == nullptr) return false;

  return ID->getName() == "weak_ptr";
}

void transferValue_SmartPointerConstructor(
    const CXXConstructExpr *Ctor, const MatchFinder::MatchResult &Result,
    TransferState<PointerNullabilityLattice> &State) {
  RecordStorageLocation &Loc = State.Env.getResultObjectLocation(*Ctor);
  // Create a `RecordValue`, associate it with the `Loc` and the expression.
  State.Env.setValue(*Ctor, refreshRecordValue(Loc, State.Env));
  StorageLocation &PtrLoc = Loc.getSyntheticField(PtrField);

  // Default and `nullptr_t` constructor.
  if (Ctor->getConstructor()->isDefaultConstructor() ||
      (Ctor->getNumArgs() >= 1 &&
       Ctor->getArg(0)->getType()->isNullPtrType())) {
    State.Env.setValue(
        PtrLoc,
        createNullPointer(PtrLoc.getType()->getPointeeType(), State.Env));
    return;
  }

  // Construct from raw pointer.
  if (Ctor->getNumArgs() >= 1 &&
      isSupportedRawPointerType(Ctor->getArg(0)->getType())) {
    if (Value *Val = State.Env.getValue(*Ctor->getArg(0)))
      State.Env.setValue(PtrLoc, *Val);
    return;
  }

  // Copy or move from an existing smart pointer.
  if (Ctor->getNumArgs() >= 1 &&
      isSupportedSmartPointerType(Ctor->getArg(0)->getType())) {
    auto *SrcLoc = cast_or_null<RecordStorageLocation>(
        State.Env.getStorageLocation(*Ctor->getArg(0)));
    if (Ctor->getNumArgs() == 2 &&
        isSupportedRawPointerType(Ctor->getArg(1)->getType())) {
      // `shared_ptr` aliasing constructor.
      if (PointerValue *Val =
              getPointerValueFromExpr(Ctor->getArg(1), State.Env))
        State.Env.setValue(PtrLoc, *Val);
    } else {
      if (PointerValue *Val =
              getPointerValueFromSmartPointer(SrcLoc, State.Env))
        State.Env.setValue(PtrLoc, *Val);
    }

    if (Ctor->getConstructor()
            ->getParamDecl(0)
            ->getType()
            ->isRValueReferenceType() &&
        SrcLoc != nullptr) {
      State.Env.setValue(
          SrcLoc->getSyntheticField(PtrField),
          createNullPointer(PtrLoc.getType()->getPointeeType(), State.Env));
    }
    return;
  }

  // Construct from `weak_ptr`. This throws if the `weak_ptr` is empty, so we
  // can assume the `shared_ptr` is non-null if the constructor returns.
  if (Ctor->getNumArgs() == 1 && isStdWeakPtrType(Ctor->getArg(0)->getType()))
    setToNonNullPointer(PtrLoc, State.Env);
}

void transferValue_SmartPointerAssignment(
    const CXXOperatorCallExpr *OpCall, const MatchFinder::MatchResult &Result,
    TransferState<PointerNullabilityLattice> &State) {
  auto *Loc = cast<RecordStorageLocation>(
      State.Env.getStorageLocation(*OpCall->getArg(0)));
  if (Loc == nullptr) return;
  StorageLocation &PtrLoc = Loc->getSyntheticField(PtrField);

  if (OpCall->getArg(1)->getType()->isNullPtrType()) {
    State.Env.setValue(
        PtrLoc,
        createNullPointer(PtrLoc.getType()->getPointeeType(), State.Env));
    return;
  }

  auto *SrcLoc = cast_or_null<RecordStorageLocation>(
      State.Env.getStorageLocation(*OpCall->getArg(1)));
  if (PointerValue *Val = getPointerValueFromSmartPointer(SrcLoc, State.Env))
    State.Env.setValue(PtrLoc, *Val);

  // If this is the move assignment operator, set the source to null.
  auto *Method = dyn_cast_or_null<CXXMethodDecl>(OpCall->getCalleeDecl());
  if (Method != nullptr &&
      Method->getParamDecl(0)->getType()->isRValueReferenceType()) {
    State.Env.setValue(
        SrcLoc->getSyntheticField(PtrField),
        createNullPointer(PtrLoc.getType()->getPointeeType(), State.Env));
  }
}

void transferValue_SmartPointerReleaseCall(
    const CXXMemberCallExpr *MCE, const MatchFinder::MatchResult &Result,
    TransferState<PointerNullabilityLattice> &State) {
  RecordStorageLocation *Loc = getImplicitObjectLocation(*MCE, State.Env);
  if (Loc == nullptr) return;
  StorageLocation &PtrLoc = Loc->getSyntheticField(PtrField);

  if (auto *Val = cast_or_null<PointerValue>(State.Env.getValue(PtrLoc)))
    State.Env.setValue(*MCE, *Val);
  State.Env.setValue(
      PtrLoc, createNullPointer(PtrLoc.getType()->getPointeeType(), State.Env));
}

void transferValue_SmartPointerResetCall(
    const CXXMemberCallExpr *MCE, const MatchFinder::MatchResult &Result,
    TransferState<PointerNullabilityLattice> &State) {
  RecordStorageLocation *Loc = getImplicitObjectLocation(*MCE, State.Env);
  if (Loc == nullptr) return;
  StorageLocation &PtrLoc = Loc->getSyntheticField(PtrField);

  // Zero-arg and `nullptr_t` overloads, as well as single-argument constructor
  // with default argument.
  if (MCE->getNumArgs() == 0 ||
      (MCE->getNumArgs() == 1 && MCE->getArg(0)->getType()->isNullPtrType()) ||
      (MCE->getNumArgs() == 1 && MCE->getArg(0)->isDefaultArgument())) {
    State.Env.setValue(
        PtrLoc,
        createNullPointer(PtrLoc.getType()->getPointeeType(), State.Env));
    return;
  }

  if (Value *Val = State.Env.getValue(*MCE->getArg(0)))
    State.Env.setValue(PtrLoc, *Val);
}

void swapSmartPointers(RecordStorageLocation *Loc1, RecordStorageLocation *Loc2,
                       Environment &Env) {
  PointerValue *Val1 = getPointerValueFromSmartPointer(Loc1, Env);
  PointerValue *Val2 = getPointerValueFromSmartPointer(Loc2, Env);

  if (Loc1) setSmartPointerValue(*Loc1, Val2, Env);
  if (Loc2) setSmartPointerValue(*Loc2, Val1, Env);
}

void transferValue_SmartPointerMemberSwapCall(
    const CXXMemberCallExpr *MCE, const MatchFinder::MatchResult &Result,
    TransferState<PointerNullabilityLattice> &State) {
  swapSmartPointers(getImplicitObjectLocation(*MCE, State.Env),
                    cast_or_null<RecordStorageLocation>(
                        State.Env.getStorageLocation(*MCE->getArg(0))),
                    State.Env);
}

void transferValue_SmartPointerFreeSwapCall(
    const CallExpr *CE, const MatchFinder::MatchResult &Result,
    TransferState<PointerNullabilityLattice> &State) {
  swapSmartPointers(cast_or_null<RecordStorageLocation>(
                        State.Env.getStorageLocation(*CE->getArg(0))),
                    cast_or_null<RecordStorageLocation>(
                        State.Env.getStorageLocation(*CE->getArg(1))),
                    State.Env);
}

void transferValue_SmartPointerGetCall(
    const CXXMemberCallExpr *MCE, const MatchFinder::MatchResult &Result,
    TransferState<PointerNullabilityLattice> &State) {
  if (Value *Val = getPointerValueFromSmartPointer(
          getImplicitObjectLocation(*MCE, State.Env), State.Env))
    State.Env.setValue(*MCE, *Val);
}

void transferValue_SmartPointerFactoryCall(
    const CallExpr *CE, const MatchFinder::MatchResult &Result,
    TransferState<PointerNullabilityLattice> &State) {
  RecordStorageLocation &Loc = State.Env.getResultObjectLocation(*CE);
  // Create a `RecordValue`, associate it with the `Loc` and the expression.
  State.Env.setValue(*CE, refreshRecordValue(Loc, State.Env));
  StorageLocation &PtrLoc = Loc.getSyntheticField(PtrField);

  setToNonNullPointer(PtrLoc, State.Env);
}

void transferValue_SmartPointer(
    const Expr *PointerExpr, const MatchFinder::MatchResult &Result,
    TransferState<PointerNullabilityLattice> &State) {
  auto *Loc = cast_or_null<RecordStorageLocation>(
      State.Env.getStorageLocation(*PointerExpr));
  if (Loc == nullptr) {
    Loc = &cast<RecordStorageLocation>(
        State.Env.createStorageLocation(*PointerExpr));
    State.Env.setStorageLocation(*PointerExpr, *Loc);
  }

  StorageLocation &PtrLoc = Loc->getSyntheticField(PtrField);
  auto *Val = cast_or_null<PointerValue>(State.Env.getValue(PtrLoc));
  if (Val == nullptr) {
    Val = cast<PointerValue>(State.Env.createValue(PtrLoc.getType()));
    State.Env.setValue(PtrLoc, *Val);
  }

  initPointerFromTypeNullability(*Val, PointerExpr, State);
}

void transferValue_Pointer(const Expr *PointerExpr,
                           const MatchFinder::MatchResult &Result,
                           TransferState<PointerNullabilityLattice> &State) {
  auto *PointerVal = getPointerValueFromExpr(PointerExpr, State.Env);
  if (!PointerVal) return;

  initPointerFromTypeNullability(*PointerVal, PointerExpr, State);

  if (const auto *Cast = dyn_cast<CastExpr>(PointerExpr);
      Cast && Cast->getCastKind() == CK_LValueToRValue) {
    PointerValue *NewPointerVal = unpackPointerValue(*PointerVal, State.Env);
    if (!NewPointerVal) return;
    if (StorageLocation *Loc =
            State.Env.getStorageLocation(*Cast->getSubExpr()))
      State.Env.setValue(*Loc, *NewPointerVal);
    State.Env.setValue(*PointerExpr, *NewPointerVal);
  }
}

// TODO(b/233582219): Implement promotion of nullability for initially
// unknown pointers when there is evidence that it is nullable, for example
// when the pointer is compared to nullptr, or casted to boolean.
void transferValue_NullCheckComparison(
    const BinaryOperator *BinaryOp, const MatchFinder::MatchResult &result,
    TransferState<PointerNullabilityLattice> &State) {
  auto &A = State.Env.arena();

  auto *LHS = getPointerValueFromExpr(BinaryOp->getLHS(), State.Env);
  auto *RHS = getPointerValueFromExpr(BinaryOp->getRHS(), State.Env);

  if (!LHS || !RHS) return;
  if (!hasPointerNullState(*LHS) || !hasPointerNullState(*RHS)) return;

  auto *LHSNull = getPointerNullState(*LHS).IsNull;
  auto *RHSNull = getPointerNullState(*RHS).IsNull;

  // If the null state of either pointer is "top", the result of the comparison
  // is a top bool, and we don't have any knowledge we can add to the flow
  // condition.
  if (LHSNull == nullptr || RHSNull == nullptr) {
    State.Env.setValue(*BinaryOp, A.makeTopValue());
    return;
  }

  // Special case: Are we comparing against `nullptr`?
  // We can avoid modifying the flow condition in this case and simply propagate
  // the nullability of the other operand (potentially with a negation).
  if (LHSNull == &A.makeLiteral(true)) {
    if (BinaryOp->getOpcode() == BO_EQ)
      State.Env.setValue(*BinaryOp, A.makeBoolValue(*RHSNull));
    else
      State.Env.setValue(*BinaryOp, A.makeBoolValue(A.makeNot(*RHSNull)));
    return;
  }
  if (RHSNull == &A.makeLiteral(true)) {
    if (BinaryOp->getOpcode() == BO_EQ)
      State.Env.setValue(*BinaryOp, A.makeBoolValue(*LHSNull));
    else
      State.Env.setValue(*BinaryOp, A.makeBoolValue(A.makeNot(*LHSNull)));
    return;
  }

  // Boolean representing the comparison between the two pointer values,
  // automatically created by the dataflow framework.
  auto &PointerComparison =
      cast<BoolValue>(State.Env.getValue(*BinaryOp))->formula();

  CHECK(BinaryOp->getOpcode() == BO_EQ || BinaryOp->getOpcode() == BO_NE);
  auto &PointerEQ = BinaryOp->getOpcode() == BO_EQ
                        ? PointerComparison
                        : A.makeNot(PointerComparison);
  auto &PointerNE = BinaryOp->getOpcode() == BO_EQ
                        ? A.makeNot(PointerComparison)
                        : PointerComparison;

  // nullptr == nullptr
  State.Env.assume(A.makeImplies(A.makeAnd(*LHSNull, *RHSNull), PointerEQ));
  // nullptr != notnull
  State.Env.assume(
      A.makeImplies(A.makeAnd(*LHSNull, A.makeNot(*RHSNull)), PointerNE));
  // notnull != nullptr
  State.Env.assume(
      A.makeImplies(A.makeAnd(A.makeNot(*LHSNull), *RHSNull), PointerNE));
}

void transferValue_NullCheckImplicitCastPtrToBool(
    const Expr *CastExpr, const MatchFinder::MatchResult &,
    TransferState<PointerNullabilityLattice> &State) {
  auto &A = State.Env.arena();
  auto *PointerVal =
      getPointerValueFromExpr(CastExpr->IgnoreImplicit(), State.Env);
  if (!PointerVal) return;

  auto Nullability = getPointerNullState(*PointerVal);
  if (Nullability.IsNull != nullptr)
    State.Env.setValue(*CastExpr,
                       A.makeBoolValue(A.makeNot(*Nullability.IsNull)));
  else
    State.Env.setValue(*CastExpr, A.makeTopValue());
}

void initializeOutputParameter(const Expr *Arg, dataflow::Environment &Env,
                               QualType ParamTy) {
  // When a function has an "output parameter" - a non-const pointer or
  // reference to a pointer of unknown nullability - assume that the function
  // may set the pointer to non-null.
  //
  // For example, in the following code sequence we assume that the function may
  // modify the pointer in a way that makes a subsequent dereference safe:
  //
  //   void maybeModify(int ** _Nonnull);
  //
  //   int *p = nullptr;
  //   initializePointer(&p);
  //   *p; // safe

  if (ParamTy.isNull()) return;
  if (ParamTy->getPointeeType().isNull()) return;
  if (!isSupportedRawPointerType(ParamTy->getPointeeType())) return;
  if (ParamTy->getPointeeType().isConstQualified()) return;

  // TODO(b/298200521): This should extend support to annotations that suggest
  // different in/out state
  TypeNullability InnerNullability =
      getNullabilityAnnotationsFromType(ParamTy->getPointeeType());
  if (InnerNullability.front().concrete() != NullabilityKind::Unspecified)
    return;

  StorageLocation *Loc = nullptr;
  if (ParamTy->isPointerType()) {
    if (PointerValue *OuterPointer = getPointerValueFromExpr(Arg, Env))
      Loc = &OuterPointer->getPointeeLoc();
  } else if (ParamTy->isReferenceType()) {
    Loc = Env.getStorageLocation(*Arg);
  }
  if (Loc == nullptr) return;

  auto *InnerPointer =
      cast<PointerValue>(Env.createValue(ParamTy->getPointeeType()));
  initPointerNullState(*InnerPointer, Env.getDataflowAnalysisContext(),
                       NullabilityKind::Unspecified);

  Env.setValue(*Loc, *InnerPointer);
}

void transferValue_CallExpr(const CallExpr *CallExpr,
                            const MatchFinder::MatchResult &Result,
                            TransferState<PointerNullabilityLattice> &State) {
  // The dataflow framework itself does not create values for `CallExpr`s.
  // However, we need these in some cases, so we produce them ourselves.

  StorageLocation *Loc = nullptr;
  if (CallExpr->isGLValue()) {
    // The function returned a reference. Create a storage location for the
    // expression so that if code creates a pointer from the reference, we will
    // produce a `PointerValue`.
    Loc = State.Env.getStorageLocation(*CallExpr);
    if (!Loc) {
      // This is subtle: We call `createStorageLocation(QualType)`, not
      // `createStorageLocation(const Expr &)`, so that we create a new
      // storage location every time.
      Loc = &State.Env.createStorageLocation(CallExpr->getType());
      State.Env.setStorageLocation(*CallExpr, *Loc);
    }
  }

  if (isSupportedRawPointerType(CallExpr->getType())) {
    // Create a pointer so that we can attach nullability to it and have the
    // nullability propagate with the pointer.
    auto *PointerVal = getPointerValueFromExpr(CallExpr, State.Env);
    if (!PointerVal) {
      PointerVal =
          cast<PointerValue>(State.Env.createValue(CallExpr->getType()));
    }
    initPointerFromTypeNullability(*PointerVal, CallExpr, State);

    if (Loc != nullptr)
      State.Env.setValue(*Loc, *PointerVal);
    else
      // `Loc` is set iff `CallExpr` is a glvalue, so we know here that it must
      // be a prvalue.
      State.Env.setValue(*CallExpr, *PointerVal);
  }

  // Make output parameters (with unknown nullability) initialized to unknown.
  const auto *FuncDecl = CallExpr->getDirectCallee();
  if (!FuncDecl) return;
  if (FuncDecl->getNumParams() != CallExpr->getNumArgs()) return;
  if (auto *II = FuncDecl->getDeclName().getAsIdentifierInfo();
      II && II->isStr("__assert_nullability")) {
    return;
  }
  for (unsigned i = 0; i < CallExpr->getNumArgs(); ++i) {
    const auto *Arg = CallExpr->getArg(i);
    initializeOutputParameter(Arg, State.Env,
                              FuncDecl->getParamDecl(i)->getType());
  }
}

void transferValue_AccessorCall(
    const CXXMemberCallExpr *MCE, const MatchFinder::MatchResult &Result,
    TransferState<PointerNullabilityLattice> &State) {
  auto *member = Result.Nodes.getNodeAs<clang::ValueDecl>("member-decl");
  PointerValue *PointerVal = nullptr;
  if (dataflow::RecordStorageLocation *RecordLoc =
          dataflow::getImplicitObjectLocation(*MCE, State.Env)) {
    StorageLocation *Loc = RecordLoc->getChild(*member);
    PointerVal = dyn_cast_or_null<PointerValue>(State.Env.getValue(*Loc));
  }
  if (!PointerVal) {
    // Use value that may have been set by the builtin transfer function or by
    // `ensurePointerHasValue()`.
    PointerVal = getPointerValueFromExpr(MCE, State.Env);
  }
  if (PointerVal) {
    State.Env.setValue(*MCE, *PointerVal);
    initPointerFromTypeNullability(*PointerVal, MCE, State);
  }
}

void transferValue_ConstMemberCall(
    const CXXMemberCallExpr *MCE, const MatchFinder::MatchResult &Result,
    TransferState<PointerNullabilityLattice> &State) {
  if (!isSupportedRawPointerType(MCE->getType()) || !MCE->isPRValue()) {
    // We can't handle it as a special case, but still need to handle it.
    transferValue_CallExpr(MCE, Result, State);
    return;
  }
  dataflow::RecordStorageLocation *RecordLoc =
      dataflow::getImplicitObjectLocation(*MCE, State.Env);
  if (RecordLoc == nullptr) {
    // We can't handle it as a special case, but still need to handle it.
    transferValue_CallExpr(MCE, Result, State);
    return;
  }
  PointerValue *PointerVal =
      State.Lattice.getConstMethodReturnValue(*RecordLoc, MCE, State.Env);
  if (PointerVal) {
    State.Env.setValue(*MCE, *PointerVal);
    initPointerFromTypeNullability(*PointerVal, MCE, State);
  }
}

void transferValue_NonConstMemberCall(
    const CXXMemberCallExpr *MCE, const MatchFinder::MatchResult &Result,
    TransferState<PointerNullabilityLattice> &State) {
  // When a non-const member function is called, reset all pointer-type fields
  // of the implicit object.
  if (dataflow::RecordStorageLocation *RecordLoc =
          dataflow::getImplicitObjectLocation(*MCE, State.Env)) {
    for (const auto [Field, FieldLoc] : RecordLoc->children()) {
      if (!isSupportedRawPointerType(Field->getType())) continue;
      Value *V = State.Env.createValue(Field->getType());
      State.Env.setValue(*FieldLoc, *V);
    }
    State.Lattice.clearConstMethodReturnValues(*RecordLoc);
  }
  // The nullability of the Expr itself still needs to be handled.
  transferValue_CallExpr(MCE, Result, State);
}

void transferType_DeclRefExpr(const DeclRefExpr *DRE,
                              const MatchFinder::MatchResult &MR,
                              TransferState<PointerNullabilityLattice> &State) {
  computeNullability(DRE, State, [&] {
    auto Nullability = getNullabilityAnnotationsFromType(DRE->getType());
    State.Lattice.overrideNullabilityFromDecl(DRE->getDecl(), Nullability);
    return Nullability;
  });
}

void transferType_MemberExpr(const MemberExpr *ME,
                             const MatchFinder::MatchResult &MR,
                             TransferState<PointerNullabilityLattice> &State) {
  computeNullability(ME, State, [&]() {
    auto BaseNullability = getNullabilityForChild(ME->getBase(), State);
    QualType MemberType = ME->getType();
    // When a MemberExpr is a part of a member function call
    // (a child of CXXMemberCallExpr), the MemberExpr models a
    // partially-applied member function, which isn't a real C++ construct.
    // The AST does not provide rich type information for such MemberExprs.
    // Instead, the AST specifies a placeholder type, specifically
    // BuiltinType::BoundMember. So we have to look at the type of the member
    // function declaration.
    if (ME->hasPlaceholderType(BuiltinType::BoundMember)) {
      MemberType = ME->getMemberDecl()->getType();
    }
    auto Nullability = substituteNullabilityAnnotationsInClassTemplate(
        MemberType, BaseNullability, ME->getBase()->getType());
    State.Lattice.overrideNullabilityFromDecl(ME->getMemberDecl(), Nullability);
    return Nullability;
  });
}

void transferType_MemberCallExpr(
    const CXXMemberCallExpr *MCE, const MatchFinder::MatchResult &MR,
    TransferState<PointerNullabilityLattice> &State) {
  computeNullability(MCE, State, [&]() {
    return ArrayRef(getNullabilityForChild(MCE->getCallee(), State))
        .take_front(countPointersInType(MCE))
        .vec();
  });
}

void transferType_CastExpr(const CastExpr *CE,
                           const MatchFinder::MatchResult &MR,
                           TransferState<PointerNullabilityLattice> &State) {
  computeNullability(CE, State, [&]() -> TypeNullability {
    // Most casts that can convert ~unrelated types drop nullability in general.
    // As a special case, preserve nullability of outer pointer types.
    // For example, int* p; (void*)p; is a BitCast, but preserves nullability.
    auto PreserveTopLevelPointers = [&](TypeNullability V) {
      auto ArgNullability = getNullabilityForChild(CE->getSubExpr(), State);
      const PointerType *ArgType = dyn_cast<PointerType>(
          CE->getSubExpr()->getType().getCanonicalType().getTypePtr());
      const PointerType *CastType =
          dyn_cast<PointerType>(CE->getType().getCanonicalType().getTypePtr());
      for (int I = 0; ArgType && CastType; ++I) {
        V[I] = ArgNullability[I];
        ArgType = dyn_cast<PointerType>(ArgType->getPointeeType().getTypePtr());
        CastType =
            dyn_cast<PointerType>(CastType->getPointeeType().getTypePtr());
      }
      return V;
    };

    switch (CE->getCastKind()) {
      // Casts between unrelated types: we can't say anything about nullability.
      case CK_LValueBitCast:
      case CK_BitCast:
      case CK_LValueToRValueBitCast:
        return PreserveTopLevelPointers(unspecifiedNullability(CE));

      // Casts between equivalent types.
      case CK_LValueToRValue:
      case CK_NoOp:
      case CK_AtomicToNonAtomic:
      case CK_NonAtomicToAtomic:
      case CK_AddressSpaceConversion:
        return getNullabilityForChild(CE->getSubExpr(), State);

      // Controlled conversions between types
      // TODO: these should be doable somehow
      case CK_BaseToDerived:
      case CK_DerivedToBase:
      case CK_UncheckedDerivedToBase:
        return PreserveTopLevelPointers(unspecifiedNullability(CE));
      case CK_UserDefinedConversion:
      case CK_ConstructorConversion:
        return unspecifiedNullability(CE);

      case CK_Dynamic: {
        auto Result = unspecifiedNullability(CE);
        // A dynamic_cast to pointer is null if the runtime check fails.
        if (isa<PointerType>(CE->getType().getCanonicalType()))
          Result.front() = NullabilityKind::Nullable;
        return Result;
      }

      // Primitive values have no nullability.
      case CK_ToVoid:
      case CK_MemberPointerToBoolean:
      case CK_PointerToBoolean:
      case CK_PointerToIntegral:
      case CK_IntegralCast:
      case CK_IntegralToBoolean:
      case CK_IntegralToFloating:
      case CK_FloatingToFixedPoint:
      case CK_FixedPointToFloating:
      case CK_FixedPointCast:
      case CK_FixedPointToIntegral:
      case CK_IntegralToFixedPoint:
      case CK_FixedPointToBoolean:
      case CK_FloatingToIntegral:
      case CK_FloatingToBoolean:
      case CK_BooleanToSignedIntegral:
      case CK_FloatingCast:
      case CK_FloatingRealToComplex:
      case CK_FloatingComplexToReal:
      case CK_FloatingComplexToBoolean:
      case CK_FloatingComplexCast:
      case CK_FloatingComplexToIntegralComplex:
      case CK_IntegralRealToComplex:
      case CK_IntegralComplexToReal:
      case CK_IntegralComplexToBoolean:
      case CK_IntegralComplexCast:
      case CK_IntegralComplexToFloatingComplex:
        return {};

      // This can definitely be null!
      case CK_NullToPointer: {
        auto Nullability = getNullabilityAnnotationsFromType(CE->getType());
        // Despite the name `NullToPointer`, the destination type of the cast
        // may be `nullptr_t` (which is, itself, not a pointer type).
        if (!CE->getType()->isNullPtrType())
          Nullability.front() = NullabilityKind::Nullable;
        return Nullability;
      }

      // Pointers out of thin air, who knows?
      case CK_IntegralToPointer:
        return unspecifiedNullability(CE);

      // Decayed objects are never null.
      case CK_ArrayToPointerDecay:
      case CK_FunctionToPointerDecay:
        return prepend(NullabilityKind::NonNull,
                       getNullabilityForChild(CE->getSubExpr(), State));

      // Despite its name, the result type of `BuiltinFnToFnPtr` is a function,
      // not a function pointer, so nullability doesn't change.
      case CK_BuiltinFnToFnPtr:
        return getNullabilityForChild(CE->getSubExpr(), State);

      // TODO: what is our model of member pointers?
      case CK_BaseToDerivedMemberPointer:
      case CK_DerivedToBaseMemberPointer:
      case CK_NullToMemberPointer:
      case CK_ReinterpretMemberPointer:
      case CK_ToUnion:  // and unions?
        return unspecifiedNullability(CE);

      // TODO: Non-C/C++ constructs, do we care about these?
      case CK_CPointerToObjCPointerCast:
      case CK_ObjCObjectLValueCast:
      case CK_MatrixCast:
      case CK_VectorSplat:
      case CK_BlockPointerToObjCPointerCast:
      case CK_AnyPointerToBlockPointerCast:
      case CK_ARCProduceObject:
      case CK_ARCConsumeObject:
      case CK_ARCReclaimReturnedObject:
      case CK_ARCExtendBlockObject:
      case CK_CopyAndAutoreleaseBlockObject:
      case CK_ZeroToOCLOpaqueType:
      case CK_IntToOCLSampler:
        return unspecifiedNullability(CE);

      case CK_Dependent:
        CHECK(false) << "Shouldn't see dependent casts here?";
    }
  });
}

void transferType_MaterializeTemporaryExpr(
    const MaterializeTemporaryExpr *MTE, const MatchFinder::MatchResult &MR,
    TransferState<PointerNullabilityLattice> &State) {
  computeNullability(MTE, State, [&]() {
    return getNullabilityForChild(MTE->getSubExpr(), State);
  });
}

void transferType_CXXBindTemporaryExpr(
    const CXXBindTemporaryExpr *BTE, const MatchFinder::MatchResult &MR,
    TransferState<PointerNullabilityLattice> &State) {
  computeNullability(BTE, State, [&]() {
    return getNullabilityForChild(BTE->getSubExpr(), State);
  });
}

void transferType_CallExpr(const CallExpr *CE,
                           const MatchFinder::MatchResult &MR,
                           TransferState<PointerNullabilityLattice> &State) {
  // TODO: Check CallExpr arguments in the diagnoser against the nullability of
  // parameters.
  computeNullability(CE, State, [&]() {
    // TODO(mboehme): Instead of relying on Clang to propagate nullability sugar
    // to the `CallExpr`'s type, we should extract nullability directly from the
    // callee `Expr .
    auto Nullability =
        substituteNullabilityAnnotationsInFunctionTemplate(CE->getType(), CE);
    if (!Nullability.empty()) {
      State.Lattice.overrideNullabilityFromDecl(CE->getCalleeDecl(),
                                                Nullability);
    }
    return Nullability;
  });
}

void transferType_UnaryOperator(
    const UnaryOperator *UO, const MatchFinder::MatchResult &MR,
    TransferState<PointerNullabilityLattice> &State) {
  computeNullability(UO, State, [&]() -> TypeNullability {
    switch (UO->getOpcode()) {
      case UO_AddrOf:
        return prepend(NullabilityKind::NonNull,
                       getNullabilityForChild(UO->getSubExpr(), State));
      case UO_Deref:
        return ArrayRef(getNullabilityForChild(UO->getSubExpr(), State))
            .drop_front()
            .vec();

      case UO_PostInc:
      case UO_PostDec:
      case UO_PreInc:
      case UO_PreDec:
      case UO_Plus:
      case UO_Minus:
      case UO_Not:
      case UO_LNot:
      case UO_Real:
      case UO_Imag:
      case UO_Extension:
        return getNullabilityForChild(UO->getSubExpr(), State);

      case UO_Coawait:
        // TODO: work out what to do here!
        return unspecifiedNullability(UO);
    }
  });
}

void transferType_NewExpr(const CXXNewExpr *NE,
                          const MatchFinder::MatchResult &MR,
                          TransferState<PointerNullabilityLattice> &State) {
  computeNullability(NE, State, [&]() {
    TypeNullability result = getNullabilityAnnotationsFromType(NE->getType());
    result.front() = NE->shouldNullCheckAllocation() ? NullabilityKind::Nullable
                                                     : NullabilityKind::NonNull;
    return result;
  });
}

void transferType_ArraySubscriptExpr(
    const ArraySubscriptExpr *ASE, const MatchFinder::MatchResult &MR,
    TransferState<PointerNullabilityLattice> &State) {
  computeNullability(ASE, State, [&]() {
    auto &BaseNullability = getNullabilityForChild(ASE->getBase(), State);
    QualType BaseType = ASE->getBase()->getType();
    CHECK(isSupportedRawPointerType(BaseType) || BaseType->isVectorType());
    return isSupportedRawPointerType(BaseType)
               ? ArrayRef(BaseNullability).slice(1).vec()
               : BaseNullability;
  });
}

void transferType_ThisExpr(const CXXThisExpr *TE,
                           const MatchFinder::MatchResult &MR,
                           TransferState<PointerNullabilityLattice> &State) {
  computeNullability(TE, State, [&]() {
    TypeNullability result = getNullabilityAnnotationsFromType(TE->getType());
    result.front() = NullabilityKind::NonNull;
    return result;
  });
}

auto buildTypeTransferer() {
  return CFGMatchSwitchBuilder<TransferState<PointerNullabilityLattice>>()
      .CaseOfCFGStmt<DeclRefExpr>(ast_matchers::declRefExpr(),
                                  transferType_DeclRefExpr)
      .CaseOfCFGStmt<MemberExpr>(ast_matchers::memberExpr(),
                                 transferType_MemberExpr)
      .CaseOfCFGStmt<CXXMemberCallExpr>(ast_matchers::cxxMemberCallExpr(),
                                        transferType_MemberCallExpr)
      .CaseOfCFGStmt<CastExpr>(ast_matchers::castExpr(), transferType_CastExpr)
      .CaseOfCFGStmt<MaterializeTemporaryExpr>(
          ast_matchers::materializeTemporaryExpr(),
          transferType_MaterializeTemporaryExpr)
      .CaseOfCFGStmt<CXXBindTemporaryExpr>(ast_matchers::cxxBindTemporaryExpr(),
                                           transferType_CXXBindTemporaryExpr)
      .CaseOfCFGStmt<CallExpr>(ast_matchers::callExpr(), transferType_CallExpr)
      .CaseOfCFGStmt<UnaryOperator>(ast_matchers::unaryOperator(),
                                    transferType_UnaryOperator)
      .CaseOfCFGStmt<CXXNewExpr>(ast_matchers::cxxNewExpr(),
                                 transferType_NewExpr)
      .CaseOfCFGStmt<ArraySubscriptExpr>(ast_matchers::arraySubscriptExpr(),
                                         transferType_ArraySubscriptExpr)
      .CaseOfCFGStmt<CXXThisExpr>(ast_matchers::cxxThisExpr(),
                                  transferType_ThisExpr)
      .Build();
}

auto buildValueTransferer() {
  // The value transfer functions must establish:
  // - if we're transferring over an Expr
  // - and the Expr has a supported pointer type
  // - and the Expr's value is modeled by the framework (or this analysis)
  // - then the PointerValue has nullability properties (is_null/from_nullable)
  return CFGMatchSwitchBuilder<TransferState<PointerNullabilityLattice>>()
      // Handles initialization of the null states of pointers.
      .CaseOfCFGStmt<Expr>(isAddrOf(), transferValue_NotNullPointer)
      // TODO(mboehme): I believe we should be able to move handling of null
      // pointers to the non-flow-sensitive part of the analysis.
      .CaseOfCFGStmt<Expr>(isNullPointerLiteral(), transferValue_NullPointer)
      .CaseOfCFGStmt<CXXScalarValueInitExpr>(isRawPointerValueInit(),
                                             transferValue_NullPointer)
      .CaseOfCFGStmt<CXXConstructExpr>(isSmartPointerConstructor(),
                                       transferValue_SmartPointerConstructor)
      .CaseOfCFGStmt<CXXOperatorCallExpr>(isSmartPointerAssignment(),
                                          transferValue_SmartPointerAssignment)
      .CaseOfCFGStmt<CXXMemberCallExpr>(isSmartPointerMethodCall("release"),
                                        transferValue_SmartPointerReleaseCall)
      .CaseOfCFGStmt<CXXMemberCallExpr>(isSmartPointerMethodCall("reset"),
                                        transferValue_SmartPointerResetCall)
      .CaseOfCFGStmt<CXXMemberCallExpr>(
          isSmartPointerMethodCall("swap"),
          transferValue_SmartPointerMemberSwapCall)
      .CaseOfCFGStmt<CallExpr>(isSmartPointerFreeSwapCall(),
                               transferValue_SmartPointerFreeSwapCall)
      .CaseOfCFGStmt<CXXMemberCallExpr>(isSmartPointerMethodCall("get"),
                                        transferValue_SmartPointerGetCall)
      .CaseOfCFGStmt<CallExpr>(isSmartPointerFactoryCall(),
                               transferValue_SmartPointerFactoryCall)
      .CaseOfCFGStmt<CXXMemberCallExpr>(isSupportedPointerAccessorCall(),
                                        transferValue_AccessorCall)
      .CaseOfCFGStmt<CXXMemberCallExpr>(isZeroParamConstMemberCall(),
                                        transferValue_ConstMemberCall)
      .CaseOfCFGStmt<CXXMemberCallExpr>(isNonConstMemberCall(),
                                        transferValue_NonConstMemberCall)
      .CaseOfCFGStmt<CallExpr>(isCallExpr(), transferValue_CallExpr)
      .CaseOfCFGStmt<Expr>(isSmartPointerGlValue(), transferValue_SmartPointer)
      .CaseOfCFGStmt<Expr>(isPointerExpr(), transferValue_Pointer)
      // Handles comparison between 2 pointers.
      .CaseOfCFGStmt<BinaryOperator>(isPointerCheckBinOp(),
                                     transferValue_NullCheckComparison)
      // Handles checking of pointer as boolean.
      .CaseOfCFGStmt<Expr>(isImplicitCastPointerToBool(),
                           transferValue_NullCheckImplicitCastPtrToBool)
      .Build();
}

// Ensure all prvalue expressions of pointer type have a `PointerValue`
// associated with them so we can track nullability through them.
void ensurePointerHasValue(const CFGElement &Elt, Environment &Env) {
  auto S = Elt.getAs<CFGStmt>();
  if (!S) return;

  auto *E = dyn_cast<Expr>(S->getStmt());
  if (E == nullptr || !E->isPRValue() ||
      !isSupportedRawPointerType(E->getType()))
    return;

  if (Env.getValue(*E) == nullptr)
    // `createValue()` always produces a value for pointer types.
    Env.setValue(*E, *Env.createValue(E->getType()));
}

}  // namespace

PointerNullabilityAnalysis::PointerNullabilityAnalysis(ASTContext &Context,
                                                       Environment &Env)
    : DataflowAnalysis<PointerNullabilityAnalysis, PointerNullabilityLattice>(
          Context),
      TypeTransferer(buildTypeTransferer()),
      ValueTransferer(buildValueTransferer()) {
  Env.getDataflowAnalysisContext().setSyntheticFieldCallback(
      [](QualType Ty) -> llvm::StringMap<QualType> {
        QualType RawPointerTy = underlyingRawPointerType(Ty);
        if (RawPointerTy.isNull()) return {};
        return {{PtrField, RawPointerTy}};
      });
}

PointerTypeNullability PointerNullabilityAnalysis::assignNullabilityVariable(
    const ValueDecl *D, dataflow::Arena &A) {
  auto [It, Inserted] = NFS.DeclTopLevelNullability.try_emplace(D);
  if (Inserted) It->second = PointerTypeNullability::createSymbolic(A);
  return It->second;
}

void PointerNullabilityAnalysis::transfer(const CFGElement &Elt,
                                          PointerNullabilityLattice &Lattice,
                                          Environment &Env) {
  TransferState<PointerNullabilityLattice> State(Lattice, Env);

  ensurePointerHasValue(Elt, Env);
  TypeTransferer(Elt, getASTContext(), State);
  ValueTransferer(Elt, getASTContext(), State);
}

static const Formula *mergeFormulas(const Formula *Bool1,
                                    const Environment &Env1,
                                    const Formula *Bool2,
                                    const Environment &Env2,
                                    Environment &MergedEnv) {
  if (Bool1 == Bool2) {
    return Bool1;
  }

  if (Bool1 == nullptr || Bool2 == nullptr) return nullptr;

  auto &A = MergedEnv.arena();

  // If `Bool1` and `Bool2` is constrained to the same true / false value, that
  // can serve as the return value - this simplifies the flow condition tracked
  // in `MergedEnv`.  Otherwise, information about which path was taken is used
  // to associate the return value with `Bool1` and `Bool2`.
  if (Env1.proves(*Bool1)) {
    if (Env2.proves(*Bool2)) {
      return &A.makeLiteral(true);
    }
  } else if (Env1.proves(A.makeNot(*Bool1)) && Env2.proves(A.makeNot(*Bool2))) {
    return &A.makeLiteral(false);
  }

  auto &MergedBool = A.makeAtomRef(A.makeAtom());
  // TODO(b/233582219): Flow conditions are not necessarily mutually
  // exclusive, a fix is in order: https://reviews.llvm.org/D130270. Update
  // this section when the patch is commited.
  auto FC1 = Env1.getFlowConditionToken();
  auto FC2 = Env2.getFlowConditionToken();
  MergedEnv.assume(A.makeOr(
      A.makeAnd(A.makeAtomRef(FC1), A.makeEquals(MergedBool, *Bool1)),
      A.makeAnd(A.makeAtomRef(FC2), A.makeEquals(MergedBool, *Bool2))));
  return &MergedBool;
}

bool PointerNullabilityAnalysis::merge(QualType Type, const Value &Val1,
                                       const Environment &Env1,
                                       const Value &Val2,
                                       const Environment &Env2,
                                       Value &MergedVal,
                                       Environment &MergedEnv) {
  if (!isSupportedRawPointerType(Type)) {
    return false;
  }

  if (!hasPointerNullState(cast<PointerValue>(Val1)) ||
      !hasPointerNullState(cast<PointerValue>(Val2))) {
    return false;
  }

  auto &MergedPointerVal = cast<PointerValue>(MergedVal);
  DataflowAnalysisContext &Ctx = MergedEnv.getDataflowAnalysisContext();
  auto &A = MergedEnv.arena();

  auto Nullability1 = getPointerNullState(cast<PointerValue>(Val1));
  auto Nullability2 = getPointerNullState(cast<PointerValue>(Val2));

  // Initialize `MergedPointerVal`'s nullability properties with atoms. These
  // are potentially replaced with "top" below.
  assert(!hasPointerNullState(MergedPointerVal));
  initPointerNullState(MergedPointerVal, Ctx);
  auto MergedNullability = getPointerNullState(MergedPointerVal);
  assert(MergedNullability.FromNullable != nullptr);
  assert(MergedNullability.IsNull != nullptr);

  if (auto *FromNullable =
          mergeFormulas(Nullability1.FromNullable, Env1,
                        Nullability2.FromNullable, Env2, MergedEnv))
    MergedEnv.assume(
        A.makeEquals(*MergedNullability.FromNullable, *FromNullable));
  else
    forgetFromNullable(MergedPointerVal, Ctx);

  if (auto *Null = mergeFormulas(Nullability1.IsNull, Env1, Nullability2.IsNull,
                                 Env2, MergedEnv))
    MergedEnv.assume(A.makeEquals(*MergedNullability.IsNull, *Null));
  else
    forgetIsNull(MergedPointerVal, Ctx);

  return true;
}

ComparisonResult PointerNullabilityAnalysis::compare(QualType Type,
                                                     const Value &Val1,
                                                     const Environment &Env1,
                                                     const Value &Val2,
                                                     const Environment &Env2) {
  if (const auto *PointerVal1 = dyn_cast<PointerValue>(&Val1)) {
    const auto &PointerVal2 = cast<PointerValue>(Val2);

    if (&PointerVal1->getPointeeLoc() != &PointerVal2.getPointeeLoc())
      return ComparisonResult::Different;

    if (hasPointerNullState(*PointerVal1) != hasPointerNullState(PointerVal2))
      return ComparisonResult::Different;

    if (!hasPointerNullState(*PointerVal1)) return ComparisonResult::Same;

    auto Nullability1 = getPointerNullState(*PointerVal1);
    auto Nullability2 = getPointerNullState(PointerVal2);

    // Ideally, we would be checking for equivalence of formulas, but that's
    // expensive, so we simply check for identity instead.
    return Nullability1.FromNullable == Nullability2.FromNullable &&
                   Nullability1.IsNull == Nullability2.IsNull
               ? ComparisonResult::Same
               : ComparisonResult::Different;
  }

  return ComparisonResult::Unknown;
}

// Returns the result of widening a nullability property.
// `Prev` is the formula in the previous iteration, `Cur` is the formula in the
// current iteration.
// If the two formulas are equivalent (though not necessarily identical),
// returns `Cur`, as this is the formula that is appropriate to use in the
// current environment (where we will produce the widened pointer). Otherwise,
// returns null, to indicate that the property should be widened to "top".
static const Formula *widenNullabilityProperty(const Formula *Prev,
                                               const Environment &PrevEnv,
                                               const Formula *Cur,
                                               Environment &CurEnv) {
  if (Prev == Cur) return Cur;
  if (Prev == nullptr || Cur == nullptr) return nullptr;

  Arena &A = CurEnv.arena();

  if (PrevEnv.proves(*Prev)) {
    if (CurEnv.proves(*Cur)) return Cur;
  } else if (PrevEnv.proves(A.makeNot(*Prev)) &&
             CurEnv.proves(A.makeNot(*Cur))) {
    return Cur;
  }

  return nullptr;
}

Value *PointerNullabilityAnalysis::widen(QualType Type, Value &Prev,
                                         const Environment &PrevEnv,
                                         Value &Current,
                                         Environment &CurrentEnv) {
  // Widen pointers to a pointer with a "top" storage location.
  if (auto *PrevPtr = dyn_cast<PointerValue>(&Prev)) {
    auto &CurPtr = cast<PointerValue>(Current);

    DataflowAnalysisContext &DACtx = CurrentEnv.getDataflowAnalysisContext();
    assert(&PrevEnv.getDataflowAnalysisContext() == &DACtx);

    if (!hasPointerNullState(*PrevPtr) || !hasPointerNullState(CurPtr))
      return nullptr;

    auto [FromNullablePrev, NullPrev] = getPointerNullState(*PrevPtr);
    auto [FromNullableCur, NullCur] = getPointerNullState(CurPtr);

    const Formula *FromNullableWidened = widenNullabilityProperty(
        FromNullablePrev, PrevEnv, FromNullableCur, CurrentEnv);
    const Formula *NullWidened =
        widenNullabilityProperty(NullPrev, PrevEnv, NullCur, CurrentEnv);

    // Is `PrevPtr` already equivalent to the widened pointer we are about to
    // produce? If so, return `PrevPtr` to signal this.
    if (&PrevPtr->getPointeeLoc() ==
            &getTopStorageLocation(DACtx, PrevPtr->getPointeeLoc().getType()) &&
        // Check whether
        // - the previous nullability property is equivalent to the current
        //   property (in which case the widened property is non-null), or
        // - the previous nullability property is already "top" (i.e. null)
        (FromNullableWidened != nullptr || FromNullablePrev == nullptr) &&
        (NullWidened != nullptr || NullPrev == nullptr)) {
      return PrevPtr;
    }

    // Widen the nullability properties.
    auto &WidenedPtr = CurrentEnv.create<PointerValue>(
        getTopStorageLocation(DACtx, CurPtr.getPointeeLoc().getType()));
    initPointerNullState(WidenedPtr, DACtx);
    auto WidenedNullability = getPointerNullState(WidenedPtr);
    assert(WidenedNullability.FromNullable != nullptr);
    assert(WidenedNullability.IsNull != nullptr);

    auto &A = CurrentEnv.arena();
    if (FromNullableWidened != nullptr)
      CurrentEnv.assume(
          A.makeEquals(*WidenedNullability.FromNullable, *FromNullableWidened));
    else
      forgetFromNullable(WidenedPtr, DACtx);
    if (NullWidened != nullptr)
      CurrentEnv.assume(A.makeEquals(*WidenedNullability.IsNull, *NullWidened));
    else
      forgetIsNull(WidenedPtr, DACtx);

    return &WidenedPtr;
  }

  return nullptr;
}

StorageLocation &PointerNullabilityAnalysis::getTopStorageLocation(
    DataflowAnalysisContext &DACtx, QualType Ty) {
  auto [It, Inserted] = TopStorageLocations.try_emplace(Ty, nullptr);
  if (Inserted) It->second = &DACtx.createStorageLocation(Ty);
  return *It->second;
}

}  // namespace clang::tidy::nullability
