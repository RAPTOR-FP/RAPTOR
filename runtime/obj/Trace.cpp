//===- Trace.cpp - FLOP Tracing wrappers ---------------------------------===//
//
//                             Raptor Project
//
// Part of the Raptor Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// If using this code in an academic setting, please cite the following:
// @incollection{raptorNeurips,
// title = {Instead of Rewriting Foreign Code for Machine Learning,
//          Automatically Synthesize Fast Gradients},
// author = {Moses, William S. and Churavy, Valentin},
// booktitle = {Advances in Neural Information Processing Systems 33},
// year = {2020},
// note = {To appear in},
// }
//
//===----------------------------------------------------------------------===//
//
// This file contains infrastructure for flop tracing
//
// It is implemented as a .cpp file and not as a header becaues we want to use
// C++ features and still be able to use it in C code.
//
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <stdint.h>
#include <stdlib.h>

#include "raptor/Common.h"
#include "raptor/raptor.h"

#ifndef RAPTOR_FPRT_TRACE_PRINT
#define RAPTOR_FPRT_TRACE_PRINT 1
#endif

static constexpr unsigned fp_max_inputs = 3;
static constexpr std::array<const char *, 3> arg_names = {"x", "y", "z"};
static_assert(arg_names.size() == fp_max_inputs);

extern "C" {
typedef struct __raptor_fp {
private:
  double result;
  unsigned char input_num;
  const char *loc;
  __raptor_fp *inputs[fp_max_inputs];
  double derivatives[fp_max_inputs];
#if RAPTOR_FPRT_TRACE_PRINT
  const char *name;
#endif

public:
  size_t id;

  double getDerivative(unsigned no) const { return derivatives[no]; }
  void setDerivative(unsigned no, double d) { derivatives[no] = d; }

  __raptor_fp *getInput(unsigned no) const { return inputs[no]; }
  void setInput(unsigned no, __raptor_fp *i) { inputs[no] = i; }

  unsigned char getInputNum() const { return input_num; }
  void setInputNum(unsigned char i) { input_num = i; }

  double getResult() const { return result; }
  void setResult(double r) { result = r; }

  const char *getLoc() const { return loc; }
  void setLoc(const char *l) { loc = l; }

#if RAPTOR_FPRT_TRACE_PRINT
  const char *getName() const { return name; }
  void setName(const char *l) { name = l; }
#endif

} __raptor_fp;
}

static void print_raptor_fp_derivatives(std::ostream &out,
                                        const __raptor_fp *fp) {
  auto seen = false;
  for (unsigned i = 0; i < fp->getInputNum(); i++) {
    if (seen)
      out << ", ";
    seen = true;
    out << "d" << arg_names[i] << " = " << fp->getDerivative(i);
  }
}
static void print_raptor_fp_value(std::ostream &out, const __raptor_fp *fp) {
  out << "[" << fp << ": " << fp->getResult() << "]";
}
static void print_raptor_fp_function(std::ostream &out, const __raptor_fp *fp) {
  std::cerr << fp->getName() << "(";
  bool seen = false;
  for (unsigned i = 0; i < fp->getInputNum(); i++) {
    if (seen)
      std::cerr << ", ";
    seen = true;
    __raptor_fp *fpinput = fp->getInput(i);
    print_raptor_fp_value(std::cerr, fpinput);
  }
  std::cerr << ")";
}
static void print_raptor_fp(std::ostream &out, const __raptor_fp *fp) {
  print_raptor_fp_function(out, fp);
  out << " -> ";
  print_raptor_fp_value(out, fp);
  out << " ";
  print_raptor_fp_derivatives(out, fp);
  out << " at " << fp->getLoc();
  out << std::endl;
}

template <typename T, unsigned NumInputs>
static void __raptor_fprt_trace_no_res_flop(std::array<T, NumInputs> inputs,
                                            const char *name, const char *loc) {
  __raptor_fp fp;
  fp.setInputNum(NumInputs);
  fp.setLoc(loc);
  for (unsigned i = 0; i < inputs.size(); i++) {
    __raptor_fp *inputfp = __raptor_fprt_ieee_64_to_ptr(inputs[i]);
    fp.setInput(i, inputfp);
  }

#if RAPTOR_FPRT_TRACE_PRINT
  fp.setName(name);
  print_raptor_fp_function(std::cerr, &fp);
  std::cerr << " at " << loc << std::endl;
#endif
}

namespace {
template <typename T, unsigned NumInputs, unsigned Input> class Derivative {
public:
  __attribute__((always_inline)) static T get(void *fn,
                                              std::array<T, NumInputs> inputs) {
    return 0;
  }
};
template <typename T> class Derivative<T, 1, 0> {
public:
  __attribute__((always_inline)) static T get(void *fn,
                                              std::array<T, 1> inputs) {
    typedef double (*fty)(double);
    return __raptor_fwddiff<T>((fty)fn, raptor_dup, inputs[0], 1.0);
  }
};
template <typename T> class Derivative<T, 2, 0> {
public:
  __attribute__((always_inline)) static T get(void *fn,
                                              std::array<T, 2> inputs) {
    typedef double (*fty)(double);
    return __raptor_fwddiff<T>((fty)fn, raptor_dup, inputs[0], 1.0,
                               raptor_const, inputs[1]);
  }
};
template <typename T> class Derivative<T, 2, 1> {
public:
  __attribute__((always_inline)) static T get(void *fn,
                                              std::array<T, 2> inputs) {
    typedef double (*fty)(double);
    return __raptor_fwddiff<T>((fty)fn, raptor_const, inputs[0], raptor_dup,
                               inputs[1], 1.0);
  }
};
template <typename T> class Derivative<T, 3, 0> {
public:
  __attribute__((always_inline)) static T get(void *fn,
                                              std::array<T, 3> inputs) {
    typedef double (*fty)(double);
    // clang-format off
    return __raptor_fwddiff<T>((fty)fn,
                               raptor_dup, inputs[0], 1.0,
                               raptor_const, inputs[1],
                               raptor_const, inputs[2]
                               );
    // clang-format on
  }
};
template <typename T> class Derivative<T, 3, 1> {
public:
  __attribute__((always_inline)) static T get(void *fn,
                                              std::array<T, 3> inputs) {
    typedef double (*fty)(double);
    // clang-format off
    return __raptor_fwddiff<T>((fty)fn,
                               raptor_const, inputs[0],
                               raptor_dup, inputs[1], 1.0,
                               raptor_const, inputs[2]
                               );
    // clang-format on
  }
};
template <typename T> class Derivative<T, 3, 2> {
public:
  __attribute__((always_inline)) static T get(void *fn,
                                              std::array<T, 3> inputs) {
    typedef double (*fty)(double);
    // clang-format off
    return __raptor_fwddiff<T>((fty)fn,
                               raptor_const, inputs[0],
                               raptor_const, inputs[1],
                               raptor_dup, inputs[2], 1.0
                               );
    // clang-format on
  }
};
} // namespace

template <typename T, unsigned NumInputs>
__attribute__((always_inline)) static void
__raptor_fprt_trace_flop(std::array<T, NumInputs> _inputs, T output_val,
                         __raptor_fp *outfp, void *fn, const char *name,
                         const char *loc) {
  std::array<__raptor_fp *, NumInputs> inputs;
  std::array<T, NumInputs> input_vals;
  for (unsigned i = 0; i < _inputs.size(); i++) {
    __raptor_fp *inputfp = __raptor_fprt_ieee_64_to_ptr(_inputs[i]);
    inputs[i] = inputfp;
    input_vals[i] = inputfp->getResult();
  }

  outfp->setResult(output_val);
  outfp->setInputNum(inputs.size());
  outfp->setLoc(loc);
  for (unsigned i = 0; i < inputs.size(); i++) {
    outfp->setInput(i, inputs[i]);
    T d;
    static_assert(inputs.size() <= fp_max_inputs);
    if (i == 0)
      d = Derivative<T, NumInputs, 0>::get(fn, input_vals);
    else if (i == 1)
      d = Derivative<T, NumInputs, 1>::get(fn, input_vals);
    else if (i == 2)
      d = Derivative<T, NumInputs, 2>::get(fn, input_vals);
    else
      llvm_unreachable("impossible");
    outfp->setDerivative(i, d);
  }

#if RAPTOR_FPRT_TRACE_PRINT
  outfp->setName(name);
  print_raptor_fp(std::cerr, outfp);
#endif
}

// TODO ultimately we probably want a linked list of arrays or something like
// that for this (std::list probably is that but we may want our own impl)
struct {
  std::list<__raptor_fp> all;
  std::list<__raptor_fp *> outputs;
  std::list<__raptor_fp *> inputs;
  std::list<__raptor_fp *> consts;
  void clear() {
    all.clear();
    outputs.clear();
    inputs.clear();
  }
} FPs;

extern "C" {

__raptor_fp *__raptor_fprt_ieee_64_new_intermediate(int64_t exponent,
                                                    int64_t significand,
                                                    int64_t mode,
                                                    const char *loc) {
  size_t id = FPs.all.size();
  FPs.all.push_back({});
  __raptor_fp *a = &FPs.all.back();
  a->id = id;
  return a;
}

double __raptor_fprt_ieee_64_get(double _a, int64_t exponent,
                                 int64_t significand, int64_t mode,
                                 const char *loc) {
  __raptor_fp *a = __raptor_fprt_ieee_64_to_ptr(_a);
  FPs.outputs.push_back(a);
  __raptor_fprt_trace_no_res_flop<double, 1>({_a}, "get", loc);
  return a->getResult();
}

double __raptor_fprt_ieee_64_new(double _a, int64_t exponent,
                                 int64_t significand, int64_t mode,
                                 const char *loc) {
  __raptor_fp *a =
      __raptor_fprt_ieee_64_new_intermediate(exponent, significand, mode, loc);
  FPs.inputs.push_back(a);
  __raptor_fprt_trace_flop<double, 0>({}, _a, a, nullptr, "new", loc);
  auto ret = __raptor_fprt_ptr_to_double(a);
  return ret;
}

double __raptor_fprt_ieee_64_const(double _a, int64_t exponent,
                                   int64_t significand, int64_t mode,
                                   const char *loc) {
  // TODO This should really be called only once for an appearance in the code,
  // currently it is called every time a flop uses a constant.
  __raptor_fp *a =
      __raptor_fprt_ieee_64_new_intermediate(exponent, significand, mode, loc);
  FPs.consts.push_back(a);
  __raptor_fprt_trace_flop<double, 0>({}, _a, a, nullptr, "const", loc);
  auto ret = __raptor_fprt_ptr_to_double(a);
  return ret;
}

void __raptor_fprt_ieee_64_delete(double a, int64_t exponent,
                                  int64_t significand, int64_t mode,
                                  const char *loc) {
  // TODO
  __raptor_fprt_trace_no_res_flop<double, 1>({a}, "delete", loc);
}

// Below sensitivity computation is taken frmo ADAPT
static double __raptor_estimate_truncation_error(double a) {
  return abs(a - (float)a);
}

void __raptor_fprt_delete_all() {
  size_t size = FPs.all.size();
  size_t i = 0;
  for (auto it = FPs.all.begin(); it != FPs.all.end(); i++, it++) {
    // Do not truncate inputs
    if (std::find(FPs.inputs.begin(), FPs.inputs.end(), &*it) !=
        FPs.inputs.end())
      continue;
    // Or consts
    if (std::find(FPs.consts.begin(), FPs.consts.end(), &*it) !=
        FPs.consts.end())
      continue;

    // Zero out all errors
    // TODO is it faster to calloc each time or should we pre-allocate and
    // memset?
    double *errors = (double *)std::calloc(size, sizeof(*errors));
    // Introduce truncation error into the current op
    // TODO we can probably re-run the original operation in the truncated
    // precision thus get the real error and not an estimation
    errors[i] = __raptor_estimate_truncation_error(it->getResult());

    size_t j = i;
    for (auto jt = it; jt != FPs.all.end(); j++, jt++)
      for (unsigned char k = 0; k < jt->getInputNum(); k++)
        errors[j] += abs(jt->getDerivative(k) * errors[jt->getInput(k)->id]);

#if RAPTOR_FPRT_TRACE_PRINT
    std::cerr << "For instance ";
    print_raptor_fp_value(std::cerr, &*it);
    std::cerr << " when truncated from double to float:" << std::endl;

    for (__raptor_fp *output : FPs.outputs) {
      std::cerr << "    wrt output ";
      print_raptor_fp_value(std::cerr, output);
      std::cerr << " at " << output->getLoc()
                << ", sensitivity = " << errors[output->id] << std::endl;
    }
#endif
  }
  FPs.clear();
}

#define __RAPTOR_MPFR_SINGOP(OP_TYPE, LLVM_OP_NAME, MPFR_FUNC_NAME, FROM_TYPE, \
                             RET, MPFR_GET, ARG1, MPFR_SET_ARG1,               \
                             ROUNDING_MODE)                                    \
  __RAPTOR_MPFR_ATTRIBUTES                                                     \
  RET __raptor_fprt_original_##FROM_TYPE##_##OP_TYPE##_##LLVM_OP_NAME(ARG1 a); \
  __RAPTOR_MPFR_ATTRIBUTES                                                     \
  RET __raptor_fprt_##FROM_TYPE##_##OP_TYPE##_##LLVM_OP_NAME(                  \
      ARG1 a, int64_t exponent, int64_t significand, int64_t mode,             \
      const char *loc) {                                                       \
    auto originalfn =                                                          \
        __raptor_fprt_original_##FROM_TYPE##_##OP_TYPE##_##LLVM_OP_NAME;       \
    RET res = originalfn(__raptor_fprt_ieee_64_to_ptr(a)->getResult());        \
    __raptor_fp *intermediate = __raptor_fprt_ieee_64_new_intermediate(        \
        exponent, significand, mode, loc);                                     \
    intermediate->setResult(res);                                              \
    double ret = __raptor_fprt_ptr_to_double(intermediate);                    \
    __raptor_fprt_trace_flop<RET, 1>({a}, res, intermediate,                   \
                                     (void *)originalfn, #LLVM_OP_NAME, loc);  \
    return ret;                                                                \
  }

// TODO this is a bit sketchy if the user cast their float to int before calling
// this. We need to detect these patterns
#define __RAPTOR_MPFR_BIN_INT(OP_TYPE, LLVM_OP_NAME, MPFR_FUNC_NAME,           \
                              FROM_TYPE, RET, MPFR_GET, ARG1, MPFR_SET_ARG1,   \
                              ARG2, ROUNDING_MODE)                             \
  __RAPTOR_MPFR_ORIGINAL_ATTRIBUTES                                            \
  RET __raptor_fprt_original_##FROM_TYPE##_##OP_TYPE##_##LLVM_OP_NAME(ARG1 a,  \
                                                                      ARG2 b); \
  __RAPTOR_MPFR_ATTRIBUTES RET                                                 \
      __raptor_fprt_##FROM_TYPE##_##OP_TYPE##_##LLVM_OP_NAME(                  \
          ARG1 a, ARG2 b, int64_t exponent, int64_t significand, int64_t mode, \
          const char *loc) {                                                   \
    auto originalfn =                                                          \
        __raptor_fprt_original_##FROM_TYPE##_##OP_TYPE##_##LLVM_OP_NAME;       \
    RET res = originalfn(__raptor_fprt_ieee_64_to_ptr(a)->getResult(),         \
                         __raptor_fprt_ieee_64_to_ptr(b)->getResult());        \
    __raptor_fp *intermediate = __raptor_fprt_ieee_64_new_intermediate(        \
        exponent, significand, mode, loc);                                     \
    intermediate->setResult(res);                                              \
    double ret = __raptor_fprt_ptr_to_double(intermediate);                    \
    __raptor_fprt_trace_flop<RET, 1>({a}, res, intermediate,                   \
                                     (void *)originalfn, #LLVM_OP_NAME, loc);  \
    return ret;                                                                \
  }

#define __RAPTOR_MPFR_BIN(OP_TYPE, LLVM_OP_NAME, MPFR_FUNC_NAME, FROM_TYPE,    \
                          RET, MPFR_GET, ARG1, MPFR_SET_ARG1, ARG2,            \
                          MPFR_SET_ARG2, ROUNDING_MODE)                        \
  __RAPTOR_MPFR_ORIGINAL_ATTRIBUTES                                            \
  RET __raptor_fprt_original_##FROM_TYPE##_##OP_TYPE##_##LLVM_OP_NAME(ARG1 a,  \
                                                                      ARG2 b); \
  __RAPTOR_MPFR_ATTRIBUTES                                                     \
  RET __raptor_fprt_##FROM_TYPE##_##OP_TYPE##_##LLVM_OP_NAME(                  \
      ARG1 a, ARG2 b, int64_t exponent, int64_t significand, int64_t mode,     \
      const char *loc) {                                                       \
    auto originalfn =                                                          \
        __raptor_fprt_original_##FROM_TYPE##_##OP_TYPE##_##LLVM_OP_NAME;       \
    RET res = originalfn(__raptor_fprt_ieee_64_to_ptr(a)->getResult(),         \
                         __raptor_fprt_ieee_64_to_ptr(b)->getResult());        \
    __raptor_fp *intermediate = __raptor_fprt_ieee_64_new_intermediate(        \
        exponent, significand, mode, loc);                                     \
    intermediate->setResult(res);                                              \
    double ret = __raptor_fprt_ptr_to_double(intermediate);                    \
    __raptor_fprt_trace_flop<RET, 2>({a, b}, res, intermediate,                \
                                     (void *)originalfn, #LLVM_OP_NAME, loc);  \
    return ret;                                                                \
  }

#define __RAPTOR_MPFR_FMULADD(LLVM_OP_NAME, FROM_TYPE, TYPE, MPFR_TYPE,         \
                              LLVM_TYPE, ROUNDING_MODE)                         \
  __RAPTOR_MPFR_ORIGINAL_ATTRIBUTES                                             \
  TYPE __raptor_fprt_original_##FROM_TYPE##_intr_##LLVM_OP_NAME##_##LLVM_TYPE(  \
      TYPE a, TYPE b, TYPE c);                                                  \
  __RAPTOR_MPFR_ATTRIBUTES                                                      \
  TYPE __raptor_fprt_##FROM_TYPE##_intr_##LLVM_OP_NAME##_##LLVM_TYPE(           \
      TYPE a, TYPE b, TYPE c, int64_t exponent, int64_t significand,            \
      int64_t mode, const char *loc) {                                          \
    auto originalfn =                                                           \
        __raptor_fprt_original_##FROM_TYPE##_intr_##LLVM_OP_NAME##_##LLVM_TYPE; \
    TYPE res = originalfn(__raptor_fprt_ieee_64_to_ptr(a)->getResult(),         \
                          __raptor_fprt_ieee_64_to_ptr(b)->getResult(),         \
                          __raptor_fprt_ieee_64_to_ptr(c)->getResult());        \
    __raptor_fp *intermediate = __raptor_fprt_ieee_64_new_intermediate(         \
        exponent, significand, mode, loc);                                      \
    intermediate->setResult(res);                                               \
    double ret = __raptor_fprt_ptr_to_double(intermediate);                     \
    __raptor_fprt_trace_flop<TYPE, 3>({a, b, c}, res, intermediate,             \
                                      (void *)originalfn, #LLVM_OP_NAME, loc);  \
    return ret;                                                                 \
  }

#define __RAPTOR_MPFR_FCMP_IMPL(NAME, ORDERED, CMP, FROM_TYPE, TYPE, MPFR_GET, \
                                ROUNDING_MODE)                                 \
  __RAPTOR_MPFR_ORIGINAL_ATTRIBUTES                                            \
  bool __raptor_fprt_original_##FROM_TYPE##_fcmp_##NAME(TYPE a, TYPE b);       \
  __RAPTOR_MPFR_ATTRIBUTES                                                     \
  bool __raptor_fprt_##FROM_TYPE##_fcmp_##NAME(                                \
      TYPE a, TYPE b, int64_t exponent, int64_t significand, int64_t mode,     \
      const char *loc) {                                                       \
    bool res = __raptor_fprt_original_##FROM_TYPE##_fcmp_##NAME(               \
        __raptor_fprt_ieee_64_to_ptr(a)->getResult(),                          \
        __raptor_fprt_ieee_64_to_ptr(b)->getResult());                         \
    __raptor_fprt_trace_no_res_flop<TYPE, 2>({a, b}, "fcmp_" #NAME, loc);      \
    return res;                                                                \
  }

__RAPTOR_MPFR_ORIGINAL_ATTRIBUTES
bool __raptor_fprt_original_ieee_64_intr_llvm_is_fpclass_f64(double a,
                                                             int32_t tests);
__RAPTOR_MPFR_ATTRIBUTES bool __raptor_fprt_ieee_64_intr_llvm_is_fpclass_f64(
    double a, int32_t tests, int64_t exponent, int64_t significand,
    int64_t mode, const char *loc) {
  __raptor_fprt_trace_no_res_flop<double, 1>({a}, "llvm_is_fpclass_f64", loc);
  return __raptor_fprt_original_ieee_64_intr_llvm_is_fpclass_f64(
      __raptor_fprt_ieee_64_to_ptr(a)->getResult(), tests);
}

#include <raptor/fprt/flops.def>

} // extern "C"
