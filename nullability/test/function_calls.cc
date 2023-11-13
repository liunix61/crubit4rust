// Part of the Crubit project, under the Apache License v2.0 with LLVM
// Exceptions. See /LICENSE for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Tests for function calls.

#include "nullability/test/check_diagnostics.h"
#include "third_party/llvm/llvm-project/third-party/unittest/googletest/include/gtest/gtest.h"

namespace clang::tidy::nullability {
namespace {

TEST(PointerNullabilityTest, CallExprWithPointerReturnType) {
  // free function
  EXPECT_TRUE(checkDiagnostics(R"cc(
    int *_Nonnull makeNonnull();
    int *_Nullable makeNullable();
    int *makeUnannotated();
    void target() {
      *makeNonnull();
      *makeNullable();  // [[unsafe]]
      *makeUnannotated();
    }
  )cc"));

  // member function
  EXPECT_TRUE(checkDiagnostics(R"cc(
    struct Foo {
      int *_Nonnull makeNonnull();
      int *_Nullable makeNullable();
      int *makeUnannotated();
    };
    void target(Foo foo) {
      *foo.makeNonnull();
      *foo.makeNullable();  // [[unsafe]]
      *foo.makeUnannotated();
    }
  )cc"));

  // function pointer
  EXPECT_TRUE(checkDiagnostics(R"cc(
    void target(int *_Nonnull (*makeNonnull)(),
                int *_Nullable (*makeNullable)(), int *(*makeUnannotated)()) {
      *makeNonnull();
      *makeNullable();  // [[unsafe]]
      *makeUnannotated();
    }
  )cc"));

  // pointer to function pointer
  EXPECT_TRUE(checkDiagnostics(R"cc(
    void target(int *_Nonnull (**makeNonnull)(),
                int *_Nullable (**makeNullable)(), int *(**makeUnannotated)()) {
      *(*makeNonnull)();
      *(*makeNullable)();  // [[unsafe]]
      *(*makeUnannotated)();
    }
  )cc"));

  // function returning a function pointer which returns a pointer
  EXPECT_TRUE(checkDiagnostics(R"cc(
    typedef int *_Nonnull (*MakeNonnullT)();
    typedef int *_Nullable (*MakeNullableT)();
    typedef int *(*MakeUnannotatedT)();
    void target(MakeNonnullT (*makeNonnull)(), MakeNullableT (*makeNullable)(),
                MakeUnannotatedT (*makeUnannotated)()) {
      *(*makeNonnull)()();
      *(*makeNullable)()();  // [[unsafe]]
      *(*makeUnannotated)()();
    }
  )cc"));

  // free function returns reference to pointer
  EXPECT_TRUE(checkDiagnostics(R"cc(
    int *_Nonnull &makeNonnull();
    int *_Nullable &makeNullable();
    int *&makeUnannotated();
    void target() {
      *makeNonnull();
      *makeNullable();  // [[unsafe]]
      *makeUnannotated();

      // Check that we can take the address of the returned reference and still
      // see the correct nullability "behind" the resulting pointer.
      __assert_nullability<NK_nonnull, NK_nonnull>(&makeNonnull());
      __assert_nullability<NK_nonnull, NK_nullable>(&makeNullable());
      __assert_nullability<NK_nonnull, NK_unspecified>(&makeUnannotated());
    }
  )cc"));

  // function called in loop
  EXPECT_TRUE(checkDiagnostics(R"cc(
    int *_Nullable makeNullable();
    bool makeBool();
    void target() {
      bool first = true;
      while (true) {
        int *x = makeNullable();
        if (first && x == nullptr) return;
        first = false;
        *x;  // [[unsafe]]
      }
    }
  )cc"));
}

TEST(PointerNullabilityTest, OutputParameterBasic) {
  EXPECT_TRUE(checkDiagnostics(R"cc(
    void maybeModifyPtr(int** p);
    void target() {
      int* p = nullptr;
      maybeModifyPtr(&p);
      *p;
    }
  )cc"));
}

TEST(PointerNullabilityTest, OutputParameterReference) {
  EXPECT_TRUE(checkDiagnostics(R"cc(
    void maybeModifyPtr(int*& r);
    void target() {
      int* p = nullptr;
      maybeModifyPtr(p);
      *p;
    }
  )cc"));
}

TEST(PointerNullabilityTest, OutputParameterReferenceConst) {
  EXPECT_TRUE(checkDiagnostics(R"cc(
    void pointerNotModified(int* const& r);
    void target() {
      int* p = nullptr;
      pointerNotModified(p);
      *p;  // [[unsafe]]
    }
  )cc"));
}

TEST(PointerNullabilityTest, OutputParameterReferencePointerToPointer) {
  EXPECT_TRUE(checkDiagnostics(R"cc(
    void maybeModifyPtr(int**& r);
    void target() {
      int** pp = nullptr;
      maybeModifyPtr(pp);
      *pp;
      **pp;
    }
  )cc"));
}

TEST(PointerNullabilityTest, OutputParameterConst) {
  EXPECT_TRUE(checkDiagnostics(R"cc(
    void pointerNotModified(int* const* p);
    void target(int* _Nullable p) {
      pointerNotModified(&p);
      *p;  // [[unsafe]]
    }
  )cc"));

  // The only const qualifier that should be considered is on the inner
  // pointer, otherwise this pattern should be considered safe.
  EXPECT_TRUE(checkDiagnostics(R"cc(
    void maybeModifyPtr(const int** const p);
    void target() {
      const int* p = nullptr;
      maybeModifyPtr(&p);
      *p;
    }
  )cc"));
}

TEST(PointerNullabilityTest, OutputParameterNonnull) {
  EXPECT_TRUE(checkDiagnostics(R"cc(
    void pointerNotModified(int* _Nonnull* p);
    void target(int* _Nonnull p) {
      pointerNotModified(&p);
      *p;
    }
  )cc"));
}

TEST(PointerNullabilityTest, OutputParameterCheckedNullable) {
  EXPECT_TRUE(checkDiagnostics(R"cc(
    void maybeModify(int* _Nullable* p);
    void target(int* _Nullable p) {
      if (!p) return;
      maybeModify(&p);
      *p;  // false negative: this dereference is actually unsafe!
    }
  )cc"));
}

TEST(PointerNullabilityTest, OutputParameterNullable) {
  EXPECT_TRUE(checkDiagnostics(R"cc(
    void maybeModifyPtr(int* _Nullable* p);
    void target() {
      int* p = nullptr;
      maybeModifyPtr(&p);
      *p;  // [[unsafe]]
    }
  )cc"));
}

TEST(PointerNullabilityTest, OutputParameterConditional) {
  // This tests that flow sensitivity is preserved, to catch for example if the
  // underlying pointer was always set to Nonnull once it's passed as an
  // output parameter.
  EXPECT_TRUE(checkDiagnostics(R"cc(
    void maybeModifyPtr(int** p);
    void target(int* _Nullable j, bool b) {
      if (b) {
        maybeModifyPtr(&j);
      }
      if (b) {
        *j;
      }
      if (!b) {
        *j;  // [[unsafe]]
      }
    }
  )cc"));
}

TEST(PointerNullabilityTest, OutputParameterWithoutAmpersandOperator) {
  // This tests that the call to maybeModifyPtr works as expected if the param
  // passed in doesn't directly use the & operator
  EXPECT_TRUE(checkDiagnostics(R"cc(
    void maybeModifyPtr(int** p);
    void target(int* _Nullable p) {
      auto pp = &p;
      maybeModifyPtr(pp);
      *p;
    }
  )cc"));
}

TEST(PointerNullabilityTest, OutputParameterTemplate) {
  EXPECT_TRUE(checkDiagnostics(R"cc(
    template <typename T>
    struct S {
      void maybeModify(T& ref);
    };
    void target(S<int*> s, int* _Nullable p) {
      s.maybeModify(p);
      *p;
    }
  )cc"));

  EXPECT_TRUE(checkDiagnostics(R"cc(
    template <typename T>
    struct S {
      void maybeModify(T& ref);
    };
    void target(S<int* _Nullable> s, int* _Nullable p) {
      s.maybeModify(p);
      *p;  // false negative
    }
  )cc"));

  EXPECT_TRUE(checkDiagnostics(R"cc(
    template <typename T>
    struct S {
      void maybeModify(T& ref);
    };
    void target(S<int* _Nonnull> s, int* _Nonnull p) {
      s.maybeModify(p);
      *p;
    }
  )cc"));
}

TEST(PointerNullabilityTest, CallExprParamAssignment) {
  // free function with single param
  EXPECT_TRUE(checkDiagnostics(R"cc(
    void takeNonnull(int *_Nonnull);
    void takeNullable(int *_Nullable);
    void takeUnannotated(int *);
    void target(int *_Nonnull ptr_nonnull, int *_Nullable ptr_nullable,
                int *ptr_unannotated) {
      takeNonnull(nullptr);  // [[unsafe]]
      takeNonnull(ptr_nonnull);
      takeNonnull(ptr_nullable);  // [[unsafe]]
      takeNonnull(ptr_unannotated);

      takeNullable(nullptr);
      takeNullable(ptr_nonnull);
      takeNullable(ptr_nullable);
      takeNullable(ptr_unannotated);

      takeUnannotated(nullptr);
      takeUnannotated(ptr_nonnull);
      takeUnannotated(ptr_nullable);
      takeUnannotated(ptr_unannotated);
    }
  )cc"));

  // free function with multiple params of mixed nullability
  EXPECT_TRUE(checkDiagnostics(R"cc(
    void takeMixed(int *, int *_Nullable, int *_Nonnull);
    void target() {
      takeMixed(nullptr, nullptr, nullptr);  // [[unsafe]]
    }
  )cc"));

  // member function
  EXPECT_TRUE(checkDiagnostics(R"cc(
    struct Foo {
      void takeNonnull(int *_Nonnull);
      void takeNullable(int *_Nullable);
      void takeUnannotated(int *);
    };
    void target(Foo foo) {
      foo.takeNonnull(nullptr);  // [[unsafe]]
      foo.takeNullable(nullptr);
      foo.takeUnannotated(nullptr);
    }
  )cc"));

  // function pointer
  EXPECT_TRUE(checkDiagnostics(R"cc(
    void target(void (*takeNonnull)(int *_Nonnull),
                void (*takeNullable)(int *_Nullable),
                void (*takeUnannotated)(int *)) {
      takeNonnull(nullptr);  // [[unsafe]]
      takeNullable(nullptr);
      takeUnannotated(nullptr);
    }
  )cc"));

  // pointer to function pointer
  //
  // TODO(b/233582219): Fix false negative. Implement support for retrieving
  // parameter types from a pointer to function pointer.
  EXPECT_TRUE(checkDiagnostics(R"cc(
    void target(void (**takeNonnull)(int *_Nonnull),
                void (**takeNullable)(int *_Nullable),
                void (**takeUnannotated)(int *)) {
      (*takeNonnull)(nullptr);  // false-negative
      (*takeNullable)(nullptr);
      (*takeUnannotated)(nullptr);
    }
  )cc"));

  // function returned from function
  //
  // TODO(b/233582219): Fix false negative. Implement support for retrieving
  // parameter types for functions returned by another function.
  EXPECT_TRUE(checkDiagnostics(R"cc(
    typedef void (*takeNonnullF)(int *_Nonnull);
    typedef void (*takeNullableF)(int *_Nullable);
    typedef void (*takeUnannotatedF)(int *);
    void target(takeNonnullF (*takeNonnull)(), takeNullableF (*takeNullable)(),
                takeUnannotatedF (*takeUnannotated)()) {
      (*takeNonnull)()(nullptr);  // false-negative
      (*takeNullable)()(nullptr);
      (*takeUnannotated)()(nullptr);
    }
  )cc"));

  // passing a reference to a nonnull pointer
  //
  // TODO(b/233582219): Fix false negative. When the nonnull pointer is passed
  // by reference into the callee which takes a nullable parameter, its value
  // may be changed to null, making it unsafe to dereference when we return from
  // the function call. Some possible approaches for handling this case:
  // (1) Disallow passing a nonnull pointer as a nullable reference - and warn
  // at the function call.
  // (2) Assume in worst case the nonnull pointer becomes nullable after the
  // call - and warn at the dereference.
  // (3) Sacrifice soundness for reduction in noise, and skip the warning.
  EXPECT_TRUE(checkDiagnostics(R"cc(
    void takeNonnullRef(int *_Nonnull &);
    void takeNullableRef(int *_Nullable &);
    void takeUnannotatedRef(int *&);
    void target(int *_Nonnull ptr_nonnull) {
      takeNonnullRef(ptr_nonnull);
      *ptr_nonnull;

      // false-negative
      takeNullableRef(ptr_nonnull);
      *ptr_nonnull;

      takeUnannotatedRef(ptr_nonnull);
      *ptr_nonnull;
    }
  )cc"));

  // passing a reference to a nullable pointer
  EXPECT_TRUE(checkDiagnostics(R"cc(
    void takeNonnullRef(int *_Nonnull &);
    void takeNullableRef(int *_Nullable &);
    void takeUnannotatedRef(int *&);
    void target(int *_Nullable ptr_nullable) {
      takeNonnullRef(ptr_nullable);  // [[unsafe]]
      *ptr_nullable;                 // [[unsafe]]

      takeNullableRef(ptr_nullable);
      *ptr_nullable;  // [[unsafe]]

      takeUnannotatedRef(ptr_nullable);
      *ptr_nullable;
    }
  )cc"));

  // passing a reference to an unannotated pointer
  //
  // TODO(b/233582219): Fix false negative. The unannotated pointer should be
  // considered nullable if it has been used as a nullable pointer.
  EXPECT_TRUE(checkDiagnostics(R"cc(
    void takeNonnullRef(int *_Nonnull &);
    void takeNullableRef(int *_Nullable &);
    void takeUnannotatedRef(int *&);
    void target(int *ptr_unannotated) {
      takeNonnullRef(ptr_unannotated);
      *ptr_unannotated;

      takeNullableRef(ptr_unannotated);
      *ptr_unannotated;  // false-negative

      takeUnannotatedRef(ptr_unannotated);
      *ptr_unannotated;
    }
  )cc"));
}

TEST(PointerNullabilityTest, CallExprMultiNonnullParams) {
  EXPECT_TRUE(checkDiagnostics(R"cc(
    void take(int *_Nonnull, int *_Nullable, int *_Nonnull);
    void target() {
      take(nullptr,  // [[unsafe]]
           nullptr,
           nullptr);  // [[unsafe]]
    }
  )cc"));
}

TEST(PointerNullabilityTest, CanOverwritePtrWithPtrCreatedFromRefReturnType) {
  // Test that if we create a pointer from a function returning a reference, we
  // can use that pointer to overwrite an existing nullable pointer and make it
  // nonnull.

  EXPECT_TRUE(checkDiagnostics(R"cc(
    int &get_int();

    void target(int *_Nullable i) {
      i = &get_int();
      *i;
    }
  )cc"));
}

TEST(PointerNullabilityTest, CanOverwritePtrWithPtrReturnedByFunction) {
  EXPECT_TRUE(checkDiagnostics(R"cc(
    int *_Nonnull get_int();

    void target(int *_Nullable i) {
      i = get_int();
      *i;
    }
  )cc"));
}

TEST(PointerNullabilityTest, CallVariadicFunction) {
  EXPECT_TRUE(checkDiagnostics(R"cc(
    void variadic(int *_Nonnull, ...);
    void target() {
      int i = 0;
      variadic(&i, nullptr, &i);
      variadic(nullptr, nullptr, &i);  // [[unsafe]]
    }
  )cc"));
}

TEST(PointerNullabilityTest, CallVariadicConstructor) {
  EXPECT_TRUE(checkDiagnostics(R"cc(
    struct S {
      S(int* _Nonnull, ...);
    };
    void target() {
      int i = 0;
      S(&i, nullptr, &i);
      S(nullptr, nullptr, &i);  // [[unsafe]]
    }
  )cc"));
}

TEST(PointerNullabilityTest, CallMemberOperatorNoParams) {
  EXPECT_TRUE(checkDiagnostics(R"cc(
    struct MakeNonnull {
      int *_Nonnull operator()();
    };
    struct MakeNullable {
      int *_Nullable operator()();
    };
    struct MakeUnannotated {
      int *operator()();
    };
    void target() {
      MakeNonnull makeNonnull;
      *makeNonnull();

      MakeNullable makeNullable;
      *makeNullable();  // [[unsafe]]

      MakeUnannotated makeUnannotated;
      *makeUnannotated();
    }
  )cc"));
}

TEST(PointerNullabilityTest, CallMemberOperatorOneParam) {
  // overloaded operator with single param
  EXPECT_TRUE(checkDiagnostics(R"cc(
    // map<int * _Nonnull, int>
    struct MapWithNonnullKeys {
      int &operator[](int *_Nonnull key);
    };
    // map<int * _Nullable, int>
    struct MapWithNullableKeys {
      int &operator[](int *_Nullable key);
    };
    // map<int *, int>
    struct MapWithUnannotatedKeys {
      int &operator[](int *key);
    };
    void target(int *_Nonnull ptr_nonnull, int *_Nullable ptr_nullable,
                int *ptr_unannotated) {
      MapWithNonnullKeys nonnull_keys;
      nonnull_keys[nullptr] = 42;  // [[unsafe]]
      nonnull_keys[ptr_nonnull] = 42;
      nonnull_keys[ptr_nullable] = 42;  // [[unsafe]]
      nonnull_keys[ptr_unannotated] = 42;

      MapWithNullableKeys nullable_keys;
      nullable_keys[nullptr] = 42;
      nullable_keys[ptr_nonnull] = 42;
      nullable_keys[ptr_nullable] = 42;
      nullable_keys[ptr_unannotated] = 42;

      MapWithUnannotatedKeys unannotated_keys;
      unannotated_keys[nullptr] = 42;
      unannotated_keys[ptr_nonnull] = 42;
      unannotated_keys[ptr_nullable] = 42;
      unannotated_keys[ptr_unannotated] = 42;
    }
  )cc"));
}

TEST(PointerNullabilityTest, CallMemberOperatorMultipleParams) {
  EXPECT_TRUE(checkDiagnostics(R"cc(
    struct TakeMixed {
      void operator()(int *, int *_Nullable, int *_Nonnull);
    };
    void target() {
      TakeMixed takeMixed;
      takeMixed(nullptr, nullptr, nullptr);  // [[unsafe]]
    }
  )cc"));
}

TEST(PointerNullabilityTest, CallFreeOperator) {
  // No nullability involved. This is just a regression test to make sure we can
  // process a call to a free overloaded operator.
  EXPECT_TRUE(checkDiagnostics(R"cc(
    struct A {};
    A operator+(A, A);
    void target() {
      A a;
      a = a + a;
    }
  )cc"));
}

// Check that we distinguish between the nullability of the return type and
// parameters.
TEST(PointerNullabilityTest, DistinguishFunctionReturnTypeAndParams) {
  EXPECT_TRUE(checkDiagnostics(R"cc(
    int *_Nullable callee(int *_Nonnull);

    void target() {
      int i = 0;
      __assert_nullability<NK_nullable>(callee(&i));
    }
  )cc"));
}

TEST(PointerNullabilityTest, DistinguishMethodReturnTypeAndParams) {
  EXPECT_TRUE(checkDiagnostics(R"cc(
    struct S {
      int *_Nullable callee(int *_Nonnull);
    };

    void target(S s) {
      int i = 0;
      __assert_nullability<NK_nullable>(s.callee(&i));
    }
  )cc"));
}

TEST(PointerNullabilityTest,
     ClassTemplate_DistinguishMethodReturnTypeAndParams) {
  EXPECT_TRUE(checkDiagnostics(R"cc(
    template <typename T0, typename T1>
    struct S {
      T0 callee(T1);
    };

    void target(S<int *_Nullable, int *_Nonnull> s) {
      int i = 0;
      __assert_nullability<NK_nullable>(s.callee(&i));
    }
  )cc"));
}

TEST(PointerNullabilityTest,
     CallFunctionTemplate_TemplateArgInReturnTypeHasNullTypeSourceInfo) {
  // This test sets up a function call where we don't have a `TypeSourceInfo`
  // for the argument to a template parameter used in the return type.
  // This is a regression test for a crash that we observed on real-world code.
  EXPECT_TRUE(checkDiagnostics(R"cc(
    template <class T>
    struct A {
      using Type = T;
    };
    template <int, class T>
    typename A<T>::Type f(T);
    void target() { f<0>(1); }
  )cc"));
}

TEST(PointerNullabilityTest, CallFunctionTemplate_PartiallyDeduced) {
  EXPECT_TRUE(checkDiagnostics(R"cc(
    template <int, class T>
    T f(T);
    void target() { f<0>(1); }
  )cc"));
}

TEST(PointerNullabilityTest, CallBuiltinFunction) {
  // Crash repro.
  EXPECT_TRUE(checkDiagnostics(R"cc(
    void target() { __builtin_operator_new(0); }
  )cc"));
}

TEST(PointerNullabilityTest, CallNoreturnDestructor) {
  // This test demonstrates that the check considers program execution to end
  // when a `noreturn` destructor is called.
  // Among other things, this is intended to demonstrate that Abseil's logging
  // instruction `LOG(FATAL)` (which creates an object with a `noreturn`
  // destructor) is correctly interpreted as terminating the program.
  EXPECT_TRUE(checkDiagnostics(R"cc(
    struct Fatal {
      __attribute__((noreturn)) ~Fatal();
      void method(int);
    };
    void target(int* _Nullable p) {
      // Do warn here, as the `*p` dereference happens before the `Fatal` object
      // is destroyed.
      Fatal().method(*p);  // [[unsafe]]
      // Don't warn here: We know that this code never gets executed.
      *p;
    }
  )cc"));
}

TEST(PointerNullabilityTest, ConstMethodNoParamsCheckFirst) {
  EXPECT_TRUE(checkDiagnostics(R"cc(
    struct C {
      int *_Nullable property() const { return x; }
      int *_Nullable x = nullptr;
    };
    void target() {
      C obj;
      if (obj.property() != nullptr) *obj.property();
    }
  )cc"));
}

TEST(PointerNullabilityTest, ConstMethodNoImpl) {
  EXPECT_TRUE(checkDiagnostics(R"cc(
    struct C {
      int *_Nullable property() const;
      void may_mutate();
    };
    void target() {
      C obj;
      if (obj.property() != nullptr) {
        obj.may_mutate();
        *obj.property();  // [[unsafe]]
      };
      if (obj.property() != nullptr) *obj.property();
    }
  )cc"));
}

TEST(PointerNullabilityTest, ConstMethodEarlyReturn) {
  EXPECT_TRUE(checkDiagnostics(R"cc(
    struct C {
      int *_Nullable property() const;
    };
    void target() {
      C c;
      if (!c.property()) return;
      // No false positive in this case, as there is no join.
      *c.property();
    }
  )cc"));
}

TEST(PointerNullabilityTest, ConstMethodEarlyReturnWithConditional) {
  EXPECT_TRUE(checkDiagnostics(R"cc(
    struct C {
      int *_Nullable property() const;
    };
    bool cond();
    void some_operation(int);
    void target() {
      C c;
      if (!c.property()) return;
      if (cond()) {
        some_operation(1);
      } else {
        some_operation(2);
      }
      // TODO(b/309667920): False positive below due to clearing the const
      // method return values on join. If this is a pattern that occurs
      // frequently in real code, we need to fix this.
      *c.property();  // [[unsafe]]
    }
  )cc"));
}

TEST(PointerNullabilityTest, ConstMethodNoRecordForCallObject) {
  EXPECT_TRUE(checkDiagnostics(R"cc(
    struct S {
      int* _Nullable property() const;
    };

    S makeS();

    void target() {
      if (makeS().property()) {
        // This is a const member call on a different object, so it's not safe.
        // But this line and the line above also don't cause any crashes.
        *(makeS().property());  // [[unsafe]]
      }
    }
  )cc"));
}

TEST(PointerNullabilityTest, FieldUndefinedValue) {
  EXPECT_TRUE(checkDiagnostics(R"cc(
    struct C {
      int *_Nullable property() const { return x; }
      int *_Nullable x = nullptr;
    };
    C foo();
    void target() {
      C obj;
      if (foo().x != nullptr) *foo().x;  // [[unsafe]]
    }
  )cc"));
}

TEST(PointerNullabilityTest, Accessor_BaseObjectReturnedByReference) {
  // Crash repro:
  // If the base object of the accessor call expression is a reference returned
  // from a function call, we have a storage location for the object but no
  // values for its fields. Check that we don't crash in this case.
  EXPECT_TRUE(checkDiagnostics(R"cc(
    struct C {
      int *_Nullable property() const { return x; }
      int *_Nullable x = nullptr;
    };
    C &foo();
    void target() {
      if (foo().property() != nullptr) int x = *foo().property();  // [[unsafe]]
    }
  )cc"));
}

TEST(PointerNullabilityTest, MethodNoParamsUndefinedValue) {
  EXPECT_TRUE(checkDiagnostics(R"cc(
    struct C {
      int *_Nullable property() const { return x; }
      int *_Nullable x = nullptr;
    };
    void target() {
      int x = 0;
      if (C().property() != nullptr) {
        *C().property();  // [[unsafe]]
      }
      C obj;
      if (obj.property() != nullptr) {
        *obj.property();
      }
    }
  )cc"));
}

}  // namespace
}  // namespace clang::tidy::nullability
