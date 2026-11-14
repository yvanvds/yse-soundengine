#pragma once

#include "../../headers/types.hpp"
#include "../../dsp/spectral/stft.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Where a ``~pfft`` subpatch is in its spectral frame: what an
     *         object inside it reads to know which bins its signal buffers
     *         hold (issue #1041).
     *
     *  A ``~pfft`` renders its subpatch once per spectral frame, as
     *  ``passes`` passes of ``count`` bins each, in bin order, all at the
     *  sample the frame completes: every object inside sees ordinary
     *  signal buffers of at most ``STANDARD_BUFFERSIZE`` samples, sample
     *  ``i`` of a pass being bin ``firstBin + i``. While a pass renders,
     *  ``CurrentSpectralPass()`` describes it on the rendering thread;
     *  anywhere else (outside a ``~pfft``, on a control thread) it is null.
     *
     *  This is the per-bin API of the frame-domain objects (``~framedelta``,
     *  ``~frameaccum``, ``~frameaverage``, ``~frame``, ``~framesnap`` ...):
     *  a wrapper reads the pass and hands its DSP class the run of bins as
     *  a ``DSP::stftFrame`` (``Frame()``), and the class keeps its per-bin
     *  state at ``firstBin + i``, so the same class works on the whole frame
     *  ``DSP::stft`` gives a callback. Per-bin state is sized for the
     *  largest frame it is meant for when the object is built, never on the
     *  audio thread; a pass beyond it is the class's to ignore.
     *
     *  An object that needs bins of a *later* pass to compute an earlier
     *  one (a bin shift down, a pitch shift down) cannot work pass by pass;
     *  that case is #1280's.
     */
    struct spectralPass {
      UInt fftSize = 0; ///< the FFT size, in samples
      UInt bins = 0; ///< the spectral frame: fftSize / 2, or fftSize with the full spectrum
      UInt hop = 0; ///< samples from one frame to the next
      UInt overlap = 0; ///< fftSize / hop
      bool fullSpectrum = false; ///< bins is the whole spectrum
      UInt firstBin = 0; ///< the bin sample 0 of this pass is
      UInt count = 0; ///< the bins (samples) of this pass
      UInt pass = 0; ///< this pass, 0 to passes - 1
      UInt passes = 0; ///< passes per frame: bins / count
      U64 frame = 0; ///< frames completed before this one since the ~pfft started

      /** @brief This pass as a run of bins over @p real / @p imaginary
       *         (the pass's own buffers, ``count`` samples). */
      DSP::stftFrame Frame(Flt* real, Flt* imaginary) const {
        DSP::stftFrame f;
        f.real = real;
        f.imaginary = imaginary;
        f.firstBin = firstBin;
        f.count = count;
        f.bins = bins;
        f.size = fftSize;
        f.hop = hop;
        f.fullSpectrum = fullSpectrum;
        f.index = frame;
        return f;
      }
    };

    /** @brief The pass being rendered on this thread, or null outside a
     *         ``~pfft`` pass. RT-safe: one thread-local load. */
    const spectralPass* CurrentSpectralPass();

    /** @brief Makes @p pass the current one for its lifetime, restoring
     *         the previous one after (a ``~pfft`` inside a ``~pfft``).
     *         Used by ``~pfft``; RT-safe. */
    class spectralPassScope {
    public:
      explicit spectralPassScope(const spectralPass* pass);
      ~spectralPassScope();
      spectralPassScope(const spectralPassScope&) = delete;
      spectralPassScope& operator=(const spectralPassScope&) = delete;
      spectralPassScope(spectralPassScope&&) = delete;
      spectralPassScope& operator=(spectralPassScope&&) = delete;

    private:
      const spectralPass* previous_;
    };

  } // namespace PATCHER
} // namespace YSE
