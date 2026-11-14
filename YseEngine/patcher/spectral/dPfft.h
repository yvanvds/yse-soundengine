#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include "../pObject.h"
#include "../inlet.h"
#include "../graphState.h"
#include "../../dsp/spectral/stft.hpp"
#include "spectralPass.h"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief A signal cord of a ``~pfft`` subpatch carried into a later
     *         stage than its source puts out in (issue #1280; see
     *         GraphState::spectralDomain::carries).
     *
     *  Built by the patcher's BuildRenderOrder on the control thread, which
     *  puts ``tap`` in the source outlet's compiled targets in place of
     *  ``target``. While the source's stage renders, what it sends lands in
     *  the tap, which stores the pass's bins in ``frame``; while the
     *  target's ``stage`` renders, Replay hands the pass's bins of the frame
     *  to ``target`` as that cord's buffer. Allocation-free on the render
     *  thread; ``frame`` is as long as the ~pfft's spectral frame.
     */
    struct spectralCarry {
      spectralCarry(pObject* host, inlet* target, UInt bins, std::uint32_t stage);
      spectralCarry(const spectralCarry&) = delete;
      spectralCarry& operator=(const spectralCarry&) = delete;
      spectralCarry(spectralCarry&&) = delete;
      spectralCarry& operator=(spectralCarry&&) = delete;
      ~spectralCarry() = default;

      /** @brief Sends the current pass's bins of the frame to ``target``.
       *         Render thread, inside a pass. */
      void Replay(THREAD thread);

      inlet tap;
      inlet* target;
      std::vector<Flt> frame;
      DSP::buffer slice;
      std::uint32_t stage;
    };

    /**
     *  @brief ``~pfft``: a subpatch run once per short-time spectral frame,
     *         on ``DSP::stftAnalysis`` / ``DSP::stftSynthesis`` halves
     *         sharing one ``DSP::frameClock`` (issue #1041).
     *
     *  Ported from MSP's ``pfft~``. A container like ``patcher``: objects
     *  are put inside it with ``SetContainer``. What sits inside (directly,
     *  or in a ``patcher`` inside it) forms the ~pfft's *domain*: it is not
     *  rendered by the patcher's block walk but by this object, once per
     *  frame (GraphState::spectralDomain).
     *
     *  **Per block** (CALC), in runs that end where a frame completes:
     *  each signal inlet is taken into its analysis, each outlet is read
     *  from its synthesis. **Per frame**, at the sample it completes: every
     *  inlet is analysed (windowed by its ``~fftin``'s window, transformed),
     *  the subpatch is rendered as passes of at most
     *  ``STANDARD_BUFFERSIZE`` bins with ``CurrentSpectralPass()`` set (each
     *  ``~fftin`` loaded with its inlet's bins for the pass, each
     *  ``~fftout``'s input added into its outlet's bins after it), and every
     *  outlet is synthesised (windowed by its ``~fftout``'s window and
     *  overlap-added with the gain ``DSP::stftSynthesis`` describes).
     *
     *  - Inlets: ``inlets`` signals (default 1), each read by the
     *    ``~fftin`` with that index; unconnected, the last number sent
     *    (#1036). Inlet 0 also takes ``clear`` (every frame and output
     *    cleared) and ``mute <0|1>`` (the subpatch stops running and the
     *    outlets fall silent after the frames in flight). Neither is saved.
     *  - Outlets: ``outlets`` signals (default 1), what the ``~fftout``
     *    objects with that index put out.
     *  - Parameters: ``name`` (free text, as for ``patcher``), ``size`` (512,
     *    a power of two 16..65536), ``overlap`` (2, a power of two up to the
     *    size), ``offset`` (0, samples into the stream of the first frame,
     *    modulo the hop), ``full`` (0: frames of size / 2 bins; 1: the whole
     *    spectrum), ``inlets`` (1..16) and ``outlets`` (0..16). Changing one
     *    rebuilds the object, so storage is allocated control side.
     *
     *  A frame-wide object inside (pObject::SpectralFrameWide, issue
     *  #1280) splits the subpatch into stages, each rendered as every pass
     *  of the frame before the next begins; see
     *  GraphState::spectralDomain.
     *
     *  Latency: ``size`` samples, whatever the block size.
     */
    PATCHER_CLASS(dPfft, YSE::OBJ::D_PFFT)
    _DO_MESSAGES
    _DO_CALCULATE

    _PARM_CLEAR
    _PARM_PARSE

    _FLOAT_IN(SetFloat)
    _INT_IN(SetInt)

    /** @brief The most signal inlets or outlets. */
    static constexpr int MAX_PINS = 16;

    UInt Size() const {
      return clock.size();
    }
    UInt Hop() const {
      return clock.interval();
    }
    /** @brief Bins per spectral frame inside: size / 2, or size. */
    UInt Bins() const {
      return full != 0 ? clock.size() : clock.size() / 2;
    }
    bool FullSpectrum() const {
      return full != 0;
    }

  private:
    void Configure();
    void Frame(const GraphState::spectralDomain* domain, YSE::THREAD thread);
    void Render(const GraphState::spectralDomain& domain, YSE::THREAD thread);
    void Windows(const GraphState::spectralDomain& domain);

    std::string name;
    int size = 512;
    int overlap = 2;
    int offset = 0;
    int full = 0;
    int ins = 1;
    int outs = 1;

    DSP::frameClock clock;
    std::vector<std::unique_ptr<DSP::stftAnalysis>> analyses;
    std::vector<std::unique_ptr<DSP::stftSynthesis>> syntheses;
    std::vector<std::unique_ptr<DSP::buffer>> results;
    U64 frames = 0;
    // The snapshot the block being rendered pinned; set for CALC() only.
    const GraphState* graph_ = nullptr;

    std::atomic<float> numbers[MAX_PINS] = {};
    std::atomic<bool> pendingClear{false};
    std::atomic<bool> muted{false};
  };

  /**
   *  @brief ``~fftin``: inside a ``~pfft``, the spectrum of one of its
   *         signal inlets (issue #1041).
   *
   *  Ported from MSP's ``fftin~``. Three signal outlets: the real and
   *  imaginary parts of the bins of the current pass, and their bin
   *  indexes. The ~pfft loads them before every pass (Load); Calculate
   *  sends them. Parameters ``inlet`` (0, the ~pfft inlet it reads) and
   *  ``window`` (``hanning``; ``square``, ``triangle``, ``hamming``,
   *  ``blackman``), the analysis window of that inlet. Outside a ~pfft it
   *  sends silence.
   */
  PATCHER_CLASS(dFftin, YSE::OBJ::D_FFTIN)
  _NO_MESSAGES
  _DO_CALCULATE

  _PARM_CLEAR
  _PARM_PARSE

  int Inlet() const {
    return index;
  }
  DSP::stftWindow::TYPE Window() const {
    return windowType;
  }

  /** @brief The ~pfft's load for one pass: @p count bins from
   *         @p firstBin, null for none (zeros). Render thread. */
  void Load(const Flt* real, const Flt* imaginary, UInt firstBin, UInt count);

private:
  int index = 0;
  std::string window;
  DSP::stftWindow::TYPE windowType = DSP::stftWindow::HANNING;
  DSP::buffer re, im, bin;
};

/**
 *  @brief ``~fftout``: inside a ``~pfft``, the spectrum one of its signal
 *         outlets is resynthesised from (issue #1041).
 *
 *  Ported from MSP's ``fftout~``. Two signal inlets, the real and imaginary
 *  parts of the bins of the current pass (unconnected: the last number
 *  sent, else 0). No outlets: after each pass the ~pfft adds what arrived
 *  into the bins of the outlet the ``outlet`` parameter names (AddTo).
 *  Parameter ``window``: the synthesis window of that outlet. Outside a
 *  ~pfft it does nothing.
 */
PATCHER_CLASS(dFftout, YSE::OBJ::D_FFTOUT)
_NO_MESSAGES
_DO_CALCULATE
_DO_RESET

_PARM_CLEAR
_PARM_PARSE

_FLOAT_IN(SetFloat)
_INT_IN(SetInt)

int Outlet() const {
  return index;
}
DSP::stftWindow::TYPE Window() const {
  return windowType;
}

/** @brief Adds this pass's input to @p real / @p imaginary, @p count
 *         bins. Render thread. */
void AddTo(Flt* real, Flt* imaginary, UInt count) const;

private:
int index = 0;
std::string window;
DSP::stftWindow::TYPE windowType = DSP::stftWindow::HANNING;
DSP::buffer* inReal = nullptr;
DSP::buffer* inImaginary = nullptr;
std::atomic<float> numbers[2] = {{0.f}, {0.f}};
}
;

/**
 *  @brief ``~fftinfo``: inside a ``~pfft``, reports its FFT size, spectral
 *         frame size, hop and full-spectrum flag (issue #1041).
 *
 *  Ported from MSP's ``fftinfo~``. A signal object so it runs in the
 *  subpatch's passes, where it reads ``CurrentSpectralPass()``: the first
 *  time it runs, and whenever the figures change, it arms its four
 *  outlets (sent after the block, #1037). A bang sends the figures last
 *  seen. Outside a ~pfft it reports nothing.
 */
PATCHER_CLASS(dFftinfo, YSE::OBJ::D_FFTINFO)
_NO_MESSAGES
_DO_CALCULATE
_BANG_IN(Bang)

private:
std::atomic<int> fftSize{0};
std::atomic<int> bins{0};
std::atomic<int> hop{0};
std::atomic<int> fullSpectrum{0};
}
;

} // namespace PATCHER
} // namespace YSE
