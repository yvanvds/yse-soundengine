/*
  ==============================================================================

    timeStretch.cpp
    Offline time stretching and pitch shifting of a whole sample buffer: a
    phase vocoder with identity phase locking (issue #1152).

  ==============================================================================
*/

#include "timeStretch.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include "sampleBuffer.hpp"
#include "../buffer.hpp"
#include "../interpolate4.hpp"
#include "../delay/retune.hpp"
#include "../spectral/stft.hpp"
#include "../../headers/constants.hpp"

using namespace YSE::DSP;

namespace {

  constexpr double PI = 3.14159265358979323846;
  constexpr double TWO_PI = 2.0 * PI;

  // The rate the frame sizes are given at.
  constexpr double REFERENCE_RATE = 48000.0;
  constexpr UInt SMALLEST_FRAME = 256;
  constexpr UInt LARGEST_FRAME = 16384;

  // How often the vocoder reports its progress, in frames.
  constexpr UInt REPORT_EVERY = 16;

  // A magnitude this far below the frame's loudest is not a peak (-100 dB).
  constexpr Flt PEAK_FLOOR = 1.0e-5f;

  // A phase in -pi .. pi.
  double Wrap(double phase) {
    return std::remainder(phase, TWO_PI);
  }

  // The power of two nearest to `value` (on a log scale), within the frame
  // limits.
  UInt NearestPowerOfTwo(double value) {
    UInt p = SMALLEST_FRAME;
    while (p < LARGEST_FRAME && static_cast<double>(p) * std::sqrt(2.0) < value)
      p <<= 1;
    return p;
  }

  // What a quality and a mode analyse with.
  struct analysisShape {
    double frame = 2048.0; // at REFERENCE_RATE
    UInt overlap = 8;
    Flt onset = 0.f; // the spectral rise that is an onset; 0: not looked for
  };

  analysisShape Shape(timeStretch::QUALITY quality, timeStretch::MODE mode) {
    analysisShape s;
    switch (quality) {
    case timeStretch::BASIC:
      s.frame = 1024.0;
      s.overlap = 4;
      break;
    case timeStretch::GOOD:
      s.frame = 2048.0;
      s.overlap = 4;
      break;
    case timeStretch::BEST:
      s.frame = 4096.0;
      s.overlap = 8;
      break;
    case timeStretch::BETTER:
    default:
      s.frame = 2048.0;
      s.overlap = 8;
      break;
    }
    switch (mode) {
    case timeStretch::MONOPHONIC:
      s.frame *= 0.5;
      break;
    case timeStretch::RHYTHMIC:
      s.frame *= 0.5;
      s.onset = 0.15f;
      break;
    case timeStretch::GENERAL:
      s.onset = 0.3f;
      break;
    case timeStretch::POLYPHONIC:
      s.frame *= 2.0;
      s.onset = 0.3f;
      break;
    case timeStretch::PADS:
      s.frame *= 2.0;
      break;
    case timeStretch::EXTREME:
      s.frame *= 4.0;
      break;
    case timeStretch::EFFICIENT:
      s.frame *= 0.5;
      s.overlap = 4;
      break;
    case timeStretch::MODE_BASIC:
    default:
      break;
    }
    return s;
  }

  double RateOf(Flt rate) {
    return std::isfinite(rate) && rate > 0.f ? static_cast<double>(rate) : REFERENCE_RATE;
  }

  // `count` samples of `input` (`frames` long) from `start`, 0 outside it.
  void Load(const Flt* input, UInt frames, long long start, Flt* out, UInt count) {
    for (UInt i = 0; i < count; ++i) {
      const long long at = start + static_cast<long long>(i);
      out[i] = at >= 0 && at < static_cast<long long>(frames) ? input[at] : 0.f;
    }
  }

  // The sample of `z` at a fractional `position`, 4-point cubic, the ends
  // held.
  Flt ReadCubic(const std::vector<Flt>& z, double position) {
    const auto last = static_cast<long long>(z.size()) - 1;
    const auto base = static_cast<long long>(std::floor(position));
    const auto frac = static_cast<Flt>(position - static_cast<double>(base));
    auto at = [&](long long i) { return z[static_cast<std::size_t>(std::clamp(i, 0LL, last))]; };
    return interpolate4::kernel(at(base - 1), at(base), at(base + 1), at(base + 2), frac);
  }

  // Where the result's samples come from: a piecewise straight line from a
  // time in the result to a time in the source. Pieces around an onset run at
  // a slope of 1, so the transient is copied rather than stretched; the rest
  // shares out the remaining length.
  struct timeMap {
    struct piece {
      double source = 0.0; // where it starts in the source
      double result = 0.0; // where it starts in the result
      double slope = 1.0; // result samples per source sample
      bool locked = false; // a transient, copied
    };
    std::vector<piece> pieces;

    // The source time of result time `t`; `locked` set for a transient.
    double operator()(double t, bool& locked) const {
      std::size_t lo = 0;
      std::size_t hi = pieces.size();
      while (hi - lo > 1) {
        const std::size_t mid = (lo + hi) / 2;
        if (pieces[mid].result <= t) {
          lo = mid;
        } else {
          hi = mid;
        }
      }
      const piece& p = pieces[lo];
      locked = p.locked;
      return p.source + (t - p.result) / p.slope;
    }
  };

  // The onsets of `input`, in samples: where a frame of `n` whose spectrum
  // rises by more than `threshold` of its whole magnitude against the frame a
  // quarter before it (at its local peak) has the largest jump in level
  // between two runs of `RUN` samples.
  constexpr UInt RUN = 32;

  std::vector<double> Onsets(const Flt* input, UInt frames, UInt n, Flt threshold) {
    std::vector<double> onsets;
    const UInt step = n / 4;
    const UInt half = n / 2;
    stftAnalysis analysis(n);
    std::vector<Flt> frame(n);
    std::vector<Flt> previous(half + 1, 0.f);
    std::vector<Flt> magnitude(half + 1, 0.f);
    std::vector<double> rise;
    std::vector<double> level;
    for (long long centre = 0; centre < static_cast<long long>(frames) + half; centre += step) {
      Load(input, frames, centre - half, frame.data(), n);
      analysis.write(frame.data(), n);
      analysis.analyse();
      double up = 0.0;
      double sum = 0.0;
      for (UInt k = 0; k <= half; ++k) {
        magnitude[k] = std::hypot(analysis.real()[k], analysis.imaginary()[k]);
        up += std::max(0.f, magnitude[k] - previous[k]);
        sum += magnitude[k];
      }
      rise.push_back(sum > 0.0 ? up / sum : 0.0);
      level.push_back(sum);
      previous.swap(magnitude);
    }
    const double loudest = level.empty() ? 0.0 : *std::max_element(level.begin(), level.end());
    for (std::size_t j = 1; j + 1 < rise.size(); ++j) {
      if (rise[j] <= static_cast<double>(threshold) || rise[j] < rise[j - 1] ||
          rise[j] < rise[j + 1] || level[j] < 1.0e-3 * loudest)
        continue;
      // The largest jump in the frame's window.
      const auto centre = static_cast<long long>(j) * static_cast<long long>(step);
      const long long from = std::max(0LL, centre - static_cast<long long>(half));
      const long long to = std::min(static_cast<long long>(frames) - static_cast<long long>(RUN),
                                    centre + static_cast<long long>(half));
      double before = -1.0;
      double best = -1.0;
      long long at = -1;
      for (long long x = from; x <= to; x += RUN) {
        double e = 0.0;
        for (UInt i = 0; i < RUN; ++i)
          e += static_cast<double>(input[x + i]) * input[x + i];
        if (before >= 0.0 && e - before > best) {
          best = e - before;
          at = x;
        }
        before = e;
      }
      if (at >= 0 && (onsets.empty() || static_cast<double>(at) > onsets.back()))
        onsets.push_back(static_cast<double>(at));
    }
    return onsets;
  }

  // The map for a source of `frames` made `ratio` times as long. Each onset
  // `t` keeps its place in the rhythm, at `ratio * t`, and the frames whose
  // window holds it (centres within `n / 2` of it) are copied at a slope of
  // 1. An onset whose copy would leave the stretch between it and its
  // neighbours beyond a quarter of the ratio either way is not copied.
  timeMap MakeMap(UInt frames, double ratio, UInt n, const std::vector<double>& onsets) {
    timeMap map;
    const double total = static_cast<double>(frames);
    const double reach = static_cast<double>(n) / 2.0; // n is a power of two
    const double low = 0.25 * std::min(ratio, 1.0);
    const double high = 4.0 * std::max(ratio, 1.0);
    auto fits = [&](double fromSource, double fromResult, double toSource, double toResult) {
      if (toSource <= fromSource) return toResult >= fromResult && toSource == fromSource;
      const double slope = (toResult - fromResult) / (toSource - fromSource);
      return slope >= low && slope <= high;
    };
    double source = 0.0;
    double result = 0.0;
    if (ratio != 1.0) {
      for (const double t : onsets) {
        const double start = t - reach;
        const double end = t + reach;
        if (start < source || end > total) continue;
        const double startResult = ratio * t - reach;
        if (!fits(source, result, start, startResult) ||
            !fits(end, ratio * t + reach, total, ratio * total))
          continue;
        if (start > source)
          map.pieces.push_back({source, result, (startResult - result) / (start - source), false});
        map.pieces.push_back({start, startResult, 1.0, true});
        source = end;
        result = ratio * t + reach;
      }
    }
    const double slope = total > source ? (ratio * total - result) / (total - source) : ratio;
    map.pieces.push_back({source, result, slope > 0.0 ? slope : ratio, false});
    return map;
  }

} // namespace

// ─── settings ────────────────────────────────────────────────────────────────

timeStretch& timeStretch::stretch(Flt factor) {
  if (std::isfinite(factor) && factor > 0.f)
    lengthFactor = std::clamp(factor, MIN_STRETCH, MAX_STRETCH);
  return *this;
}

timeStretch& timeStretch::pitch(Flt factor) {
  if (std::isfinite(factor) && factor > 0.f) pitchFactor = std::clamp(factor, MIN_PITCH, MAX_PITCH);
  return *this;
}

timeStretch& timeStretch::cents(Flt value) {
  if (std::isfinite(value)) pitch(static_cast<Flt>(std::exp2(static_cast<double>(value) / 1200.0)));
  return *this;
}

timeStretch& timeStretch::quality(QUALITY value) {
  if (value >= BASIC && value <= BEST) frameQuality = value;
  return *this;
}

timeStretch& timeStretch::mode(MODE value) {
  if (value >= MODE_BASIC && value <= EFFICIENT) material = value;
  return *this;
}

timeStretch& timeStretch::pitchCorrection(bool on) {
  correcting = on;
  return *this;
}

timeStretch& timeStretch::standardPitch(Flt hz) {
  if (std::isfinite(hz)) standardA = std::clamp(hz, MIN_STANDARD_PITCH, MAX_STANDARD_PITCH);
  return *this;
}

timeStretch& timeStretch::slurTime(Flt ms) {
  if (std::isfinite(ms)) slur = std::max(ms, 0.f);
  return *this;
}

UInt timeStretch::outputFrames(UInt frames) const {
  if (frames == 0) return 0;
  const double length = std::round(static_cast<double>(frames) * lengthFactor);
  return static_cast<UInt>(std::max(1.0, length));
}

UInt timeStretch::frameSize(Flt rate) const {
  const analysisShape s = Shape(frameQuality, material);
  return NearestPowerOfTwo(s.frame * RateOf(rate) / REFERENCE_RATE);
}

UInt timeStretch::overlap() const {
  return Shape(frameQuality, material).overlap;
}

bool timeStretch::progressSpan::report(Flt done) const {
  if (function == nullptr) return true;
  return function(from + width * std::clamp(done, 0.f, 1.f), user);
}

// ─── processing ──────────────────────────────────────────────────────────────

bool timeStretch::Vocode(const Flt* input, UInt frames, Flt rate, std::vector<Flt>& output,
                         const progressSpan& span) const {
  const double r = static_cast<double>(lengthFactor) * static_cast<double>(pitchFactor);
  const analysisShape shape = Shape(frameQuality, material);
  const UInt n = frameSize(rate);
  const UInt half = n / 2;
  const timeMap map =
      MakeMap(frames, r, n,
              shape.onset > 0.f ? Onsets(input, frames, n, shape.onset) : std::vector<double>());
  double slowest = r;
  for (const timeMap::piece& p : map.pieces)
    slowest = std::min(slowest, p.slope);
  // The source is read every hop / slope samples; keep that within half a
  // frame.
  UInt ovs = shape.overlap;
  while (ovs < n / 4 &&
         static_cast<double>(n) / static_cast<double>(ovs) / slowest > static_cast<double>(half))
    ovs <<= 1;
  const UInt hop = n / ovs;
  const UInt probe = n / 8; // the second analysis, this much earlier
  const auto length =
      static_cast<std::size_t>(std::max(1.0, std::round(static_cast<double>(frames) * r)));
  // Above Nyquist once resampled up: cleared.
  const UInt top =
      pitchFactor > 1.f ? static_cast<UInt>(static_cast<double>(half) / pitchFactor) : half;

  stftAnalysis now(n);
  stftAnalysis earlier(n);
  stftSynthesis out(n, ovs);
  std::vector<Flt> frame(n);
  std::vector<Flt> magnitude(half + 1);
  std::vector<Flt> earlierMagnitude(half + 1);
  std::vector<double> sourcePhase(half + 1);
  std::vector<double> earlierPhase(half + 1);
  std::vector<double> phase(half + 1, 0.0);
  std::vector<UInt> peaks;
  peaks.reserve(half + 1);

  // Frame m covers samples m * hop - n up to m * hop of the result: what the
  // synthesis puts out from sample n on is the result's first.
  const std::size_t total = n + length;
  const std::size_t frameCount = (total + hop - 1) / hop;
  std::vector<Flt> stream(frameCount * hop);

  bool fresh = true;
  const double toBin = TWO_PI / static_cast<double>(n);

  for (std::size_t m = 0; m < frameCount; ++m) {
    bool locked = false;
    const double centre = map(static_cast<double>(m * hop) - static_cast<double>(half), locked);
    const auto start = static_cast<long long>(std::llround(centre - static_cast<double>(half)));

    Load(input, frames, start, frame.data(), n);
    now.write(frame.data(), n);
    now.analyse();
    Load(input, frames, start - static_cast<long long>(probe), frame.data(), n);
    earlier.write(frame.data(), n);
    earlier.analyse();

    const Flt* re = now.real();
    const Flt* im = now.imaginary();
    const Flt* reEarlier = earlier.real();
    const Flt* imEarlier = earlier.imaginary();
    Flt loudest = 0.f;
    for (UInt k = 0; k <= half; ++k) {
      magnitude[k] = std::hypot(re[k], im[k]);
      earlierMagnitude[k] = std::hypot(reEarlier[k], imEarlier[k]);
      loudest = std::max(loudest, magnitude[k]);
      sourcePhase[k] = std::atan2(static_cast<double>(im[k]), static_cast<double>(re[k]));
      earlierPhase[k] =
          std::atan2(static_cast<double>(imEarlier[k]), static_cast<double>(reEarlier[k]));
    }

    if (!(loudest > 0.f)) {
      // Silence (or before the start): the next sound starts afresh.
      fresh = true;
    } else {
      peaks.clear();
      const Flt floor = loudest * PEAK_FLOOR;
      for (UInt k = 0; k <= top; ++k) {
        const Flt v = magnitude[k];
        if (v <= floor) continue;
        if (k >= 1 && v <= magnitude[k - 1]) continue;
        if (k >= 2 && v <= magnitude[k - 2]) continue;
        if (k + 1 <= half && v < magnitude[k + 1]) continue;
        if (k + 2 <= half && v < magnitude[k + 2]) continue;
        peaks.push_back(k);
      }

      // Through a transient copied at a slope of 1, a bin that is rising
      // (twice its level a probe earlier) takes the source's phase, so the
      // transient keeps its shape; the rest go on as before, so a tone
      // under it does not jump.
      const auto rising = [&](UInt k) {
        return locked && magnitude[k] > 2.f * earlierMagnitude[k] + floor;
      };
      if (fresh || peaks.empty()) {
        // The source's phases as they are: a coherent start.
        for (UInt k = 0; k <= half; ++k)
          phase[k] = sourcePhase[k];
      } else {
        // Each peak advances by its own frequency, measured over `probe`.
        for (const UInt p : peaks) {
          const double expected = toBin * static_cast<double>(p);
          const double deviation =
              Wrap(sourcePhase[p] - earlierPhase[p] - expected * static_cast<double>(probe));
          const double omega = expected + deviation / static_cast<double>(probe);
          phase[p] = rising(p) ? sourcePhase[p] : Wrap(phase[p] + omega * static_cast<double>(hop));
        }
        // The bins around a peak keep their phase relative to it (identity
        // phase locking); each belongs to the nearest peak.
        for (std::size_t i = 0; i < peaks.size(); ++i) {
          const UInt p = peaks[i];
          const UInt low = i == 0 ? 0 : (peaks[i - 1] + p) / 2 + 1;
          const UInt high = i + 1 == peaks.size() ? half : (p + peaks[i + 1]) / 2;
          for (UInt k = low; k <= high; ++k) {
            if (k != p) phase[k] = Wrap(phase[p] + sourcePhase[k] - sourcePhase[p]);
          }
        }
        for (UInt k = 0; k <= half; ++k) {
          if (rising(k)) phase[k] = sourcePhase[k];
        }
      }
      fresh = false;
    }

    Flt* outRe = out.real();
    Flt* outIm = out.imaginary();
    for (UInt k = 0; k <= half; ++k) {
      if (k <= top) {
        outRe[k] = magnitude[k] * static_cast<Flt>(std::cos(phase[k]));
        outIm[k] = magnitude[k] * static_cast<Flt>(std::sin(phase[k]));
      } else {
        outRe[k] = 0.f;
        outIm[k] = 0.f;
      }
    }
    out.synthesise();
    out.read(stream.data() + m * hop, hop);

    if (m % REPORT_EVERY == 0 &&
        !span.report(static_cast<Flt>(static_cast<double>(m) / static_cast<double>(frameCount))))
      return false;
  }

  output.assign(stream.begin() + n, stream.begin() + static_cast<std::ptrdiff_t>(n + length));
  return true;
}

bool timeStretch::Correct(std::vector<Flt>& samples, Flt rate, const progressSpan& span) const {
  retune corrector(pitchShifter::BETTER);
  tuning notes;
  notes.chromatic();
  // The tracker hears the samples at the engine rate: an A scaled the same
  // way lands on the same notes.
  notes.standardPitch(static_cast<Flt>(static_cast<double>(standardA) *
                                       static_cast<double>(SAMPLERATE) / RateOf(rate)));
  corrector.notes(notes);
  // The corrector counts its slur at the engine rate; the slur is in the
  // buffer's time.
  const double engine = SAMPLERATE > 0 ? static_cast<double>(SAMPLERATE) : RateOf(rate);
  corrector.slur(static_cast<Flt>(static_cast<double>(slur) * RateOf(rate) / engine));
  const std::size_t latency = corrector.latency();

  const std::size_t size = samples.size();
  const std::size_t total = size + latency;
  std::vector<Flt> result(size, 0.f);
  buffer block(STANDARD_BUFFERSIZE);
  std::size_t blocks = 0;
  for (std::size_t at = 0; at < total; at += STANDARD_BUFFERSIZE, ++blocks) {
    const auto count = static_cast<UInt>(std::min<std::size_t>(STANDARD_BUFFERSIZE, total - at));
    if (block.getLength() != count) block.resize(count);
    Flt* in = block.getPtr();
    for (UInt i = 0; i < count; ++i)
      in[i] = at + i < size ? samples[at + i] : 0.f;
    const Flt* corrected = corrector(block).getPtr();
    for (UInt i = 0; i < count; ++i) {
      const std::size_t to = at + i;
      if (to >= latency && to - latency < size) result[to - latency] = corrected[i];
    }
    if (blocks % (REPORT_EVERY * 8) == 0 &&
        !span.report(static_cast<Flt>(static_cast<double>(at) / static_cast<double>(total))))
      return false;
  }
  samples.swap(result);
  return true;
}

bool timeStretch::render(const Flt* input, UInt frames, Flt rate, std::vector<Flt>& output,
                         progressFunction progress, void* user) const {
  if (input == nullptr || frames == 0) return false;
  // The vocoder's share of the work, against the correction's.
  const Flt vocoderShare = correcting ? 0.7f : 1.f;
  progressSpan vocoding{progress, user, 0.f, vocoderShare};
  progressSpan correction{progress, user, vocoderShare, 1.f - vocoderShare};

  const UInt length = outputFrames(frames);
  std::vector<Flt> result;
  if (lengthFactor == 1.f && pitchFactor == 1.f) {
    result.assign(input, input + frames);
  } else {
    std::vector<Flt> vocoded;
    if (!Vocode(input, frames, rate, vocoded, vocoding)) return false;
    if (pitchFactor == 1.f) {
      result.swap(vocoded);
      result.resize(length, 0.f);
    } else {
      result.resize(length);
      const auto step = static_cast<double>(pitchFactor);
      for (UInt i = 0; i < length; ++i)
        result[i] = ReadCubic(vocoded, static_cast<double>(i) * step);
    }
  }
  if (correcting && !Correct(result, rate, correction)) return false;
  if (progress != nullptr && !progress(1.f, user)) return false;
  output.swap(result);
  return true;
}

bool timeStretch::process(const sampleBuffer& source, sampleBuffer& target,
                          progressFunction progress, void* user) const {
  const UInt channels = source.channels();
  const UInt frames = source.frames();
  if (channels == 0 || frames == 0) return false;
  const Flt rate = source.sampleRate();

  const UInt length = outputFrames(frames);
  std::vector<Flt> planar(static_cast<std::size_t>(channels) * length, 0.f);
  std::vector<Flt> input(frames);
  std::vector<Flt> output;

  // Each channel takes its share; the last report (1) waits for the
  // publish.
  constexpr Flt BEFORE_PUBLISH = 0.999f;
  for (UInt c = 0; c < channels; ++c) {
    // The source may change meanwhile: what is missing reads as silence.
    const UInt got = source.getSamples(c, 0, input.data(), frames);
    std::fill(input.begin() + got, input.end(), 0.f);
    const Flt width = BEFORE_PUBLISH / static_cast<Flt>(channels);
    struct channelSpan {
      progressFunction function;
      void* user;
      Flt from;
      Flt width;
      static bool Forward(Flt done, void* self) {
        auto* s = static_cast<channelSpan*>(self);
        return s->function == nullptr || s->function(s->from + s->width * done, s->user);
      }
    } span{progress, user, width * static_cast<Flt>(c), width};
    if (!render(input.data(), frames, rate, output, &channelSpan::Forward, &span)) return false;
    std::copy_n(output.begin(), std::min<std::size_t>(output.size(), length),
                planar.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(c) * length));
  }
  // A last chance to stop before anything changes.
  if (progress != nullptr && !progress(BEFORE_PUBLISH, user)) return false;
  target.setContents(channels, length, planar.data());
  if (progress != nullptr) progress(1.f, user);
  return true;
}
