#include "pinkNoise.hpp"

namespace {
  // Same generator and mapping as DSP::noise (dsp/oscillators.cpp), in
  // unsigned arithmetic so the wrap-around is defined: the low 31 bits,
  // centred, give a white sample in [-1, 1).
  inline Dbl White(std::uint32_t& state) {
    const auto bits = static_cast<std::int32_t>(state & 0x7fffffffu);
    const Dbl sample = static_cast<Dbl>(bits - 0x40000000) * (1.0 / 0x40000000);
    state = state * 435898247u + 382842987u;
    return sample;
  }

  inline UInt Clamp(UInt length) {
    return length > YSE::STANDARD_BUFFERSIZE ? YSE::STANDARD_BUFFERSIZE : length;
  }

  // Kellet's output gain: an RMS of about 0.2 for a uniform white input,
  // with peaks rarely above 0.9.
  constexpr Dbl OUTPUT_GAIN = 0.11;
} // namespace

namespace YSE {
  namespace DSP {

    pinkNoise::pinkNoise()
      : state(DEFAULT_SEED),
        b0(0),
        b1(0),
        b2(0),
        b3(0),
        b4(0),
        b5(0),
        b6(0),
        output(STANDARD_BUFFERSIZE) {}

    pinkNoise& pinkNoise::seed(std::uint32_t value) {
      state = value;
      b0 = b1 = b2 = b3 = b4 = b5 = b6 = 0.0;
      return *this;
    }

    buffer& pinkNoise::operator()(UInt length) {
      length = Clamp(length);
      // Within the capacity set in the constructor: no allocation.
      if (output.getLength() != length) output.resize(length);

      Flt* out = output.getPtr();
      for (UInt i = 0; i < length; ++i) {
        const Dbl white = White(state);
        // Paul Kellet's refined pink filter: six one-pole low-passes spread
        // over the spectrum, plus a direct and a one-sample-delayed path.
        b0 = 0.99886 * b0 + white * 0.0555179;
        b1 = 0.99332 * b1 + white * 0.0750759;
        b2 = 0.96900 * b2 + white * 0.1538520;
        b3 = 0.86650 * b3 + white * 0.3104856;
        b4 = 0.55000 * b4 + white * 0.5329522;
        b5 = -0.7616 * b5 - white * 0.0168980;
        Dbl pink = (b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362) * OUTPUT_GAIN;
        b6 = white * 0.115926;
        if (pink > 1.0) pink = 1.0;
        if (pink < -1.0) pink = -1.0;
        out[i] = static_cast<Flt>(pink);
      }
      return output;
    }

  } // namespace DSP
} // namespace YSE
