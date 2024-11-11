// Part of the Crubit project, under the Apache License v2.0 with LLVM
// Exceptions. See /LICENSE for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "nullability/inference/infer_tu.h"

#include <optional>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "nullability/inference/augmented_test_inputs.h"
#include "nullability/inference/inference.proto.h"
#include "nullability/pragma.h"
#include "nullability/proto_matchers.h"
#include "nullability/type_nullability.h"
#include "clang/AST/Decl.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/ASTMatchers/ASTMatchersMacros.h"
#include "clang/Basic/LLVM.h"
#include "clang/Index/USRGeneration.h"
#include "clang/Testing/TestAST.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "third_party/llvm/llvm-project/third-party/unittest/googlemock/include/gmock/gmock.h"
#include "third_party/llvm/llvm-project/third-party/unittest/googletest/include/gtest/gtest.h"

namespace clang::tidy::nullability {
namespace {
using ::clang::ast_matchers::asString;
using ::clang::ast_matchers::functionDecl;
using ::clang::ast_matchers::hasDeclContext;
using ::clang::ast_matchers::hasName;
using ::clang::ast_matchers::hasTemplateArgument;
using ::clang::ast_matchers::isTemplateInstantiation;
using ::clang::ast_matchers::refersToType;
using ::clang::ast_matchers::varDecl;
using ::testing::_;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::IsSupersetOf;
using ::testing::UnorderedElementsAre;

test::EnableSmartPointers Enable;

MATCHER_P2(inferredSlot, I, Nullability, "") {
  return testing::ExplainMatchResult(Eq(I), arg.first, result_listener) &&
         testing::ExplainMatchResult(Eq(Nullability), arg.second.nullability(),
                                     result_listener);
}
MATCHER_P3(inferredSlot, I, Nullability, Conflict, "") {
  return testing::ExplainMatchResult(Eq(I), arg.first, result_listener) &&
         testing::ExplainMatchResult(Eq(Nullability), arg.second.nullability(),
                                     result_listener) &&
         testing::ExplainMatchResult(Eq(Conflict), arg.second.conflict(),
                                     result_listener);
}
MATCHER_P2(inferenceMatcher, USR, SlotsMatcher, "") {
  return testing::ExplainMatchResult(Eq(USR), arg.first, result_listener) &&
         testing::ExplainMatchResult(SlotsMatcher, arg.second, result_listener);
}

AST_MATCHER(Decl, isCanonical) { return Node.isCanonicalDecl(); }

class InferTUTest : public ::testing::Test {
 protected:
  std::optional<TestAST> AST;
  NullabilityPragmas Pragmas;

  void build(llvm::StringRef Code) {
    AST.emplace(getAugmentedTestInputs(Code, Pragmas));
  }

  auto infer() { return inferTU(AST->context(), Pragmas); }

  // Returns a matcher for an InferenceResults entry.
  // The DeclMatcher should uniquely identify the symbol being described.
  // (We use this to compute the USR we expect to find in the inference proto).
  // Slots should describe the slots that were inferred.
  template <typename MatcherT>
  testing::Matcher<
      std::pair<std::string, absl::flat_hash_map<Slot, SlotInference>>>
  inference(
      MatcherT DeclMatcher,
      std::vector<testing::Matcher<std::pair<Slot, const SlotInference &>>>
          Slots) {
    llvm::SmallString<128> USR;
    auto Matches = ast_matchers::match(
        ast_matchers::namedDecl(isCanonical(), DeclMatcher).bind("decl"),
        AST->context());
    EXPECT_EQ(Matches.size(), 1);
    if (auto *D = ast_matchers::selectFirst<Decl>("decl", Matches))
      EXPECT_FALSE(index::generateUSRForDecl(D, USR));
    return inferenceMatcher(USR, testing::UnorderedElementsAreArray(Slots));
  }
};

TEST_F(InferTUTest, UncheckedDeref) {
  build(R"cc(
    void target(int *P, bool Cond) {
      if (Cond) *P;
    }

    void guarded(int *P) {
      if (P) *P;
    }
  )cc");

  EXPECT_THAT(infer(),
              ElementsAre(inference(hasName("target"),
                                    {inferredSlot(1, Nullability::NONNULL)})));
}

TEST_F(InferTUTest, Samples) {
  llvm::StringRef Code =
      "void target(int * P) { *P + *P; }\n"
      "void another(int X) { target(&X); }";
  //   123456789012345678901234567890123456789
  //   0        1         2         3

  build(Code);
  auto Results = infer();
  ASSERT_THAT(Results,
              ElementsAre(inference(hasName("target"),
                                    {inferredSlot(1, Nullability::NONNULL)})));
  EXPECT_THAT(Results.begin()->second[Slot(1)].sample_evidence(),
              testing::UnorderedElementsAre(
                  EqualsProto(R"pb(location: "input.cc:2:30"
                                   kind: NONNULL_ARGUMENT)pb"),
                  EqualsProto(R"pb(location: "input.cc:1:24"
                                   kind: UNCHECKED_DEREFERENCE)pb"),
                  EqualsProto(R"pb(location: "input.cc:1:29"
                                   kind: UNCHECKED_DEREFERENCE)pb")));
}

TEST_F(InferTUTest, Annotations) {
  build(R"cc(
    Nonnull<int *> target(int *A, int *B);
    Nonnull<int *> target(int *A, Nullable<int *> P) { *P; }
  )cc");

  EXPECT_THAT(infer(),
              ElementsAre(inference(hasName("target"),
                                    {
                                        inferredSlot(0, Nullability::NONNULL),
                                        inferredSlot(2, Nullability::NULLABLE),
                                    })));
}

TEST_F(InferTUTest, AnnotationsConflict) {
  build(R"cc(
    Nonnull<int *> target();
    Nullable<int *> target();
  )cc");

  EXPECT_THAT(infer(),
              ElementsAre(inference(hasName("target"),
                                    {inferredSlot(0, Nullability::UNKNOWN)})));
}

TEST_F(InferTUTest, ParamsFromCallSite) {
  build(R"cc(
    void callee(int* P, int* Q, int* R);
    void target(int* A, Nonnull<int*> B, Nullable<int*> C) { callee(A, B, C); }
  )cc");

  ASSERT_THAT(infer(),
              Contains(inference(hasName("callee"),
                                 {
                                     inferredSlot(1, Nullability::UNKNOWN),
                                     inferredSlot(2, Nullability::NONNULL),
                                     inferredSlot(3, Nullability::NULLABLE),
                                 })));
}

TEST_F(InferTUTest, ReturnTypeNullable) {
  build(R"cc(
    int* target() { return nullptr; }
  )cc");
  EXPECT_THAT(infer(),
              ElementsAre(inference(hasName("target"),
                                    {inferredSlot(0, Nullability::NULLABLE)})));
}

TEST_F(InferTUTest, ReturnTypeNonnull) {
  build(R"cc(
    Nonnull<int*> providesNonnull();
    int* target() { return providesNonnull(); }
  )cc");
  EXPECT_THAT(infer(),
              Contains(inference(hasName("target"),
                                 {inferredSlot(0, Nullability::NONNULL)})));
}

TEST_F(InferTUTest, ReturnTypeNonnullAndUnknown) {
  build(R"cc(
    Nonnull<int*> providesNonnull();
    int* target(bool B, int* Q) {
      if (B) return Q;
      return providesNonnull();
    }
  )cc");
  EXPECT_THAT(infer(),
              Contains(inference(hasName("target"),
                                 {inferredSlot(0, Nullability::UNKNOWN)})));
}

TEST_F(InferTUTest, ReturnTypeNonnullAndNullable) {
  build(R"cc(
    Nonnull<int*> providesNonnull();
    int* target(bool B) {
      if (B) return nullptr;
      return providesNonnull();
    }
  )cc");
  EXPECT_THAT(infer(),
              Contains(inference(hasName("target"),
                                 {inferredSlot(0, Nullability::NULLABLE)})));
}

TEST_F(InferTUTest, ReturnTypeDereferenced) {
  build(R"cc(
    struct S {
      void member();
    };

    S* makePtr();
    void target() { makePtr()->member(); }
  )cc");
  EXPECT_THAT(infer(),
              ElementsAre(inference(hasName("makePtr"),
                                    {inferredSlot(0, Nullability::NONNULL)})));
}

TEST_F(InferTUTest, PassedToNonnull) {
  build(R"cc(
    void takesNonnull(Nonnull<int*>);
    void target(int* P) { takesNonnull(P); }
  )cc");
  EXPECT_THAT(infer(),
              Contains(inference(hasName("target"),
                                 {inferredSlot(1, Nullability::NONNULL)})));
}

TEST_F(InferTUTest, PassedToMutableNullableRef) {
  build(R"cc(
    void takesMutableNullableRef(Nullable<int*>&);
    void target(int* P) { takesMutableNullableRef(P); }
  )cc");
  EXPECT_THAT(infer(),
              Contains(inference(hasName("target"),
                                 {inferredSlot(1, Nullability::NULLABLE)})));
}

TEST_F(InferTUTest, AssignedFromNullable) {
  build(R"cc(
    void target(int* P) { P = nullptr; }
  )cc");
  EXPECT_THAT(infer(),
              Contains(inference(hasName("target"),
                                 {inferredSlot(1, Nullability::NULLABLE)})));
}

TEST_F(InferTUTest, CHECKMacro) {
  build(R"cc(
    // macro must use the parameter, but otherwise body doesn't matter
#define CHECK(X) X
    void target(int* P) { CHECK(P); }
  )cc");
  EXPECT_THAT(infer(),
              Contains(inference(hasName("target"),
                                 {inferredSlot(1, Nullability::NONNULL)})));
}

TEST_F(InferTUTest, CHECKNEMacro) {
  build(R"cc(
    // macro must use the first parameter, but otherwise body doesn't matter
#define CHECK_NE(X, Y) X
    void target(int* P, int* Q, int* R, int* S) {
      CHECK_NE(P, nullptr);
      CHECK_NE(nullptr, Q);
      int* A = nullptr;
      CHECK_NE(A, R);
      CHECK_NE(S, A);
    }
  )cc");
  EXPECT_THAT(
      infer(),
      IsSupersetOf(
          {inference(hasName("target"),
                     {inferredSlot(1, Nullability::NONNULL),
                      inferredSlot(2, Nullability::NONNULL),
                      inferredSlot(3, Nullability::NONNULL),
                      inferredSlot(4, Nullability::NONNULL)}),
           inference(hasName("A"), {inferredSlot(0, Nullability::NULLABLE)})}));
}

TEST_F(InferTUTest, Fields) {
  build(R"cc(
    int* getIntPtr();
    struct S {
      int* UncheckedDeref;
      int* DefaultNullAndUncheckedDeref = nullptr;
      int* Uninitialized;
      int NotATarget = *getIntPtr();

      void method() {
        *UncheckedDeref;
        *DefaultNullAndUncheckedDeref;
      }
    };

    void foo() {
      // Use the implicitly-declared default constructor so that it will be
      // generated.
      S AnS;
    }

    class C {
     public:
      C() : NullConstructorInit(nullptr) {
        NullInConstructorAndUncheckedDeref = nullptr;
        NullInConstructor = nullptr;
      }

      void method() { *NullInConstructorAndUncheckedDeref; }

     private:
      int* NullInConstructorAndUncheckedDeref;
      int* NullConstructorInit;
      int* NullInConstructor;
    };
  )cc");
  EXPECT_THAT(
      infer(),
      UnorderedElementsAre(
          inference(hasName("UncheckedDeref"),
                    {inferredSlot(0, Nullability::NONNULL)}),
          // Unchecked deref is strong evidence and a default null
          // member initializer is weak.
          inference(hasName("DefaultNullAndUncheckedDeref"),
                    {inferredSlot(0, Nullability::NONNULL)}),
          // No inference for uninitialized.,
          inference(hasName("getIntPtr"),
                    {inferredSlot(0, Nullability::NONNULL)}),
          // Initialization to null in the constructor or another
          // function body is strong, producing a conflict.
          inference(hasName("NullInConstructorAndUncheckedDeref"),
                    {inferredSlot(0, Nullability::NONNULL, /*Conflict*/ true)}),
          inference(hasName("NullConstructorInit"),
                    {inferredSlot(0, Nullability::NULLABLE)}),
          inference(hasName("NullInConstructor"),
                    {inferredSlot(0, Nullability::NULLABLE)})));
}

TEST_F(InferTUTest, FieldsImplicitlyDeclaredConstructorNeverUsed) {
  build(R"cc(
    Nullable<bool *> getNullable();
    struct S {
      int *I = nullptr;
      bool *B = getNullable();
      char *C = static_cast<char *>(nullptr);
    };

    void foo(S AnS);
  )cc");
  // Because the implicitly-declared default constructor is never used, it is
  // not present in the AST and we never analyze it. So, we collect no evidence
  // from default member initializers.
  EXPECT_THAT(infer(),
              AllOf(AllOf(Not(Contains(inference(hasName("I"), {_}))),
                          Not(Contains(inference(hasName("B"), {_}))),
                          Not(Contains(inference(hasName("C"), {_}))))));
}

TEST_F(InferTUTest, FieldsImplicitlyDeclaredConstructorUsed) {
  build(R"cc(
    Nullable<bool *> getNullable();
    struct S {
      int *I = nullptr;
      bool *B = getNullable();
      char *C = static_cast<char *>(nullptr);
    };
    // A use of the implicitly-declared default constructor, so it is generated
    // and included in the AST for us to analyze, allowing us to infer from
    // default member initializers.
    void foo() { S AnS; }
  )cc");
  EXPECT_THAT(
      infer(),
      IsSupersetOf(
          {inference(hasName("I"), {inferredSlot(0, Nullability::NULLABLE)}),
           inference(hasName("B"), {inferredSlot(0, Nullability::NULLABLE)}),
           inference(hasName("C"), {inferredSlot(0, Nullability::NULLABLE)})}));
}

TEST_F(InferTUTest, GlobalVariables) {
  build(R"cc(
    int* getIntPtr();

    int* I;
    bool* B;
    int NotATarget = *getIntPtr();

    void target() {
      I = nullptr;
      *B;
    }
  )cc");
  EXPECT_THAT(
      infer(),
      UnorderedElementsAre(
          inference(hasName("I"), {inferredSlot(0, Nullability::NULLABLE)}),
          inference(hasName("B"), {inferredSlot(0, Nullability::NONNULL)}),
          inference(hasName("getIntPtr"),
                    {inferredSlot(0, Nullability::NONNULL)})));
}

TEST_F(InferTUTest, StaticMemberVariables) {
  build(R"cc(
    struct S {
      static int* SI;
      static bool* SB;
    };

    void target() {
      *S::SI;
      S::SB = nullptr;
    }
  )cc");
  EXPECT_THAT(
      infer(),
      UnorderedElementsAre(
          inference(hasName("SI"), {inferredSlot(0, Nullability::NONNULL)}),
          inference(hasName("SB"), {inferredSlot(0, Nullability::NULLABLE)})));
}

TEST_F(InferTUTest, Locals) {
  build(R"cc(
    void target() {
      int* A = nullptr;
      static int* B = nullptr;
    }
  )cc");
  EXPECT_THAT(
      infer(),
      UnorderedElementsAre(
          inference(hasName("A"), {inferredSlot(0, Nullability::NULLABLE)}),
          inference(hasName("B"), {inferredSlot(0, Nullability::NULLABLE)})));
}

TEST_F(InferTUTest, Filter) {
  build(R"cc(
    int* target1() { return nullptr; }
    int* target2() { return nullptr; }
  )cc");
  EXPECT_THAT(inferTU(AST->context(), Pragmas, /*Iterations=*/1,
                      [&](const Decl &D) {
                        return cast<NamedDecl>(D).getNameAsString() !=
                               "target2";
                      }),
              ElementsAre(inference(hasName("target1"), {_})));
}

TEST_F(InferTUTest, AutoNoStarType) {
  build(R"cc(
    int *_Nullable getNullable();

    void func() { auto AutoLocal = getNullable(); }

    int *autoParamAkaTemplate(auto P) {
      auto AutoLocalInTemplate = getNullable();
      *P;
      return getNullable();
    }

    auto autoReturn(int *Q) {
      *Q;
      auto AutoLocalInAutoReturn = getNullable();
      return getNullable();
    }

    auto autoReturnAndParam(auto R) {
      *R;
      return getNullable();
    }
  )cc");
  EXPECT_THAT(infer(),
              UnorderedElementsAre(
                  // Already annotated.
                  inference(hasName("getNullable"),
                            {inferredSlot(0, Nullability::NULLABLE)}),
                  // We infer for local variables with type `auto*`.
                  inference(hasName("AutoLocal"),
                            {inferredSlot(0, Nullability::NULLABLE)}),
                  // We infer for return types with type `auto*`, for the
                  // parameters of functions with return type `auto*`, and for
                  // local variables in these functions.
                  inference(hasName("autoReturn"),
                            {inferredSlot(0, Nullability::NULLABLE),
                             inferredSlot(1, Nullability::NONNULL)}),
                  inference(hasName("AutoLocalInAutoReturn"),
                            {inferredSlot(0, Nullability::NULLABLE)})
                  // We don't infer anything for or from functions with
                  // parameters of type `auto*`, because these are templates.
                  ));
}

TEST_F(InferTUTest, AutoStarType) {
  build(R"cc(
    int *_Nullable getNullable();

    void func() { auto *AutoStarLocal = getNullable(); }

    int *autoStarParamAkaTemplate(auto *P) {
      auto *AutoStarLocalInTemplate = getNullable();
      *P;
      return getNullable();
    }

    auto *autoStarReturn(int *Q) {
      *Q;
      auto *AutoStarLocalInAutoStarReturn = getNullable();
      return getNullable();
    }

    auto *autoStarReturnAndParam(auto *R) {
      *R;
      return getNullable();
    }

    void templateUsagesToForceInstantiation() {
      int *UnimportantLocal = nullptr;
      autoStarParamAkaTemplate(UnimportantLocal);

      autoStarReturnAndParam<bool *>(nullptr);
    }
  )cc");
  EXPECT_THAT(
      infer(),
      UnorderedElementsAre(
          // Already annotated.
          inference(hasName("getNullable"),
                    {inferredSlot(0, Nullability::NULLABLE)}),
          // We infer for local variables with type `auto*`.
          inference(hasName("AutoStarLocal"),
                    {inferredSlot(0, Nullability::NULLABLE)}),
          // We infer for return types with type `auto*`, for the
          // parameters of functions with return type `auto*`, and for
          // local variables in these functions.
          inference(hasName("autoStarReturn"),
                    {inferredSlot(0, Nullability::NULLABLE),
                     inferredSlot(1, Nullability::NONNULL)}),
          inference(hasName("AutoStarLocalInAutoStarReturn"),
                    {inferredSlot(0, Nullability::NULLABLE)}),
          // We infer for function template instantiations and for the local
          // variables in the instantiations.
          inference(functionDecl(hasName("autoStarParamAkaTemplate"),
                                 isTemplateInstantiation()),
                    {inferredSlot(0, Nullability::NULLABLE),
                     inferredSlot(1, Nullability::NONNULL, /*Conflict*/ true)}),
          inference(functionDecl(hasName("autoStarReturnAndParam"),
                                 isTemplateInstantiation()),
                    {inferredSlot(0, Nullability::NULLABLE),
                     inferredSlot(1, Nullability::NONNULL, /*Conflict*/ true)}),
          inference(
              varDecl(hasName("AutoStarLocalInTemplate"),
                      hasDeclContext(functionDecl(isTemplateInstantiation()))),
              {inferredSlot(0, Nullability::NULLABLE)}),
          inference(hasName("UnimportantLocal"),
                    {inferredSlot(0, Nullability::NULLABLE)})));
}

TEST_F(InferTUTest, IterationsPropagateInferences) {
  build(R"cc(
    void takesToBeNonnull(int* X) { *X; }
    int* returnsToBeNonnull(int* A) { return A; }
    int* target(int* P, int* Q, int* R) {
      *P;
      takesToBeNonnull(Q);
      Q = R;
      return returnsToBeNonnull(P);
    }
  )cc");
  EXPECT_THAT(
      inferTU(AST->context(), Pragmas, /*Iterations=*/1),
      UnorderedElementsAre(
          inference(hasName("target"), {inferredSlot(0, Nullability::UNKNOWN),
                                        inferredSlot(1, Nullability::NONNULL),
                                        inferredSlot(2, Nullability::UNKNOWN)}),
          inference(hasName("returnsToBeNonnull"),
                    {inferredSlot(0, Nullability::UNKNOWN),
                     inferredSlot(1, Nullability::UNKNOWN)}),
          inference(hasName("takesToBeNonnull"),
                    {inferredSlot(1, Nullability::NONNULL)})));
  EXPECT_THAT(
      inferTU(AST->context(), Pragmas, /*Iterations=*/2),
      UnorderedElementsAre(
          inference(hasName("target"), {inferredSlot(0, Nullability::UNKNOWN),
                                        inferredSlot(1, Nullability::NONNULL),
                                        inferredSlot(2, Nullability::NONNULL)}),
          inference(hasName("returnsToBeNonnull"),
                    {inferredSlot(0, Nullability::UNKNOWN),
                     inferredSlot(1, Nullability::NONNULL)}),
          inference(hasName("takesToBeNonnull"),
                    {inferredSlot(1, Nullability::NONNULL)})));
  EXPECT_THAT(
      inferTU(AST->context(), Pragmas, /*Iterations=*/3),
      UnorderedElementsAre(
          inference(hasName("target"), {inferredSlot(0, Nullability::UNKNOWN),
                                        inferredSlot(1, Nullability::NONNULL),
                                        inferredSlot(2, Nullability::NONNULL),
                                        inferredSlot(3, Nullability::NONNULL)}),
          inference(hasName("returnsToBeNonnull"),
                    {inferredSlot(0, Nullability::NONNULL),
                     inferredSlot(1, Nullability::NONNULL)}),
          inference(hasName("takesToBeNonnull"),
                    {inferredSlot(1, Nullability::NONNULL)})));
  EXPECT_THAT(
      inferTU(AST->context(), Pragmas, /*Iterations=*/4),
      UnorderedElementsAre(
          inference(hasName("target"), {inferredSlot(0, Nullability::NONNULL),
                                        inferredSlot(1, Nullability::NONNULL),
                                        inferredSlot(2, Nullability::NONNULL),
                                        inferredSlot(3, Nullability::NONNULL)}),
          inference(hasName("returnsToBeNonnull"),
                    {inferredSlot(0, Nullability::NONNULL),
                     inferredSlot(1, Nullability::NONNULL)}),
          inference(hasName("takesToBeNonnull"),
                    {inferredSlot(1, Nullability::NONNULL)})));
}

TEST_F(InferTUTest, Pragma) {
  build(R"cc(
#pragma nullability file_default nonnull
    void target(int* DefaultNonnull, NullabilityUnknown<int*> InferredNonnull,
                Nullable<int*> Nullable,
                NullabilityUnknown<int*> InferredNullable,
                NullabilityUnknown<int*> Unknown) {
      DefaultNonnull = InferredNonnull;
      DefaultNonnull = nullptr;
      InferredNullable = Nullable;
    }
  )cc");
  EXPECT_THAT(infer(),
              UnorderedElementsAre(inference(
                  hasName("target"),
                  {
                      // annotation by pragma beats assignment from null, so
                      // default_nonnull should still be inferred NONNULL
                      inferredSlot(1, Nullability::NONNULL),
                      // an explicit unknown does not override a Nonnull
                      // inference, even if it overrides the pragma
                      inferredSlot(2, Nullability::NONNULL),
                      // an explicit nullable overrides pragma default
                      inferredSlot(3, Nullability::NULLABLE),
                      // an explicit unknown does not override a Nullable
                      // inference, which does override the pragma
                      inferredSlot(4, Nullability::NULLABLE)
                      // an explicit unknown overrides the pragma, but produces
                      // no inference, so nothing for slot 5.
                  })));
}

TEST_F(InferTUTest, FunctionTemplate) {
  build(R"cc(
    template <typename T>
    T functionTemplate(int* P, Nullable<int*> Q, T* R, Nullable<T*> S, T U) {
      *P;
      *R;
      return U;
    }

    void usage() {
      int I = 0;
      int* A = &I;
      int* B = &I;
      int* C = &I;
      int* D = &I;
      int* E = &I;
      // In the first iteration, infer (for the instantiation) P and R as
      // Nonnull, Q and S as Nullable, U as Nonnull, and Unknown for the int*
      // return type (which hasn't yet seen the inference of U as Nonnull).
      int* TargetIntStarResult = functionTemplate(A, B, &C, &D, E);
      // Infer (for the instantiation) P and R as Nonnull, Q and S as Nullable,
      // and nothing for the int U and int return type.
      int TargetIntResult = functionTemplate(A, B, C, D, I);
    }
  )cc");
  EXPECT_THAT(
      infer(),
      IsSupersetOf(
          {inference(
               functionDecl(
                   hasName("functionTemplate"), isTemplateInstantiation(),
                   hasTemplateArgument(0, refersToType(asString("int *")))),
               {inferredSlot(0, Nullability::UNKNOWN),
                inferredSlot(1, Nullability::NONNULL),
                inferredSlot(2, Nullability::NULLABLE),
                inferredSlot(3, Nullability::NONNULL),
                inferredSlot(4, Nullability::NULLABLE),
                inferredSlot(5, Nullability::NONNULL)}),
           inference(functionDecl(
                         hasName("functionTemplate"), isTemplateInstantiation(),
                         hasTemplateArgument(0, refersToType(asString("int")))),
                     {inferredSlot(1, Nullability::NONNULL),
                      inferredSlot(2, Nullability::NULLABLE),
                      inferredSlot(3, Nullability::NONNULL),
                      inferredSlot(4, Nullability::NULLABLE)})}));
}

using InferTUSmartPointerTest = InferTUTest;

TEST_F(InferTUSmartPointerTest, Annotations) {
  build(R"cc(
#include <memory>
    Nonnull<std::unique_ptr<int>> target(std::unique_ptr<int> A,
                                         std::unique_ptr<int> B);
    Nonnull<std::unique_ptr<int>> target(std::unique_ptr<int> A,
                                         Nullable<std::unique_ptr<int>> P) {
      *P;
    }
  )cc");

  EXPECT_THAT(infer(),
              Contains(inference(hasName("target"),
                                 {
                                     inferredSlot(0, Nullability::NONNULL),
                                     inferredSlot(2, Nullability::NULLABLE),
                                 })));
}

TEST_F(InferTUSmartPointerTest, ParamsFromCallSite) {
  build(R"cc(
#include <memory>
#include <utility>
    void callee(std::unique_ptr<int> P, std::unique_ptr<int> Q,
                std::unique_ptr<int> R);
    void target(std::unique_ptr<int> A, Nonnull<std::unique_ptr<int>> B,
                Nullable<std::unique_ptr<int>> C) {
      callee(std::move(A), std::move(B), std::move(C));
    }
  )cc");

  EXPECT_THAT(infer(),
              Contains(inference(hasName("callee"),
                                 {
                                     inferredSlot(1, Nullability::UNKNOWN),
                                     inferredSlot(2, Nullability::NONNULL),
                                     inferredSlot(3, Nullability::NULLABLE),
                                 })));
}

TEST_F(InferTUSmartPointerTest, ReturnTypeNullable) {
  build(R"cc(
#include <memory>
    std::unique_ptr<int> target() { return std::unique_ptr<int>(); }
  )cc");
  EXPECT_THAT(infer(),
              Contains(inference(hasName("target"),
                                 {inferredSlot(0, Nullability::NULLABLE)})));
}

TEST_F(InferTUSmartPointerTest, ReturnTypeNonnull) {
  build(R"cc(
#include <memory>
    std::unique_ptr<int> target() { return std::make_unique<int>(0); }
  )cc");
  EXPECT_THAT(infer(),
              Contains(inference(hasName("target"),
                                 {inferredSlot(0, Nullability::NONNULL)})));
}

using InferTUVirtualMethodsTest = InferTUTest;

TEST_F(InferTUVirtualMethodsTest, SafeVarianceNoConflicts) {
  build(R"cc(
    struct Base {
      virtual int* foo(int* P) {
        *P;
        return nullptr;
      }
    };

    struct Derived : public Base {
      int* foo(int* P) override {
        static int I = 0;
        P = nullptr;
        return &I;
      }
    };
  )cc");

  EXPECT_THAT(infer(),
              UnorderedElementsAre(
                  inference(hasName("Base::foo"),
                            {inferredSlot(0, Nullability::NULLABLE),
                             inferredSlot(1, Nullability::NONNULL)}),
                  inference(hasName("Derived::foo"),
                            {inferredSlot(0, Nullability::NONNULL),
                             inferredSlot(1, Nullability::NULLABLE)})));
}

TEST_F(InferTUVirtualMethodsTest, BaseConstrainsDerived) {
  build(R"cc(
    struct Base {
      virtual Nonnull<int*> foo(int* P) {
        static int I = 0;
        P = nullptr;
        return &I;
      }
    };

    struct Derived : public Base {
      int* foo(int* P) override;
    };
  )cc");

  EXPECT_THAT(infer(),
              UnorderedElementsAre(
                  inference(hasName("Base::foo"),
                            {inferredSlot(0, Nullability::NONNULL),
                             inferredSlot(1, Nullability::NULLABLE)}),
                  inference(hasName("Derived::foo"),
                            {inferredSlot(0, Nullability::NONNULL),
                             inferredSlot(1, Nullability::NULLABLE)})));
}

TEST_F(InferTUVirtualMethodsTest, DerivedConstrainsBase) {
  build(R"cc(
    struct Base {
      virtual int* foo(int* P);
    };

    struct Derived : public Base {
      int* foo(int* P) override {
        *P;
        return nullptr;
      }
    };
  )cc");

  EXPECT_THAT(infer(), UnorderedElementsAre(
                           inference(hasName("Base::foo"),
                                     {inferredSlot(0, Nullability::NULLABLE),
                                      inferredSlot(1, Nullability::NONNULL)}),
                           inference(hasName("Derived::foo"),
                                     {inferredSlot(0, Nullability::NULLABLE),
                                      inferredSlot(1, Nullability::NONNULL)})));
}

TEST_F(InferTUVirtualMethodsTest, Conflict) {
  build(R"cc(
    struct Base {
      virtual int* foo(int* P);
    };

    struct Derived : public Base {
      int* foo(int* P) override {
        *P;
        return nullptr;
      }
    };

    void usage() {
      Base B;
      // Conflict-producing nonnull return type evidence is only possible
      // from a usage site. Since we need a usage, produce the parameter
      // evidence here as well.
      *B.foo(nullptr);
    }
  )cc");

  EXPECT_THAT(
      infer(),
      UnorderedElementsAre(
          inference(hasName("Base::foo"),
                    {inferredSlot(0, Nullability::NONNULL, /*Conflict*/ true),
                     inferredSlot(1, Nullability::NONNULL, /*Conflict*/ true)}),
          inference(
              hasName("Derived::foo"),
              {inferredSlot(0, Nullability::NONNULL, /*Conflict*/ true),
               inferredSlot(1, Nullability::NONNULL, /*Conflict*/ true)})));
}

TEST_F(InferTUVirtualMethodsTest, MultipleDerived) {
  build(R"cc(
    struct Base {
      virtual void foo(int* P) { P = nullptr; }
    };

    struct DerivedA : public Base {
      void foo(int* P) override;
    };

    struct DerivedB : public Base {
      void foo(int* P) override;
    };
  )cc");
  EXPECT_THAT(infer(),
              UnorderedElementsAre(
                  inference(hasName("Base::foo"),
                            {inferredSlot(1, Nullability::NULLABLE)}),
                  inference(hasName("DerivedA::foo"),
                            {inferredSlot(1, Nullability::NULLABLE)}),
                  inference(hasName("DerivedB::foo"),
                            {inferredSlot(1, Nullability::NULLABLE)})));
}

TEST_F(InferTUVirtualMethodsTest, MultipleBase) {
  build(R"cc(
    struct BaseA {
      virtual void foo(int* P);
    };

    struct BaseB {
      virtual void foo(int* P);
    };

    struct Derived : public BaseA, public BaseB {
      void foo(int* P) override { *P; }
    };
  )cc");

  EXPECT_THAT(infer(), UnorderedElementsAre(
                           inference(hasName("BaseA::foo"),
                                     {inferredSlot(1, Nullability::NONNULL)}),
                           inference(hasName("BaseB::foo"),
                                     {inferredSlot(1, Nullability::NONNULL)}),
                           inference(hasName("Derived::foo"),
                                     {inferredSlot(1, Nullability::NONNULL)})));
}

}  // namespace
}  // namespace clang::tidy::nullability
