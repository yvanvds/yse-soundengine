// `~pfft`, `~fftin`, `~fftout`, `~fftinfo` (issue #1041): see dPfft.h. The
// transforms, windows and overlap-add are DSP::stftAnalysis' and
// DSP::stftSynthesis'; what is here is the frame loop that runs the subpatch
// between them, and the pin and parameter mapping.
#include "dPfft.h"

#include <algorithm>
#include <string_view>
#include "../inlet.h"
#include "../pListArgs.h"
#include "../pSelector.h"
#include "../../implementations/logImplementation.h"

using namespace YSE::PATCHER;
using YSE::DSP::stft;
using YSE::DSP::stftWindow;

namespace {
  thread_local const spectralPass* tPass = nullptr;

  int Clamp(int value, int lowest, int highest) {
    return value < lowest ? lowest : (value > highest ? highest : value);
  }

  // The token starting at or after `from`, walked in place so nothing
  // allocates on whichever thread delivers the message; `next` is where it
  // ends.
  std::string_view Token(const std::string& text, std::size_t from, std::size_t& next) {
    const char* chars = text.c_str();
    const std::size_t length = text.size();
    std::size_t begin = from;
    while (begin < length && IsSelectorSeparator(chars[begin]))
      begin++;
    std::size_t end = begin;
    while (end < length && !IsSelectorSeparator(chars[end]))
      end++;
    next = end;
    return std::string_view(chars + begin, end - begin);
  }

  UInt Argument(int value) {
    return value < 0 ? 0u : static_cast<UInt>(value);
  }

  // Control thread: a window name as the ~fftin / ~fftout arguments give it.
  // MSP's names; "hann" is accepted for Hanning. Anything else is Hanning,
  // with a warning.
  stftWindow::TYPE WindowNamed(const std::string& name, const char* object) {
    if (name.empty() || name == "hanning" || name == "hann") return stftWindow::HANNING;
    if (name == "square") return stftWindow::SQUARE;
    if (name == "triangle") return stftWindow::TRIANGLE;
    if (name == "hamming") return stftWindow::HAMMING;
    if (name == "blackman") return stftWindow::BLACKMAN;
    YSE::INTERNAL::LogImpl().emit(YSE::E_WARNING, std::string(object) + ": unknown window '" +
                                                      name + "', using hanning");
    return stftWindow::HANNING;
  }
} // namespace

const spectralPass* YSE::PATCHER::CurrentSpectralPass() {
  return tPass;
}

spectralPassScope::spectralPassScope(const spectralPass* pass) : previous_(tPass) {
  tPass = pass;
}

spectralPassScope::~spectralPassScope() {
  tPass = previous_;
}

// ─── ~pfft ────────────────────────────────────────────────────────────────────

#define className dPfft

CONSTRUCT_DSP() {
  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(name);
  ADD_PARAM(size);
  ADD_PARAM(overlap);
  ADD_PARAM(offset);
  ADD_PARAM(full);
  ADD_PARAM(ins);
  ADD_PARAM(outs);
  Configure();

  ADD_DESCRIPTION("A subpatch run once per spectral frame: each signal inlet is cut into "
                  "overlapping windowed frames and transformed, the objects inside process the "
                  "bins (~fftin to ~fftout), and the result is transformed back and overlap-added "
                  "to the outlets.");
  ADD_CATEGORY(pCategory::SPECTRAL);
  PARAM_DOC("name", "",
            "A name for the subpatch, for a host to show. The engine does not read it; the "
            "contents are placed inside with SetContainer.",
            "");
  PARAM_DOC("size", "512",
            "The FFT size in samples, rounded up to a power of two. The subpatch sees frames of "
            "size / 2 bins (size with full). Changing it rebuilds the object.",
            "16-65536");
  PARAM_DOC("overlap", "2",
            "How many frames overlap: a frame every size / overlap samples (the hop), rounded "
            "up to a power of two. 4 is a good choice for most processing.",
            "1 to size");
  PARAM_DOC("offset", "0",
            "Samples into the stream that the first frame starts, taken modulo the hop.",
            "0 to hop - 1");
  PARAM_DOC("full", "0",
            "0: the subpatch processes bins 0 to size / 2 - 1 and the rest is mirrored; 1: the "
            "whole spectrum, size bins.",
            "0 or 1");
  PARAM_DOC("inlets", "1", "How many signal inlets, read inside by ~fftin 0, ~fftin 1 ...", "1-16");
  PARAM_DOC("outlets", "1", "How many signal outlets, written inside by ~fftout 0, ~fftout 1 ...",
            "0-16");
}

// Control thread, on an object no audio thread has seen yet: the clear and
// parse callbacks make every change of argument a rebuild. The pins come from
// the arguments, so they are rebuilt here too, before anything is wired.
void dPfft::Configure() {
  const UInt frame = stft::fitSize(Argument(size));
  const UInt hop = frame / stft::fitOverlap(Argument(overlap), frame);
  clock = DSP::frameClock(frame, hop, Argument(offset));
  const int inCount = Clamp(ins, 1, MAX_PINS);
  const int outCount = Clamp(outs, 0, MAX_PINS);
  const bool fullSpectrum = full != 0;

  analyses.clear();
  syntheses.clear();
  results.clear();
  for (int i = 0; i < inCount; i++) {
    analyses.push_back(std::make_unique<DSP::stftAnalysis>(frame, fullSpectrum));
  }
  for (int o = 0; o < outCount; o++) {
    syntheses.push_back(std::make_unique<DSP::stftSynthesis>(frame, frame / hop, fullSpectrum));
    results.push_back(std::make_unique<DSP::buffer>(STANDARD_BUFFERSIZE));
    *results.back() = 0.f;
  }
  frames = 0;

  inputs.clear();
  for (int i = 0; i < inCount; i++) {
    inputs.emplace_back(this, false, i);
    REG_SIGNAL_IN;
    REG_FLOAT_IN(SetFloat);
    REG_INT_IN(SetInt);
    inputs.back().SetDoc(
        i == 0 ? "in 0" : "in",
        i == 0 ? "A signal, analysed frame by frame for the ~fftin objects with this index "
                 "inside; unconnected, the last number sent here (0). Also 'clear' (clears "
                 "every frame) and 'mute 1' / 'mute 0' (stops and restarts the subpatch); not "
                 "saved."
               : "A signal, analysed frame by frame for the ~fftin objects with this index "
                 "inside; unconnected, the last number sent here (0).",
        "");
  }
  outputs.clear();
  for (int o = 0; o < outCount; o++) {
    ADD_OUT_BUFFER;
    outputs.back().SetDoc("out",
                          "What the ~fftout objects with this index inside resynthesise, "
                          "overlap-added: the subpatch's output, size samples after its input.",
                          "");
  }
}

PARM_CLEAR() {
  name.clear();
  size = 512;
  overlap = 2;
  offset = 0;
  full = 0;
  ins = 1;
  outs = 1;
  Configure();
}

PARM_PARSE() {
  Configure();
}

FLOAT_IN(SetFloat) {
  (void)thread;
  if (inlet >= 0 && inlet < MAX_PINS) numbers[inlet] = value;
}

INT_IN(SetInt) {
  SetFloat(static_cast<float>(value), inlet, thread);
}

MESSAGES() {
  (void)value;
  std::size_t next = 0;
  const std::string_view word = Token(message, 0, next);
  if (word == "clear") {
    pendingClear = true;
  } else if (word == "mute") {
    // `mute` alone mutes, as `mute 1` does.
    const std::string_view arg = Token(message, next, next);
    float number = 1.f;
    if (!arg.empty() && !ReadNumericToken(arg.data(), arg.size(), number)) return;
    muted = number != 0.f;
  }
}

// Every ~fftin sets the analysis window of the inlet it reads, every
// ~fftout the synthesis window of its outlet (the first one by storage ID
// when two share an index). An outlet's gain is computed for the analysis
// window of the inlet with its index, or inlet 0's. Allocation-free: a
// changed window fills a table of `size` values.
void dPfft::Windows(const GraphState::spectralDomain& domain) {
  bool seen[MAX_PINS] = {};
  for (pObject* object : domain.inputs) {
    const auto* in = static_cast<const dFftin*>(object);
    const int k = in->Inlet();
    if (k < 0 || k >= static_cast<int>(analyses.size()) || seen[k]) continue;
    seen[k] = true;
    analyses[static_cast<std::size_t>(k)]->window(in->Window());
  }
  bool written[MAX_PINS] = {};
  for (pObject* object : domain.outputs) {
    const auto* out = static_cast<const dFftout*>(object);
    const int j = out->Outlet();
    if (j < 0 || j >= static_cast<int>(syntheses.size()) || written[j]) continue;
    written[j] = true;
    const std::size_t source =
        static_cast<std::size_t>(j) < analyses.size() ? static_cast<std::size_t>(j) : 0;
    syntheses[static_cast<std::size_t>(j)]->window(out->Window(), analyses[source]->window());
  }
}

// One spectral frame of the subpatch, in passes of at most
// STANDARD_BUFFERSIZE bins, bin order, all at the sample the frame completes.
void dPfft::Render(const GraphState::spectralDomain& domain, YSE::THREAD thread) {
  spectralPass pass;
  pass.fftSize = clock.size();
  pass.bins = Bins();
  pass.hop = clock.interval();
  pass.overlap = clock.size() / clock.interval();
  pass.fullSpectrum = FullSpectrum();
  pass.count = std::min(pass.bins, STANDARD_BUFFERSIZE);
  pass.passes = pass.bins / pass.count;
  pass.frame = frames;

  const spectralPassScope current(&pass);
  // Arrivals store and wait for the walk below, as in the patcher's own
  // render (issue #1185); normally already open around the ~pfft's step.
  const renderOrderScope order;
  for (UInt p = 0; p < pass.passes; p++) {
    pass.pass = p;
    pass.firstBin = p * pass.count;
    for (pObject* object : domain.inputs) {
      auto* in = static_cast<dFftin*>(object);
      const int k = in->Inlet();
      if (k >= 0 && k < static_cast<int>(analyses.size())) {
        DSP::stftAnalysis& a = *analyses[static_cast<std::size_t>(k)];
        in->Load(a.real() + pass.firstBin, a.imaginary() + pass.firstBin, pass.firstBin,
                 pass.count);
      } else {
        in->Load(nullptr, nullptr, pass.firstBin, pass.count);
      }
    }
    for (pObject* object : domain.objects) {
      object->ResetDSP();
    }
    for (const GraphState::renderStep& step : domain.renderOrder) {
      if (graph_->Muted(step.muteBegin, step.muteEnd)) {
        step.object->SendSilence(thread);
      } else if (step.startPoint) {
        step.object->Calculate(thread);
      } else if (thread != YSE::T_GUI || !step.object->IsDSPObject()) {
        step.object->CalculateIfReady(thread);
      }
    }
    for (pObject* object : domain.outputs) {
      const auto* out = static_cast<const dFftout*>(object);
      const int j = out->Outlet();
      if (j < 0 || j >= static_cast<int>(syntheses.size())) continue;
      DSP::stftSynthesis& s = *syntheses[static_cast<std::size_t>(j)];
      out->AddTo(s.real() + pass.firstBin, s.imaginary() + pass.firstBin, pass.count);
    }
  }
}

void dPfft::Frame(const GraphState::spectralDomain* domain, YSE::THREAD thread) {
  if (domain != nullptr) Windows(*domain);
  for (auto& a : analyses) {
    a->analyse();
  }
  if (domain != nullptr && !muted.load(std::memory_order_relaxed)) Render(*domain, thread);
  // Unwritten bins are 0 (synthesise leaves them so): silence for an outlet
  // no ~fftout reaches, and the Nyquist bin of a half spectrum.
  for (auto& s : syntheses) {
    s->synthesise();
  }
  frames++;
}

CALC() {
  if (pendingClear.exchange(false)) {
    clock.reset();
    for (auto& a : analyses) {
      a->reset();
    }
    for (auto& s : syntheses) {
      s->reset();
    }
    frames = 0;
  }

  // The subpatch this ~pfft renders, from the block's pinned snapshot.
  const graphReadScope scope(this);
  graph_ = scope.Graph();
  const GraphState::spectralDomain* domain = graph_ != nullptr ? graph_->Spectral(this) : nullptr;

  // The inputs, unconnected ones read as their numbers (#1036).
  const Flt* in[MAX_PINS] = {};
  UInt length = STANDARD_BUFFERSIZE;
  for (std::size_t k = 0; k < analyses.size(); k++) {
    DSP::buffer& b = inputs[k].Signal(numbers[k].load());
    in[k] = b.getPtr();
    length = std::min(length, b.getLength());
  }
  for (auto& r : results) {
    // Within the capacity set at construction: no allocation.
    if (r->getLength() != length) r->resize(length);
  }

  // The block in runs, each ending where a frame completes; the frame's
  // work runs right there, inside the block.
  UInt i = 0;
  while (i < length) {
    const UInt run = std::min(length - i, clock.untilFrame());
    for (std::size_t k = 0; k < analyses.size(); k++) {
      analyses[k]->write(in[k] + i, run);
    }
    for (std::size_t o = 0; o < syntheses.size(); o++) {
      syntheses[o]->read(results[o]->getPtr() + i, run);
    }
    i += run;
    if (clock.advance(run)) Frame(domain, thread);
  }
  graph_ = nullptr;

  for (std::size_t o = results.size(); o-- > 0;) {
    outputs[o].SendBuffer(results[o].get(), thread);
  }
}

#undef className

// ─── ~fftin ───────────────────────────────────────────────────────────────────

#define className dFftin

CONSTRUCT_DSP() {
  re.resize(STANDARD_BUFFERSIZE);
  im.resize(STANDARD_BUFFERSIZE);
  bin.resize(STANDARD_BUFFERSIZE);
  re = 0.f;
  im = 0.f;
  bin = 0.f;

  ADD_OUT_BUFFER;
  ADD_OUT_BUFFER;
  ADD_OUT_BUFFER;

  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(index);
  ADD_PARAM(window);

  ADD_DESCRIPTION("Inside a ~pfft: the spectrum of one of its signal inlets, the real and "
                  "imaginary parts of each bin and the bin's index, a spectral frame at a time.");
  ADD_CATEGORY(pCategory::SPECTRAL);
  OUTLET_DOC(0, "real",
             "The real part of the bins: sample i of each pass is bin first + i, with the bin "
             "index on the right outlet. Not divided by the size.",
             "");
  OUTLET_DOC(1, "imaginary", "The imaginary part of the bins, timed as the real part.", "");
  OUTLET_DOC(2, "bin",
             "The index of each bin, 0 to the spectral frame size - 1: bin k is k * samplerate "
             "/ size Hz.",
             "0 to frame size - 1");
  PARAM_DOC("inlet", "0", "Which signal inlet of the ~pfft this reads, from 0.", "0-15");
  PARAM_DOC("window", "hanning",
            "The analysis window of that inlet: square, triangle, hanning, hamming or "
            "blackman.",
            "");
}

PARM_CLEAR() {
  index = 0;
  window.clear();
  windowType = stftWindow::HANNING;
}

PARM_PARSE() {
  windowType = WindowNamed(window, "~fftin");
}

void dFftin::Load(const Flt* real, const Flt* imaginary, UInt firstBin, UInt count) {
  count = std::min(count, STANDARD_BUFFERSIZE);
  // Within the capacity set in the constructor: no allocation.
  if (re.getLength() != count) {
    re.resize(count);
    im.resize(count);
    bin.resize(count);
  }
  Flt* r = re.getPtr();
  Flt* m = im.getPtr();
  Flt* b = bin.getPtr();
  for (UInt i = 0; i < count; i++) {
    r[i] = real != nullptr ? real[i] : 0.f;
    m[i] = imaginary != nullptr ? imaginary[i] : 0.f;
    b[i] = static_cast<Flt>(firstBin + i);
  }
}

CALC() {
  outputs[2].SendBuffer(&bin, thread);
  outputs[1].SendBuffer(&im, thread);
  outputs[0].SendBuffer(&re, thread);
}

#undef className

// ─── ~fftout ──────────────────────────────────────────────────────────────────

#define className dFftout

CONSTRUCT_DSP() {
  inputs.emplace_back(this, false, 0);
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);
  inputs.emplace_back(this, false, 1);
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);

  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(index);
  ADD_PARAM(window);

  ADD_DESCRIPTION("Inside a ~pfft: the spectrum one of its signal outlets is resynthesised from, "
                  "real and imaginary parts of each bin, a spectral frame at a time.");
  ADD_CATEGORY(pCategory::SPECTRAL);
  INLET_DOC(0, "real",
            "The real part of the bins, sample i of each pass being bin first + i as ~fftin "
            "puts them out. A signal; unconnected, the last number sent here (0).",
            "");
  INLET_DOC(1, "imaginary", "The imaginary part of the bins, likewise.", "");
  PARAM_DOC("outlet", "0", "Which signal outlet of the ~pfft this writes, from 0.", "0-15");
  PARAM_DOC("window", "hanning",
            "The synthesis window of that outlet: square, triangle, hanning, hamming or "
            "blackman.",
            "");
}

PARM_CLEAR() {
  index = 0;
  window.clear();
  windowType = stftWindow::HANNING;
}

PARM_PARSE() {
  windowType = WindowNamed(window, "~fftout");
}

FLOAT_IN(SetFloat) {
  (void)thread;
  if (inlet >= 0 && inlet < 2) numbers[inlet] = value;
}

INT_IN(SetInt) {
  SetFloat(static_cast<float>(value), inlet, thread);
}

RESET()
inReal = nullptr;
inImaginary = nullptr;
}

CALC() {
  (void)thread;
  inReal = &inputs[0].Signal(numbers[0].load());
  inImaginary = &inputs[1].Signal(numbers[1].load());
}

void dFftout::AddTo(Flt* real, Flt* imaginary, UInt count) const {
  if (inReal != nullptr) {
    const UInt n = std::min(count, inReal->getLength());
    const Flt* src = inReal->getPtr();
    for (UInt i = 0; i < n; i++)
      real[i] += src[i];
  }
  if (inImaginary != nullptr) {
    const UInt n = std::min(count, inImaginary->getLength());
    const Flt* src = inImaginary->getPtr();
    for (UInt i = 0; i < n; i++)
      imaginary[i] += src[i];
  }
}

#undef className

// ─── ~fftinfo ─────────────────────────────────────────────────────────────────

#define className dFftinfo

CONSTRUCT_DSP() {
  inputs.emplace_back(this, false, 0);
  REG_BANG_IN(Bang);

  ADD_OUT_INT;
  ADD_OUT_INT;
  ADD_OUT_INT;
  ADD_OUT_INT;

  ADD_DESCRIPTION("Inside a ~pfft: reports its FFT size, spectral frame size, hop and "
                  "full-spectrum flag, when the subpatch first runs and on a bang.");
  ADD_CATEGORY(pCategory::SPECTRAL);
  INLET_DOC(0, "bang", "Reports the figures again.", "");
  OUTLET_DOC(0, "FFT size", "The ~pfft's FFT size.", "samples");
  OUTLET_DOC(1, "frame size",
             "The bins of a spectral frame inside: half the FFT size, or the FFT size with the "
             "full spectrum.",
             "bins");
  OUTLET_DOC(2, "hop", "Samples from one frame to the next: the FFT size / the overlap.",
             "samples");
  OUTLET_DOC(3, "full spectrum", "1 when the subpatch processes the whole spectrum, else 0.",
             "0 or 1");
}

CALC() {
  (void)thread;
  const spectralPass* pass = CurrentSpectralPass();
  if (pass == nullptr) return;
  const int size = static_cast<int>(pass->fftSize);
  const int frame = static_cast<int>(pass->bins);
  const int step = static_cast<int>(pass->hop);
  const int whole = pass->fullSpectrum ? 1 : 0;
  if (fftSize.load() == size && bins.load() == frame && hop.load() == step &&
      fullSpectrum.load() == whole)
    return;
  fftSize = size;
  bins = frame;
  hop = step;
  fullSpectrum = whole;
  outputs[3].ArmInt(whole);
  outputs[2].ArmInt(step);
  outputs[1].ArmInt(frame);
  outputs[0].ArmInt(size);
  MarkOutletArmed();
}

BANG_IN(Bang) {
  (void)inlet;
  if (fftSize.load() == 0) return;
  outputs[3].SendInt(fullSpectrum.load(), thread);
  outputs[2].SendInt(hop.load(), thread);
  outputs[1].SendInt(bins.load(), thread);
  outputs[0].SendInt(fftSize.load(), thread);
}

#undef className
