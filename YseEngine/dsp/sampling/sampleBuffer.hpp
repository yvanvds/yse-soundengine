/*
  ==============================================================================

    sampleBuffer.hpp
    A shared, named, multichannel sample buffer (issue #1039).

  ==============================================================================
*/

#ifndef YSE_DSP_SAMPLING_SAMPLEBUFFER_HPP
#define YSE_DSP_SAMPLING_SAMPLEBUFFER_HPP

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"

namespace YSE {
  namespace DSP {

    class sampleBufferReader;
    class sampleView;

    /** @brief Window shapes for ``sampleBuffer::applyWindow``. */
    enum class SAMPLE_WINDOW {
      TRIANGLE,
      HAMMING,
      HANNING,
      BLACKMAN,
      WELCH,
      KAISER,
    };

    /** @brief File formats ``sampleBuffer::write`` can produce. */
    enum class SAMPLE_FILE {
      /** From the extension: ``.aif`` / ``.aiff``, ``.flac``, anything else WAV. */
      AUTO,
      /** WAV, 32-bit float. */
      WAV,
      /** AIFF, 32-bit float. */
      AIFF,
      /** FLAC, 24-bit integer. */
      FLAC,
    };

    /**
     *  @brief A multichannel block of samples that many readers share, by
     *         name or by pointer: the memory behind Max's ``buffer~``.
     *
     *  A ``sampleBuffer`` holds some number of channels of the same length,
     *  and the sample rate the samples were recorded at. You change it from
     *  a control or background thread (load a file, resize it, clear it,
     *  normalize it), and you read it from the audio thread through a
     *  ``sampleBufferReader``. A change never stops the audio, and a reader
     *  never sees a half-made change.
     *
     *  **Getting one.** ``sampleBuffer::create()`` makes a private buffer.
     *  ``sampleBufferRegistry::acquire("drums")`` gives the buffer called
     *  ``drums``, creating it if nobody holds it yet. A patch's
     *  ``~buffer drums`` and every sampling object that names ``drums`` use
     *  the same registry, so C++ and patches share buffers both ways.
     *
     *  **Lifetime.** A buffer is held by ``std::shared_ptr``. The registry
     *  only remembers names weakly: a buffer lives while something holds it,
     *  and its name is free again after the last holder lets go.
     *
     *  **Changing it.** Every change builds new storage on the calling
     *  thread, then publishes it in one atomic store. Readers that started
     *  their block before the store finish it on the old storage. The old
     *  storage is freed later, on a control or background thread, once no
     *  reader holds it (see ``collect``). The audio thread never allocates
     *  or frees storage. Changes from several threads are serialised by a
     *  lock that only control and background threads take.
     *
     *  The cost of that model: a change copies the whole buffer, so it
     *  costs memory and time in proportion to its size. That is fine for
     *  loading, resizing and editing. It is not meant for writing a few
     *  samples at audio rate; the recording objects (``~record``,
     *  ``~poke``) get a writer of their own in their own issues.
     *
     *  Every member that changes the buffer, loads or saves a file, or calls
     *  ``collect`` may allocate, lock or touch the disk: never call one from
     *  the audio callback. ``channels``, ``frames``, ``sampleRate``,
     *  ``lengthMs``, ``version``, ``id`` and ``name`` are lock-free and safe
     *  from any thread.
     */
    class API sampleBuffer {
    public:
      /**
       *  @brief A private buffer, not in the registry.
       *
       *  @param channels    Number of channels.
       *  @param frames      Length in samples, per channel. All zero.
       *  @param sampleRate  The rate the samples are at. 0 means the engine
       *                     rate (``SAMPLERATE``), whatever it is when read.
       */
      static std::shared_ptr<sampleBuffer> create(UInt channels = 1, UInt frames = 0,
                                                  Flt sampleRate = 0.f);

      ~sampleBuffer();
      sampleBuffer(const sampleBuffer&) = delete;
      sampleBuffer& operator=(const sampleBuffer&) = delete;
      sampleBuffer(sampleBuffer&&) = delete;
      sampleBuffer& operator=(sampleBuffer&&) = delete;

      // ─── state, from any thread ────────────────────────────────────────────

      /** @brief The registry name, or empty for a private buffer. */
      const std::string& name() const {
        return bufferName;
      }

      /** @brief A number no other buffer in this process has. */
      std::uint64_t id() const {
        return bufferId;
      }

      /** @brief Number of channels. */
      UInt channels() const;

      /** @brief Length in samples, per channel. */
      UInt frames() const;

      /** @brief The rate the samples are at, in Hz: the file's after a load,
       *         else the engine rate. */
      Flt sampleRate() const;

      /** @brief Length in milliseconds at ``sampleRate``. */
      Flt lengthMs() const;

      /**
       *  @brief How many times the buffer has changed. 0 for a buffer that
       *         was never changed after it was created.
       *
       *  A reader can compare it between blocks to notice a new file.
       *  ``channels``, ``frames`` and ``sampleRate`` are read one by one, so
       *  during a change they can briefly disagree with each other. A
       *  ``sampleView`` never does.
       */
      std::uint64_t version() const;

      // ─── files ─────────────────────────────────────────────────────────────

      /**
       *  @brief Read an audio file into the buffer (Max's ``read``).
       *
       *  @param path        The file. Relative paths are relative to the
       *                     working directory. The host's virtual file
       *                     system (``IO()``) is used when it is active.
       *  @param startMs     Where to start reading in the file.
       *  @param durationMs  How much to read. Negative (the default) reads
       *                     to the end of the file and resizes the buffer to
       *                     fit. 0 or more keeps the buffer's length: it is
       *                     cleared, and up to that much is read into its
       *                     start.
       *  @param channels    How many channels the buffer gets. Negative (the
       *                     default) keeps the buffer's count, 0 takes the
       *                     file's. When the file has more channels than the
       *                     buffer, the extra ones are mixed into the buffer's
       *                     channels in turn (file channel ``c`` into buffer
       *                     channel ``c % channels``).
       *  @return false when the file cannot be read; the buffer is then
       *          unchanged.
       *
       *  The buffer takes the file's sample rate. Samples are not resampled:
       *  a reader plays them at the right speed by comparing
       *  ``sampleRate`` with the engine rate.
       */
      bool read(const char* path, Flt startMs = 0.f, Flt durationMs = -1.f, Int channels = -1);

      /**
       *  @brief Read a whole file and take its length and channel count
       *         (Max's ``replace``): ``read(path, 0, -1, 0)``.
       */
      bool replace(const char* path);

      /**
       *  @brief Save the buffer to an audio file, every channel.
       *
       *  @return false when the buffer is empty, the file cannot be written,
       *          or the host's virtual file system is active (it is
       *          read-only).
       */
      bool write(const char* path, SAMPLE_FILE format = SAMPLE_FILE::AUTO) const;

      // ─── shape ─────────────────────────────────────────────────────────────

      /**
       *  @brief Set the length in samples and, when @p channels is 0 or
       *         more, the channel count (Max's ``sizeinsamps``).
       *
       *  What fits in the new shape is kept. New samples are zero.
       */
      void setSize(UInt frames, Int channels = -1);

      /** @brief ``setSize`` in milliseconds at ``sampleRate`` (Max's
       *         ``setsize``). */
      void setSizeMs(Flt ms, Int channels = -1);

      /** @brief Set the rate the samples are at, without changing them. 0
       *         means the engine rate. */
      void setSampleRate(Flt rate);

      /**
       *  @brief Keep only the samples from @p startFrame up to (not
       *         including) @p endFrame (Max's ``crop``).
       *
       *  @return false, and no change, when the range holds no samples.
       */
      bool crop(UInt startFrame, UInt endFrame);

      /** @brief ``crop`` in milliseconds. */
      bool cropMs(Flt startMs, Flt endMs);

      // ─── contents ──────────────────────────────────────────────────────────

      /** @brief Set every sample to 0. */
      void clear();

      /** @brief Set every sample to @p value. */
      void fill(Flt value);

      /**
       *  @brief Fill every channel with @p cycles / @p divisor cycles of a
       *         sine, or a cosine (Max's ``fill sin`` and ``fill cos``).
       */
      void fillSine(Flt cycles = 1.f, Flt divisor = 1.f, bool cosine = false);

      /**
       *  @brief Multiply every channel by a window (Max's ``apply``).
       *
       *  @param half   Use only the second half of the window, stretched
       *                over the buffer: a fade out.
       *  @param beta   The Kaiser window's beta. Ignored by the others.
       */
      void applyWindow(SAMPLE_WINDOW window, bool half = false, Flt beta = 6.f);

      /** @brief Multiply every sample by @p gain. */
      void applyGain(Flt gain);

      /** @brief Add @p offset to every sample. */
      void applyOffset(Flt offset);

      /** @brief Replace every sample by its difference from the one before
       *         (the first sample is kept). */
      void applyDeltas();

      /**
       *  @brief Scale every sample so that the largest absolute value
       *         becomes @p peak. A silent buffer stays silent.
       */
      void normalize(Flt peak = 1.f);

      /** @brief Take the shape, rate and samples of @p other (Max's
       *         ``duplicate``). */
      void copyFrom(const sampleBuffer& other);

      /**
       *  @brief Write @p count samples into channel @p channel, starting at
       *         @p startFrame. Samples that fall outside the buffer are
       *         dropped; the buffer does not grow.
       */
      void setSamples(UInt channel, UInt startFrame, const Flt* values, UInt count);

      /**
       *  @brief Copy up to @p count samples of channel @p channel, from
       *         @p startFrame, into @p out.
       *
       *  @return How many were copied: fewer at the end of the buffer, 0 for
       *          a channel it does not have.
       */
      UInt getSamples(UInt channel, UInt startFrame, Flt* out, UInt count) const;

      // ─── memory ────────────────────────────────────────────────────────────

      /**
       *  @brief Free old storage that no reader holds any more.
       *
       *  Every change and every reader that lets go of the buffer already
       *  calls it. Storage a reader was still holding at that moment waits
       *  for the next call; ``system::update()`` makes one for every buffer
       *  that has such storage (``sampleBufferRegistry::collect``). Call it
       *  yourself when you run the engine without ``update()``.
       */
      void collect();

      /** @brief How many old storages are waiting for ``collect``.
       *         Diagnostics and tests. */
      std::size_t retiredCount() const;

      /** @brief The internal storage block. Not part of the API. */
      struct storage;

    private:
      friend class sampleBufferReader;
      friend class sampleBufferRegistry;

      // One reader's claim on a storage: the reader writes it on the audio
      // thread, collect() reads it on the control thread.
      struct hazard {
        std::atomic<const storage*> held{nullptr};
      };

      sampleBuffer(std::string name, std::uint64_t id, UInt channels, UInt frames, Flt sampleRate);

      // Control side. Both take `mutex`.
      std::shared_ptr<const storage> snapshot() const;
      void publish(std::shared_ptr<storage> next);

      // Under `mutex`.
      void publishLocked(std::shared_ptr<storage> next);
      void collectLocked();

      // Copy the current storage, let `edit` change the copy, publish it.
      template <class Edit> void edit(Edit&& change);

      hazard* attach();
      void detach(hazard* slot);

      const std::string bufferName;
      const std::uint64_t bufferId;

      mutable std::mutex mutex;
      std::shared_ptr<const storage> owner; // under mutex
      std::vector<std::shared_ptr<const storage>> retired; // under mutex
      std::vector<std::unique_ptr<hazard>> hazards; // under mutex

      // What the audio thread reads.
      std::atomic<const storage*> published{nullptr};

      std::atomic<UInt> shapeChannels{0};
      std::atomic<UInt> shapeFrames{0};
      std::atomic<Flt> shapeRate{0.f};
      std::atomic<std::uint64_t> changes{0};
    };

    /**
     *  @brief The process-wide names of ``sampleBuffer`` objects.
     *
     *  One flat namespace, shared by C++ and by every patcher: the
     *  ``~buffer drums`` in one patch, a ``~groove drums`` in another (or in
     *  a ``~poly`` voice), and a host's ``acquire("drums")`` all mean the
     *  same buffer. Names are not scoped to a patcher, unlike ``.coll`` or
     *  ``.array``, because a sample buffer is an asset that patches and the
     *  host load once and share. All functions lock: control and background
     *  threads only.
     */
    class API sampleBufferRegistry {
    public:
      /**
       *  @brief The buffer called @p name, created empty (one channel, no
       *         samples) when nobody holds it.
       *
       *  @param created  When not null, set to whether this call created it.
       */
      static std::shared_ptr<sampleBuffer> acquire(const std::string& name,
                                                   bool* created = nullptr);

      /** @brief The buffer called @p name, or null when nobody holds it. */
      static std::shared_ptr<sampleBuffer> find(const std::string& name);

      /** @brief The buffer with this ``id``, named or not, or null when it
       *         no longer exists. */
      static std::shared_ptr<sampleBuffer> findById(std::uint64_t id);

      /** @brief How many named buffers exist. */
      static std::size_t count();

      /** @brief ``sampleBuffer::collect`` on every buffer that has old
       *         storage waiting. ``system::update()`` calls it. */
      static void collect();
    };

    /**
     *  @brief A view of a buffer's samples that stays valid while it
     *         exists. Made by ``sampleBufferReader::pin``.
     *
     *  Channels are stored one after another (``channel(c)`` points at
     *  ``frames()`` samples). Keep a view for one block at most: while you
     *  hold it, the storage it shows cannot be freed.
     */
    class API sampleView {
    public:
      sampleView(sampleView&& other) noexcept;
      sampleView& operator=(sampleView&&) = delete;
      sampleView(const sampleView&) = delete;
      sampleView& operator=(const sampleView&) = delete;
      ~sampleView();

      /** @brief Whether there is nothing to read: no buffer bound, no
       *         channels, or no samples. */
      bool empty() const {
        return numFrames == 0 || numChannels == 0;
      }

      /** @brief Number of channels. */
      UInt channels() const {
        return numChannels;
      }

      /** @brief Length in samples, per channel. */
      UInt frames() const {
        return numFrames;
      }

      /** @brief The rate the samples are at, in Hz (never 0). */
      Flt sampleRate() const {
        return rate;
      }

      /** @brief The buffer's ``version`` this storage belongs to. */
      std::uint64_t version() const {
        return storageVersion;
      }

      /** @brief The samples of channel @p c, or null when there is no such
       *         channel. */
      const Flt* channel(UInt c) const {
        return c < numChannels ? samples + static_cast<std::size_t>(c) * numFrames : nullptr;
      }

      /** @brief One sample; 0 outside the buffer. */
      Flt at(UInt c, UInt frame) const {
        if (c >= numChannels || frame >= numFrames) return 0.f;
        return samples[static_cast<std::size_t>(c) * numFrames + frame];
      }

      /** @brief Channel @p c at a fractional position, linearly
       *         interpolated; 0 outside the buffer. */
      Flt linear(UInt c, Dbl position) const;

    private:
      friend class sampleBufferReader;
      sampleView() = default;

      std::atomic<const sampleBuffer::storage*>* slot = nullptr;
      const Flt* samples = nullptr;
      UInt numChannels = 0;
      UInt numFrames = 0;
      Flt rate = 0.f;
      std::uint64_t storageVersion = 0;
    };

    /**
     *  @brief The way to read a ``sampleBuffer`` on the audio thread.
     *
     *  Bind it to a buffer on a control thread, then call ``pin`` once per
     *  block on the audio thread and read through the ``sampleView`` it
     *  returns. Pinning allocates nothing and takes no lock; while the view
     *  exists the storage it shows stays allocated, however the buffer is
     *  changed meanwhile. The next block's ``pin`` sees the change.
     *
     *  .. code-block:: cpp
     *
     *     // control thread
     *     reader.bind(DSP::sampleBufferRegistry::acquire("drums"));
     *
     *     // audio thread, once per block
     *     DSP::sampleView view = reader.pin();
     *     if (view.empty()) { out = 0.f; return; }
     *     const Flt* left = view.channel(0);
     *
     *  One reader is one claim: pin it from one thread at a time, and let
     *  the view go before the next ``pin``. Each object that reads gets its
     *  own reader. ``bind``, the constructor and the destructor allocate and
     *  lock, so they belong on a control thread, never while a view of this
     *  reader exists.
     */
    class API sampleBufferReader {
    public:
      sampleBufferReader() = default;
      explicit sampleBufferReader(std::shared_ptr<sampleBuffer> buffer);
      ~sampleBufferReader();
      sampleBufferReader(const sampleBufferReader&) = delete;
      sampleBufferReader& operator=(const sampleBufferReader&) = delete;
      sampleBufferReader(sampleBufferReader&&) = delete;
      sampleBufferReader& operator=(sampleBufferReader&&) = delete;

      /** @brief Read @p buffer from now on; null unbinds. Control thread. */
      void bind(std::shared_ptr<sampleBuffer> buffer);

      /** @brief The bound buffer, or null. */
      const std::shared_ptr<sampleBuffer>& buffer() const {
        return target;
      }

      /** @brief The buffer's current samples, held until the view goes.
       *         Audio thread: no allocation, no lock. Empty when unbound. */
      sampleView pin() const;

    private:
      std::shared_ptr<sampleBuffer> target;
      sampleBuffer::hazard* slot = nullptr;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_SAMPLING_SAMPLEBUFFER_HPP
