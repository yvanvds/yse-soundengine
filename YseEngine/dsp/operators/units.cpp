#include "units.hpp"

#include <cmath>
#include <limits>

namespace {
  using YSE::DSP::buffer;

  constexpr Flt kLn10Over20 = 0.115129254649702f; // ln(10) / 20
  constexpr Flt kDbCeiling = 770.f; // 10 ** (770 / 20) is still a finite float

  inline UInt Clamp(UInt length) {
    return length > YSE::STANDARD_BUFFERSIZE ? YSE::STANDARD_BUFFERSIZE : length;
  }

  inline Flt Finite(Flt x) {
    return std::isfinite(x) ? x : 0.f;
  }

  inline Flt AmpDb(Flt amp) {
    Flt a = std::fabs(amp);
    if (!(a > 0.f)) return YSE::DSP::DB_FLOOR; // 0 and NaN
    if (a > std::numeric_limits<Flt>::max()) a = std::numeric_limits<Flt>::max();
    const Flt db = 20.f * std::log10(a);
    return db < YSE::DSP::DB_FLOOR ? YSE::DSP::DB_FLOOR : db;
  }

  inline Flt DbAmp(Flt db) {
    if (!(db > YSE::DSP::DB_FLOOR)) return 0.f; // the floor, below it, and NaN
    if (db > kDbCeiling) db = kDbCeiling;
    return std::exp(kLn10Over20 * db);
  }

  inline Flt LiveRate() {
    return static_cast<Flt>(YSE::SAMPLERATE);
  }

  // Sizes `output` to the block, within the capacity set in the constructor:
  // no allocation.
  inline UInt Prepare(buffer& output, const buffer& in) {
    const UInt length = Clamp(in.getLength());
    if (output.getLength() != length) output.resize(length);
    return length;
  }
} // namespace

namespace YSE {
  namespace DSP {

    Flt AmpToDb(Flt amp) {
      return AmpDb(amp);
    }

    Flt DbToAmp(Flt db) {
      return DbAmp(db);
    }

    Flt MsToSamples(Flt ms) {
      return msToSamples::process(ms, LiveRate());
    }

    Flt SamplesToMs(Flt samples) {
      return samplesToMs::process(samples, LiveRate());
    }

    // ─── ampToDb ─────────────────────────────────────────────────────────────

    ampToDb::ampToDb() : output(STANDARD_BUFFERSIZE) {}

    Flt ampToDb::process(Flt in) {
      return AmpDb(in);
    }

    buffer& ampToDb::operator()(buffer& in) {
      const UInt length = Prepare(output, in);
      const Flt* a = in.getPtr();
      Flt* out = output.getPtr();
      for (UInt i = 0; i < length; ++i)
        out[i] = AmpDb(a[i]);
      return output;
    }

    // ─── dbToAmp ─────────────────────────────────────────────────────────────

    dbToAmp::dbToAmp() : output(STANDARD_BUFFERSIZE) {}

    Flt dbToAmp::process(Flt in) {
      return DbAmp(in);
    }

    buffer& dbToAmp::operator()(buffer& in) {
      const UInt length = Prepare(output, in);
      const Flt* a = in.getPtr();
      Flt* out = output.getPtr();
      for (UInt i = 0; i < length; ++i)
        out[i] = DbAmp(a[i]);
      return output;
    }

    // ─── msToSamples ─────────────────────────────────────────────────────────

    msToSamples::msToSamples() : output(STANDARD_BUFFERSIZE) {}

    Flt msToSamples::process(Flt ms, Flt sampleRate) {
      return Finite(ms * (sampleRate / 1000.f));
    }

    buffer& msToSamples::operator()(buffer& in) {
      const UInt length = Prepare(output, in);
      // The rate is read every block, never cached: it follows the session.
      const Flt factor = LiveRate() / 1000.f;
      const Flt* a = in.getPtr();
      Flt* out = output.getPtr();
      for (UInt i = 0; i < length; ++i)
        out[i] = Finite(a[i] * factor);
      return output;
    }

    // ─── samplesToMs ─────────────────────────────────────────────────────────

    samplesToMs::samplesToMs() : output(STANDARD_BUFFERSIZE) {}

    Flt samplesToMs::process(Flt samples, Flt sampleRate) {
      if (!(sampleRate > 0.f)) return 0.f;
      return Finite(samples * (1000.f / sampleRate));
    }

    buffer& samplesToMs::operator()(buffer& in) {
      const UInt length = Prepare(output, in);
      const Flt rate = LiveRate();
      const Flt factor = rate > 0.f ? 1000.f / rate : 0.f;
      const Flt* a = in.getPtr();
      Flt* out = output.getPtr();
      for (UInt i = 0; i < length; ++i)
        out[i] = Finite(a[i] * factor);
      return output;
    }

  } // namespace DSP
} // namespace YSE
