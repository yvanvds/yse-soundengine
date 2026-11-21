/*
  ==============================================================================

    fileRecorder.hpp
    Records signals to a sound file on disk (issue #1156).

  ==============================================================================
*/

#ifndef YSE_DSP_SAMPLING_FILERECORDER_HPP
#define YSE_DSP_SAMPLING_FILERECORDER_HPP

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "../../headers/constants.hpp"
#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief Records signals to a sound file on disk, as they play: what
     *         the patcher's ``~sfrecord`` does.
     *
     *  ### Two threads
     *
     *  The audio thread never touches the disk. Each block it copies the
     *  inputs into a ring of ``ringFrames`` frames allocated with the
     *  recorder, and ``write``, called on a background thread, moves what
     *  the ring holds into the file. The two meet through the ring's two
     *  counters and a small queue of marks (a recording's start and its
     *  end), without a lock. When the ring is full, because ``write`` was not
     *  called soon enough, the frames that do not fit are dropped and
     *  counted (``dropped``); the audio thread never waits. ``pending()``
     *  tells the background side that there is something to write.
     *
     *  ### A file per recording
     *
     *  ``open`` creates the file (overwriting one of that name) with the
     *  sample type, file channels and byte order set before it. A recording
     *  then starts on the audio thread (``record``, ``loop``) and fills the
     *  file; when it stops, the next ``write`` finishes the file and closes
     *  it, as Max's ``sfrecord~`` closes its file on a stop. Recording again
     *  takes another ``open``. Stopping while nothing records closes the
     *  open file too, empty. ``close`` finishes the file from the control
     *  side at once: what was recorded until then is kept and a recording
     *  still running is cut. ``open`` while a file is open closes it first.
     *
     *  - **Starting.** ``record(true)`` records until ``record(false)``;
     *    ``recordFor(ms)`` for that long. ``loop(ms)`` records until stopped,
     *    keeping the last @p ms: the file holds that span, rewritten in a
     *    circle, and with ``sortLoop`` on it is put in order when it is
     *    finished, so it starts with the oldest frame. A loop needs a file
     *    that can be rewritten in place: not FLAC or OGG, for which it does
     *    not start. A start without an open file does nothing.
     *  - **Ramps.** ``beginRamp`` fades a recording in over its first ms
     *    (gain ``k / n`` at frame ``k`` of ``n``), ``endRamp`` fades it out
     *    over its last (the last frame at 0). A stop with an end ramp records
     *    on for the length of the ramp; a timed recording fades within its
     *    length.
     *  - **Time.** The block ``operator()`` returns holds, at each sample,
     *    how long the recording has run, in ms at the live ``SAMPLERATE``:
     *    0 at its first frame. After a stop it holds the recording's length.
     *  - **Rate.** The file takes the engine's sample rate when the
     *    recording starts (the file ``open`` made is made again if the rate
     *    has changed since). Ramps and durations are turned into frames at
     *    the rate of that moment.
     *
     *  ### The file
     *
     *  Written through libsndfile, the library ``sampleBuffer::write`` and
     *  every file the engine reads go through. The type comes from the
     *  extension unless it is given: ``.wav``, ``.flac``, ``.ogg``, ``.au``,
     *  ``.caf``, ``.w64``, ``.raw`` / ``.pcm``, and AIFF for anything else.
     *  FLAC holds 8, 16 or 24-bit integers and takes 24 bits for another
     *  sample type; OGG is Vorbis and takes none. Integer samples are
     *  quantized here, by ``quantization`` (round or floor) after
     *  ``dither`` (triangular, optionally with first-order noise shaping),
     *  and clipped to the type's range. ``fileChannels`` other than the
     *  input count drops the inputs past it or writes silence in the
     *  channels past them.
     *
     *  .. code-block:: cpp
     *
     *     // control thread
     *     DSP::fileRecorder rec(2);
     *     rec.sampleType(DSP::fileRecorder::SAMPLE_TYPE::INT24);
     *     rec.open("take.wav");
     *
     *     // audio thread, once per block
     *     rec.record(true);                 // from this block on
     *     DSP::buffer* in[] = {&left, &right};
     *     rec(in, 2);
     *
     *     // a background thread, now and then
     *     if (rec.pending()) rec.write();
     *
     *  Every audio-thread call allocates nothing, takes no lock and blocks
     *  on no I/O. ``open``, ``close``, ``write`` and ``resize`` take one lock
     *  among themselves and touch the disk: control and background threads
     *  only. The setters marked so are atomics any thread may call. The
     *  audio-thread calls belong to one thread at a time.
     */
    class API fileRecorder {
    public:
      /** @brief The most inputs, and file channels. */
      static constexpr UInt MAX_CHANNELS = 64;
      /** @brief The ring's length when none is given, in frames. */
      static constexpr UInt DEFAULT_RING_FRAMES = 65536;
      /** @brief The shortest ring: a shorter one is made this long. */
      static constexpr UInt MIN_RING_FRAMES = 4096;

      /** @brief The kind of file. */
      enum class FILE_TYPE : std::uint8_t { AUTO, AIFF, WAV, FLAC, OGG, AU, CAF, W64, RAW };

      /** @brief How each sample is stored. */
      enum class SAMPLE_TYPE : std::uint8_t {
        INT8,
        INT16,
        INT24,
        INT32,
        FLOAT32,
        FLOAT64,
        MULAW,
        ALAW
      };

      /** @brief What is added before integer samples are quantized. */
      enum class DITHER : std::uint8_t {
        /** Nothing. */
        OFF,
        /** Triangular noise of one step either way (TPDF). */
        TRIANGULAR,
        /** The same, with the quantization error fed back (first order). */
        NOISE_SHAPED
      };

      /** @brief How integer samples are quantized. */
      enum class QUANTIZE : std::uint8_t {
        /** To the nearest step. */
        ROUND,
        /** To the step below. */
        FLOOR
      };

      /** @brief A recorder for @p inputs inputs (1 to ``MAX_CHANNELS``)
       *         with a ring of @p ringFrames frames. Allocates. */
      explicit fileRecorder(UInt inputs = 1, UInt ringFrames = DEFAULT_RING_FRAMES);
      /** @brief Finishes and closes an open file, keeping what was
       *         recorded. Not on the audio thread. */
      ~fileRecorder();
      fileRecorder(const fileRecorder&) = delete;
      fileRecorder& operator=(const fileRecorder&) = delete;
      fileRecorder(fileRecorder&&) = delete;
      fileRecorder& operator=(fileRecorder&&) = delete;

      // ─── control and background threads ──────────────────────────────────

      /** @brief Another input count and ring length; closes an open file.
       *         Allocates: before the audio thread sees the recorder. */
      fileRecorder& resize(UInt inputs, UInt ringFrames = DEFAULT_RING_FRAMES);
      /** @brief The number of inputs. */
      UInt inputs() const {
        return width;
      }
      /** @brief The ring's length in frames. */
      UInt ringFrames() const {
        return capacity;
      }

      /** @brief How the next ``open`` stores samples (default 16-bit
       *         integers). Any thread. */
      fileRecorder& sampleType(SAMPLE_TYPE type) {
        nextType.store(static_cast<std::uint8_t>(type), std::memory_order_relaxed);
        return *this;
      }
      SAMPLE_TYPE sampleType() const {
        return static_cast<SAMPLE_TYPE>(nextType.load(std::memory_order_relaxed));
      }
      /** @brief Whether the next raw file ``open`` makes has the byte order
       *         opposite to this machine's. Any thread. */
      fileRecorder& byteSwap(bool on) {
        nextSwap.store(on, std::memory_order_relaxed);
        return *this;
      }
      bool byteSwap() const {
        return nextSwap.load(std::memory_order_relaxed);
      }
      /** @brief The channels of the next file ``open`` makes, 1 to
       *         ``MAX_CHANNELS``; 0 (the default) is the input count. Any
       *         thread. */
      fileRecorder& fileChannels(UInt channels) {
        nextChannels.store(channels > MAX_CHANNELS ? MAX_CHANNELS : channels,
                           std::memory_order_relaxed);
        return *this;
      }
      UInt fileChannels() const {
        return nextChannels.load(std::memory_order_relaxed);
      }
      /** @brief The dither for integer samples, from the next ``write``.
       *         Any thread. */
      fileRecorder& dither(DITHER mode) {
        ditherMode.store(static_cast<std::uint8_t>(mode), std::memory_order_relaxed);
        return *this;
      }
      DITHER dither() const {
        return static_cast<DITHER>(ditherMode.load(std::memory_order_relaxed));
      }
      /** @brief The quantization of integer samples, from the next
       *         ``write``. Any thread. */
      fileRecorder& quantization(QUANTIZE rule) {
        quantizeRule.store(static_cast<std::uint8_t>(rule), std::memory_order_relaxed);
        return *this;
      }
      QUANTIZE quantization() const {
        return static_cast<QUANTIZE>(quantizeRule.load(std::memory_order_relaxed));
      }
      /** @brief Whether a loop recording is put in order when its file is
       *         finished. Any thread. */
      fileRecorder& sortLoop(bool on) {
        sorting.store(on, std::memory_order_relaxed);
        return *this;
      }
      bool sortLoop() const {
        return sorting.load(std::memory_order_relaxed);
      }

      /**
       *  @brief Create @p path to record into, of @p type (``AUTO``: by the
       *         extension), closing an open file first.
       *
       *  False, with no file open, when it cannot be created: an empty
       *  name, a folder that is not there, a type that does not take the
       *  sample type, or the host's virtual file system installed (it reads
       *  only). Blocks on the disk.
       */
      bool open(const std::string& path, FILE_TYPE type = FILE_TYPE::AUTO);
      /** @brief Write what was recorded and close the open file, cutting a
       *         recording that still runs. False when no file was open.
       *         Blocks on the disk. */
      bool close();
      /** @brief The open file, empty for none. */
      std::string file() const;
      /** @brief Whether a file is open: one ``open`` made and no stop has
       *         closed yet. Wait-free; any thread. */
      bool isOpen() const {
        return armed.load(std::memory_order_acquire) != 0;
      }

      /**
       *  @brief Move what the ring holds into the file, and finish the file
       *         after a stop. Returns the frames written.
       *
       *  Blocks on the disk. A write the disk refuses (it is full, say)
       *  closes the file with what it holds, and the recording stops at the
       *  next block (``failures`` counts it).
       */
      UInt write();
      /** @brief Whether ``write`` has something to do. Wait-free; any
       *         thread. */
      bool pending() const;
      /** @brief Frames the files hold, of the file open or the last one
       *         closed. */
      Long framesWritten() const;
      /** @brief Files that could not be written to the end. Any thread. */
      std::uint64_t failures() const {
        return failed.load(std::memory_order_relaxed);
      }

      // ─── the audio thread ─────────────────────────────────────────────────

      /** @brief Start recording into the open file, from this block, until
       *         stopped; or stop (closing the open file when nothing
       *         records). */
      fileRecorder& record(bool on);
      /** @brief Start recording for @p ms, then stop. 0 or less: until
       *         stopped. */
      fileRecorder& recordFor(Flt ms);
      /** @brief Start recording until stopped, keeping the last @p ms. */
      fileRecorder& loop(Flt ms);
      /** @brief The fade-in at the start of a recording, in ms. */
      fileRecorder& beginRamp(Flt ms) {
        rampIn = ms > 0.f ? ms : 0.f;
        return *this;
      }
      Flt beginRamp() const {
        return rampIn;
      }
      /** @brief The fade-out at the end of a recording, in ms. */
      fileRecorder& endRamp(Flt ms) {
        rampOut = ms > 0.f ? ms : 0.f;
        return *this;
      }
      Flt endRamp() const {
        return rampOut;
      }

      /** @brief One block of one input. Returns the time output. */
      buffer& operator()(buffer& in);
      /**
       *  @brief One block of @p count inputs, input ``i`` into file
       *         channel ``i``. Returns the time output.
       *
       *  The block is as long as the shortest input, up to
       *  ``STANDARD_BUFFERSIZE``. Inputs past ``inputs()`` are not recorded;
       *  inputs it has and @p count does not give record silence. None may
       *  be null.
       */
      buffer& operator()(buffer* const* inputs, UInt count);

      /** @brief The time output of the last block. */
      buffer& out() {
        return clock;
      }
      /** @brief Whether a recording runs, fading out included. */
      bool recording() const {
        return state != STATE::IDLE;
      }
      /** @brief Frames of the current (or last) recording dropped because
       *         the ring was full. Wait-free; any thread. */
      std::uint64_t dropped() const {
        return lost.load(std::memory_order_relaxed);
      }

    private:
      // A start or the end of a recording, at a frame of the ring.
      struct mark {
        enum KIND : std::uint8_t { START, STOP };
        KIND kind = START;
        std::uint32_t session = 0;
        std::uint64_t frame = 0; // the ring's frame count where it applies
        Long loopFrames = 0; // a loop's length; 0 none
        UInt rate = 0; // the engine's rate at a start
      };
      static constexpr std::uint32_t MARKS = 64;

      // Control side, under `edit`.
      bool OpenLocked(const std::string& path, FILE_TYPE type);
      UInt WriteLocked();
      bool Reopen(UInt rate);
      void Finish();
      void Rotate();
      UInt Store(std::uint64_t from, UInt frames);
      void Apply(const mark& m);

      // Audio side.
      enum class STATE : std::uint8_t { IDLE, RECORDING };
      void Start(Flt ms, Flt loopMs);
      void Stop();
      bool PushMark(mark::KIND kind, std::uint32_t which, Long loopFrames, UInt rate);

      mutable std::mutex edit;

      // The ring: `width` samples a frame, frames interleaved. The audio
      // thread writes frames and counts them in `head`; `write` reads them
      // and counts them in `tail`.
      UInt width = 1;
      UInt capacity = DEFAULT_RING_FRAMES;
      std::vector<Flt> ring;
      std::atomic<std::uint64_t> head{0};
      std::atomic<std::uint64_t> tail{0};
      mark marks[MARKS];
      std::atomic<std::uint32_t> markHead{0};
      std::atomic<std::uint32_t> markTail{0};

      // The open file's session, 0 for none: set by `open`, cleared when the
      // file is closed. Each session records once.
      std::atomic<std::uint32_t> armed{0};
      std::atomic<bool> rewritable{false};
      std::atomic<std::uint64_t> failed{0};
      std::atomic<std::uint64_t> lost{0};

      // Settings any thread writes.
      std::atomic<std::uint8_t> nextType{static_cast<std::uint8_t>(SAMPLE_TYPE::INT16)};
      std::atomic<bool> nextSwap{false};
      std::atomic<UInt> nextChannels{0};
      std::atomic<std::uint8_t> ditherMode{0};
      std::atomic<std::uint8_t> quantizeRule{0};
      std::atomic<bool> sorting{false};

      // The writer's, under `edit`.
      struct writer;
      std::unique_ptr<writer> disk;
      std::uint32_t sessions = 0;

      // The audio thread's.
      buffer clock;
      STATE state = STATE::IDLE;
      std::uint32_t session = 0; // of the recording that runs
      std::uint32_t used = 0; // the last session recorded into
      bool stopOwed = false; // a STOP the mark queue had no room for
      std::uint32_t stopSession = 0;
      Long elapsed = 0; // frames recorded
      Long remaining = -1; // frames until the stop; -1 none
      Long fadeIn = 0; // the begin ramp in frames
      Long fadeOut = 0; // the end ramp in frames
      Flt rampIn = 0.f;
      Flt rampOut = 0.f;
      // A request, applied at the top of the next block.
      enum class REQUEST : std::uint8_t { NONE, START, STOP };
      REQUEST request = REQUEST::NONE;
      Flt requestMs = 0.f;
      Flt requestLoopMs = 0.f;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_SAMPLING_FILERECORDER_HPP
