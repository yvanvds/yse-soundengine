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
    class sampleBufferWriter;
    class sampleView;
    class sampleWriteView;

    namespace detail {
      // One sample, loaded and stored as a relaxed atomic (issue #1144). A
      // sampleBufferWriter writes published samples in place while readers
      // and control-side copies read them, possibly on other threads; the
      // relaxed access makes those concurrent reads and writes well defined
      // (and visible to TSan) and costs a plain load or store on every
      // target. No ordering: a reader sees the old value or the new one.
#if defined(__GNUC__) || defined(__clang__)
      inline Flt LoadSample(const Flt* at) {
        Flt value;
        __atomic_load(at, &value, __ATOMIC_RELAXED);
        return value;
      }
      inline void StoreSample(Flt* at, Flt value) {
        __atomic_store(at, &value, __ATOMIC_RELAXED);
      }
#else
      // MSVC: an aligned 32-bit volatile access is a single load or store.
      inline Flt LoadSample(const Flt* at) {
        return *static_cast<const volatile Flt*>(at);
      }
      inline void StoreSample(Flt* at, Flt value) {
        *static_cast<volatile Flt*>(at) = value;
      }
#endif
    } // namespace detail

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
     *  @brief A loop a sound file defines, in sample frames of that file.
     *
     *  Positions are as the file states them: a later ``read`` with a start
     *  offset, a ``crop`` or a ``setSize`` does not move them.
     */
    struct sampleLoop {
      /** @brief Whether the file defines this loop. */
      bool defined = false;
      /** @brief The first frame of the loop. */
      UInt start = 0;
      /** @brief The frame the loop ends at. */
      UInt end = 0;
    };

    /**
     *  @brief The instrument data a sound file carries: an AIFF ``INST``
     *         chunk, or a WAV ``smpl`` chunk.
     *
     *  A WAV ``smpl`` chunk has a base note, a detune and loops but no key
     *  or velocity range and no gain, so those keep the full range (0 to
     *  127) and a gain of 1.
     */
    struct sampleInstrument {
      /** @brief Whether the file has instrument data at all. */
      bool defined = false;
      /** @brief The MIDI note the sample sounds at, unshifted. */
      Int baseNote = 60;
      /** @brief The detune, in cents. */
      Int detune = 0;
      /** @brief The lowest MIDI note the sample is meant for. */
      Int keyLow = 0;
      /** @brief The highest MIDI note the sample is meant for. */
      Int keyHigh = 127;
      /** @brief The lowest MIDI velocity the sample is meant for. */
      Int velocityLow = 0;
      /** @brief The highest MIDI velocity the sample is meant for. */
      Int velocityHigh = 127;
      /** @brief The gain the file states: decibels in AIFF, 1 for WAV. */
      Int gain = 0;
      /** @brief The loop to play while a note is held: the file's first
       *         loop (an AIFF sustain loop). */
      sampleLoop sustainLoop;
      /** @brief The loop to play after a note is released: the file's
       *         second loop (an AIFF release loop). */
      sampleLoop releaseLoop;
    };

    /**
     *  @brief Everything ``sampleBuffer::info`` reports, taken at one
     *         moment: the buffer's shape and the file last read into it.
     *
     *  The shape and the file fields always agree with each other, unlike
     *  separate calls to ``channels``, ``frames`` and ``sampleRate`` made
     *  during a change.
     */
    struct API sampleBufferInfo {
      /** @brief Number of channels. */
      UInt channels = 0;
      /** @brief Length in samples, per channel. */
      UInt frames = 0;
      /** @brief The rate the samples are at, in Hz (never 0). */
      Flt sampleRate = 0.f;
      /** @brief Length in milliseconds at ``sampleRate``. */
      Flt lengthMs = 0.f;
      /** @brief The buffer's ``version`` this snapshot belongs to. */
      std::uint64_t version = 0;

      /**
       *  @brief The path of the file last read into the buffer, as ``read``
       *         was given it; empty when no file was read.
       *
       *  It stays after later edits, and a ``copyFrom`` takes the source's.
       */
      std::string filePath;
      /** @brief How many channels that file has, which may differ from
       *         ``channels``; 0 when no file was read. */
      UInt fileChannels = 0;
      /** @brief That file's instrument data and loops. */
      sampleInstrument instrument;

      /** @brief Whether a file was read into the buffer. */
      bool hasFile() const {
        return !filePath.empty();
      }

      /** @brief The last part of ``filePath``, after its final ``/`` or
       *         ``\``. */
      std::string fileName() const;

      /** @brief @p frameCount frames in milliseconds at ``sampleRate``. */
      Flt framesToMs(UInt frameCount) const;
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
     *  samples at audio rate.
     *
     *  **Writing at audio rate.** A ``sampleBufferWriter`` writes samples
     *  in place, into the storage that is published, without a copy, a
     *  lock or an allocation: what ``~poke``, ``~peek`` and ``~record``
     *  use. Its writes change the samples but not the shape, and do not
     *  count as a change (``version`` stays). Readers see them at once.
     *  A change made while a writer writes wins: what the writer wrote
     *  into the old storage after the change copied it is not in the new
     *  one.
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

      /**
       *  @brief The shape, rate and file details at one moment: what Max's
       *         ``info~`` reports.
       *
       *  The file details are those of the last successful ``read`` or
       *  ``replace`` (or of the source of a ``copyFrom``): its path, its
       *  channel count, and its instrument data and loops. Edits keep
       *  them. Takes the buffer's lock and copies the path, so it belongs
       *  on a control or background thread, never on the audio thread.
       */
      sampleBufferInfo info() const;

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
       *  ``sampleRate`` with the engine rate. The file's path, channel count
       *  and instrument data become what ``info`` reports.
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
       *  @param window The window shape.
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

      /** @brief Take the shape, rate and samples of @p other, and the file
       *         details its ``info`` reports (Max's ``duplicate``). */
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
      friend class sampleBufferWriter;
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
      // The last file read (info()): path, channels, instrument. Under mutex.
      std::string lastFilePath;
      UInt lastFileChannels = 0;
      sampleInstrument lastFileInstrument;
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
       *  @param name     The buffer's name. Any string, including one a
       *                  patch spells as a ``~buffer`` argument.
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
     *
     *  A ``sampleBufferWriter`` may write the samples while you read them.
     *  ``at`` and ``linear`` read each sample as a relaxed atomic, so that
     *  is well defined from any thread. Reading through ``channel`` is a
     *  plain read: fine while every writer of the buffer runs on the same
     *  thread as you (one patcher's objects do), a data race when one runs
     *  on another render thread at the same moment.
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
        return detail::LoadSample(samples + static_cast<std::size_t>(c) * numFrames + frame);
      }

      /** @brief Channel @p c at a fractional position, linearly
       *         interpolated; 0 outside the buffer. */
      Flt linear(UInt c, Dbl position) const;

      /**
       *  @brief Channel @p c at a fractional position, through the 4-point
       *         cubic kernel of ``interpolate4`` (``interpolate4::kernel``).
       *
       *  The position is held inside the buffer: below 0 (or not a number)
       *  it reads the first sample, past the last sample it reads the last
       *  one, and the neighbours the kernel needs past either end repeat
       *  the end sample. Exact at whole positions. 0 for a channel the
       *  buffer lacks or an empty buffer. What the buffer players read with
       *  (``bufferPlayer``, issue #1140).
       */
      Flt cubic(UInt c, Dbl position) const;

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

    /**
     *  @brief Read and write access to a buffer's samples for one block.
     *         Made by ``sampleBufferWriter::pin``.
     *
     *  Writes go into the published storage in place, so every reader of
     *  the buffer sees them at once, and they keep the buffer's shape:
     *  a write outside it is dropped. Every access is a relaxed atomic one,
     *  so readers and other writers on other threads may use the same
     *  samples at the same moment; of two writes to one sample, one wins.
     *  Keep a view for one block at most, as a ``sampleView``.
     */
    class API sampleWriteView {
    public:
      sampleWriteView(sampleWriteView&& other) noexcept;
      sampleWriteView& operator=(sampleWriteView&&) = delete;
      sampleWriteView(const sampleWriteView&) = delete;
      sampleWriteView& operator=(const sampleWriteView&) = delete;
      ~sampleWriteView();

      /** @brief Whether there is nothing to read or write: no buffer
       *         bound, no channels, or no samples. */
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

      /** @brief The buffer's ``version`` this storage belongs to. Writes do
       *         not change it. */
      std::uint64_t version() const {
        return storageVersion;
      }

      /** @brief One sample; 0 outside the buffer. */
      Flt at(UInt c, UInt frame) const {
        if (c >= numChannels || frame >= numFrames) return 0.f;
        return detail::LoadSample(samples + static_cast<std::size_t>(c) * numFrames + frame);
      }

      /** @brief Set one sample. @return false, and nothing written, outside
       *         the buffer. */
      bool set(UInt c, UInt frame, Flt value) {
        if (c >= numChannels || frame >= numFrames) return false;
        detail::StoreSample(samples + static_cast<std::size_t>(c) * numFrames + frame, value);
        return true;
      }

      /**
       *  @brief Write @p count samples into channel @p c from
       *         @p startFrame.
       *
       *  @return How many were written: fewer at the end of the buffer, 0
       *          for a channel it does not have.
       */
      UInt write(UInt c, UInt startFrame, const Flt* values, UInt count);

      /**
       *  @brief Copy up to @p count samples of channel @p c, from
       *         @p startFrame, into @p out.
       *
       *  @return How many were copied: fewer at the end of the buffer, 0 for
       *          a channel it does not have.
       */
      UInt read(UInt c, UInt startFrame, Flt* out, UInt count) const;

    private:
      friend class sampleBufferWriter;
      sampleWriteView() = default;

      std::atomic<const sampleBuffer::storage*>* slot = nullptr;
      Flt* samples = nullptr;
      UInt numChannels = 0;
      UInt numFrames = 0;
      Flt rate = 0.f;
      std::uint64_t storageVersion = 0;
    };

    /**
     *  @brief The way to write a ``sampleBuffer`` at audio rate (issue
     *         #1144): in place, without a copy, a lock or an allocation.
     *
     *  Used like a ``sampleBufferReader``: bind it to a buffer on a control
     *  thread, then call ``pin`` once per block (or once per message) and
     *  read and write through the ``sampleWriteView`` it returns. While the
     *  view exists the storage it shows stays allocated, however the buffer
     *  is changed meanwhile; the next ``pin`` sees the change.
     *
     *  .. code-block:: cpp
     *
     *     // control thread
     *     writer.bind(DSP::sampleBufferRegistry::acquire("loop"));
     *
     *     // audio thread, once per block
     *     DSP::sampleWriteView view = writer.pin();
     *     view.write(0, position, block.getPtr(), block.getLength());
     *
     *  A writer's samples are visible to every reader of the buffer at once,
     *  and a change of the buffer made meanwhile wins (see ``sampleBuffer``).
     *  One writer is one claim, as one reader is: pin it from one thread at
     *  a time, and let the view go before the next ``pin``. ``bind``, the
     *  constructor and the destructor allocate and lock, so they belong on
     *  a control thread, never while a view of this writer exists.
     */
    class API sampleBufferWriter {
    public:
      sampleBufferWriter() = default;
      explicit sampleBufferWriter(std::shared_ptr<sampleBuffer> buffer);
      ~sampleBufferWriter();
      sampleBufferWriter(const sampleBufferWriter&) = delete;
      sampleBufferWriter& operator=(const sampleBufferWriter&) = delete;
      sampleBufferWriter(sampleBufferWriter&&) = delete;
      sampleBufferWriter& operator=(sampleBufferWriter&&) = delete;

      /** @brief Write @p buffer from now on; null unbinds. Control thread. */
      void bind(std::shared_ptr<sampleBuffer> buffer);

      /** @brief The bound buffer, or null. */
      const std::shared_ptr<sampleBuffer>& buffer() const {
        return target;
      }

      /** @brief The buffer's current samples, held until the view goes.
       *         Any thread, one at a time: no allocation, no lock. Empty
       *         when unbound. */
      sampleWriteView pin() const;

    private:
      std::shared_ptr<sampleBuffer> target;
      sampleBuffer::hazard* slot = nullptr;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_SAMPLING_SAMPLEBUFFER_HPP
