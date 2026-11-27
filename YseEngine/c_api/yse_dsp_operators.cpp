#include "yse_c/yse_dsp_operators.h"
#include "yse_dsp_processor_internal.hpp"

#include "../dsp/operators/accumulator.hpp"
#include "../dsp/operators/arithmetic.hpp"
#include "../dsp/operators/bitwise.hpp"
#include "../dsp/operators/compare.hpp"
#include "../dsp/operators/degrade.hpp"
#include "../dsp/operators/delta.hpp"
#include "../dsp/operators/downsample.hpp"
#include "../dsp/operators/fold.hpp"
#include "../dsp/operators/overdrive.hpp"
#include "../dsp/operators/polar.hpp"
#include "../dsp/operators/rounding.hpp"
#include "../dsp/operators/scale.hpp"
#include "../dsp/operators/trig.hpp"
#include "../dsp/operators/units.hpp"

namespace {
  namespace DSP = YSE::DSP;
  using yse_c::processor::drive;
  using yse_c::processor::emit;
  using yse_c::processor::handle;
  using yse_c::processor::make;
  using yse_c::processor::object;
  using yse_c::processor::one_signal;
  using yse_c::processor::query;
  using yse_c::processor::stage;
  using yse_c::processor::three_signals;
  using yse_c::processor::two_signals;
  using yse_c::processor::with;

  // One wrapper type per class; the count is how many host arrays a process
  // call stages (the input plus a signal operand or the signal settings).
  using Accumulator = handle<DSP::accumulator, 2>;
  using ReverseSubtract = handle<DSP::reverseSubtract, 2>;
  using ReverseDivide = handle<DSP::reverseDivide, 2>;
  using Modulo = handle<DSP::modulo, 2>;
  using Bitwise = handle<DSP::bitwise, 2>;
  using Bitnot = handle<DSP::bitnot, 1>;
  using Bitshift = handle<DSP::bitshift, 1>;
  using Bitsafe = handle<DSP::bitsafe, 1>;
  using Compare = handle<DSP::compare, 2>;
  using Minimum = handle<DSP::minimum, 2>;
  using Maximum = handle<DSP::maximum, 2>;
  using Degrade = handle<DSP::degrade, 3>;
  using Delta = handle<DSP::delta, 1>;
  using Downsample = handle<DSP::downsample, 2>;
  using Fold = handle<DSP::fold, 3>;
  using Overdrive = handle<DSP::overdrive, 2>;
  using CartToPolar = handle<DSP::cartToPolar, 2>;
  using PolarToCart = handle<DSP::polarToCart, 2>;
  using PhaseWrap = handle<DSP::phaseWrap, 1>;
  using Round = handle<DSP::round, 2>;
  using Trunc = handle<DSP::trunc, 1>;
  using Scale = handle<DSP::scale, 6>;
  using Trig = handle<DSP::trig, 1>;
  using ArcTangent2 = handle<DSP::arcTangent2, 2>;
  using AmpToDb = handle<DSP::ampToDb, 1>;
  using DbToAmp = handle<DSP::dbToAmp, 1>;
  using MsToSamples = handle<DSP::msToSamples, 1>;
  using SamplesToMs = handle<DSP::samplesToMs, 1>;

  // The constants the header spells as macros.
  static_assert(YSE_DSP_DEGRADE_DEFAULT_BITS == DSP::degrade::DEFAULT_BITS,
                "yse_dsp_operators.h: YSE_DSP_DEGRADE_DEFAULT_BITS drifted");
  static_assert(YSE_DSP_DEGRADE_MIN_BITS == DSP::degrade::MIN_BITS,
                "yse_dsp_operators.h: YSE_DSP_DEGRADE_MIN_BITS drifted");
  static_assert(YSE_DSP_DEGRADE_MAX_BITS == DSP::degrade::MAX_BITS,
                "yse_dsp_operators.h: YSE_DSP_DEGRADE_MAX_BITS drifted");
  static_assert(YSE_DSP_DOWNSAMPLE_MIN_PERIOD == DSP::downsample::MIN_PERIOD,
                "yse_dsp_operators.h: YSE_DSP_DOWNSAMPLE_MIN_PERIOD drifted");
  static_assert(YSE_DSP_DB_FLOOR == DSP::DB_FLOOR, "yse_dsp_operators.h: YSE_DSP_DB_FLOOR drifted");
  // A mask crosses as unsigned int and a shift as int: both must be the
  // engine's 32-bit Int.
  static_assert(sizeof(unsigned int) == sizeof(Int) && sizeof(int) == sizeof(Int),
                "yse_dsp_operators.h: a bit mask must be 32 bits");

  // Whether an enum crossing the ABI is one of the values 0 .. last.
  bool in_enum(int value, int last) noexcept {
    return value >= 0 && value <= last;
  }

  // An enum crossing the ABI as the engine enum E, if it is within 0 .. last.
  template <typename E> bool to_enum(int value, int last, E& out) noexcept {
    if (!in_enum(value, last)) return false;
    out = static_cast<E>(value);
    return true;
  }

  // A constructor's enum: the engine default for a value outside the enum.
  template <typename E> E or_default(int value, int last, E fallback) noexcept {
    E e = fallback;
    to_enum(value, last, e);
    return e;
  }

  DSP::BIT_MODE bit_mode(YseDspBitMode mode) noexcept {
    return or_default(mode, YSE_DSP_BIT_INTEGER_RAW, DSP::BIT_MODE::RAW);
  }

  // A one-in, one-out block at the current settings.
  template <typename H>
  unsigned int plain(YseDspProcessor* p, const float* in, float* out, unsigned int length) {
    return one_signal<H>(p, in, out, length,
                         [](H& h, DSP::buffer& a) -> DSP::buffer& { return h.object(a); });
  }

  // A binary operator's block against one number.
  template <typename H>
  unsigned int against(YseDspProcessor* p, const float* in, float right, float* out,
                       unsigned int length) {
    return one_signal<H>(p, in, out, length,
                         [&](H& h, DSP::buffer& a) -> DSP::buffer& { return h.object(a, right); });
  }

  // A block with one more staged array: a binary operator's signal operand,
  // or a per-sample setting.
  template <typename H>
  unsigned int paired(YseDspProcessor* p, const float* in, const float* second, float* out,
                      unsigned int length) {
    return two_signals<H>(
        p, in, second, out, length,
        [](H& h, DSP::buffer& a, DSP::buffer& b) -> DSP::buffer& { return h.object(a, b); });
  }

  // A block with two more staged arrays.
  template <typename H>
  unsigned int tripled(YseDspProcessor* p, const float* in, const float* second, const float* third,
                       float* out, unsigned int length) {
    return three_signals<H>(p, in, second, third, out, length,
                            [](H& h, DSP::buffer& a, DSP::buffer& b,
                               DSP::buffer& c) -> DSP::buffer& { return h.object(a, b, c); });
  }

  // One sample through the handle's process(), 0 if `p` is not an H.
  template <typename H> float one(YseDspProcessor* p, float in) {
    auto* o = object<H>(p);
    return o != nullptr ? o->process(in) : 0.f;
  }

  // The polar converters: `run(h, o, n)` makes one engine block, whose two
  // outputs `first` / `second` go to the optional host arrays.
  template <typename H, typename Run, typename First, typename Second>
  unsigned int two_outputs(YseDspProcessor* p, float* a, float* b, unsigned int length, Run&& run,
                           First&& first, Second&& second) {
    return drive<H>(p, length, [&](H& h, unsigned o, unsigned n) {
      run(h, o, n);
      emit(first(h.object), a != nullptr ? a + o : nullptr, n);
      emit(second(h.object), b != nullptr ? b + o : nullptr, n);
    });
  }

  // Write a value call's result to an optional output.
  void put(float* dst, float value) noexcept {
    if (dst != nullptr) *dst = value;
  }
} // namespace

extern "C" {

// ─── accumulator ────────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_accumulator_create(double initial) {
  return make<Accumulator>("yse_dsp_accumulator_create", initial);
}
YSE_C_API void yse_dsp_accumulator_set(YseDspProcessor* p, double value) {
  with<Accumulator>(p, [&](DSP::accumulator& f) { f.set(value); });
}
YSE_C_API void yse_dsp_accumulator_reset(YseDspProcessor* p) {
  with<Accumulator>(p, [](DSP::accumulator& f) { f.reset(); });
}
YSE_C_API double yse_dsp_accumulator_get_sum(YseDspProcessor* p) {
  return query<Accumulator>(p, 0.0, [](DSP::accumulator& f) { return f.sum(); });
}
YSE_C_API float yse_dsp_accumulator_process_sample(YseDspProcessor* p, float in) {
  return one<Accumulator>(p, in);
}
YSE_C_API float yse_dsp_accumulator_process_sample_reset(YseDspProcessor* p, float in,
                                                         float reset) {
  return query<Accumulator>(p, 0.f, [&](DSP::accumulator& f) { return f.process(in, reset); });
}
YSE_C_API unsigned int yse_dsp_accumulator_process(YseDspProcessor* p, const float* in, float* out,
                                                   unsigned int length) {
  return plain<Accumulator>(p, in, out, length);
}
YSE_C_API unsigned int yse_dsp_accumulator_process_reset(YseDspProcessor* p, const float* in,
                                                         const float* reset, float* out,
                                                         unsigned int length) {
  return paired<Accumulator>(p, in, reset, out, length);
}

// ─── reverseSubtract, reverseDivide, modulo ─────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_reverse_subtract_create(void) {
  return make<ReverseSubtract>("yse_dsp_reverse_subtract_create");
}
YSE_C_API float yse_dsp_reverse_subtract_value(float in, float right) {
  return DSP::reverseSubtract::process(in, right);
}
YSE_C_API unsigned int yse_dsp_reverse_subtract_process(YseDspProcessor* p, const float* in,
                                                        float right, float* out,
                                                        unsigned int length) {
  return against<ReverseSubtract>(p, in, right, out, length);
}
YSE_C_API unsigned int yse_dsp_reverse_subtract_process_signal(YseDspProcessor* p, const float* in,
                                                               const float* right, float* out,
                                                               unsigned int length) {
  return paired<ReverseSubtract>(p, in, right, out, length);
}

YSE_C_API YseDspProcessor* yse_dsp_reverse_divide_create(void) {
  return make<ReverseDivide>("yse_dsp_reverse_divide_create");
}
YSE_C_API float yse_dsp_reverse_divide_value(float in, float right) {
  return DSP::reverseDivide::process(in, right);
}
YSE_C_API unsigned int yse_dsp_reverse_divide_process(YseDspProcessor* p, const float* in,
                                                      float right, float* out,
                                                      unsigned int length) {
  return against<ReverseDivide>(p, in, right, out, length);
}
YSE_C_API unsigned int yse_dsp_reverse_divide_process_signal(YseDspProcessor* p, const float* in,
                                                             const float* right, float* out,
                                                             unsigned int length) {
  return paired<ReverseDivide>(p, in, right, out, length);
}

YSE_C_API YseDspProcessor* yse_dsp_modulo_create(void) {
  return make<Modulo>("yse_dsp_modulo_create");
}
YSE_C_API float yse_dsp_modulo_value(float in, float divisor) {
  return DSP::modulo::process(in, divisor);
}
YSE_C_API unsigned int yse_dsp_modulo_process(YseDspProcessor* p, const float* in, float divisor,
                                              float* out, unsigned int length) {
  return against<Modulo>(p, in, divisor, out, length);
}
YSE_C_API unsigned int yse_dsp_modulo_process_signal(YseDspProcessor* p, const float* in,
                                                     const float* divisor, float* out,
                                                     unsigned int length) {
  return paired<Modulo>(p, in, divisor, out, length);
}

// ─── bitwise ────────────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_bitwise_create(YseDspBitwiseOp op, unsigned int mask,
                                                  YseDspBitMode mode) {
  return make<Bitwise>("yse_dsp_bitwise_create",
                       or_default(op, YSE_DSP_BITWISE_XOR, DSP::bitwise::OP::AND),
                       static_cast<Int>(mask), bit_mode(mode));
}
YSE_C_API void yse_dsp_bitwise_set_op(YseDspProcessor* p, YseDspBitwiseOp op) {
  with<Bitwise>(p, [&](DSP::bitwise& f) {
    DSP::bitwise::OP e;
    if (to_enum(op, YSE_DSP_BITWISE_XOR, e)) f.setOp(e);
  });
}
YSE_C_API YseDspBitwiseOp yse_dsp_bitwise_get_op(YseDspProcessor* p) {
  return query<Bitwise>(p, YSE_DSP_BITWISE_AND,
                        [](DSP::bitwise& f) { return static_cast<YseDspBitwiseOp>(f.getOp()); });
}
YSE_C_API void yse_dsp_bitwise_set_mask(YseDspProcessor* p, unsigned int mask) {
  with<Bitwise>(p, [&](DSP::bitwise& f) { f.setMask(static_cast<Int>(mask)); });
}
YSE_C_API unsigned int yse_dsp_bitwise_get_mask(YseDspProcessor* p) {
  return query<Bitwise>(p, 0u,
                        [](DSP::bitwise& f) { return static_cast<unsigned int>(f.getMask()); });
}
YSE_C_API void yse_dsp_bitwise_set_mode(YseDspProcessor* p, YseDspBitMode mode) {
  with<Bitwise>(p, [&](DSP::bitwise& f) {
    DSP::BIT_MODE e;
    if (to_enum(mode, YSE_DSP_BIT_INTEGER_RAW, e)) f.setMode(e);
  });
}
YSE_C_API YseDspBitMode yse_dsp_bitwise_get_mode(YseDspProcessor* p) {
  return query<Bitwise>(p, YSE_DSP_BIT_RAW,
                        [](DSP::bitwise& f) { return static_cast<YseDspBitMode>(f.getMode()); });
}
YSE_C_API float yse_dsp_bitwise_value(YseDspBitwiseOp op, YseDspBitMode mode, float left,
                                      unsigned int right_bits) {
  DSP::bitwise::OP o;
  DSP::BIT_MODE m;
  if (!to_enum(op, YSE_DSP_BITWISE_XOR, o) || !to_enum(mode, YSE_DSP_BIT_INTEGER_RAW, m))
    return 0.f;
  return DSP::bitwise::process(o, m, left, static_cast<Int>(right_bits));
}
YSE_C_API unsigned int yse_dsp_bitwise_process(YseDspProcessor* p, const float* in, float* out,
                                               unsigned int length) {
  return plain<Bitwise>(p, in, out, length);
}
YSE_C_API unsigned int yse_dsp_bitwise_process_signal(YseDspProcessor* p, const float* in,
                                                      const float* right, float* out,
                                                      unsigned int length) {
  return paired<Bitwise>(p, in, right, out, length);
}

// ─── bitnot ─────────────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_bitnot_create(YseDspBitMode mode) {
  return make<Bitnot>("yse_dsp_bitnot_create", bit_mode(mode));
}
YSE_C_API void yse_dsp_bitnot_set_mode(YseDspProcessor* p, YseDspBitMode mode) {
  with<Bitnot>(p, [&](DSP::bitnot& f) {
    DSP::BIT_MODE e;
    if (to_enum(mode, YSE_DSP_BIT_INTEGER_RAW, e)) f.setMode(e);
  });
}
YSE_C_API YseDspBitMode yse_dsp_bitnot_get_mode(YseDspProcessor* p) {
  return query<Bitnot>(p, YSE_DSP_BIT_RAW,
                       [](DSP::bitnot& f) { return static_cast<YseDspBitMode>(f.getMode()); });
}
YSE_C_API float yse_dsp_bitnot_value(YseDspBitMode mode, float in) {
  DSP::BIT_MODE m;
  if (!to_enum(mode, YSE_DSP_BIT_INTEGER_RAW, m)) return 0.f;
  return DSP::bitnot::process(m, in);
}
YSE_C_API unsigned int yse_dsp_bitnot_process(YseDspProcessor* p, const float* in, float* out,
                                              unsigned int length) {
  return plain<Bitnot>(p, in, out, length);
}

// ─── bitshift ───────────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_bitshift_create(int shift, YseDspBitMode mode) {
  return make<Bitshift>("yse_dsp_bitshift_create", static_cast<Int>(shift), bit_mode(mode));
}
YSE_C_API void yse_dsp_bitshift_set_shift(YseDspProcessor* p, int shift) {
  with<Bitshift>(p, [&](DSP::bitshift& f) { f.setShift(static_cast<Int>(shift)); });
}
YSE_C_API int yse_dsp_bitshift_get_shift(YseDspProcessor* p) {
  return query<Bitshift>(p, 0, [](DSP::bitshift& f) { return static_cast<int>(f.getShift()); });
}
YSE_C_API void yse_dsp_bitshift_set_mode(YseDspProcessor* p, YseDspBitMode mode) {
  with<Bitshift>(p, [&](DSP::bitshift& f) {
    DSP::BIT_MODE e;
    if (to_enum(mode, YSE_DSP_BIT_INTEGER_RAW, e)) f.setMode(e);
  });
}
YSE_C_API YseDspBitMode yse_dsp_bitshift_get_mode(YseDspProcessor* p) {
  return query<Bitshift>(p, YSE_DSP_BIT_RAW,
                         [](DSP::bitshift& f) { return static_cast<YseDspBitMode>(f.getMode()); });
}
YSE_C_API float yse_dsp_bitshift_value(YseDspBitMode mode, int shift, float in) {
  DSP::BIT_MODE m;
  if (!to_enum(mode, YSE_DSP_BIT_INTEGER_RAW, m)) return 0.f;
  return DSP::bitshift::process(m, static_cast<Int>(shift), in);
}
YSE_C_API unsigned int yse_dsp_bitshift_process(YseDspProcessor* p, const float* in, float* out,
                                                unsigned int length) {
  return plain<Bitshift>(p, in, out, length);
}

// ─── bitsafe ────────────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_bitsafe_create(void) {
  return make<Bitsafe>("yse_dsp_bitsafe_create");
}
YSE_C_API float yse_dsp_bitsafe_value(float in) {
  return DSP::bitsafe::process(in);
}
YSE_C_API unsigned int yse_dsp_bitsafe_process(YseDspProcessor* p, const float* in, float* out,
                                               unsigned int length) {
  return plain<Bitsafe>(p, in, out, length);
}

// ─── compare, minimum, maximum ──────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_compare_create(YseDspCompareOp op) {
  return make<Compare>("yse_dsp_compare_create",
                       or_default(op, YSE_DSP_COMPARE_LESS_EQUAL, DSP::compare::OP::EQUAL));
}
YSE_C_API void yse_dsp_compare_set_operator(YseDspProcessor* p, YseDspCompareOp op) {
  with<Compare>(p, [&](DSP::compare& f) {
    DSP::compare::OP e;
    if (to_enum(op, YSE_DSP_COMPARE_LESS_EQUAL, e)) f.setOperator(e);
  });
}
YSE_C_API YseDspCompareOp yse_dsp_compare_get_operator(YseDspProcessor* p) {
  return query<Compare>(p, YSE_DSP_COMPARE_EQUAL, [](DSP::compare& f) {
    return static_cast<YseDspCompareOp>(f.getOperator());
  });
}
YSE_C_API void yse_dsp_compare_set_fuzzy(YseDspProcessor* p, float tolerance) {
  with<Compare>(p, [&](DSP::compare& f) { f.setFuzzy(tolerance); });
}
YSE_C_API float yse_dsp_compare_get_fuzzy(YseDspProcessor* p) {
  return query<Compare>(p, 0.f, [](DSP::compare& f) { return f.getFuzzy(); });
}
YSE_C_API float yse_dsp_compare_value(YseDspCompareOp op, float in, float right, float fuzzy) {
  DSP::compare::OP o;
  if (!to_enum(op, YSE_DSP_COMPARE_LESS_EQUAL, o)) return 0.f;
  return DSP::compare::process(o, in, right, fuzzy);
}
YSE_C_API unsigned int yse_dsp_compare_process(YseDspProcessor* p, const float* in, float right,
                                               float* out, unsigned int length) {
  return against<Compare>(p, in, right, out, length);
}
YSE_C_API unsigned int yse_dsp_compare_process_signal(YseDspProcessor* p, const float* in,
                                                      const float* right, float* out,
                                                      unsigned int length) {
  return paired<Compare>(p, in, right, out, length);
}

YSE_C_API YseDspProcessor* yse_dsp_minimum_create(void) {
  return make<Minimum>("yse_dsp_minimum_create");
}
YSE_C_API float yse_dsp_minimum_value(float in, float right) {
  return DSP::minimum::process(in, right);
}
YSE_C_API unsigned int yse_dsp_minimum_process(YseDspProcessor* p, const float* in, float right,
                                               float* out, unsigned int length) {
  return against<Minimum>(p, in, right, out, length);
}
YSE_C_API unsigned int yse_dsp_minimum_process_signal(YseDspProcessor* p, const float* in,
                                                      const float* right, float* out,
                                                      unsigned int length) {
  return paired<Minimum>(p, in, right, out, length);
}

YSE_C_API YseDspProcessor* yse_dsp_maximum_create(void) {
  return make<Maximum>("yse_dsp_maximum_create");
}
YSE_C_API float yse_dsp_maximum_value(float in, float right) {
  return DSP::maximum::process(in, right);
}
YSE_C_API unsigned int yse_dsp_maximum_process(YseDspProcessor* p, const float* in, float right,
                                               float* out, unsigned int length) {
  return against<Maximum>(p, in, right, out, length);
}
YSE_C_API unsigned int yse_dsp_maximum_process_signal(YseDspProcessor* p, const float* in,
                                                      const float* right, float* out,
                                                      unsigned int length) {
  return paired<Maximum>(p, in, right, out, length);
}

// ─── degrade ────────────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_degrade_create(void) {
  return make<Degrade>("yse_dsp_degrade_create");
}
YSE_C_API void yse_dsp_degrade_set_ratio(YseDspProcessor* p, float value) {
  with<Degrade>(p, [&](DSP::degrade& f) { f.ratio(value); });
}
YSE_C_API float yse_dsp_degrade_get_ratio(YseDspProcessor* p) {
  return query<Degrade>(p, 0.f, [](DSP::degrade& f) { return f.ratio(); });
}
YSE_C_API void yse_dsp_degrade_set_bits(YseDspProcessor* p, int bits) {
  with<Degrade>(p, [&](DSP::degrade& f) { f.bits(bits); });
}
YSE_C_API int yse_dsp_degrade_get_bits(YseDspProcessor* p) {
  return query<Degrade>(p, 0, [](DSP::degrade& f) { return f.bits(); });
}
YSE_C_API void yse_dsp_degrade_reset(YseDspProcessor* p) {
  with<Degrade>(p, [](DSP::degrade& f) { f.reset(); });
}
YSE_C_API float yse_dsp_degrade_get_held(YseDspProcessor* p) {
  return query<Degrade>(p, 0.f, [](DSP::degrade& f) { return f.held(); });
}
YSE_C_API float yse_dsp_degrade_process_sample(YseDspProcessor* p, float in) {
  return one<Degrade>(p, in);
}
YSE_C_API unsigned int yse_dsp_degrade_process(YseDspProcessor* p, const float* in, float* out,
                                               unsigned int length) {
  return plain<Degrade>(p, in, out, length);
}
YSE_C_API unsigned int yse_dsp_degrade_process_signal(YseDspProcessor* p, const float* in,
                                                      const float* ratio, const float* bits,
                                                      float* out, unsigned int length) {
  return tripled<Degrade>(p, in, ratio, bits, out, length);
}

// ─── delta ──────────────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_delta_create(void) {
  return make<Delta>("yse_dsp_delta_create");
}
YSE_C_API void yse_dsp_delta_set(YseDspProcessor* p, float value) {
  with<Delta>(p, [&](DSP::delta& f) { f.set(value); });
}
YSE_C_API void yse_dsp_delta_reset(YseDspProcessor* p) {
  with<Delta>(p, [](DSP::delta& f) { f.reset(); });
}
YSE_C_API float yse_dsp_delta_get_previous(YseDspProcessor* p) {
  return query<Delta>(p, 0.f, [](DSP::delta& f) { return f.previous(); });
}
YSE_C_API float yse_dsp_delta_process_sample(YseDspProcessor* p, float in) {
  return one<Delta>(p, in);
}
YSE_C_API unsigned int yse_dsp_delta_process(YseDspProcessor* p, const float* in, float* out,
                                             unsigned int length) {
  return plain<Delta>(p, in, out, length);
}

// ─── downsample ─────────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_downsample_create(void) {
  return make<Downsample>("yse_dsp_downsample_create");
}
YSE_C_API void yse_dsp_downsample_set_period(YseDspProcessor* p, float value) {
  with<Downsample>(p, [&](DSP::downsample& f) { f.period(value); });
}
YSE_C_API float yse_dsp_downsample_get_period(YseDspProcessor* p) {
  return query<Downsample>(p, 0.f, [](DSP::downsample& f) { return f.period(); });
}
YSE_C_API void yse_dsp_downsample_reset(YseDspProcessor* p) {
  with<Downsample>(p, [](DSP::downsample& f) { f.reset(); });
}
YSE_C_API float yse_dsp_downsample_get_held(YseDspProcessor* p) {
  return query<Downsample>(p, 0.f, [](DSP::downsample& f) { return f.held(); });
}
YSE_C_API float yse_dsp_downsample_process_sample(YseDspProcessor* p, float in) {
  return one<Downsample>(p, in);
}
YSE_C_API unsigned int yse_dsp_downsample_process(YseDspProcessor* p, const float* in, float* out,
                                                  unsigned int length) {
  return plain<Downsample>(p, in, out, length);
}
YSE_C_API unsigned int yse_dsp_downsample_process_signal(YseDspProcessor* p, const float* in,
                                                         const float* period, float* out,
                                                         unsigned int length) {
  return paired<Downsample>(p, in, period, out, length);
}

// ─── fold ───────────────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_fold_create(void) {
  return make<Fold>("yse_dsp_fold_create");
}
YSE_C_API void yse_dsp_fold_set_mode(YseDspProcessor* p, YseDspFoldMode mode) {
  with<Fold>(p, [&](DSP::fold& f) {
    DSP::fold::MODE e;
    if (to_enum(mode, YSE_DSP_FOLD_MODE_CLIP, e)) f.mode(e);
  });
}
YSE_C_API YseDspFoldMode yse_dsp_fold_get_mode(YseDspProcessor* p) {
  return query<Fold>(p, YSE_DSP_FOLD_MODE_FOLD,
                     [](DSP::fold& f) { return static_cast<YseDspFoldMode>(f.mode()); });
}
YSE_C_API void yse_dsp_fold_set_low(YseDspProcessor* p, float value) {
  with<Fold>(p, [&](DSP::fold& f) { f.low(value); });
}
YSE_C_API void yse_dsp_fold_set_high(YseDspProcessor* p, float value) {
  with<Fold>(p, [&](DSP::fold& f) { f.high(value); });
}
YSE_C_API void yse_dsp_fold_set_range(YseDspProcessor* p, float low, float high) {
  with<Fold>(p, [&](DSP::fold& f) { f.range(low, high); });
}
YSE_C_API float yse_dsp_fold_get_low(YseDspProcessor* p) {
  return query<Fold>(p, 0.f, [](DSP::fold& f) { return f.low(); });
}
YSE_C_API float yse_dsp_fold_get_high(YseDspProcessor* p) {
  return query<Fold>(p, 0.f, [](DSP::fold& f) { return f.high(); });
}
YSE_C_API float yse_dsp_fold_value(float value, float low, float high, YseDspFoldMode mode) {
  DSP::fold::MODE m;
  if (!to_enum(mode, YSE_DSP_FOLD_MODE_CLIP, m)) return 0.f;
  return DSP::fold::Limit(value, low, high, m);
}
YSE_C_API float yse_dsp_fold_process_sample(YseDspProcessor* p, float in) {
  return one<Fold>(p, in);
}
YSE_C_API unsigned int yse_dsp_fold_process(YseDspProcessor* p, const float* in, float* out,
                                            unsigned int length) {
  return plain<Fold>(p, in, out, length);
}
YSE_C_API unsigned int yse_dsp_fold_process_signal(YseDspProcessor* p, const float* in,
                                                   const float* low, const float* high, float* out,
                                                   unsigned int length) {
  return tripled<Fold>(p, in, low, high, out, length);
}

// ─── overdrive ──────────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_overdrive_create(void) {
  return make<Overdrive>("yse_dsp_overdrive_create");
}
YSE_C_API void yse_dsp_overdrive_set_drive(YseDspProcessor* p, float value) {
  with<Overdrive>(p, [&](DSP::overdrive& f) { f.drive(value); });
}
YSE_C_API float yse_dsp_overdrive_get_drive(YseDspProcessor* p) {
  return query<Overdrive>(p, 0.f, [](DSP::overdrive& f) { return f.drive(); });
}
YSE_C_API float yse_dsp_overdrive_process_sample(YseDspProcessor* p, float in) {
  return one<Overdrive>(p, in);
}
YSE_C_API unsigned int yse_dsp_overdrive_process(YseDspProcessor* p, const float* in, float* out,
                                                 unsigned int length) {
  return plain<Overdrive>(p, in, out, length);
}
YSE_C_API unsigned int yse_dsp_overdrive_process_signal(YseDspProcessor* p, const float* in,
                                                        const float* drive, float* out,
                                                        unsigned int length) {
  return paired<Overdrive>(p, in, drive, out, length);
}

// ─── cartToPolar, polarToCart ───────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_cart_to_polar_create(void) {
  return make<CartToPolar>("yse_dsp_cart_to_polar_create");
}
YSE_C_API void yse_dsp_cart_to_polar_value(float re, float im, float* magnitude, float* phase) {
  float m = 0.f;
  float ph = 0.f;
  DSP::cartToPolar::process(re, im, m, ph);
  put(magnitude, m);
  put(phase, ph);
}
YSE_C_API unsigned int yse_dsp_cart_to_polar_process(YseDspProcessor* p, const float* re, float im,
                                                     float* magnitude, float* phase,
                                                     unsigned int length) {
  if (re == nullptr) return 0;
  return two_outputs<CartToPolar>(
      p, magnitude, phase, length,
      [&](CartToPolar& h, unsigned o, unsigned n) { h.object(stage(h.scratch[0], re + o, n), im); },
      [](DSP::cartToPolar& f) -> DSP::buffer& { return f.magnitude(); },
      [](DSP::cartToPolar& f) -> DSP::buffer& { return f.phase(); });
}
YSE_C_API unsigned int yse_dsp_cart_to_polar_process_signal(YseDspProcessor* p, const float* re,
                                                            const float* im, float* magnitude,
                                                            float* phase, unsigned int length) {
  if (re == nullptr || im == nullptr) return 0;
  return two_outputs<CartToPolar>(
      p, magnitude, phase, length,
      [&](CartToPolar& h, unsigned o, unsigned n) {
        h.object(stage(h.scratch[0], re + o, n), stage(h.scratch[1], im + o, n));
      },
      [](DSP::cartToPolar& f) -> DSP::buffer& { return f.magnitude(); },
      [](DSP::cartToPolar& f) -> DSP::buffer& { return f.phase(); });
}

YSE_C_API YseDspProcessor* yse_dsp_polar_to_cart_create(void) {
  return make<PolarToCart>("yse_dsp_polar_to_cart_create");
}
YSE_C_API void yse_dsp_polar_to_cart_value(float magnitude, float phase, float* re, float* im) {
  float r = 0.f;
  float i = 0.f;
  DSP::polarToCart::process(magnitude, phase, r, i);
  put(re, r);
  put(im, i);
}
YSE_C_API unsigned int yse_dsp_polar_to_cart_process(YseDspProcessor* p, const float* magnitude,
                                                     float phase, float* re, float* im,
                                                     unsigned int length) {
  if (magnitude == nullptr) return 0;
  return two_outputs<PolarToCart>(
      p, re, im, length,
      [&](PolarToCart& h, unsigned o, unsigned n) {
        h.object(stage(h.scratch[0], magnitude + o, n), phase);
      },
      [](DSP::polarToCart& f) -> DSP::buffer& { return f.real(); },
      [](DSP::polarToCart& f) -> DSP::buffer& { return f.imaginary(); });
}
YSE_C_API unsigned int yse_dsp_polar_to_cart_process_signal(YseDspProcessor* p,
                                                            const float* magnitude,
                                                            const float* phase, float* re,
                                                            float* im, unsigned int length) {
  if (magnitude == nullptr || phase == nullptr) return 0;
  return two_outputs<PolarToCart>(
      p, re, im, length,
      [&](PolarToCart& h, unsigned o, unsigned n) {
        h.object(stage(h.scratch[0], magnitude + o, n), stage(h.scratch[1], phase + o, n));
      },
      [](DSP::polarToCart& f) -> DSP::buffer& { return f.real(); },
      [](DSP::polarToCart& f) -> DSP::buffer& { return f.imaginary(); });
}

// ─── phaseWrap ──────────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_phase_wrap_create(void) {
  return make<PhaseWrap>("yse_dsp_phase_wrap_create");
}
YSE_C_API float yse_dsp_phase_wrap_value(float in) {
  return DSP::phaseWrap::process(in);
}
YSE_C_API unsigned int yse_dsp_phase_wrap_process(YseDspProcessor* p, const float* in, float* out,
                                                  unsigned int length) {
  return plain<PhaseWrap>(p, in, out, length);
}

// ─── round, trunc ───────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_round_create(float step, int nearest) {
  return make<Round>("yse_dsp_round_create", step, nearest != 0);
}
YSE_C_API void yse_dsp_round_set_step(YseDspProcessor* p, float step) {
  with<Round>(p, [&](DSP::round& f) { f.setStep(step); });
}
YSE_C_API float yse_dsp_round_get_step(YseDspProcessor* p) {
  return query<Round>(p, 0.f, [](DSP::round& f) { return f.getStep(); });
}
YSE_C_API void yse_dsp_round_set_nearest(YseDspProcessor* p, int nearest) {
  with<Round>(p, [&](DSP::round& f) { f.setNearest(nearest != 0); });
}
YSE_C_API int yse_dsp_round_get_nearest(YseDspProcessor* p) {
  return query<Round>(p, 0, [](DSP::round& f) { return f.getNearest() ? 1 : 0; });
}
YSE_C_API float yse_dsp_round_value(float in, float step, int nearest) {
  return DSP::round::process(in, step, nearest != 0);
}
YSE_C_API unsigned int yse_dsp_round_process(YseDspProcessor* p, const float* in, float* out,
                                             unsigned int length) {
  return plain<Round>(p, in, out, length);
}
YSE_C_API unsigned int yse_dsp_round_process_signal(YseDspProcessor* p, const float* in,
                                                    const float* step, float* out,
                                                    unsigned int length) {
  return paired<Round>(p, in, step, out, length);
}

YSE_C_API YseDspProcessor* yse_dsp_trunc_create(void) {
  return make<Trunc>("yse_dsp_trunc_create");
}
YSE_C_API float yse_dsp_trunc_value(float in) {
  return DSP::trunc::process(in);
}
YSE_C_API unsigned int yse_dsp_trunc_process(YseDspProcessor* p, const float* in, float* out,
                                             unsigned int length) {
  return plain<Trunc>(p, in, out, length);
}

// ─── scale ──────────────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_scale_create(void) {
  return make<Scale>("yse_dsp_scale_create");
}
YSE_C_API void yse_dsp_scale_set_mode(YseDspProcessor* p, YseDspScaleMode mode) {
  with<Scale>(p, [&](DSP::scale& f) {
    DSP::scale::MODE e;
    if (to_enum(mode, YSE_DSP_SCALE_MODERN_INVERSE, e)) f.mode(e);
  });
}
YSE_C_API YseDspScaleMode yse_dsp_scale_get_mode(YseDspProcessor* p) {
  return query<Scale>(p, YSE_DSP_SCALE_MODERN,
                      [](DSP::scale& f) { return static_cast<YseDspScaleMode>(f.mode()); });
}
YSE_C_API void yse_dsp_scale_set_in_low(YseDspProcessor* p, float value) {
  with<Scale>(p, [&](DSP::scale& f) { f.inLow(value); });
}
YSE_C_API void yse_dsp_scale_set_in_high(YseDspProcessor* p, float value) {
  with<Scale>(p, [&](DSP::scale& f) { f.inHigh(value); });
}
YSE_C_API void yse_dsp_scale_set_out_low(YseDspProcessor* p, float value) {
  with<Scale>(p, [&](DSP::scale& f) { f.outLow(value); });
}
YSE_C_API void yse_dsp_scale_set_out_high(YseDspProcessor* p, float value) {
  with<Scale>(p, [&](DSP::scale& f) { f.outHigh(value); });
}
YSE_C_API void yse_dsp_scale_set_exponent(YseDspProcessor* p, float value) {
  with<Scale>(p, [&](DSP::scale& f) { f.exponent(value); });
}
YSE_C_API void yse_dsp_scale_set_in_range(YseDspProcessor* p, float low, float high) {
  with<Scale>(p, [&](DSP::scale& f) { f.inRange(low, high); });
}
YSE_C_API void yse_dsp_scale_set_out_range(YseDspProcessor* p, float low, float high) {
  with<Scale>(p, [&](DSP::scale& f) { f.outRange(low, high); });
}
YSE_C_API float yse_dsp_scale_get_in_low(YseDspProcessor* p) {
  return query<Scale>(p, 0.f, [](DSP::scale& f) { return f.inLow(); });
}
YSE_C_API float yse_dsp_scale_get_in_high(YseDspProcessor* p) {
  return query<Scale>(p, 0.f, [](DSP::scale& f) { return f.inHigh(); });
}
YSE_C_API float yse_dsp_scale_get_out_low(YseDspProcessor* p) {
  return query<Scale>(p, 0.f, [](DSP::scale& f) { return f.outLow(); });
}
YSE_C_API float yse_dsp_scale_get_out_high(YseDspProcessor* p) {
  return query<Scale>(p, 0.f, [](DSP::scale& f) { return f.outHigh(); });
}
YSE_C_API float yse_dsp_scale_get_exponent(YseDspProcessor* p) {
  return query<Scale>(p, 0.f, [](DSP::scale& f) { return f.exponent(); });
}
YSE_C_API float yse_dsp_scale_value(float value, float in_low, float in_high, float out_low,
                                    float out_high, float exponent, YseDspScaleMode mode) {
  DSP::scale::MODE m;
  if (!to_enum(mode, YSE_DSP_SCALE_MODERN_INVERSE, m)) return 0.f;
  return DSP::scale::Map(value, in_low, in_high, out_low, out_high, exponent, m);
}
YSE_C_API float yse_dsp_scale_process_sample(YseDspProcessor* p, float in) {
  return one<Scale>(p, in);
}
YSE_C_API unsigned int yse_dsp_scale_process(YseDspProcessor* p, const float* in, float* out,
                                             unsigned int length) {
  return plain<Scale>(p, in, out, length);
}
YSE_C_API unsigned int yse_dsp_scale_process_signal(YseDspProcessor* p, const float* in,
                                                    const float* in_low, const float* in_high,
                                                    const float* out_low, const float* out_high,
                                                    const float* exponent, float* out,
                                                    unsigned int length) {
  if (in == nullptr || in_low == nullptr || in_high == nullptr || out_low == nullptr ||
      out_high == nullptr || exponent == nullptr || out == nullptr)
    return 0;
  return drive<Scale>(p, length, [&](Scale& h, unsigned o, unsigned n) {
    emit(h.object(stage(h.scratch[0], in + o, n), stage(h.scratch[1], in_low + o, n),
                  stage(h.scratch[2], in_high + o, n), stage(h.scratch[3], out_low + o, n),
                  stage(h.scratch[4], out_high + o, n), stage(h.scratch[5], exponent + o, n)),
         out + o, n);
  });
}

// ─── trig, arcTangent2 ──────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_trig_create(YseDspTrigFunction fn) {
  return make<Trig>("yse_dsp_trig_create", or_default(fn, YSE_DSP_TRIG_ATANH, DSP::trig::FN::COS));
}
YSE_C_API void yse_dsp_trig_set_function(YseDspProcessor* p, YseDspTrigFunction fn) {
  with<Trig>(p, [&](DSP::trig& f) {
    DSP::trig::FN e;
    if (to_enum(fn, YSE_DSP_TRIG_ATANH, e)) f.setFunction(e);
  });
}
YSE_C_API YseDspTrigFunction yse_dsp_trig_get_function(YseDspProcessor* p) {
  return query<Trig>(p, YSE_DSP_TRIG_COS,
                     [](DSP::trig& f) { return static_cast<YseDspTrigFunction>(f.getFunction()); });
}
YSE_C_API float yse_dsp_trig_value(YseDspTrigFunction fn, float in) {
  DSP::trig::FN f;
  if (!to_enum(fn, YSE_DSP_TRIG_ATANH, f)) return 0.f;
  return DSP::trig::process(f, in);
}
YSE_C_API unsigned int yse_dsp_trig_process(YseDspProcessor* p, const float* in, float* out,
                                            unsigned int length) {
  return plain<Trig>(p, in, out, length);
}

YSE_C_API YseDspProcessor* yse_dsp_arc_tangent2_create(void) {
  return make<ArcTangent2>("yse_dsp_arc_tangent2_create");
}
YSE_C_API float yse_dsp_arc_tangent2_value(float y, float x) {
  return DSP::arcTangent2::process(y, x);
}
YSE_C_API unsigned int yse_dsp_arc_tangent2_process(YseDspProcessor* p, const float* y, float x,
                                                    float* out, unsigned int length) {
  return against<ArcTangent2>(p, y, x, out, length);
}
YSE_C_API unsigned int yse_dsp_arc_tangent2_process_signal(YseDspProcessor* p, const float* y,
                                                           const float* x, float* out,
                                                           unsigned int length) {
  return paired<ArcTangent2>(p, y, x, out, length);
}

// ─── ampToDb, dbToAmp, msToSamples, samplesToMs ─────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_amp_to_db_create(void) {
  return make<AmpToDb>("yse_dsp_amp_to_db_create");
}
YSE_C_API float yse_dsp_amp_to_db_value(float amplitude) {
  return DSP::ampToDb::process(amplitude);
}
YSE_C_API unsigned int yse_dsp_amp_to_db_process(YseDspProcessor* p, const float* in, float* out,
                                                 unsigned int length) {
  return plain<AmpToDb>(p, in, out, length);
}

YSE_C_API YseDspProcessor* yse_dsp_db_to_amp_create(void) {
  return make<DbToAmp>("yse_dsp_db_to_amp_create");
}
YSE_C_API float yse_dsp_db_to_amp_value(float db) {
  return DSP::dbToAmp::process(db);
}
YSE_C_API unsigned int yse_dsp_db_to_amp_process(YseDspProcessor* p, const float* in, float* out,
                                                 unsigned int length) {
  return plain<DbToAmp>(p, in, out, length);
}

YSE_C_API YseDspProcessor* yse_dsp_ms_to_samples_create(void) {
  return make<MsToSamples>("yse_dsp_ms_to_samples_create");
}
YSE_C_API float yse_dsp_ms_to_samples_value(float ms, float sample_rate) {
  return DSP::msToSamples::process(ms, sample_rate);
}
YSE_C_API unsigned int yse_dsp_ms_to_samples_process(YseDspProcessor* p, const float* in,
                                                     float* out, unsigned int length) {
  return plain<MsToSamples>(p, in, out, length);
}

YSE_C_API YseDspProcessor* yse_dsp_samples_to_ms_create(void) {
  return make<SamplesToMs>("yse_dsp_samples_to_ms_create");
}
YSE_C_API float yse_dsp_samples_to_ms_value(float samples, float sample_rate) {
  return DSP::samplesToMs::process(samples, sample_rate);
}
YSE_C_API unsigned int yse_dsp_samples_to_ms_process(YseDspProcessor* p, const float* in,
                                                     float* out, unsigned int length) {
  return plain<SamplesToMs>(p, in, out, length);
}

} // extern "C"
