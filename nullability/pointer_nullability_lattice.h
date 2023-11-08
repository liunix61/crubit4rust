// Part of the Crubit project, under the Apache License v2.0 with LLVM
// Exceptions. See /LICENSE for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef CRUBIT_NULLABILITY_POINTER_NULLABILITY_LATTICE_H_
#define CRUBIT_NULLABILITY_POINTER_NULLABILITY_LATTICE_H_

#include <functional>
#include <optional>
#include <ostream>

#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "nullability/type_nullability.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Expr.h"
#include "clang/Analysis/FlowSensitive/DataflowAnalysisContext.h"
#include "clang/Analysis/FlowSensitive/DataflowEnvironment.h"
#include "clang/Analysis/FlowSensitive/DataflowLattice.h"
#include "clang/Analysis/FlowSensitive/StorageLocation.h"
#include "clang/Analysis/FlowSensitive/Value.h"
#include "clang/Basic/LLVM.h"
#include "llvm/ADT/FunctionExtras.h"

namespace clang::tidy::nullability {
class PointerNullabilityLattice {
 public:
  struct NonFlowSensitiveState {
    absl::flat_hash_map<const Expr *, TypeNullability> ExprToNullability;
    // Overridden symbolic nullability for pointer-typed decls.
    // These are set by PointerNullabilityAnalysis::assignNullabilityVariable,
    // and take precedence over the declared type and over any result from
    // ConcreteNullabilityOverride.
    absl::flat_hash_map<const ValueDecl *, PointerTypeNullability>
        DeclTopLevelNullability;
    // Returns overriding concrete nullability for decls. This is set by
    // PointerNullabilityAnalysis::assignNullabilityOverride, and the result, if
    // present, takes precedence over the declared type.
    llvm::unique_function<std::optional<const PointerTypeNullability *>(
        const Decl &) const>
        ConcreteNullabilityOverride = [](const Decl &) { return std::nullopt; };
  };

  PointerNullabilityLattice(NonFlowSensitiveState &NFS) : NFS(NFS) {}

  const TypeNullability *getExprNullability(const Expr *E) const {
    auto I = NFS.ExprToNullability.find(&dataflow::ignoreCFGOmittedNodes(*E));
    return I == NFS.ExprToNullability.end() ? nullptr : &I->second;
  }

  // If the `ExprToNullability` map already contains an entry for `E`, does
  // nothing. Otherwise, inserts a new entry with key `E` and value computed by
  // the provided GetNullability.
  // Returns the (cached or computed) nullability.
  const TypeNullability &insertExprNullabilityIfAbsent(
      const Expr *E, const std::function<TypeNullability()> &GetNullability) {
    E = &dataflow::ignoreCFGOmittedNodes(*E);
    if (auto It = NFS.ExprToNullability.find(E);
        It != NFS.ExprToNullability.end())
      return It->second;
    // Deliberately perform a separate lookup after calling GetNullability.
    // It may invalidate iterators, e.g. inserting missing vectors for children.
    auto [Iterator, Inserted] =
        NFS.ExprToNullability.insert({E, GetNullability()});
    CHECK(Inserted) << "GetNullability inserted same " << E->getStmtClassName();
    return Iterator->second;
  }

  // Gets the PointerValue associated with the RecordStorageLocation and
  // MethodDecl of the CallExpr, creating one if it doesn't yet exist. Requires
  // the CXXMemberCallExpr to have a supported pointer type.
  dataflow::PointerValue *getConstMethodReturnValue(
      const dataflow::RecordStorageLocation &RecordLoc,
      const CXXMemberCallExpr *MCE, dataflow::Environment &Env) {
    auto &ObjMap = ConstMethodReturnValues[&RecordLoc];
    auto it = ObjMap.find(MCE->getMethodDecl());
    if (it != ObjMap.end()) return it->second;
    auto *PV = cast<dataflow::PointerValue>(Env.createValue(MCE->getType()));
    ObjMap.insert({MCE->getMethodDecl(), PV});
    return PV;
  }

  void clearConstMethodReturnValues(
      const dataflow::RecordStorageLocation &RecordLoc) {
    ConstMethodReturnValues.erase(&RecordLoc);
  }

  // If nullability for the decl D has been overridden, patch N to reflect it.
  // (N is the nullability of an access to D).
  void overrideNullabilityFromDecl(const Decl *D, TypeNullability &N) const;

  bool operator==(const PointerNullabilityLattice &Other) const { return true; }

  dataflow::LatticeJoinEffect join(const PointerNullabilityLattice &Other) {
    if (ConstMethodReturnValues.empty())
      return dataflow::LatticeJoinEffect::Unchanged;
    // Conservatively, just clear the `ConstMethodReturnValues` map entirely.
    // This means that we can't check the return value from a const method
    // before a join, then call the method again to use the pointer after the
    // join -- we'll get a false positive in this case.
    // TODO(b/309667920): Add code to actually join the maps if it turns out
    // these types of false positives are common.
    ConstMethodReturnValues.clear();
    return dataflow::LatticeJoinEffect::Changed;
  }

 private:
  // Owned by the PointerNullabilityAnalysis object, shared by all lattice
  // elements within one analysis run.
  NonFlowSensitiveState &NFS;

  // Maps a record storage location and const method to the value to return
  // from that const method.
  llvm::SmallDenseMap<
      const dataflow::RecordStorageLocation *,
      llvm::SmallDenseMap<const CXXMethodDecl *, dataflow::PointerValue *>>
      ConstMethodReturnValues;
};

inline std::ostream &operator<<(std::ostream &OS,
                                const PointerNullabilityLattice &) {
  return OS << "noop";
}

}  // namespace clang::tidy::nullability

#endif  // CRUBIT_NULLABILITY_POINTER_NULLABILITY_LATTICE_H_
