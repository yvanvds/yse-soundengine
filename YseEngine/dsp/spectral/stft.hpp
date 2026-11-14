/*
  ==============================================================================

    stft.hpp
    The short-time Fourier transform: a stream cut into overlapping,
    windowed frames, each transformed, handed to a frame callback,
    transformed back and overlap-added into an output stream (issue #1041).

  ==============================================================================
*/

#ifndef YSE_DSP_SPECTRAL_STFT_HPP
#define YSE_DSP_SPECTRAL_STFT_HPP

#include <vector>
#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"
#include "frameClock.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief The window shapes a short-time Fourier transform weights its
     *         frames with.
     *
     *  Every shape is *periodic*: sample ``n`` of a window of ``size``
     *  samples is the shape at ``n / size`` of a period, so the shape at
     *  ``size`` would equal the one at 0. That is the form whose overlapped
     *  copies sum to a constant (a Hann window at an overlap of 2 sums to
     *  1). The names are MSP's.
     */
    class API stftWindow {
    public:
      /** @brief A window shape. */
      enum TYPE {
        SQUARE, ///< 1 everywhere: no window
        TRIANGLE, ///< 0 at the edges, 1 in the middle, straight in between
        HANNING, ///< 0.5 - 0.5 cos(2 pi n / size): the default
        HAMMING, ///< 0.54 - 0.46 cos(2 pi n / size)
        BLACKMAN, ///< 0.42 - 0.5 cos(2 pi n / size) + 0.08 cos(4 pi n / size)
      };

      /** @brief Sample @p n (taken modulo @p size) of a window of @p size
       *         samples. */
      static Flt value(TYPE type, UInt n, UInt size);

      /** @brief Writes a whole window of @p size samples to @p out. */
      static void fill(TYPE type, Flt* out, UInt size);
    };

    /**
     *  @brief A run of bins of one short-time spectrum: what a frame
     *         callback gets, and the view a frame-domain process works on.
     *
     *  ``real[i]`` and ``imaginary[i]`` are bin ``firstBin + i``, for ``i``
     *  from 0 to ``count - 1``. ``DSP::stft`` hands its callback the whole
     *  frame (``firstBin`` 0, ``count`` equal to ``bins``); a ``~pfft``
     *  subpatch sees a frame as passes of at most ``STANDARD_BUFFERSIZE``
     *  bins, and a pass is a run of this kind too (see
     *  ``PATCHER::spectralPass``). A class that keeps state per bin (the
     *  previous frame's phase, a running sum) indexes it with
     *  ``firstBin + i`` and so works on either.
     *
     *  Bin ``k`` is the frequency ``k * SAMPLERATE / size``. The values are
     *  those of an unscaled transform: a sine of amplitude 1 that fits a
     *  whole number of cycles in the frame shows up, under a square window,
     *  with a magnitude of ``size / 2``; a window scales that by its mean.
     */
    struct stftFrame {
      Flt* real = nullptr; ///< the real parts
      Flt* imaginary = nullptr; ///< the imaginary parts
      UInt firstBin = 0; ///< the bin ``real[0]`` holds
      UInt count = 0; ///< how many bins ``real`` and ``imaginary`` hold
      UInt bins = 0; ///< the bins of the whole frame
      UInt size = 0; ///< the FFT size, in samples
      UInt hop = 0; ///< samples from one frame to the next
      bool fullSpectrum = false; ///< ``bins`` is the whole spectrum
      U64 index = 0; ///< the frames completed before this one
    };

    /**
     *  @brief The analysis half of a short-time Fourier transform: the
     *         last ``size()`` samples of a stream, windowed and
     *         transformed on request.
     *
     *  ``write()`` takes in samples as they come; ``analyse()`` windows the
     *  latest ``size()`` of them (the oldest first; the stream reads as 0
     *  before its first sample) and transforms them into ``real()`` and
     *  ``imaginary()``. *When* to analyse is the caller's business, usually
     *  where a ``frameClock`` reports a completed frame; ``stft`` pairs this
     *  with an ``stftSynthesis`` on one clock, and ``~pfft`` runs several
     *  of each on one.
     *
     *  The spectrum holds ``bins()`` bins: ``size() / 2 + 1`` (DC to
     *  Nyquist, the rest mirrors them for a real stream) or, with the full
     *  spectrum, all ``size()``. The arrays are ``size()`` long either way.
     *  The transform is not scaled.
     *
     *  Storage is allocated in the constructor and never again; ``write``,
     *  ``analyse``, ``window`` and ``reset`` allocate nothing. Nothing
     *  depends on the sample rate. One thread at a time.
     */
    class API stftAnalysis {
    public:
      /** @brief Frames of @p size samples (fitted with ``stft::fitSize``),
       *         weighted with @p window. Allocates. */
      explicit stftAnalysis(UInt size = 512, bool fullSpectrum = false,
                            stftWindow::TYPE window = stftWindow::HANNING);

      UInt size() const {
        return n;
      }
      UInt bins() const {
        return full ? n : n / 2 + 1;
      }
      bool fullSpectrum() const {
        return full;
      }
      stftWindow::TYPE window() const {
        return type;
      }

      /** @brief A new window shape, used from the next ``analyse()``. Fills
       *         the window table (``size()`` values) when it changes;
       *         allocates nothing. */
      void window(stftWindow::TYPE shape);

      /** @brief Takes in @p count samples. */
      void write(const Flt* samples, UInt count);

      /** @brief Windows the latest ``size()`` samples and transforms them
       *         into ``real()`` / ``imaginary()``. */
      void analyse();

      Flt* real() {
        return re.data();
      }
      Flt* imaginary() {
        return im.data();
      }
      const Flt* real() const {
        return re.data();
      }
      const Flt* imaginary() const {
        return im.data();
      }

      /** @brief Back to the start: the samples taken in and the spectrum
       *         cleared. */
      void reset();

    private:
      UInt n;
      bool full;
      stftWindow::TYPE type;
      std::vector<Flt> history; // ring of the last n samples
      std::vector<Flt> shape;
      std::vector<Flt> re, im;
      UInt pos = 0; // where the next sample goes, the oldest one
    };

    /**
     *  @brief The synthesis half of a short-time Fourier transform: a
     *         spectrum transformed back, windowed and overlap-added into an
     *         output stream.
     *
     *  Write a frame's bins into ``real()`` / ``imaginary()`` (the first
     *  ``bins()`` of them; in half-spectrum mode the rest is filled in as
     *  their mirror) and call ``synthesise()``: the frame is transformed
     *  back, weighted with the synthesis window and the overlap-add gain
     *  below, and added into the output starting at the next sample
     *  ``read()`` puts out. The bins are 0 again afterwards, ready to be
     *  written (or summed into) for the next frame. ``read()`` puts out the
     *  overlap-added stream, sample by sample.
     *
     *  **Overlap-add gain.** The frames overlap ``overlap()`` times (one
     *  every ``hop()`` samples), and each output sample is the sum of the
     *  overlapping frames' windowed samples. The gain divides that sum by
     *  the sum of the analysis window times the synthesis window over the
     *  overlapping frames *at that sample* (it repeats every hop) and by
     *  the size (the unscaled inverse transform). So an unchanged spectrum
     *  comes back as exactly the input, for any window pair and overlap
     *  whose window products sum, at every sample, to at least 1/1000 of
     *  their largest sum: a Hann pair at an overlap of 2 as well as 4. Where
     *  they do not (an overlap of 1 with a tapering window), the gain
     *  divides by the mean of that sum instead, and the frame edges come
     *  out attenuated.
     *
     *  Storage is allocated in the constructor; ``synthesise``, ``read``,
     *  ``window``, ``clear`` and ``reset`` allocate nothing. One thread at
     *  a time.
     */
    class API stftSynthesis {
    public:
      /** @brief Frames of @p size samples (``stft::fitSize``), @p overlap
       *         times overlapped (``stft::fitOverlap``), weighted with
       *         @p window, with the gain computed for frames analysed with
       *         @p analysisWindow. Allocates. */
      explicit stftSynthesis(UInt size = 512, UInt overlap = 4, bool fullSpectrum = false,
                             stftWindow::TYPE window = stftWindow::HANNING,
                             stftWindow::TYPE analysisWindow = stftWindow::HANNING);

      UInt size() const {
        return n;
      }
      UInt hop() const {
        return h;
      }
      UInt overlap() const {
        return n / h;
      }
      UInt bins() const {
        return full ? n : n / 2 + 1;
      }
      bool fullSpectrum() const {
        return full;
      }
      stftWindow::TYPE window() const {
        return type;
      }
      stftWindow::TYPE analysisWindow() const {
        return analysisType;
      }

      /** @brief A new synthesis window, and the analysis window the gain is
       *         computed for, from the next ``synthesise()``. Recomputes the
       *         tables (``size()`` window values) when either changes;
       *         allocates nothing. */
      void window(stftWindow::TYPE synthesis, stftWindow::TYPE analysis);

      /** @brief The gain applied ``position`` samples into a frame (see the
       *         class notes). */
      Flt gain(UInt position) const {
        return norm[position % h];
      }

      Flt* real() {
        return re.data();
      }
      Flt* imaginary() {
        return im.data();
      }

      /** @brief Sets every bin to 0. */
      void clear();

      /** @brief Transforms the bins back and adds the frame into the output
       *         from the next sample on; the bins are 0 afterwards. */
      void synthesise();

      /** @brief Puts out the next @p count samples of the output. */
      void read(Flt* out, UInt count);

      /** @brief Back to the start: bins and output cleared. */
      void reset();

    private:
      void tables();

      UInt n;
      UInt h;
      bool full;
      stftWindow::TYPE type;
      stftWindow::TYPE analysisType;
      std::vector<Flt> shape;
      std::vector<Flt> norm; // per position in the hop
      std::vector<Flt> re, im;
      std::vector<Flt> ola; // ring of the next n output samples
      UInt pos = 0; // the next output sample
    };

    /**
     *  @brief A short-time Fourier transform: a stream cut into overlapping
     *         windowed frames, each one transformed, handed to a callback
     *         that may change its bins, transformed back and overlap-added
     *         into the output.
     *
     *  The engine of every frequency-domain effect, and what ``~pfft``
     *  runs a subpatch on. Usable from C++ alone:
     *
     *  @code
     *  YSE::DSP::stft fx(1024, 4);              // 1024 samples, hop 256
     *  auto gate = [](YSE::DSP::stftFrame& f) {  // zero the quiet bins
     *    for (UInt i = 0; i < f.count; ++i)
     *      if (f.real[i] * f.real[i] + f.imaginary[i] * f.imaginary[i] < 1.f)
     *        f.real[i] = f.imaginary[i] = 0.f;
     *  };
     *  YSE::DSP::buffer& out = fx(in, gate);    // once per block
     *  @endcode
     *
     *  **Framing.** Frames of ``size()`` samples (a power of two, 16 to
     *  ``MAXIMUM_SIZE``), one every ``hop() = size() / overlap()`` samples,
     *  the first ``offset()`` samples in, on a ``frameClock``. A frame is
     *  analysed, handed to the callback and synthesised at the exact sample
     *  it completes, inside whatever block that is: several frames can
     *  complete in one block (a hop below the block size) or none. The
     *  output therefore does not depend on the block size.
     *
     *  **Latency.** The frame's output starts the sample after its last
     *  input sample, so the output lags the input by ``latency()``, the
     *  size: with an unchanged spectrum the output is the input exactly,
     *  ``size()`` samples later (see ``stftSynthesis`` for the gain). That
     *  is the least a sample-exact, block-independent STFT can have, and
     *  the timing ``streamingFft`` uses too, so their frames line up.
     *
     *  **The callback** gets an ``stftFrame`` over the whole spectrum,
     *  ``bins()`` bins: ``size() / 2 + 1`` (DC to Nyquist) for a real
     *  stream, or all ``size()`` with the full spectrum (a complex
     *  transform's, whose upper half mirrors the lower). It may change the
     *  bins in place. The callback runs on the processing thread, inside
     *  the block: it must not allocate or block either.
     *
     *  **Windows.** Hann by default on both sides; any ``stftWindow`` on
     *  either.
     *
     *  Storage is allocated in the constructor and never again; processing,
     *  ``window``, ``offset`` and ``reset`` allocate nothing. Each block
     *  call processes at most ``STANDARD_BUFFERSIZE`` samples, as many as
     *  its input has. Nothing depends on the sample rate. One thread at a
     *  time.
     */
    class API stft {
    public:
      static constexpr UInt DEFAULT_SIZE = 512;
      static constexpr UInt DEFAULT_OVERLAP = 4;
      static constexpr UInt MINIMUM_SIZE = 16;
      static constexpr UInt MAXIMUM_SIZE = 1u << 16;

      /** @brief A frame size as the class uses it: the power of two at or
       *         above @p size, from ``MINIMUM_SIZE`` to ``MAXIMUM_SIZE``. */
      static UInt fitSize(UInt size);

      /** @brief An overlap as the class uses it with frames of @p size
       *         (fitted): the power of two at or above @p overlap, from 1 to
       *         the size (a hop of at least one sample). */
      static UInt fitOverlap(UInt overlap, UInt size);

      /** @brief What a frame callback is: the frame, and the pointer handed
       *         to ``operator()``. */
      typedef void (*frameFunction)(stftFrame& frame, void* user);

      /** @brief Frames of @p size samples, @p overlap times overlapped, the
       *         first @p offset samples in (modulo the hop), Hann windows on
       *         both sides. Allocates. */
      explicit stft(UInt size = DEFAULT_SIZE, UInt overlap = DEFAULT_OVERLAP,
                    bool fullSpectrum = false, UInt offset = 0);

      UInt size() const {
        return clock.size();
      }
      UInt hop() const {
        return clock.interval();
      }
      UInt overlap() const {
        return clock.size() / clock.interval();
      }
      UInt offset() const {
        return clock.offset();
      }
      UInt bins() const {
        return in.bins();
      }
      bool fullSpectrum() const {
        return in.fullSpectrum();
      }

      /** @brief Samples from a sample's input to its output: ``size()``. */
      UInt latency() const {
        return clock.size();
      }

      /** @brief Frames completed since construction or ``reset()``. */
      U64 frames() const {
        return completed;
      }

      /** @brief The analysis and synthesis windows, from the next frame. */
      void window(stftWindow::TYPE analysis, stftWindow::TYPE synthesis);
      stftWindow::TYPE analysisWindow() const {
        return in.window();
      }
      stftWindow::TYPE synthesisWindow() const {
        return out.window();
      }

      /** @brief A new offset, taken modulo the hop: the frames move along
       *         the stream from now on. */
      void offset(UInt samples) {
        clock.offset(samples);
      }

      /** @brief Back to the start: the input, the output and the frame
       *         count cleared. */
      void reset();

      /** @brief One block, each frame passed on unchanged: the input,
       *         ``latency()`` samples later. */
      buffer& operator()(buffer& input) {
        return process(input, nullptr, nullptr);
      }

      /** @brief One block, @p function called with @p user for every frame
       *         that completes in it. */
      buffer& operator()(buffer& input, frameFunction function, void* user) {
        return process(input, function, user);
      }

      /** @brief One block, @p callable (anything that can be called with an
       *         ``stftFrame&``) called for every frame that completes in it.
       *         Nothing is copied or allocated. */
      template <class Callable> buffer& operator()(buffer& input, Callable& callable) {
        return process(
            input, [](stftFrame& frame, void* user) { (*static_cast<Callable*>(user))(frame); },
            &callable);
      }

      /** @brief The output of the last block. */
      buffer& output() {
        return result;
      }

      /** @brief The two halves, for a process that wants its own frame
       *         loop (several inputs or outputs on one clock, as ``~pfft``
       *         has). */
      stftAnalysis& analysis() {
        return in;
      }
      stftSynthesis& synthesis() {
        return out;
      }

    private:
      buffer& process(buffer& input, frameFunction function, void* user);

      frameClock clock;
      stftAnalysis in;
      stftSynthesis out;
      U64 completed = 0;
      buffer result;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_SPECTRAL_STFT_HPP
