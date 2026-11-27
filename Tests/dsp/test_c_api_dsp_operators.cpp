// C-API tests for the signal operators of the MSP port (issue #1300):
// yse_c/yse_dsp_operators.h (accumulator, reverseSubtract, reverseDivide,
// modulo, bitwise, bitnot, bitshift, bitsafe, compare, minimum, maximum,
// degrade, delta, downsample, fold, overdrive, cartToPolar, polarToCart,
// phaseWrap, round, trunc, scale, trig, arcTangent2, ampToDb, dbToAmp,
// msToSamples, samplesToMs), on the shared YseDspProcessor handle of #1173.
//
// The DSP itself is covered by each class's own suite (test_accumulator.cpp,
// ...). What these cases pin is the bridge:
//
//   * Ownership — every constructor pairs with yse_dsp_processor_destroy, and
//     destroy frees the size that was allocated.
//   * The class check — a handle of another class (another operator, or a
//     filter) is treated like NULL, and the real class's state is untouched.
//   * NULL handles and NULL arrays — 0 / no-op, and nothing is written.
//   * Round-trips — every setter / getter pair the engine has, constructor
//     arguments, and enum setters and constructors given a value out of range.
//   * Parity — every process entry point produces exactly what the engine
//     class produces when driven directly in 128-sample blocks, for a length
//     that is not a multiple of 128, both outputs of the polar converters
//     included; the value and per-sample calls match the engine's statics and
//     per-sample calls bit for bit.
//   * Real-time safety — process, value, sample and setter calls allocate
//     nothing.
//
// No engine session is needed: the classes are plain objects that read the
// global SAMPLERATE, which the C and C++ sides here share.

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <string>
#include <vector>

#include "support/alloc_probe.hpp"

#include "yse_c/yse_dsp_filters.h"
#include "yse_c/yse_dsp_operators.h"
#include "yse_c/yse_dsp_processor.h"

#include "dsp/buffer.hpp"
#include "dsp/operators/accumulator.hpp"
#include "dsp/operators/arithmetic.hpp"
#include "dsp/operators/bitwise.hpp"
#include "dsp/operators/compare.hpp"
#include "dsp/operators/degrade.hpp"
#include "dsp/operators/delta.hpp"
#include "dsp/operators/downsample.hpp"
#include "dsp/operators/fold.hpp"
#include "dsp/operators/overdrive.hpp"
#include "dsp/operators/polar.hpp"
#include "dsp/operators/rounding.hpp"
#include "dsp/operators/scale.hpp"
#include "dsp/operators/trig.hpp"
#include "dsp/operators/units.hpp"
#include "headers/constants.hpp"

namespace {
  namespace DSP = YSE::DSP;
  constexpr unsigned BLOCK = YSE::STANDARD_BUFFERSIZE;
  // Not a multiple of the block size: the last block is short.
  constexpr unsigned LENGTH = 3 * BLOCK + 37;
  const float NaN = std::numeric_limits<float>::quiet_NaN();
  const float Inf = std::numeric_limits<float>::infinity();

  // A deterministic, broadband test signal around `offset`.
  std::vector<float> Signal(unsigned length, float scale = 1.f, float offset = 0.f,
                            unsigned seed = 12345u) {
    std::vector<float> v(length);
    unsigned state = seed;
    for (unsigned i = 0; i < length; ++i) {
      state = state * 1103515245u + 12345u;
      const float noise = static_cast<float>((state >> 9) & 0xFFFF) / 32768.f - 1.f;
      v[i] = offset + scale * (0.5f * std::sin(0.05f * static_cast<float>(i)) + 0.5f * noise);
    }
    return v;
  }

  // A 0/1 gate that is high for `on` of every `period` samples.
  std::vector<float> Gate(unsigned length, unsigned period, unsigned on) {
    std::vector<float> v(length);
    for (unsigned i = 0; i < length; ++i)
      v[i] = (i % period) < on ? 1.f : 0.f;
    return v;
  }

  // Drives an engine object the way the bridge must: consecutive blocks of
  // at most BLOCK samples, `body(offset, count)` per block.
  template <typename F> void Blocks(unsigned length, F&& body) {
    for (unsigned offset = 0; offset < length; offset += BLOCK)
      body(offset, std::min(BLOCK, length - offset));
  }

  DSP::buffer& Stage(DSP::buffer& b, const std::vector<float>& src, unsigned offset, unsigned n) {
    b.resize(n);
    std::memcpy(b.getPtr(), src.data() + offset, sizeof(float) * n);
    return b;
  }

  // Copy an engine block into the reference run at `offset`.
  void Put(DSP::buffer& b, std::vector<float>& dst, unsigned offset, unsigned n) {
    REQUIRE(b.getLength() == n);
    std::memcpy(dst.data() + offset, b.getPtr(), sizeof(float) * n);
  }

  // Bit for bit: a NaN matches a NaN, and 0 does not match -0.
  bool Same(float a, float b) {
    std::uint32_t x = 0, y = 0;
    std::memcpy(&x, &a, sizeof x);
    std::memcpy(&y, &b, sizeof y);
    return x == y;
  }

  void CheckSame(const std::vector<float>& a, const std::vector<float>& b) {
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
      if (!Same(a[i], b[i])) {
        INFO("sample " << i << ": " << a[i] << " vs " << b[i]);
        CHECK(Same(a[i], b[i]));
        return;
      }
    }
  }

  // A signal that is not constant: a parity check against a flat line proves
  // little.
  bool Moves(const std::vector<float>& v) {
    return std::any_of(v.begin(), v.end(), [&](float x) { return !Same(x, v[0]); });
  }

  using Ctor = std::function<YseDspProcessor*()>;

  struct Named {
    const char* name;
    Ctor make;
  };

  std::vector<Named> AllCtors() {
    return {
        {"accumulator", [] { return yse_dsp_accumulator_create(0.0); }},
        {"reverse_subtract", [] { return yse_dsp_reverse_subtract_create(); }},
        {"reverse_divide", [] { return yse_dsp_reverse_divide_create(); }},
        {"modulo", [] { return yse_dsp_modulo_create(); }},
        {"bitwise",
         [] { return yse_dsp_bitwise_create(YSE_DSP_BITWISE_AND, 0u, YSE_DSP_BIT_RAW); }},
        {"bitnot", [] { return yse_dsp_bitnot_create(YSE_DSP_BIT_RAW); }},
        {"bitshift", [] { return yse_dsp_bitshift_create(0, YSE_DSP_BIT_RAW); }},
        {"bitsafe", [] { return yse_dsp_bitsafe_create(); }},
        {"compare", [] { return yse_dsp_compare_create(YSE_DSP_COMPARE_EQUAL); }},
        {"minimum", [] { return yse_dsp_minimum_create(); }},
        {"maximum", [] { return yse_dsp_maximum_create(); }},
        {"degrade", [] { return yse_dsp_degrade_create(); }},
        {"delta", [] { return yse_dsp_delta_create(); }},
        {"downsample", [] { return yse_dsp_downsample_create(); }},
        {"fold", [] { return yse_dsp_fold_create(); }},
        {"overdrive", [] { return yse_dsp_overdrive_create(); }},
        {"cart_to_polar", [] { return yse_dsp_cart_to_polar_create(); }},
        {"polar_to_cart", [] { return yse_dsp_polar_to_cart_create(); }},
        {"phase_wrap", [] { return yse_dsp_phase_wrap_create(); }},
        {"round", [] { return yse_dsp_round_create(1.f, 1); }},
        {"trunc", [] { return yse_dsp_trunc_create(); }},
        {"scale", [] { return yse_dsp_scale_create(); }},
        {"trig", [] { return yse_dsp_trig_create(YSE_DSP_TRIG_COS); }},
        {"arc_tangent2", [] { return yse_dsp_arc_tangent2_create(); }},
        {"amp_to_db", [] { return yse_dsp_amp_to_db_create(); }},
        {"db_to_amp", [] { return yse_dsp_db_to_amp_create(); }},
        {"ms_to_samples", [] { return yse_dsp_ms_to_samples_create(); }},
        {"samples_to_ms", [] { return yse_dsp_samples_to_ms_create(); }},
    };
  }

  // One parity case: the C run and the engine reference, each a list of
  // outputs.
  using Outputs = std::vector<std::vector<float>>;
  struct Parity {
    std::string name;
    std::function<Outputs()> c;
    std::function<Outputs()> engine;
  };

  // Engine references for the shapes the operators share.
  template <typename T> Outputs RefPlain(T& f, const std::vector<float>& in) {
    Outputs r(1, std::vector<float>(LENGTH));
    DSP::buffer a;
    Blocks(LENGTH, [&](unsigned o, unsigned n) { Put(f(Stage(a, in, o, n)), r[0], o, n); });
    return r;
  }
  template <typename T> Outputs RefConst(T& f, const std::vector<float>& in, float right) {
    Outputs r(1, std::vector<float>(LENGTH));
    DSP::buffer a;
    Blocks(LENGTH, [&](unsigned o, unsigned n) { Put(f(Stage(a, in, o, n), right), r[0], o, n); });
    return r;
  }
  template <typename T>
  Outputs RefPaired(T& f, const std::vector<float>& in, const std::vector<float>& second) {
    Outputs r(1, std::vector<float>(LENGTH));
    DSP::buffer a, b;
    Blocks(LENGTH, [&](unsigned o, unsigned n) {
      Put(f(Stage(a, in, o, n), Stage(b, second, o, n)), r[0], o, n);
    });
    return r;
  }
  template <typename T>
  Outputs RefTripled(T& f, const std::vector<float>& in, const std::vector<float>& second,
                     const std::vector<float>& third) {
    Outputs r(1, std::vector<float>(LENGTH));
    DSP::buffer a, b, c;
    Blocks(LENGTH, [&](unsigned o, unsigned n) {
      Put(f(Stage(a, in, o, n), Stage(b, second, o, n), Stage(c, third, o, n)), r[0], o, n);
    });
    return r;
  }

  // The C side of a single-output call: runs `call` into a fresh array and
  // destroys the handle.
  Outputs RunC(YseDspProcessor* p, const std::function<unsigned(float*)>& call) {
    REQUIRE(p != nullptr);
    std::vector<float> out(LENGTH, 7.f);
    REQUIRE(call(out.data()) == LENGTH);
    yse_dsp_processor_destroy(p);
    return Outputs{out};
  }
  Outputs RunC2(YseDspProcessor* p, const std::function<unsigned(float*, float*)>& call) {
    REQUIRE(p != nullptr);
    Outputs out(2, std::vector<float>(LENGTH, 7.f));
    REQUIRE(call(out[0].data(), out[1].data()) == LENGTH);
    yse_dsp_processor_destroy(p);
    return out;
  }

  // The C signatures the shared shapes have.
  using CreateFn = YseDspProcessor* (*)();
  using PlainFn = unsigned int (*)(YseDspProcessor*, const float*, float*, unsigned int);
  using ConstFn = unsigned int (*)(YseDspProcessor*, const float*, float, float*, unsigned int);
  using PairedFn = unsigned int (*)(YseDspProcessor*, const float*, const float*, float*,
                                    unsigned int);
  using BinaryValueFn = float (*)(float, float);
  using UnaryValueFn = float (*)(float);

  // The inputs every value call is checked against: ordinary values, the
  // edges of each formula's domain, zeros of both signs and non-finite ones.
  std::vector<float> Probes() {
    return {0.f,   -0.f,   1.f,   -1.f,  0.5f,   -0.5f,       2.5f,         -2.5f,
            3.75f, 0.999f, 1e-8f, 100.f, -100.f, 1e30f,       -1e30f,       89.f,
            -89.f, 7.f,    NaN,   Inf,   -Inf,   3.14159265f, -3.14159265f, 6.5f};
  }
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("c api operators: every constructor pairs with destroy and frees what it "
            "allocated") {
    for (const Named& c : AllCtors()) {
      INFO("constructor: " << std::string(c.name));
      TestHelpers::AllocWatch watch;
      YseDspProcessor* p = c.make();
      REQUIRE(p != nullptr);
      const std::size_t allocated = watch.newSize();
      yse_dsp_processor_destroy(p);
      if (allocated == 0) {
        MESSAGE("alloc probe watch inactive (sanitizer owns new/delete): size check skipped");
        continue;
      }
      if (watch.sawSizedDelete()) CHECK(watch.deleteSize() == allocated);
    }
  }

  TEST_CASE("c api operators: a handle of another class is refused like NULL") {
    YseDspProcessor* acc = yse_dsp_accumulator_create(2.5);
    REQUIRE(acc != nullptr);
    YseDspProcessor* sc = yse_dsp_scale_create();
    yse_dsp_scale_set_out_range(sc, 10.f, 20.f);
    YseDspProcessor* lp = yse_dsp_lores_create();
    REQUIRE(lp != nullptr);

    const std::vector<float> in = Signal(LENGTH);
    std::vector<float> out(LENGTH, 7.f);
    std::vector<float> extra(LENGTH, 7.f);

    for (YseDspProcessor* wrong : {acc, lp}) {
      // Setters of every other class do nothing ...
      yse_dsp_bitwise_set_op(wrong, YSE_DSP_BITWISE_XOR);
      yse_dsp_bitwise_set_mask(wrong, 0xFFu);
      yse_dsp_bitnot_set_mode(wrong, YSE_DSP_BIT_INTEGER);
      yse_dsp_bitshift_set_shift(wrong, 4);
      yse_dsp_compare_set_operator(wrong, YSE_DSP_COMPARE_LESS);
      yse_dsp_compare_set_fuzzy(wrong, 0.5f);
      yse_dsp_degrade_set_bits(wrong, 3);
      yse_dsp_degrade_reset(wrong);
      yse_dsp_delta_set(wrong, 1.f);
      yse_dsp_downsample_set_period(wrong, 3.f);
      yse_dsp_fold_set_range(wrong, -1.f, 2.f);
      yse_dsp_overdrive_set_drive(wrong, 4.f);
      yse_dsp_round_set_step(wrong, 0.5f);
      yse_dsp_round_set_nearest(wrong, 0);
      yse_dsp_scale_set_exponent(wrong, 2.f);
      yse_dsp_trig_set_function(wrong, YSE_DSP_TRIG_TANH);
      // ... getters return 0 ...
      CHECK(yse_dsp_bitwise_get_mask(wrong) == 0u);
      CHECK(yse_dsp_bitwise_get_op(wrong) == YSE_DSP_BITWISE_AND);
      CHECK(yse_dsp_bitshift_get_shift(wrong) == 0);
      CHECK(yse_dsp_bitshift_get_mode(wrong) == YSE_DSP_BIT_RAW);
      CHECK(yse_dsp_compare_get_fuzzy(wrong) == 0.f);
      CHECK(yse_dsp_degrade_get_bits(wrong) == 0);
      CHECK(yse_dsp_degrade_get_ratio(wrong) == 0.f);
      CHECK(yse_dsp_delta_get_previous(wrong) == 0.f);
      CHECK(yse_dsp_downsample_get_period(wrong) == 0.f);
      CHECK(yse_dsp_fold_get_high(wrong) == 0.f);
      CHECK(yse_dsp_fold_get_mode(wrong) == YSE_DSP_FOLD_MODE_FOLD);
      CHECK(yse_dsp_overdrive_get_drive(wrong) == 0.f);
      CHECK(yse_dsp_round_get_step(wrong) == 0.f);
      CHECK(yse_dsp_round_get_nearest(wrong) == 0);
      CHECK(yse_dsp_scale_get_out_high(wrong) == 0.f);
      CHECK(yse_dsp_trig_get_function(wrong) == YSE_DSP_TRIG_COS);
      // ... and sample and process calls return 0 and write nothing.
      CHECK(yse_dsp_degrade_process_sample(wrong, 0.3f) == 0.f);
      CHECK(yse_dsp_delta_process_sample(wrong, 0.3f) == 0.f);
      CHECK(yse_dsp_fold_process_sample(wrong, 3.f) == 0.f);
      CHECK(yse_dsp_scale_process_sample(wrong, 3.f) == 0.f);
      CHECK(yse_dsp_reverse_subtract_process(wrong, in.data(), 1.f, out.data(), LENGTH) == 0);
      CHECK(yse_dsp_reverse_divide_process_signal(wrong, in.data(), in.data(), out.data(),
                                                  LENGTH) == 0);
      CHECK(yse_dsp_modulo_process(wrong, in.data(), 1.f, out.data(), LENGTH) == 0);
      CHECK(yse_dsp_bitwise_process(wrong, in.data(), out.data(), LENGTH) == 0);
      CHECK(yse_dsp_bitnot_process(wrong, in.data(), out.data(), LENGTH) == 0);
      CHECK(yse_dsp_bitshift_process(wrong, in.data(), out.data(), LENGTH) == 0);
      CHECK(yse_dsp_bitsafe_process(wrong, in.data(), out.data(), LENGTH) == 0);
      CHECK(yse_dsp_compare_process(wrong, in.data(), 0.f, out.data(), LENGTH) == 0);
      CHECK(yse_dsp_minimum_process(wrong, in.data(), 0.f, out.data(), LENGTH) == 0);
      CHECK(yse_dsp_maximum_process_signal(wrong, in.data(), in.data(), out.data(), LENGTH) == 0);
      CHECK(yse_dsp_degrade_process_signal(wrong, in.data(), in.data(), in.data(), out.data(),
                                           LENGTH) == 0);
      CHECK(yse_dsp_delta_process(wrong, in.data(), out.data(), LENGTH) == 0);
      CHECK(yse_dsp_downsample_process(wrong, in.data(), out.data(), LENGTH) == 0);
      CHECK(yse_dsp_fold_process_signal(wrong, in.data(), in.data(), in.data(), out.data(),
                                        LENGTH) == 0);
      CHECK(yse_dsp_overdrive_process(wrong, in.data(), out.data(), LENGTH) == 0);
      CHECK(yse_dsp_cart_to_polar_process(wrong, in.data(), 0.5f, out.data(), extra.data(),
                                          LENGTH) == 0);
      CHECK(yse_dsp_polar_to_cart_process_signal(wrong, in.data(), in.data(), out.data(),
                                                 extra.data(), LENGTH) == 0);
      CHECK(yse_dsp_phase_wrap_process(wrong, in.data(), out.data(), LENGTH) == 0);
      CHECK(yse_dsp_round_process(wrong, in.data(), out.data(), LENGTH) == 0);
      CHECK(yse_dsp_trunc_process(wrong, in.data(), out.data(), LENGTH) == 0);
      CHECK(yse_dsp_scale_process(wrong, in.data(), out.data(), LENGTH) == 0);
      CHECK(yse_dsp_scale_process_signal(wrong, in.data(), in.data(), in.data(), in.data(),
                                         in.data(), in.data(), out.data(), LENGTH) == 0);
      CHECK(yse_dsp_trig_process(wrong, in.data(), out.data(), LENGTH) == 0);
      CHECK(yse_dsp_arc_tangent2_process(wrong, in.data(), 1.f, out.data(), LENGTH) == 0);
      CHECK(yse_dsp_amp_to_db_process(wrong, in.data(), out.data(), LENGTH) == 0);
      CHECK(yse_dsp_db_to_amp_process(wrong, in.data(), out.data(), LENGTH) == 0);
      CHECK(yse_dsp_ms_to_samples_process(wrong, in.data(), out.data(), LENGTH) == 0);
      CHECK(yse_dsp_samples_to_ms_process(wrong, in.data(), out.data(), LENGTH) == 0);
    }
    // The accumulator's own functions refuse a scale and the filter.
    for (YseDspProcessor* wrong : {sc, lp}) {
      yse_dsp_accumulator_set(wrong, 9.0);
      yse_dsp_accumulator_reset(wrong);
      CHECK(yse_dsp_accumulator_get_sum(wrong) == 0.0);
      CHECK(yse_dsp_accumulator_process_sample(wrong, 1.f) == 0.f);
      CHECK(yse_dsp_accumulator_process_sample_reset(wrong, 1.f, 1.f) == 0.f);
      CHECK(yse_dsp_accumulator_process(wrong, in.data(), out.data(), LENGTH) == 0);
      CHECK(yse_dsp_accumulator_process_reset(wrong, in.data(), in.data(), out.data(), LENGTH) ==
            0);
    }
    for (float v : out)
      REQUIRE(v == 7.f);
    for (float v : extra)
      REQUIRE(v == 7.f);

    // The accumulator's and the scale's own state is what it was.
    CHECK(yse_dsp_accumulator_get_sum(acc) == 2.5);
    CHECK(yse_dsp_scale_get_out_low(sc) == 10.f);
    CHECK(yse_dsp_scale_get_out_high(sc) == 20.f);
    CHECK(yse_dsp_scale_get_exponent(sc) == 1.f);
    CHECK(yse_dsp_accumulator_process(acc, in.data(), out.data(), LENGTH) == LENGTH);
    for (YseDspProcessor* p : {acc, sc, lp})
      yse_dsp_processor_destroy(p);
  }

  TEST_CASE("c api operators: NULL handles and NULL arrays are no-ops that write nothing") {
    const std::vector<float> in = Signal(LENGTH);
    std::vector<float> out(LENGTH, 7.f);
    std::vector<float> extra(LENGTH, 7.f);

    // NULL handle.
    yse_dsp_accumulator_set(nullptr, 1.0);
    yse_dsp_accumulator_reset(nullptr);
    yse_dsp_bitwise_set_mode(nullptr, YSE_DSP_BIT_INTEGER);
    yse_dsp_bitshift_set_mode(nullptr, YSE_DSP_BIT_INTEGER);
    yse_dsp_compare_set_operator(nullptr, YSE_DSP_COMPARE_LESS);
    yse_dsp_degrade_set_ratio(nullptr, 0.5f);
    yse_dsp_delta_reset(nullptr);
    yse_dsp_downsample_reset(nullptr);
    yse_dsp_fold_set_mode(nullptr, YSE_DSP_FOLD_MODE_CLIP);
    yse_dsp_fold_set_low(nullptr, 0.2f);
    yse_dsp_fold_set_high(nullptr, 0.2f);
    yse_dsp_scale_set_mode(nullptr, YSE_DSP_SCALE_CLASSIC);
    yse_dsp_scale_set_in_low(nullptr, 1.f);
    yse_dsp_scale_set_in_range(nullptr, 1.f, 2.f);
    yse_dsp_trig_set_function(nullptr, YSE_DSP_TRIG_SIN);
    CHECK(yse_dsp_accumulator_get_sum(nullptr) == 0.0);
    CHECK(yse_dsp_bitwise_get_mode(nullptr) == YSE_DSP_BIT_RAW);
    CHECK(yse_dsp_bitnot_get_mode(nullptr) == YSE_DSP_BIT_RAW);
    CHECK(yse_dsp_compare_get_operator(nullptr) == YSE_DSP_COMPARE_EQUAL);
    CHECK(yse_dsp_degrade_get_held(nullptr) == 0.f);
    CHECK(yse_dsp_downsample_get_held(nullptr) == 0.f);
    CHECK(yse_dsp_fold_get_low(nullptr) == 0.f);
    CHECK(yse_dsp_scale_get_mode(nullptr) == YSE_DSP_SCALE_MODERN);
    CHECK(yse_dsp_scale_get_in_low(nullptr) == 0.f);
    CHECK(yse_dsp_scale_get_in_high(nullptr) == 0.f);
    CHECK(yse_dsp_accumulator_process_sample(nullptr, 1.f) == 0.f);
    CHECK(yse_dsp_downsample_process_sample(nullptr, 1.f) == 0.f);
    CHECK(yse_dsp_overdrive_process_sample(nullptr, 1.f) == 0.f);
    CHECK(yse_dsp_accumulator_process(nullptr, in.data(), out.data(), LENGTH) == 0);
    CHECK(yse_dsp_modulo_process_signal(nullptr, in.data(), in.data(), out.data(), LENGTH) == 0);
    CHECK(yse_dsp_bitwise_process_signal(nullptr, in.data(), in.data(), out.data(), LENGTH) == 0);
    CHECK(yse_dsp_compare_process_signal(nullptr, in.data(), in.data(), out.data(), LENGTH) == 0);
    CHECK(yse_dsp_cart_to_polar_process_signal(nullptr, in.data(), in.data(), out.data(),
                                               extra.data(), LENGTH) == 0);
    CHECK(yse_dsp_polar_to_cart_process(nullptr, in.data(), 0.f, out.data(), extra.data(),
                                        LENGTH) == 0);
    CHECK(yse_dsp_round_process_signal(nullptr, in.data(), in.data(), out.data(), LENGTH) == 0);
    CHECK(yse_dsp_arc_tangent2_process_signal(nullptr, in.data(), in.data(), out.data(), LENGTH) ==
          0);
    yse_dsp_processor_destroy(nullptr);

    // A live handle with a NULL required array.
    std::vector<YseDspProcessor*> all;
    for (const Named& c : AllCtors())
      all.push_back(c.make());
    YseDspProcessor* acc = all[0];
    YseDspProcessor* rs = all[1];
    YseDspProcessor* rd = all[2];
    YseDspProcessor* mo = all[3];
    YseDspProcessor* bw = all[4];
    YseDspProcessor* bn = all[5];
    YseDspProcessor* bs = all[6];
    YseDspProcessor* sf = all[7];
    YseDspProcessor* cm = all[8];
    YseDspProcessor* mn = all[9];
    YseDspProcessor* mx = all[10];
    YseDspProcessor* dg = all[11];
    YseDspProcessor* dl = all[12];
    YseDspProcessor* ds = all[13];
    YseDspProcessor* fd = all[14];
    YseDspProcessor* od = all[15];
    YseDspProcessor* cp = all[16];
    YseDspProcessor* pc = all[17];
    YseDspProcessor* pw = all[18];
    YseDspProcessor* rn = all[19];
    YseDspProcessor* tr = all[20];
    YseDspProcessor* sc = all[21];
    YseDspProcessor* tg = all[22];
    YseDspProcessor* at = all[23];
    YseDspProcessor* ad = all[24];
    YseDspProcessor* da = all[25];
    YseDspProcessor* ms = all[26];
    YseDspProcessor* sm = all[27];

    CHECK(yse_dsp_accumulator_process(acc, nullptr, out.data(), LENGTH) == 0);
    CHECK(yse_dsp_accumulator_process_reset(acc, in.data(), nullptr, out.data(), LENGTH) == 0);
    CHECK(yse_dsp_reverse_subtract_process(rs, in.data(), 1.f, nullptr, LENGTH) == 0);
    CHECK(yse_dsp_reverse_subtract_process_signal(rs, in.data(), nullptr, out.data(), LENGTH) == 0);
    CHECK(yse_dsp_reverse_divide_process(rd, nullptr, 1.f, out.data(), LENGTH) == 0);
    CHECK(yse_dsp_modulo_process_signal(mo, nullptr, in.data(), out.data(), LENGTH) == 0);
    CHECK(yse_dsp_bitwise_process(bw, nullptr, out.data(), LENGTH) == 0);
    CHECK(yse_dsp_bitwise_process_signal(bw, in.data(), nullptr, out.data(), LENGTH) == 0);
    CHECK(yse_dsp_bitnot_process(bn, in.data(), nullptr, LENGTH) == 0);
    CHECK(yse_dsp_bitshift_process(bs, nullptr, out.data(), LENGTH) == 0);
    CHECK(yse_dsp_bitsafe_process(sf, nullptr, out.data(), LENGTH) == 0);
    CHECK(yse_dsp_compare_process(cm, nullptr, 0.f, out.data(), LENGTH) == 0);
    CHECK(yse_dsp_compare_process_signal(cm, in.data(), nullptr, out.data(), LENGTH) == 0);
    CHECK(yse_dsp_minimum_process_signal(mn, in.data(), in.data(), nullptr, LENGTH) == 0);
    CHECK(yse_dsp_maximum_process(mx, nullptr, 0.f, out.data(), LENGTH) == 0);
    CHECK(yse_dsp_degrade_process(dg, nullptr, out.data(), LENGTH) == 0);
    CHECK(yse_dsp_degrade_process_signal(dg, in.data(), in.data(), nullptr, out.data(), LENGTH) ==
          0);
    CHECK(yse_dsp_delta_process(dl, in.data(), nullptr, LENGTH) == 0);
    CHECK(yse_dsp_downsample_process_signal(ds, in.data(), nullptr, out.data(), LENGTH) == 0);
    CHECK(yse_dsp_fold_process(fd, nullptr, out.data(), LENGTH) == 0);
    CHECK(yse_dsp_fold_process_signal(fd, in.data(), nullptr, in.data(), out.data(), LENGTH) == 0);
    CHECK(yse_dsp_overdrive_process_signal(od, in.data(), nullptr, out.data(), LENGTH) == 0);
    CHECK(yse_dsp_cart_to_polar_process(cp, nullptr, 0.f, out.data(), extra.data(), LENGTH) == 0);
    CHECK(yse_dsp_cart_to_polar_process_signal(cp, in.data(), nullptr, out.data(), extra.data(),
                                               LENGTH) == 0);
    CHECK(yse_dsp_polar_to_cart_process(pc, nullptr, 0.f, out.data(), extra.data(), LENGTH) == 0);
    CHECK(yse_dsp_polar_to_cart_process_signal(pc, nullptr, in.data(), out.data(), extra.data(),
                                               LENGTH) == 0);
    CHECK(yse_dsp_phase_wrap_process(pw, nullptr, out.data(), LENGTH) == 0);
    CHECK(yse_dsp_round_process(rn, in.data(), nullptr, LENGTH) == 0);
    CHECK(yse_dsp_round_process_signal(rn, in.data(), nullptr, out.data(), LENGTH) == 0);
    CHECK(yse_dsp_trunc_process(tr, nullptr, out.data(), LENGTH) == 0);
    CHECK(yse_dsp_scale_process(sc, nullptr, out.data(), LENGTH) == 0);
    // Each of the six scale arrays is required.
    for (int missing = 0; missing < 7; ++missing) {
      INFO("missing scale array " << missing);
      const float* a[6];
      for (int i = 0; i < 6; ++i)
        a[i] = missing == i ? nullptr : in.data();
      float* o = missing == 6 ? nullptr : out.data();
      CHECK(yse_dsp_scale_process_signal(sc, a[0], a[1], a[2], a[3], a[4], a[5], o, LENGTH) == 0);
    }
    CHECK(yse_dsp_trig_process(tg, nullptr, out.data(), LENGTH) == 0);
    CHECK(yse_dsp_arc_tangent2_process(at, nullptr, 1.f, out.data(), LENGTH) == 0);
    CHECK(yse_dsp_arc_tangent2_process_signal(at, in.data(), nullptr, out.data(), LENGTH) == 0);
    CHECK(yse_dsp_amp_to_db_process(ad, nullptr, out.data(), LENGTH) == 0);
    CHECK(yse_dsp_db_to_amp_process(da, in.data(), nullptr, LENGTH) == 0);
    CHECK(yse_dsp_ms_to_samples_process(ms, nullptr, out.data(), LENGTH) == 0);
    CHECK(yse_dsp_samples_to_ms_process(sm, in.data(), nullptr, LENGTH) == 0);
    for (float v : out)
      REQUIRE(v == 7.f);
    for (float v : extra)
      REQUIRE(v == 7.f);

    // The polar outputs are optional: one or both may be NULL, and the call
    // still runs (and advances nothing else: these classes keep no state).
    CHECK(yse_dsp_cart_to_polar_process(cp, in.data(), 0.5f, nullptr, nullptr, LENGTH) == LENGTH);
    CHECK(yse_dsp_cart_to_polar_process(cp, in.data(), 0.5f, out.data(), nullptr, LENGTH) ==
          LENGTH);
    CHECK(yse_dsp_cart_to_polar_process_signal(cp, in.data(), in.data(), nullptr, extra.data(),
                                               LENGTH) == LENGTH);
    CHECK(yse_dsp_polar_to_cart_process(pc, in.data(), 0.5f, nullptr, extra.data(), LENGTH) ==
          LENGTH);
    CHECK(yse_dsp_polar_to_cart_process_signal(pc, in.data(), in.data(), out.data(), nullptr,
                                               LENGTH) == LENGTH);
    // The value calls' outputs are optional too.
    yse_dsp_cart_to_polar_value(1.f, 1.f, nullptr, nullptr);
    yse_dsp_polar_to_cart_value(1.f, 1.f, nullptr, nullptr);
    float only = 7.f;
    yse_dsp_cart_to_polar_value(0.f, 2.f, &only, nullptr);
    CHECK(only == 2.f);
    yse_dsp_polar_to_cart_value(2.f, 0.f, nullptr, &only);
    CHECK(only == 0.f);
    // A zero length is a valid, empty call.
    CHECK(yse_dsp_trunc_process(tr, in.data(), out.data(), 0) == 0);

    for (YseDspProcessor* p : all)
      yse_dsp_processor_destroy(p);
  }

  TEST_CASE("c api operators: setters and getters round-trip") {
    SUBCASE("accumulator and delta") {
      YseDspProcessor* a = yse_dsp_accumulator_create(-1.25);
      CHECK(yse_dsp_accumulator_get_sum(a) == -1.25);
      yse_dsp_accumulator_set(a, 1e12 + 0.5);
      CHECK(yse_dsp_accumulator_get_sum(a) == 1e12 + 0.5);
      yse_dsp_accumulator_reset(a);
      CHECK(yse_dsp_accumulator_get_sum(a) == 0.0);
      CHECK(yse_dsp_accumulator_process_sample(a, 2.f) == 2.f);
      CHECK(yse_dsp_accumulator_process_sample_reset(a, 3.f, 1.f) == 3.f);
      CHECK(yse_dsp_accumulator_get_sum(a) == 3.0);
      YseDspProcessor* d = yse_dsp_delta_create();
      CHECK(yse_dsp_delta_get_previous(d) == 0.f);
      yse_dsp_delta_set(d, 0.75f);
      CHECK(yse_dsp_delta_get_previous(d) == 0.75f);
      CHECK(yse_dsp_delta_process_sample(d, 1.f) == 0.25f);
      CHECK(yse_dsp_delta_get_previous(d) == 1.f);
      yse_dsp_delta_reset(d);
      CHECK(yse_dsp_delta_get_previous(d) == 0.f);
      yse_dsp_processor_destroy(a);
      yse_dsp_processor_destroy(d);
    }

    SUBCASE("bitwise, bitnot and bitshift") {
      YseDspProcessor* b =
          yse_dsp_bitwise_create(YSE_DSP_BITWISE_XOR, 0xFFFF0000u, YSE_DSP_BIT_RAW_INTEGER);
      CHECK(yse_dsp_bitwise_get_op(b) == YSE_DSP_BITWISE_XOR);
      CHECK(yse_dsp_bitwise_get_mask(b) == 0xFFFF0000u);
      CHECK(yse_dsp_bitwise_get_mode(b) == YSE_DSP_BIT_RAW_INTEGER);
      yse_dsp_bitwise_set_op(b, YSE_DSP_BITWISE_OR);
      yse_dsp_bitwise_set_mask(b, 0x80000001u);
      yse_dsp_bitwise_set_mode(b, YSE_DSP_BIT_INTEGER_RAW);
      CHECK(yse_dsp_bitwise_get_op(b) == YSE_DSP_BITWISE_OR);
      CHECK(yse_dsp_bitwise_get_mask(b) == 0x80000001u);
      CHECK(yse_dsp_bitwise_get_mode(b) == YSE_DSP_BIT_INTEGER_RAW);
      // Out of range: ignored.
      yse_dsp_bitwise_set_op(b, static_cast<YseDspBitwiseOp>(3));
      yse_dsp_bitwise_set_op(b, static_cast<YseDspBitwiseOp>(-1));
      yse_dsp_bitwise_set_mode(b, static_cast<YseDspBitMode>(4));
      CHECK(yse_dsp_bitwise_get_op(b) == YSE_DSP_BITWISE_OR);
      CHECK(yse_dsp_bitwise_get_mode(b) == YSE_DSP_BIT_INTEGER_RAW);
      yse_dsp_processor_destroy(b);
      // A constructor out of range takes the engine defaults.
      b = yse_dsp_bitwise_create(static_cast<YseDspBitwiseOp>(9), 5u,
                                 static_cast<YseDspBitMode>(-2));
      CHECK(yse_dsp_bitwise_get_op(b) == YSE_DSP_BITWISE_AND);
      CHECK(yse_dsp_bitwise_get_mode(b) == YSE_DSP_BIT_RAW);
      CHECK(yse_dsp_bitwise_get_mask(b) == 5u);
      yse_dsp_processor_destroy(b);

      YseDspProcessor* n = yse_dsp_bitnot_create(YSE_DSP_BIT_INTEGER);
      CHECK(yse_dsp_bitnot_get_mode(n) == YSE_DSP_BIT_INTEGER);
      yse_dsp_bitnot_set_mode(n, YSE_DSP_BIT_RAW);
      CHECK(yse_dsp_bitnot_get_mode(n) == YSE_DSP_BIT_RAW);
      yse_dsp_bitnot_set_mode(n, static_cast<YseDspBitMode>(4));
      CHECK(yse_dsp_bitnot_get_mode(n) == YSE_DSP_BIT_RAW);
      yse_dsp_processor_destroy(n);
      n = yse_dsp_bitnot_create(static_cast<YseDspBitMode>(4));
      CHECK(yse_dsp_bitnot_get_mode(n) == YSE_DSP_BIT_RAW);
      yse_dsp_processor_destroy(n);

      YseDspProcessor* s = yse_dsp_bitshift_create(-3, YSE_DSP_BIT_INTEGER);
      CHECK(yse_dsp_bitshift_get_shift(s) == -3);
      CHECK(yse_dsp_bitshift_get_mode(s) == YSE_DSP_BIT_INTEGER);
      yse_dsp_bitshift_set_shift(s, 40);
      yse_dsp_bitshift_set_mode(s, YSE_DSP_BIT_RAW_INTEGER);
      CHECK(yse_dsp_bitshift_get_shift(s) == 40);
      CHECK(yse_dsp_bitshift_get_mode(s) == YSE_DSP_BIT_RAW_INTEGER);
      yse_dsp_bitshift_set_mode(s, static_cast<YseDspBitMode>(-1));
      CHECK(yse_dsp_bitshift_get_mode(s) == YSE_DSP_BIT_RAW_INTEGER);
      yse_dsp_processor_destroy(s);
    }

    SUBCASE("compare") {
      YseDspProcessor* c = yse_dsp_compare_create(YSE_DSP_COMPARE_GREATER_EQUAL);
      CHECK(yse_dsp_compare_get_operator(c) == YSE_DSP_COMPARE_GREATER_EQUAL);
      CHECK(yse_dsp_compare_get_fuzzy(c) == 0.f);
      yse_dsp_compare_set_operator(c, YSE_DSP_COMPARE_LESS_EQUAL);
      yse_dsp_compare_set_fuzzy(c, 0.25f);
      CHECK(yse_dsp_compare_get_operator(c) == YSE_DSP_COMPARE_LESS_EQUAL);
      CHECK(yse_dsp_compare_get_fuzzy(c) == 0.25f);
      // A negative tolerance is taken as 0, as the engine takes it.
      yse_dsp_compare_set_fuzzy(c, -1.f);
      DSP::compare ref;
      ref.setFuzzy(-1.f);
      CHECK(yse_dsp_compare_get_fuzzy(c) == ref.getFuzzy());
      yse_dsp_compare_set_operator(c, static_cast<YseDspCompareOp>(6));
      CHECK(yse_dsp_compare_get_operator(c) == YSE_DSP_COMPARE_LESS_EQUAL);
      yse_dsp_processor_destroy(c);
      c = yse_dsp_compare_create(static_cast<YseDspCompareOp>(6));
      CHECK(yse_dsp_compare_get_operator(c) == YSE_DSP_COMPARE_EQUAL);
      yse_dsp_processor_destroy(c);
    }

    SUBCASE("degrade and downsample") {
      YseDspProcessor* d = yse_dsp_degrade_create();
      CHECK(yse_dsp_degrade_get_ratio(d) == 1.f);
      CHECK(yse_dsp_degrade_get_bits(d) == YSE_DSP_DEGRADE_DEFAULT_BITS);
      yse_dsp_degrade_set_ratio(d, 0.3f);
      yse_dsp_degrade_set_bits(d, 6);
      CHECK(yse_dsp_degrade_get_ratio(d) == 0.3f);
      CHECK(yse_dsp_degrade_get_bits(d) == 6);
      yse_dsp_degrade_set_ratio(d, 2.f);
      yse_dsp_degrade_set_bits(d, 99);
      CHECK(yse_dsp_degrade_get_ratio(d) == 1.f);
      CHECK(yse_dsp_degrade_get_bits(d) == YSE_DSP_DEGRADE_MAX_BITS);
      yse_dsp_degrade_set_bits(d, -5);
      CHECK(yse_dsp_degrade_get_bits(d) == YSE_DSP_DEGRADE_MIN_BITS);
      yse_dsp_degrade_set_ratio(d, NaN);
      CHECK(yse_dsp_degrade_get_ratio(d) == 1.f);
      (void)yse_dsp_degrade_process_sample(d, 0.4f);
      CHECK(yse_dsp_degrade_get_held(d) == 0.4f);
      yse_dsp_degrade_reset(d);
      CHECK(yse_dsp_degrade_get_held(d) == 0.f);
      yse_dsp_processor_destroy(d);

      YseDspProcessor* s = yse_dsp_downsample_create();
      CHECK(yse_dsp_downsample_get_period(s) == 1.f);
      yse_dsp_downsample_set_period(s, 2.5f);
      CHECK(yse_dsp_downsample_get_period(s) == 2.5f);
      yse_dsp_downsample_set_period(s, 0.2f);
      CHECK(yse_dsp_downsample_get_period(s) == YSE_DSP_DOWNSAMPLE_MIN_PERIOD);
      yse_dsp_downsample_set_period(s, NaN);
      CHECK(yse_dsp_downsample_get_period(s) == YSE_DSP_DOWNSAMPLE_MIN_PERIOD);
      (void)yse_dsp_downsample_process_sample(s, 0.6f);
      CHECK(yse_dsp_downsample_get_held(s) == 0.6f);
      yse_dsp_downsample_reset(s);
      CHECK(yse_dsp_downsample_get_held(s) == 0.f);
      yse_dsp_processor_destroy(s);
    }

    SUBCASE("fold and overdrive") {
      YseDspProcessor* f = yse_dsp_fold_create();
      CHECK(yse_dsp_fold_get_mode(f) == YSE_DSP_FOLD_MODE_FOLD);
      CHECK(yse_dsp_fold_get_low(f) == 0.f);
      CHECK(yse_dsp_fold_get_high(f) == 1.f);
      yse_dsp_fold_set_mode(f, YSE_DSP_FOLD_MODE_WRAP);
      yse_dsp_fold_set_low(f, -2.f);
      yse_dsp_fold_set_high(f, 3.f);
      CHECK(yse_dsp_fold_get_mode(f) == YSE_DSP_FOLD_MODE_WRAP);
      CHECK(yse_dsp_fold_get_low(f) == -2.f);
      CHECK(yse_dsp_fold_get_high(f) == 3.f);
      yse_dsp_fold_set_range(f, 0.5f, Inf);
      CHECK(yse_dsp_fold_get_low(f) == 0.5f);
      CHECK(yse_dsp_fold_get_high(f) == 3.f);
      yse_dsp_fold_set_mode(f, static_cast<YseDspFoldMode>(3));
      CHECK(yse_dsp_fold_get_mode(f) == YSE_DSP_FOLD_MODE_WRAP);
      yse_dsp_processor_destroy(f);

      YseDspProcessor* o = yse_dsp_overdrive_create();
      CHECK(yse_dsp_overdrive_get_drive(o) == 1.f);
      yse_dsp_overdrive_set_drive(o, 4.5f);
      CHECK(yse_dsp_overdrive_get_drive(o) == 4.5f);
      yse_dsp_overdrive_set_drive(o, NaN);
      CHECK(yse_dsp_overdrive_get_drive(o) == 4.5f);
      yse_dsp_processor_destroy(o);
    }

    SUBCASE("round") {
      YseDspProcessor* r = yse_dsp_round_create(0.5f, 0);
      CHECK(yse_dsp_round_get_step(r) == 0.5f);
      CHECK(yse_dsp_round_get_nearest(r) == 0);
      yse_dsp_round_set_step(r, 12.f);
      yse_dsp_round_set_nearest(r, 7);
      CHECK(yse_dsp_round_get_step(r) == 12.f);
      CHECK(yse_dsp_round_get_nearest(r) == 1);
      yse_dsp_processor_destroy(r);
    }

    SUBCASE("scale") {
      YseDspProcessor* s = yse_dsp_scale_create();
      CHECK(yse_dsp_scale_get_mode(s) == YSE_DSP_SCALE_MODERN);
      CHECK(yse_dsp_scale_get_in_low(s) == 0.f);
      CHECK(yse_dsp_scale_get_in_high(s) == 1.f);
      CHECK(yse_dsp_scale_get_out_low(s) == 0.f);
      CHECK(yse_dsp_scale_get_out_high(s) == 1.f);
      CHECK(yse_dsp_scale_get_exponent(s) == 1.f);
      yse_dsp_scale_set_mode(s, YSE_DSP_SCALE_MODERN_INVERSE);
      yse_dsp_scale_set_in_low(s, -1.f);
      yse_dsp_scale_set_in_high(s, 2.f);
      yse_dsp_scale_set_out_low(s, 10.f);
      yse_dsp_scale_set_out_high(s, -10.f);
      yse_dsp_scale_set_exponent(s, 3.f);
      CHECK(yse_dsp_scale_get_mode(s) == YSE_DSP_SCALE_MODERN_INVERSE);
      CHECK(yse_dsp_scale_get_in_low(s) == -1.f);
      CHECK(yse_dsp_scale_get_in_high(s) == 2.f);
      CHECK(yse_dsp_scale_get_out_low(s) == 10.f);
      CHECK(yse_dsp_scale_get_out_high(s) == -10.f);
      CHECK(yse_dsp_scale_get_exponent(s) == 3.f);
      yse_dsp_scale_set_in_range(s, 4.f, 5.f);
      yse_dsp_scale_set_out_range(s, 6.f, NaN);
      CHECK(yse_dsp_scale_get_in_low(s) == 4.f);
      CHECK(yse_dsp_scale_get_in_high(s) == 5.f);
      CHECK(yse_dsp_scale_get_out_low(s) == 6.f);
      CHECK(yse_dsp_scale_get_out_high(s) == -10.f);
      yse_dsp_scale_set_exponent(s, Inf);
      CHECK(yse_dsp_scale_get_exponent(s) == 3.f);
      yse_dsp_scale_set_mode(s, static_cast<YseDspScaleMode>(3));
      CHECK(yse_dsp_scale_get_mode(s) == YSE_DSP_SCALE_MODERN_INVERSE);
      yse_dsp_processor_destroy(s);
    }

    SUBCASE("trig") {
      YseDspProcessor* t = yse_dsp_trig_create(YSE_DSP_TRIG_ATANH);
      CHECK(yse_dsp_trig_get_function(t) == YSE_DSP_TRIG_ATANH);
      yse_dsp_trig_set_function(t, YSE_DSP_TRIG_SINH);
      CHECK(yse_dsp_trig_get_function(t) == YSE_DSP_TRIG_SINH);
      yse_dsp_trig_set_function(t, static_cast<YseDspTrigFunction>(12));
      CHECK(yse_dsp_trig_get_function(t) == YSE_DSP_TRIG_SINH);
      yse_dsp_processor_destroy(t);
      t = yse_dsp_trig_create(static_cast<YseDspTrigFunction>(12));
      CHECK(yse_dsp_trig_get_function(t) == YSE_DSP_TRIG_COS);
      yse_dsp_processor_destroy(t);
    }
  }

  TEST_CASE("c api operators: every process entry point matches the engine class") {
    const std::vector<float> noise = Signal(LENGTH);
    const std::vector<float> other = Signal(LENGTH, 1.f, 0.f, 999u);
    const std::vector<float> wide = Signal(LENGTH, 5.f);
    const std::vector<float> ints = Signal(LENGTH, 200.f);
    const std::vector<float> positive = Signal(LENGTH, 1.f, 2.f, 77u);
    const std::vector<float> angles = Signal(LENGTH, 20.f);
    const std::vector<float> resets = Gate(LENGTH, 97, 1);
    const std::vector<float> levels = Signal(LENGTH, 30.f, -20.f);
    std::vector<float> dirty = noise;
    for (unsigned i = 3; i < LENGTH; i += 41)
      dirty[i] = (i % 3 == 0) ? NaN : ((i % 3 == 1) ? Inf : -Inf);

    std::vector<Parity> cases;
    cases.reserve(96);

    // The one-in, one-out operators at their current settings.
    struct PlainCase {
      const char* name;
      std::function<YseDspProcessor*()> make;
      PlainFn process;
      const std::vector<float>* input;
      std::function<Outputs()> engine;
    };
    const std::vector<PlainCase> plain = {
        {"accumulator", [] { return yse_dsp_accumulator_create(0.5); }, yse_dsp_accumulator_process,
         &noise,
         [&] {
           DSP::accumulator f(0.5);
           return RefPlain(f, noise);
         }},
        {"bitwise mask",
         [] { return yse_dsp_bitwise_create(YSE_DSP_BITWISE_AND, 0xFFFF0000u, YSE_DSP_BIT_RAW); },
         yse_dsp_bitwise_process, &noise,
         [&] {
           DSP::bitwise f(DSP::bitwise::OP::AND, static_cast<Int>(0xFFFF0000u));
           return RefPlain(f, noise);
         }},
        {"bitwise integer",
         [] { return yse_dsp_bitwise_create(YSE_DSP_BITWISE_XOR, 0x55u, YSE_DSP_BIT_INTEGER); },
         yse_dsp_bitwise_process, &ints,
         [&] {
           DSP::bitwise f(DSP::bitwise::OP::XOR, 0x55, DSP::BIT_MODE::INTEGER);
           return RefPlain(f, ints);
         }},
        {"bitnot", [] { return yse_dsp_bitnot_create(YSE_DSP_BIT_INTEGER); },
         yse_dsp_bitnot_process, &ints,
         [&] {
           DSP::bitnot f(DSP::BIT_MODE::INTEGER);
           return RefPlain(f, ints);
         }},
        {"bitshift left", [] { return yse_dsp_bitshift_create(3, YSE_DSP_BIT_INTEGER); },
         yse_dsp_bitshift_process, &ints,
         [&] {
           DSP::bitshift f(3, DSP::BIT_MODE::INTEGER);
           return RefPlain(f, ints);
         }},
        {"bitshift right raw", [] { return yse_dsp_bitshift_create(-2, YSE_DSP_BIT_RAW); },
         yse_dsp_bitshift_process, &noise,
         [&] {
           DSP::bitshift f(-2, DSP::BIT_MODE::RAW);
           return RefPlain(f, noise);
         }},
        {"bitsafe", [] { return yse_dsp_bitsafe_create(); }, yse_dsp_bitsafe_process, &dirty,
         [&] {
           DSP::bitsafe f;
           return RefPlain(f, dirty);
         }},
        {"degrade",
         [] {
           auto* p = yse_dsp_degrade_create();
           yse_dsp_degrade_set_ratio(p, 0.3f);
           yse_dsp_degrade_set_bits(p, 4);
           return p;
         },
         yse_dsp_degrade_process, &noise,
         [&] {
           DSP::degrade f;
           f.ratio(0.3f).bits(4);
           return RefPlain(f, noise);
         }},
        {"delta",
         [] {
           auto* p = yse_dsp_delta_create();
           yse_dsp_delta_set(p, 0.5f);
           return p;
         },
         yse_dsp_delta_process, &noise,
         [&] {
           DSP::delta f;
           f.set(0.5f);
           return RefPlain(f, noise);
         }},
        {"downsample",
         [] {
           auto* p = yse_dsp_downsample_create();
           yse_dsp_downsample_set_period(p, 2.5f);
           return p;
         },
         yse_dsp_downsample_process, &noise,
         [&] {
           DSP::downsample f;
           f.period(2.5f);
           return RefPlain(f, noise);
         }},
        {"overdrive",
         [] {
           auto* p = yse_dsp_overdrive_create();
           yse_dsp_overdrive_set_drive(p, 3.f);
           return p;
         },
         yse_dsp_overdrive_process, &wide,
         [&] {
           DSP::overdrive f;
           f.drive(3.f);
           return RefPlain(f, wide);
         }},
        {"phase_wrap", [] { return yse_dsp_phase_wrap_create(); }, yse_dsp_phase_wrap_process,
         &angles,
         [&] {
           DSP::phaseWrap f;
           return RefPlain(f, angles);
         }},
        {"round nearest", [] { return yse_dsp_round_create(0.25f, 1); }, yse_dsp_round_process,
         &wide,
         [&] {
           DSP::round f(0.25f, true);
           return RefPlain(f, wide);
         }},
        {"round floor", [] { return yse_dsp_round_create(0.5f, 0); }, yse_dsp_round_process, &wide,
         [&] {
           DSP::round f(0.5f, false);
           return RefPlain(f, wide);
         }},
        {"trunc", [] { return yse_dsp_trunc_create(); }, yse_dsp_trunc_process, &wide,
         [&] {
           DSP::trunc f;
           return RefPlain(f, wide);
         }},
        {"scale modern",
         [] {
           auto* p = yse_dsp_scale_create();
           yse_dsp_scale_set_in_range(p, -1.f, 1.f);
           yse_dsp_scale_set_out_range(p, 10.f, 100.f);
           yse_dsp_scale_set_exponent(p, 2.f);
           return p;
         },
         yse_dsp_scale_process, &noise,
         [&] {
           DSP::scale f;
           f.inRange(-1.f, 1.f).outRange(10.f, 100.f).exponent(2.f);
           return RefPlain(f, noise);
         }},
        {"scale classic",
         [] {
           auto* p = yse_dsp_scale_create();
           yse_dsp_scale_set_mode(p, YSE_DSP_SCALE_CLASSIC);
           yse_dsp_scale_set_out_range(p, 0.f, 5.f);
           yse_dsp_scale_set_exponent(p, 1.06f);
           return p;
         },
         yse_dsp_scale_process, &noise,
         [&] {
           DSP::scale f;
           f.mode(DSP::scale::MODE::CLASSIC).outRange(0.f, 5.f).exponent(1.06f);
           return RefPlain(f, noise);
         }},
        {"amp_to_db", [] { return yse_dsp_amp_to_db_create(); }, yse_dsp_amp_to_db_process, &noise,
         [&] {
           DSP::ampToDb f;
           return RefPlain(f, noise);
         }},
        {"db_to_amp", [] { return yse_dsp_db_to_amp_create(); }, yse_dsp_db_to_amp_process, &levels,
         [&] {
           DSP::dbToAmp f;
           return RefPlain(f, levels);
         }},
        {"ms_to_samples", [] { return yse_dsp_ms_to_samples_create(); },
         yse_dsp_ms_to_samples_process, &wide,
         [&] {
           DSP::msToSamples f;
           return RefPlain(f, wide);
         }},
        {"samples_to_ms", [] { return yse_dsp_samples_to_ms_create(); },
         yse_dsp_samples_to_ms_process, &ints,
         [&] {
           DSP::samplesToMs f;
           return RefPlain(f, ints);
         }},
    };
    for (const PlainCase& c : plain)
      cases.push_back({c.name,
                       [=] {
                         YseDspProcessor* p = c.make();
                         return RunC(
                             p, [&](float* o) { return c.process(p, c.input->data(), o, LENGTH); });
                       },
                       c.engine});

    // Every trig function.
    for (int fn = YSE_DSP_TRIG_COS; fn <= YSE_DSP_TRIG_ATANH; ++fn) {
      // A wide input: the clamped functions see their domain edges.
      const std::vector<float>& input = wide;
      cases.push_back({"trig " + std::to_string(fn),
                       [&input, fn] {
                         YseDspProcessor* p = yse_dsp_trig_create(YSE_DSP_TRIG_COS);
                         yse_dsp_trig_set_function(p, static_cast<YseDspTrigFunction>(fn));
                         return RunC(p, [&](float* o) {
                           return yse_dsp_trig_process(p, input.data(), o, LENGTH);
                         });
                       },
                       [&input, fn] {
                         DSP::trig f(static_cast<DSP::trig::FN>(fn));
                         return RefPlain(f, input);
                       }});
    }

    // Every fold mode, set and per sample.
    for (int mode = YSE_DSP_FOLD_MODE_FOLD; mode <= YSE_DSP_FOLD_MODE_CLIP; ++mode) {
      cases.push_back({"fold " + std::to_string(mode),
                       [&, mode] {
                         YseDspProcessor* p = yse_dsp_fold_create();
                         yse_dsp_fold_set_mode(p, static_cast<YseDspFoldMode>(mode));
                         yse_dsp_fold_set_range(p, 0.4f, -0.3f);
                         return RunC(p, [&](float* o) {
                           return yse_dsp_fold_process(p, wide.data(), o, LENGTH);
                         });
                       },
                       [&, mode] {
                         DSP::fold f;
                         f.mode(static_cast<DSP::fold::MODE>(mode)).range(0.4f, -0.3f);
                         return RefPlain(f, wide);
                       }});
      cases.push_back({"fold signal " + std::to_string(mode),
                       [&, mode] {
                         YseDspProcessor* p = yse_dsp_fold_create();
                         yse_dsp_fold_set_mode(p, static_cast<YseDspFoldMode>(mode));
                         return RunC(p, [&](float* o) {
                           return yse_dsp_fold_process_signal(p, wide.data(), noise.data(),
                                                              positive.data(), o, LENGTH);
                         });
                       },
                       [&, mode] {
                         DSP::fold f;
                         f.mode(static_cast<DSP::fold::MODE>(mode));
                         return RefTripled(f, wide, noise, positive);
                       }});
    }

    // The binary operators, against one number and against a signal.
    struct BinaryCase {
      const char* name;
      CreateFn make;
      ConstFn constant;
      PairedFn signal;
      std::function<Outputs(const std::vector<float>&, float)> engineConst;
      std::function<Outputs(const std::vector<float>&, const std::vector<float>&)> engineSignal;
      const std::vector<float>* in;
      float right;
      const std::vector<float>* rightSignal;
    };
    const std::vector<BinaryCase> binary = {
        {"reverse_subtract", yse_dsp_reverse_subtract_create, yse_dsp_reverse_subtract_process,
         yse_dsp_reverse_subtract_process_signal,
         [](const std::vector<float>& a, float r) {
           DSP::reverseSubtract f;
           return RefConst(f, a, r);
         },
         [](const std::vector<float>& a, const std::vector<float>& b) {
           DSP::reverseSubtract f;
           return RefPaired(f, a, b);
         },
         &noise, 1.f, &other},
        {"reverse_divide", yse_dsp_reverse_divide_create, yse_dsp_reverse_divide_process,
         yse_dsp_reverse_divide_process_signal,
         [](const std::vector<float>& a, float r) {
           DSP::reverseDivide f;
           return RefConst(f, a, r);
         },
         [](const std::vector<float>& a, const std::vector<float>& b) {
           DSP::reverseDivide f;
           return RefPaired(f, a, b);
         },
         &noise, 1.f, &other},
        {"modulo", yse_dsp_modulo_create, yse_dsp_modulo_process, yse_dsp_modulo_process_signal,
         [](const std::vector<float>& a, float r) {
           DSP::modulo f;
           return RefConst(f, a, r);
         },
         [](const std::vector<float>& a, const std::vector<float>& b) {
           DSP::modulo f;
           return RefPaired(f, a, b);
         },
         &wide, 2.f, &positive},
        {"minimum", yse_dsp_minimum_create, yse_dsp_minimum_process, yse_dsp_minimum_process_signal,
         [](const std::vector<float>& a, float r) {
           DSP::minimum f;
           return RefConst(f, a, r);
         },
         [](const std::vector<float>& a, const std::vector<float>& b) {
           DSP::minimum f;
           return RefPaired(f, a, b);
         },
         &noise, 0.1f, &other},
        {"maximum", yse_dsp_maximum_create, yse_dsp_maximum_process, yse_dsp_maximum_process_signal,
         [](const std::vector<float>& a, float r) {
           DSP::maximum f;
           return RefConst(f, a, r);
         },
         [](const std::vector<float>& a, const std::vector<float>& b) {
           DSP::maximum f;
           return RefPaired(f, a, b);
         },
         &noise, 0.f, &other},
        {"arc_tangent2", yse_dsp_arc_tangent2_create, yse_dsp_arc_tangent2_process,
         yse_dsp_arc_tangent2_process_signal,
         [](const std::vector<float>& a, float r) {
           DSP::arcTangent2 f;
           return RefConst(f, a, r);
         },
         [](const std::vector<float>& a, const std::vector<float>& b) {
           DSP::arcTangent2 f;
           return RefPaired(f, a, b);
         },
         &noise, -0.5f, &other},
    };
    for (const BinaryCase& c : binary) {
      cases.push_back({std::string(c.name),
                       [=] {
                         YseDspProcessor* p = c.make();
                         return RunC(p, [&](float* o) {
                           return c.constant(p, c.in->data(), c.right, o, LENGTH);
                         });
                       },
                       [=] { return c.engineConst(*c.in, c.right); }});
      cases.push_back({std::string(c.name) + " signal",
                       [=] {
                         YseDspProcessor* p = c.make();
                         return RunC(p, [&](float* o) {
                           return c.signal(p, c.in->data(), c.rightSignal->data(), o, LENGTH);
                         });
                       },
                       [=] { return c.engineSignal(*c.in, *c.rightSignal); }});
    }

    // Every compare operator, against a number and a signal, with a tolerance.
    for (int op = YSE_DSP_COMPARE_EQUAL; op <= YSE_DSP_COMPARE_LESS_EQUAL; ++op) {
      cases.push_back({"compare " + std::to_string(op),
                       [&, op] {
                         YseDspProcessor* p =
                             yse_dsp_compare_create(static_cast<YseDspCompareOp>(op));
                         yse_dsp_compare_set_fuzzy(p, 0.3f);
                         return RunC(p, [&](float* o) {
                           return yse_dsp_compare_process(p, wide.data(), 1.f, o, LENGTH);
                         });
                       },
                       [&, op] {
                         DSP::compare f(static_cast<DSP::compare::OP>(op));
                         f.setFuzzy(0.3f);
                         return RefConst(f, wide, 1.f);
                       }});
      cases.push_back({"compare signal " + std::to_string(op),
                       [&, op] {
                         YseDspProcessor* p = yse_dsp_compare_create(YSE_DSP_COMPARE_EQUAL);
                         yse_dsp_compare_set_operator(p, static_cast<YseDspCompareOp>(op));
                         yse_dsp_compare_set_fuzzy(p, 0.2f);
                         return RunC(p, [&](float* o) {
                           return yse_dsp_compare_process_signal(p, noise.data(), other.data(), o,
                                                                 LENGTH);
                         });
                       },
                       [&, op] {
                         DSP::compare f(static_cast<DSP::compare::OP>(op));
                         f.setFuzzy(0.2f);
                         return RefPaired(f, noise, other);
                       }});
    }

    // The per-sample settings and the reset signal.
    cases.push_back({"accumulator reset",
                     [&] {
                       YseDspProcessor* p = yse_dsp_accumulator_create(0.0);
                       return RunC(p, [&](float* o) {
                         return yse_dsp_accumulator_process_reset(p, noise.data(), resets.data(), o,
                                                                  LENGTH);
                       });
                     },
                     [&] {
                       DSP::accumulator f;
                       return RefPaired(f, noise, resets);
                     }});
    cases.push_back({"bitwise signal",
                     [&] {
                       YseDspProcessor* p =
                           yse_dsp_bitwise_create(YSE_DSP_BITWISE_OR, 0u, YSE_DSP_BIT_RAW_INTEGER);
                       return RunC(p, [&](float* o) {
                         return yse_dsp_bitwise_process_signal(p, noise.data(), ints.data(), o,
                                                               LENGTH);
                       });
                     },
                     [&] {
                       DSP::bitwise f(DSP::bitwise::OP::OR, 0, DSP::BIT_MODE::RAW_INTEGER);
                       return RefPaired(f, noise, ints);
                     }});
    const std::vector<float> ratios = Signal(LENGTH, 0.4f, 0.5f);
    const std::vector<float> bits = Signal(LENGTH, 8.f, 10.f);
    cases.push_back({"degrade signal",
                     [&] {
                       YseDspProcessor* p = yse_dsp_degrade_create();
                       return RunC(p, [&](float* o) {
                         return yse_dsp_degrade_process_signal(p, noise.data(), ratios.data(),
                                                               bits.data(), o, LENGTH);
                       });
                     },
                     [&] {
                       DSP::degrade f;
                       return RefTripled(f, noise, ratios, bits);
                     }});
    const std::vector<float> periods = Signal(LENGTH, 2.f, 3.f);
    cases.push_back({"downsample signal",
                     [&] {
                       YseDspProcessor* p = yse_dsp_downsample_create();
                       return RunC(p, [&](float* o) {
                         return yse_dsp_downsample_process_signal(p, noise.data(), periods.data(),
                                                                  o, LENGTH);
                       });
                     },
                     [&] {
                       DSP::downsample f;
                       return RefPaired(f, noise, periods);
                     }});
    cases.push_back({"overdrive signal",
                     [&] {
                       YseDspProcessor* p = yse_dsp_overdrive_create();
                       return RunC(p, [&](float* o) {
                         return yse_dsp_overdrive_process_signal(p, wide.data(), positive.data(), o,
                                                                 LENGTH);
                       });
                     },
                     [&] {
                       DSP::overdrive f;
                       return RefPaired(f, wide, positive);
                     }});
    const std::vector<float> steps = Signal(LENGTH, 0.5f, 0.6f);
    cases.push_back({"round signal",
                     [&] {
                       YseDspProcessor* p = yse_dsp_round_create(1.f, 0);
                       return RunC(p, [&](float* o) {
                         return yse_dsp_round_process_signal(p, wide.data(), steps.data(), o,
                                                             LENGTH);
                       });
                     },
                     [&] {
                       DSP::round f(1.f, false);
                       return RefPaired(f, wide, steps);
                     }});
    const std::vector<float> inLow = Signal(LENGTH, 0.2f, -1.f, 1u);
    const std::vector<float> inHigh = Signal(LENGTH, 0.2f, 1.f, 2u);
    const std::vector<float> outLow = Signal(LENGTH, 5.f, 0.f, 3u);
    const std::vector<float> outHigh = Signal(LENGTH, 5.f, 50.f, 4u);
    const std::vector<float> exponents = Signal(LENGTH, 0.5f, 1.5f, 5u);
    cases.push_back(
        {"scale signal",
         [&] {
           YseDspProcessor* p = yse_dsp_scale_create();
           yse_dsp_scale_set_mode(p, YSE_DSP_SCALE_MODERN_INVERSE);
           return RunC(p, [&](float* o) {
             return yse_dsp_scale_process_signal(p, noise.data(), inLow.data(), inHigh.data(),
                                                 outLow.data(), outHigh.data(), exponents.data(), o,
                                                 LENGTH);
           });
         },
         [&] {
           DSP::scale f;
           f.mode(DSP::scale::MODE::MODERN_INVERSE);
           Outputs r(1, std::vector<float>(LENGTH));
           DSP::buffer a, b, c, d, e, g;
           Blocks(LENGTH, [&](unsigned o, unsigned n) {
             Put(f(Stage(a, noise, o, n), Stage(b, inLow, o, n), Stage(c, inHigh, o, n),
                   Stage(d, outLow, o, n), Stage(e, outHigh, o, n), Stage(g, exponents, o, n)),
                 r[0], o, n);
           });
           return r;
         }});

    // The polar converters: both outputs.
    cases.push_back({"cart_to_polar",
                     [&] {
                       YseDspProcessor* p = yse_dsp_cart_to_polar_create();
                       return RunC2(p, [&](float* m, float* ph) {
                         return yse_dsp_cart_to_polar_process(p, noise.data(), -0.3f, m, ph,
                                                              LENGTH);
                       });
                     },
                     [&] {
                       DSP::cartToPolar f;
                       Outputs r(2, std::vector<float>(LENGTH));
                       DSP::buffer a;
                       Blocks(LENGTH, [&](unsigned o, unsigned n) {
                         f(Stage(a, noise, o, n), -0.3f);
                         Put(f.magnitude(), r[0], o, n);
                         Put(f.phase(), r[1], o, n);
                       });
                       return r;
                     }});
    cases.push_back({"cart_to_polar signal",
                     [&] {
                       YseDspProcessor* p = yse_dsp_cart_to_polar_create();
                       return RunC2(p, [&](float* m, float* ph) {
                         return yse_dsp_cart_to_polar_process_signal(p, noise.data(), other.data(),
                                                                     m, ph, LENGTH);
                       });
                     },
                     [&] {
                       DSP::cartToPolar f;
                       Outputs r(2, std::vector<float>(LENGTH));
                       DSP::buffer a, b;
                       Blocks(LENGTH, [&](unsigned o, unsigned n) {
                         f(Stage(a, noise, o, n), Stage(b, other, o, n));
                         Put(f.magnitude(), r[0], o, n);
                         Put(f.phase(), r[1], o, n);
                       });
                       return r;
                     }});
    cases.push_back({"polar_to_cart",
                     [&] {
                       YseDspProcessor* p = yse_dsp_polar_to_cart_create();
                       return RunC2(p, [&](float* re, float* im) {
                         return yse_dsp_polar_to_cart_process(p, noise.data(), 2.f, re, im, LENGTH);
                       });
                     },
                     [&] {
                       DSP::polarToCart f;
                       Outputs r(2, std::vector<float>(LENGTH));
                       DSP::buffer a;
                       Blocks(LENGTH, [&](unsigned o, unsigned n) {
                         f(Stage(a, noise, o, n), 2.f);
                         Put(f.real(), r[0], o, n);
                         Put(f.imaginary(), r[1], o, n);
                       });
                       return r;
                     }});
    cases.push_back({"polar_to_cart signal",
                     [&] {
                       YseDspProcessor* p = yse_dsp_polar_to_cart_create();
                       return RunC2(p, [&](float* re, float* im) {
                         return yse_dsp_polar_to_cart_process_signal(p, noise.data(), angles.data(),
                                                                     re, im, LENGTH);
                       });
                     },
                     [&] {
                       DSP::polarToCart f;
                       Outputs r(2, std::vector<float>(LENGTH));
                       DSP::buffer a, b;
                       Blocks(LENGTH, [&](unsigned o, unsigned n) {
                         f(Stage(a, noise, o, n), Stage(b, angles, o, n));
                         Put(f.real(), r[0], o, n);
                         Put(f.imaginary(), r[1], o, n);
                       });
                       return r;
                     }});

    for (const Parity& c : cases) {
      INFO("case: " << c.name);
      const Outputs expected = c.engine();
      for (const std::vector<float>& e : expected)
        REQUIRE(Moves(e));
      const Outputs got = c.c();
      REQUIRE(got.size() == expected.size());
      for (std::size_t i = 0; i < got.size(); ++i) {
        INFO("output " << i);
        CheckSame(got[i], expected[i]);
      }
    }
  }

  TEST_CASE("c api operators: the value calls match the engine's statics") {
    const std::vector<float> probes = Probes();
    struct Binary {
      const char* name;
      BinaryValueFn c;
      BinaryValueFn engine;
    };
    const std::vector<Binary> binary = {
        {"reverse_subtract", yse_dsp_reverse_subtract_value, DSP::reverseSubtract::process},
        {"reverse_divide", yse_dsp_reverse_divide_value, DSP::reverseDivide::process},
        {"modulo", yse_dsp_modulo_value, DSP::modulo::process},
        {"minimum", yse_dsp_minimum_value, DSP::minimum::process},
        {"maximum", yse_dsp_maximum_value, DSP::maximum::process},
        {"arc_tangent2", yse_dsp_arc_tangent2_value, DSP::arcTangent2::process},
        {"ms_to_samples", yse_dsp_ms_to_samples_value, DSP::msToSamples::process},
        {"samples_to_ms", yse_dsp_samples_to_ms_value, DSP::samplesToMs::process},
    };
    for (const Binary& b : binary)
      for (float x : probes)
        for (float y : probes) {
          INFO(b.name << "(" << x << ", " << y << ")");
          CHECK(Same(b.c(x, y), b.engine(x, y)));
        }

    struct Unary {
      const char* name;
      UnaryValueFn c;
      UnaryValueFn engine;
    };
    const std::vector<Unary> unary = {
        {"bitsafe", yse_dsp_bitsafe_value, DSP::bitsafe::process},
        {"phase_wrap", yse_dsp_phase_wrap_value, DSP::phaseWrap::process},
        {"trunc", yse_dsp_trunc_value, DSP::trunc::process},
        {"amp_to_db", yse_dsp_amp_to_db_value, DSP::ampToDb::process},
        {"db_to_amp", yse_dsp_db_to_amp_value, DSP::dbToAmp::process},
    };
    for (const Unary& u : unary)
      for (float x : probes) {
        INFO(u.name << "(" << x << ")");
        CHECK(Same(u.c(x), u.engine(x)));
      }

    const unsigned masks[] = {0u, 0xFFFF0000u, 0x80000000u, 0x7F800000u, 0x55u, 0xFFFFFFFFu};
    const int shifts[] = {-40, -31, -3, 0, 1, 5, 31, 32};
    for (float x : probes) {
      INFO("x = " << x);
      for (int m = YSE_DSP_BIT_RAW; m <= YSE_DSP_BIT_INTEGER_RAW; ++m) {
        const auto mode = static_cast<DSP::BIT_MODE>(m);
        for (int op = YSE_DSP_BITWISE_AND; op <= YSE_DSP_BITWISE_XOR; ++op)
          for (unsigned mask : masks)
            CHECK(Same(yse_dsp_bitwise_value(static_cast<YseDspBitwiseOp>(op),
                                             static_cast<YseDspBitMode>(m), x, mask),
                       DSP::bitwise::process(static_cast<DSP::bitwise::OP>(op), mode, x,
                                             static_cast<Int>(mask))));
        CHECK(Same(yse_dsp_bitnot_value(static_cast<YseDspBitMode>(m), x),
                   DSP::bitnot::process(mode, x)));
        for (int s : shifts)
          CHECK(Same(yse_dsp_bitshift_value(static_cast<YseDspBitMode>(m), s, x),
                     DSP::bitshift::process(mode, s, x)));
      }
      for (int op = YSE_DSP_COMPARE_EQUAL; op <= YSE_DSP_COMPARE_LESS_EQUAL; ++op)
        for (float y : probes)
          for (float fuzzy : {0.f, 0.5f})
            CHECK(Same(yse_dsp_compare_value(static_cast<YseDspCompareOp>(op), x, y, fuzzy),
                       DSP::compare::process(static_cast<DSP::compare::OP>(op), x, y, fuzzy)));
      for (int fn = YSE_DSP_TRIG_COS; fn <= YSE_DSP_TRIG_ATANH; ++fn)
        CHECK(Same(yse_dsp_trig_value(static_cast<YseDspTrigFunction>(fn), x),
                   DSP::trig::process(static_cast<DSP::trig::FN>(fn), x)));
      for (int mode = YSE_DSP_FOLD_MODE_FOLD; mode <= YSE_DSP_FOLD_MODE_CLIP; ++mode)
        CHECK(Same(yse_dsp_fold_value(x, 0.5f, -0.25f, static_cast<YseDspFoldMode>(mode)),
                   DSP::fold::Limit(x, 0.5f, -0.25f, static_cast<DSP::fold::MODE>(mode))));
      for (int mode = YSE_DSP_SCALE_MODERN; mode <= YSE_DSP_SCALE_MODERN_INVERSE; ++mode)
        CHECK(Same(
            yse_dsp_scale_value(x, -1.f, 2.f, 10.f, -5.f, 1.5f, static_cast<YseDspScaleMode>(mode)),
            DSP::scale::Map(x, -1.f, 2.f, 10.f, -5.f, 1.5f, static_cast<DSP::scale::MODE>(mode))));
      for (float step : {0.f, 0.25f, -3.f, 1e-30f})
        for (int nearest : {0, 1})
          CHECK(Same(yse_dsp_round_value(x, step, nearest),
                     DSP::round::process(x, step, nearest != 0)));
      for (float y : {0.f, -0.5f, 2.f, NaN, Inf}) {
        float m = 7.f, ph = 7.f, em = 0.f, eph = 0.f;
        yse_dsp_cart_to_polar_value(x, y, &m, &ph);
        DSP::cartToPolar::process(x, y, em, eph);
        CHECK(Same(m, em));
        CHECK(Same(ph, eph));
        float re = 7.f, im = 7.f, ere = 0.f, eim = 0.f;
        yse_dsp_polar_to_cart_value(x, y, &re, &im);
        DSP::polarToCart::process(x, y, ere, eim);
        CHECK(Same(re, ere));
        CHECK(Same(im, eim));
      }
    }
    // A sample-rate conversion at the live rate is the value call at it.
    CHECK(Same(yse_dsp_ms_to_samples_value(12.5f, static_cast<float>(YSE::SAMPLERATE)),
               DSP::MsToSamples(12.5f)));
    CHECK(Same(yse_dsp_samples_to_ms_value(480.f, static_cast<float>(YSE::SAMPLERATE)),
               DSP::SamplesToMs(480.f)));

    // An enum outside its range makes a value call return 0.
    CHECK(yse_dsp_bitwise_value(static_cast<YseDspBitwiseOp>(3), YSE_DSP_BIT_RAW, 1.f, 1u) == 0.f);
    CHECK(yse_dsp_bitwise_value(YSE_DSP_BITWISE_OR, static_cast<YseDspBitMode>(4), 1.f, 1u) == 0.f);
    CHECK(yse_dsp_bitnot_value(static_cast<YseDspBitMode>(-1), 1.f) == 0.f);
    CHECK(yse_dsp_bitshift_value(static_cast<YseDspBitMode>(4), 1, 1.f) == 0.f);
    CHECK(yse_dsp_compare_value(static_cast<YseDspCompareOp>(6), 1.f, 1.f, 0.f) == 0.f);
    CHECK(yse_dsp_trig_value(static_cast<YseDspTrigFunction>(12), 0.5f) == 0.f);
    CHECK(yse_dsp_trig_value(static_cast<YseDspTrigFunction>(-1), 0.5f) == 0.f);
    CHECK(yse_dsp_fold_value(5.f, 0.f, 1.f, static_cast<YseDspFoldMode>(3)) == 0.f);
    CHECK(yse_dsp_scale_value(0.5f, 0.f, 1.f, 3.f, 4.f, 1.f, static_cast<YseDspScaleMode>(3)) ==
          0.f);
  }

  TEST_CASE("c api operators: the per-sample calls match the engine's") {
    const std::vector<float> noise = Signal(LENGTH);
    const std::vector<float> wide = Signal(LENGTH, 5.f);
    const std::vector<float> resets = Gate(LENGTH, 31, 1);

    YseDspProcessor* acc = yse_dsp_accumulator_create(1.0);
    DSP::accumulator eacc(1.0);
    YseDspProcessor* accr = yse_dsp_accumulator_create(0.0);
    DSP::accumulator eaccr;
    YseDspProcessor* dg = yse_dsp_degrade_create();
    yse_dsp_degrade_set_ratio(dg, 0.35f);
    yse_dsp_degrade_set_bits(dg, 5);
    DSP::degrade edg;
    edg.ratio(0.35f).bits(5);
    YseDspProcessor* dl = yse_dsp_delta_create();
    DSP::delta edl;
    YseDspProcessor* ds = yse_dsp_downsample_create();
    yse_dsp_downsample_set_period(ds, 3.3f);
    DSP::downsample eds;
    eds.period(3.3f);
    YseDspProcessor* fd = yse_dsp_fold_create();
    yse_dsp_fold_set_mode(fd, YSE_DSP_FOLD_MODE_WRAP);
    yse_dsp_fold_set_range(fd, -1.f, 0.5f);
    DSP::fold efd;
    efd.mode(DSP::fold::MODE::WRAP).range(-1.f, 0.5f);
    YseDspProcessor* od = yse_dsp_overdrive_create();
    yse_dsp_overdrive_set_drive(od, 0.5f);
    DSP::overdrive eod;
    eod.drive(0.5f);
    YseDspProcessor* sc = yse_dsp_scale_create();
    yse_dsp_scale_set_out_range(sc, -3.f, 7.f);
    yse_dsp_scale_set_exponent(sc, 2.5f);
    DSP::scale esc;
    esc.outRange(-3.f, 7.f).exponent(2.5f);

    for (unsigned i = 0; i < LENGTH; ++i) {
      INFO("sample " << i);
      REQUIRE(Same(yse_dsp_accumulator_process_sample(acc, noise[i]), eacc.process(noise[i])));
      REQUIRE(Same(yse_dsp_accumulator_process_sample_reset(accr, noise[i], resets[i]),
                   eaccr.process(noise[i], resets[i])));
      REQUIRE(Same(yse_dsp_degrade_process_sample(dg, noise[i]), edg.process(noise[i])));
      REQUIRE(Same(yse_dsp_delta_process_sample(dl, noise[i]), edl.process(noise[i])));
      REQUIRE(Same(yse_dsp_downsample_process_sample(ds, noise[i]), eds.process(noise[i])));
      REQUIRE(Same(yse_dsp_fold_process_sample(fd, wide[i]), efd.process(wide[i])));
      REQUIRE(Same(yse_dsp_overdrive_process_sample(od, wide[i]), eod.process(wide[i])));
      REQUIRE(Same(yse_dsp_scale_process_sample(sc, noise[i]), esc.process(noise[i])));
    }
    CHECK(yse_dsp_accumulator_get_sum(acc) == eacc.sum());
    CHECK(yse_dsp_accumulator_get_sum(accr) == eaccr.sum());
    CHECK(yse_dsp_degrade_get_held(dg) == edg.held());
    CHECK(yse_dsp_delta_get_previous(dl) == edl.previous());
    CHECK(yse_dsp_downsample_get_held(ds) == eds.held());
    for (YseDspProcessor* p : {acc, accr, dg, dl, ds, fd, od, sc})
      yse_dsp_processor_destroy(p);
  }

  TEST_CASE("c api operators: a block call carries state across calls and updates the "
            "getters") {
    const std::vector<float> noise = Signal(LENGTH);
    const std::vector<float> periods = Signal(LENGTH, 2.f, 3.f);
    const std::vector<float> low = Signal(LENGTH, 0.1f, -0.5f, 7u);
    const std::vector<float> high = Signal(LENGTH, 0.1f, 0.5f, 8u);
    std::vector<float> out(LENGTH);

    // Two calls in a row continue where the first one stopped: the bridge
    // keeps the engine object, not a copy per call.
    YseDspProcessor* acc = yse_dsp_accumulator_create(0.0);
    REQUIRE(yse_dsp_accumulator_process(acc, noise.data(), out.data(), LENGTH) == LENGTH);
    REQUIRE(yse_dsp_accumulator_process(acc, noise.data(), out.data(), 100) == 100);
    DSP::accumulator eacc;
    DSP::buffer a;
    Blocks(LENGTH, [&](unsigned o, unsigned n) { eacc(Stage(a, noise, o, n)); });
    Stage(a, noise, 0, 100);
    DSP::buffer& last = eacc(a);
    CHECK(Same(out[99], last.getPtr()[99]));
    CHECK(yse_dsp_accumulator_get_sum(acc) == eacc.sum());
    yse_dsp_processor_destroy(acc);

    // The signal-rate settings leave the last value read in the getters.
    YseDspProcessor* ds = yse_dsp_downsample_create();
    REQUIRE(yse_dsp_downsample_process_signal(ds, noise.data(), periods.data(), out.data(),
                                              LENGTH) == LENGTH);
    DSP::downsample eds;
    eds.period(periods[LENGTH - 1]);
    CHECK(yse_dsp_downsample_get_period(ds) == eds.period());
    yse_dsp_processor_destroy(ds);
    YseDspProcessor* fd = yse_dsp_fold_create();
    REQUIRE(yse_dsp_fold_process_signal(fd, noise.data(), low.data(), high.data(), out.data(),
                                        LENGTH) == LENGTH);
    CHECK(yse_dsp_fold_get_low(fd) == low[LENGTH - 1]);
    CHECK(yse_dsp_fold_get_high(fd) == high[LENGTH - 1]);
    yse_dsp_processor_destroy(fd);
  }

  TEST_CASE("c api operators: in-place processing matches out-of-place") {
    const std::vector<float> noise = Signal(LENGTH);
    const std::vector<float> other = Signal(LENGTH, 1.f, 0.f, 999u);

    // One input reused as the output.
    std::vector<float> ref(LENGTH), io = noise;
    YseDspProcessor* a = yse_dsp_delta_create();
    YseDspProcessor* b = yse_dsp_delta_create();
    REQUIRE(yse_dsp_delta_process(a, noise.data(), ref.data(), LENGTH) == LENGTH);
    REQUIRE(yse_dsp_delta_process(b, io.data(), io.data(), LENGTH) == LENGTH);
    CheckSame(io, ref);
    yse_dsp_processor_destroy(a);
    yse_dsp_processor_destroy(b);

    // The signal operand reused as the output.
    io = other;
    a = yse_dsp_modulo_create();
    REQUIRE(yse_dsp_modulo_process_signal(a, noise.data(), other.data(), ref.data(), LENGTH) ==
            LENGTH);
    REQUIRE(yse_dsp_modulo_process_signal(a, noise.data(), io.data(), io.data(), LENGTH) == LENGTH);
    CheckSame(io, ref);
    yse_dsp_processor_destroy(a);

    // Both polar inputs reused as the two outputs.
    std::vector<float> m(LENGTH), ph(LENGTH), re = noise, im = other;
    a = yse_dsp_cart_to_polar_create();
    REQUIRE(yse_dsp_cart_to_polar_process_signal(a, noise.data(), other.data(), m.data(), ph.data(),
                                                 LENGTH) == LENGTH);
    REQUIRE(yse_dsp_cart_to_polar_process_signal(a, re.data(), im.data(), re.data(), im.data(),
                                                 LENGTH) == LENGTH);
    CheckSame(re, m);
    CheckSame(im, ph);
    yse_dsp_processor_destroy(a);

    // bitsafe in place is the engine's apply().
    std::vector<float> dirty = noise;
    dirty[5] = NaN;
    dirty[300] = -Inf;
    std::vector<float> applied = dirty;
    Blocks(LENGTH, [&](unsigned o, unsigned n) {
      DSP::buffer buf;
      Stage(buf, applied, o, n);
      DSP::bitsafe::apply(buf);
      std::memcpy(applied.data() + o, buf.getPtr(), sizeof(float) * n);
    });
    a = yse_dsp_bitsafe_create();
    REQUIRE(yse_dsp_bitsafe_process(a, dirty.data(), dirty.data(), LENGTH) == LENGTH);
    CheckSame(dirty, applied);
    CHECK(dirty[5] == 0.f);
    CHECK(dirty[300] == 0.f);
    yse_dsp_processor_destroy(a);
  }

  TEST_CASE("c api operators: process, value, sample and setter calls allocate nothing") {
    const std::vector<float> in = Signal(LENGTH);
    const std::vector<float> two = Signal(LENGTH, 1.f, 2.f, 77u);
    std::vector<float> o1(LENGTH), o2(LENGTH);
    float x = 0.f, y = 0.f;

    std::vector<YseDspProcessor*> all;
    for (const Named& c : AllCtors())
      all.push_back(c.make());
    YseDspProcessor* acc = all[0];
    YseDspProcessor* rs = all[1];
    YseDspProcessor* rd = all[2];
    YseDspProcessor* mo = all[3];
    YseDspProcessor* bw = all[4];
    YseDspProcessor* bn = all[5];
    YseDspProcessor* bs = all[6];
    YseDspProcessor* sf = all[7];
    YseDspProcessor* cm = all[8];
    YseDspProcessor* mn = all[9];
    YseDspProcessor* mx = all[10];
    YseDspProcessor* dg = all[11];
    YseDspProcessor* dl = all[12];
    YseDspProcessor* ds = all[13];
    YseDspProcessor* fd = all[14];
    YseDspProcessor* od = all[15];
    YseDspProcessor* cp = all[16];
    YseDspProcessor* pc = all[17];
    YseDspProcessor* pw = all[18];
    YseDspProcessor* rn = all[19];
    YseDspProcessor* tr = all[20];
    YseDspProcessor* sc = all[21];
    YseDspProcessor* tg = all[22];
    YseDspProcessor* at = all[23];
    YseDspProcessor* ad = all[24];
    YseDspProcessor* da = all[25];
    YseDspProcessor* ms = all[26];
    YseDspProcessor* sm = all[27];

    {
      TestHelpers::ProbeScope probe;
      yse_dsp_accumulator_set(acc, 1.0);
      yse_dsp_accumulator_process(acc, in.data(), o1.data(), LENGTH);
      yse_dsp_accumulator_process_reset(acc, in.data(), two.data(), o1.data(), LENGTH);
      (void)yse_dsp_accumulator_process_sample_reset(acc, 1.f, 0.f);
      yse_dsp_accumulator_reset(acc);
      yse_dsp_reverse_subtract_process(rs, in.data(), 1.f, o1.data(), LENGTH);
      yse_dsp_reverse_subtract_process_signal(rs, in.data(), two.data(), o1.data(), LENGTH);
      yse_dsp_reverse_divide_process(rd, in.data(), 1.f, o1.data(), LENGTH);
      yse_dsp_reverse_divide_process_signal(rd, in.data(), two.data(), o1.data(), LENGTH);
      yse_dsp_modulo_process(mo, in.data(), 0.3f, o1.data(), LENGTH);
      yse_dsp_modulo_process_signal(mo, in.data(), two.data(), o1.data(), LENGTH);
      yse_dsp_bitwise_set_op(bw, YSE_DSP_BITWISE_XOR);
      yse_dsp_bitwise_set_mask(bw, 0xF0F0F0F0u);
      yse_dsp_bitwise_set_mode(bw, YSE_DSP_BIT_INTEGER);
      yse_dsp_bitwise_process(bw, in.data(), o1.data(), LENGTH);
      yse_dsp_bitwise_process_signal(bw, in.data(), two.data(), o1.data(), LENGTH);
      yse_dsp_bitnot_set_mode(bn, YSE_DSP_BIT_INTEGER);
      yse_dsp_bitnot_process(bn, in.data(), o1.data(), LENGTH);
      yse_dsp_bitshift_set_shift(bs, 2);
      yse_dsp_bitshift_set_mode(bs, YSE_DSP_BIT_INTEGER);
      yse_dsp_bitshift_process(bs, in.data(), o1.data(), LENGTH);
      yse_dsp_bitsafe_process(sf, in.data(), o1.data(), LENGTH);
      yse_dsp_compare_set_operator(cm, YSE_DSP_COMPARE_GREATER);
      yse_dsp_compare_set_fuzzy(cm, 0.1f);
      yse_dsp_compare_process(cm, in.data(), 0.f, o1.data(), LENGTH);
      yse_dsp_compare_process_signal(cm, in.data(), two.data(), o1.data(), LENGTH);
      yse_dsp_minimum_process(mn, in.data(), 0.f, o1.data(), LENGTH);
      yse_dsp_minimum_process_signal(mn, in.data(), two.data(), o1.data(), LENGTH);
      yse_dsp_maximum_process(mx, in.data(), 0.f, o1.data(), LENGTH);
      yse_dsp_maximum_process_signal(mx, in.data(), two.data(), o1.data(), LENGTH);
      yse_dsp_degrade_set_ratio(dg, 0.5f);
      yse_dsp_degrade_set_bits(dg, 8);
      yse_dsp_degrade_process(dg, in.data(), o1.data(), LENGTH);
      yse_dsp_degrade_process_signal(dg, in.data(), two.data(), two.data(), o1.data(), LENGTH);
      (void)yse_dsp_degrade_process_sample(dg, 0.5f);
      yse_dsp_degrade_reset(dg);
      yse_dsp_delta_set(dl, 0.2f);
      yse_dsp_delta_process(dl, in.data(), o1.data(), LENGTH);
      (void)yse_dsp_delta_process_sample(dl, 0.5f);
      yse_dsp_downsample_set_period(ds, 2.f);
      yse_dsp_downsample_process(ds, in.data(), o1.data(), LENGTH);
      yse_dsp_downsample_process_signal(ds, in.data(), two.data(), o1.data(), LENGTH);
      yse_dsp_downsample_reset(ds);
      yse_dsp_fold_set_mode(fd, YSE_DSP_FOLD_MODE_CLIP);
      yse_dsp_fold_set_range(fd, -0.5f, 0.5f);
      yse_dsp_fold_process(fd, in.data(), o1.data(), LENGTH);
      yse_dsp_fold_process_signal(fd, in.data(), in.data(), two.data(), o1.data(), LENGTH);
      (void)yse_dsp_fold_process_sample(fd, 3.f);
      yse_dsp_overdrive_set_drive(od, 2.f);
      yse_dsp_overdrive_process(od, in.data(), o1.data(), LENGTH);
      yse_dsp_overdrive_process_signal(od, in.data(), two.data(), o1.data(), LENGTH);
      yse_dsp_cart_to_polar_process(cp, in.data(), 0.5f, o1.data(), o2.data(), LENGTH);
      yse_dsp_cart_to_polar_process_signal(cp, in.data(), two.data(), o1.data(), o2.data(), LENGTH);
      yse_dsp_polar_to_cart_process(pc, in.data(), 0.5f, o1.data(), o2.data(), LENGTH);
      yse_dsp_polar_to_cart_process_signal(pc, in.data(), two.data(), o1.data(), nullptr, LENGTH);
      yse_dsp_phase_wrap_process(pw, in.data(), o1.data(), LENGTH);
      yse_dsp_round_set_step(rn, 0.25f);
      yse_dsp_round_set_nearest(rn, 0);
      yse_dsp_round_process(rn, in.data(), o1.data(), LENGTH);
      yse_dsp_round_process_signal(rn, in.data(), two.data(), o1.data(), LENGTH);
      yse_dsp_trunc_process(tr, in.data(), o1.data(), LENGTH);
      yse_dsp_scale_set_mode(sc, YSE_DSP_SCALE_CLASSIC);
      yse_dsp_scale_set_in_range(sc, -1.f, 1.f);
      yse_dsp_scale_set_out_range(sc, 0.f, 10.f);
      yse_dsp_scale_set_exponent(sc, 1.06f);
      yse_dsp_scale_process(sc, in.data(), o1.data(), LENGTH);
      yse_dsp_scale_process_signal(sc, in.data(), in.data(), two.data(), in.data(), two.data(),
                                   two.data(), o1.data(), LENGTH);
      (void)yse_dsp_scale_process_sample(sc, 0.5f);
      yse_dsp_trig_set_function(tg, YSE_DSP_TRIG_TANH);
      yse_dsp_trig_process(tg, in.data(), o1.data(), LENGTH);
      yse_dsp_arc_tangent2_process(at, in.data(), 1.f, o1.data(), LENGTH);
      yse_dsp_arc_tangent2_process_signal(at, in.data(), two.data(), o1.data(), LENGTH);
      yse_dsp_amp_to_db_process(ad, in.data(), o1.data(), LENGTH);
      yse_dsp_db_to_amp_process(da, in.data(), o1.data(), LENGTH);
      yse_dsp_ms_to_samples_process(ms, in.data(), o1.data(), LENGTH);
      yse_dsp_samples_to_ms_process(sm, in.data(), o1.data(), LENGTH);
      // The value calls.
      (void)yse_dsp_bitwise_value(YSE_DSP_BITWISE_AND, YSE_DSP_BIT_RAW, 1.f, 0xFFu);
      (void)yse_dsp_compare_value(YSE_DSP_COMPARE_LESS, 1.f, 2.f, 0.f);
      (void)yse_dsp_trig_value(YSE_DSP_TRIG_ASINH, 1.f);
      (void)yse_dsp_fold_value(3.f, 0.f, 1.f, YSE_DSP_FOLD_MODE_FOLD);
      (void)yse_dsp_scale_value(3.f, 0.f, 1.f, 0.f, 2.f, 2.f, YSE_DSP_SCALE_MODERN);
      yse_dsp_cart_to_polar_value(1.f, 1.f, &x, &y);
      yse_dsp_polar_to_cart_value(1.f, 1.f, &x, &y);
      (void)yse_dsp_ms_to_samples_value(10.f, 48000.f);
      // A refused call (wrong class) is allocation-free too.
      yse_dsp_accumulator_process(sc, in.data(), o1.data(), LENGTH);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }

    for (YseDspProcessor* p : all)
      yse_dsp_processor_destroy(p);
  }

} // TEST_SUITE("dsp")
