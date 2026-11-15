#include "dSpectroscope.h"
#include "meterCommon.h"
#include "../spectral/spectralPass.h"

using namespace YSE::PATCHER;
using namespace YSE::PATCHER::meterCommon;
using YSE::DSP::spectrumAnalyzer;

namespace {
  constexpr unsigned int HEADER = 5; // mode, size, bins, serial, rate

  // Appends one cell of a frame. Host thread.
  void Append(std::string& out, float value) {
    char text[YSE::PATCHER::kExprValueTextMax];
    const int written = YSE::PATCHER::ExprFormatValue(YSE::PATCHER::ExprValue::Float(value), text,
                                                      YSE::PATCHER::kExprValueTextMax);
    if (written > 0)
      out.append(text, static_cast<std::size_t>(written));
    else
      out.append("0.");
  }

  // Cell `index` of `f`; "" past the end. Host thread.
  std::string FrameCell(const spectrumAnalyzer::frame& f, unsigned int index) {
    switch (index) {
    case 0:
      return f.mode == spectrumAnalyzer::PHASE ? "1" : "0";
    case 1:
      return std::to_string(f.size);
    case 2:
      return std::to_string(f.bins);
    case 3:
      return std::to_string(f.serial);
    case 4:
      return std::to_string(f.rate);
    default:
      break;
    }
    const unsigned int bin = index - HEADER;
    if (bin >= f.bins) return std::string();
    return Cell(f.values[bin]);
  }
} // namespace

#define className dSpectroscope

CONSTRUCT_DSP() {
  interval = spectrumAnalyzer::DEFAULT_INTERVAL;
  phasespect = 0.f;
  size = static_cast<float>(spectrumAnalyzer::DEFAULT_SIZE);

  // in 0: the signal, or the real part inside a ~pfft; the word messages
  ADD_IN_0;
  REG_SIGNAL_IN;

  // in 1: the imaginary part inside a ~pfft
  ADD_IN_1;
  REG_SIGNAL_IN;

  ADD_PARAM(interval);
  ADD_PARAM(phasespect);
  ADD_PARAM(size);

  ADD_DESCRIPTION("A spectrum analyser: the amplitude or phase spectrum of a signal, or of a "
                  "~pfft's frames, every update interval, for the host to draw as a spectrogram "
                  "or a sonogram.");
  ADD_CATEGORY(pCategory::ANALYSIS);
  INLET_DOC(0, "signal",
            "The signal to analyse; unconnected it reads as silence. Inside a ~pfft, the real "
            "part of the bins, from ~fftin's left outlet. Also takes 'interval <ms>', "
            "'phasespect <0|1>' and 'size <n>'.",
            "any float");
  INLET_DOC(1, "imaginary",
            "Inside a ~pfft, the imaginary part of the bins, from ~fftin's second outlet. "
            "Elsewhere it does nothing.",
            "any float");
  PARAM_DOC("interval", "20",
            "The update interval in milliseconds ('~spectroscope 50'): a frame every interval, "
            "at the live sample rate.",
            "10-2000 ms");
  PARAM_DOC("phasespect", "0",
            "What the bins hold ('~spectroscope 20 1'): 0 the amplitude, scaled so a sine reads "
            "its amplitude in its bin, 1 the phase in radians.",
            "0 or 1");
  PARAM_DOC("size", "4096",
            "The FFT size outside a ~pfft ('~spectroscope 20 0 1024'), made the power of two at "
            "or above it: a frame holds size / 2 + 1 bins, bin j at j * samplerate / size Hz. "
            "Inside a ~pfft the frames are the ~pfft's.",
            "16-16384");
}

MESSAGES() {
  (void)value;
  // A missing or malformed number is ignored.
  std::size_t next = 0;
  const std::string_view word = Token(message, 0, next);
  float number = 0.f;
  if (!Number(message, next, number)) return;
  if (word == "interval") {
    interval = number;
  } else if (word == "phasespect") {
    phasespect = number;
  } else if (word == "size") {
    size = number;
  }
}

CALC() {
  (void)thread;
  fn.interval(interval.load())
      .mode(spectrumAnalyzer::Mode(phasespect.load()))
      .size(spectrumAnalyzer::Size(size.load()));
  // Unconnected, an inlet reads as silence (#1036). Inside a ~pfft a block
  // is a pass of the frame's bins. No outlets: the host polls the frames.
  const spectralPass* pass = CurrentSpectralPass();
  if (pass != nullptr)
    fn(pass->Frame(inputs[0].Signal(0.f).getPtr(), inputs[1].Signal(0.f).getPtr()));
  else
    fn(inputs[0].Signal(0.f));
}

// Host thread: the newest frame, every cell.
GUI_VALUE() {
  std::lock_guard<std::mutex> lock(reading);
  const spectrumAnalyzer::frame f = fn.acquire();
  std::string out;
  out.reserve(32 + static_cast<std::size_t>(f.bins) * 12);
  for (unsigned int i = 0; i < HEADER; i++) {
    if (i > 0) out += ' ';
    out += FrameCell(f, i);
  }
  for (UInt i = 0; i < f.bins; i++) {
    out += ' ';
    Append(out, f.values[i]);
  }
  return out;
}

GUI_VALUE_COUNT() {
  std::lock_guard<std::mutex> lock(reading);
  return HEADER + fn.held().bins;
}

// Cell 0 takes the newest frame; the others read the one it took.
GUI_VALUE_AT() {
  std::lock_guard<std::mutex> lock(reading);
  const spectrumAnalyzer::frame f = index == 0 ? fn.acquire() : fn.held();
  return FrameCell(f, index);
}

#undef className
