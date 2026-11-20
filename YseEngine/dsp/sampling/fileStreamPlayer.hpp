/*
  ==============================================================================

    fileStreamPlayer.hpp
    Plays sound files streamed from disk, with cues (issue #1155).

  ==============================================================================
*/

#ifndef YSE_DSP_SAMPLING_FILESTREAMPLAYER_HPP
#define YSE_DSP_SAMPLING_FILESTREAMPLAYER_HPP

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
#include "../delay/pitchShifter.hpp"
#include "playerOutputs.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief A table of cues: numbered spans of sound files that a
     *         ``fileStreamPlayer`` plays. What the patcher's ``~sflist``
     *         holds, and what every ``~sfplay`` keeps for its own cues.
     *
     *  A cue is a number from ``FIRST_CUE`` (2; cue 1 is the player's open
     *  file) to ``LAST_CUE``, a file, a span of it in milliseconds and a
     *  speed relative to the player's. A table only describes cues; a player
     *  using it opens a stream of its own for each (``fileStreamPlayer::sync``).
     *
     *  Tables are shared by name: ``acquire("strings")`` gives every caller
     *  the same table while one holds it, and ``acquire("")`` a new, unnamed
     *  one. ``version()`` counts the changes, so a player can tell that its
     *  streams no longer match.
     *
     *  ``preload`` opens the file's header to check it and to convert the
     *  span, so it blocks on the disk: control and background threads only.
     *  Every call is thread-safe; the edits take a lock.
     */
    class API fileStreamCues {
    public:
      /** @brief The first cue number a table holds. */
      static constexpr Int FIRST_CUE = 2;
      /** @brief The last. */
      static constexpr Int LAST_CUE = 32767;

      /** @brief One cue. ``endMs`` 0 or below is the end of the file;
       *         ``startFrame`` and ``endFrame`` are the span in the file's
       *         frames, at its own rate. */
      struct cue {
        Int number = 0;
        std::string file;
        Flt startMs = 0.f;
        Flt endMs = 0.f;
        Flt speed = 1.f;
        Long startFrame = 0;
        Long endFrame = 0;
      };

      /** @brief The table called @p name, made when nobody holds it; a new,
       *         unnamed table for an empty name. */
      static std::shared_ptr<fileStreamCues> acquire(const std::string& name);
      /** @brief The table called @p name while somebody holds it, else
       *         null. */
      static std::shared_ptr<fileStreamCues> find(const std::string& name);

      ~fileStreamCues();
      fileStreamCues(const fileStreamCues&) = delete;
      fileStreamCues& operator=(const fileStreamCues&) = delete;
      fileStreamCues(fileStreamCues&&) = delete;
      fileStreamCues& operator=(fileStreamCues&&) = delete;

      /** @brief The name, empty for an unnamed table. */
      const std::string& name() const {
        return tableName;
      }

      /** @brief Make @p file the table's open file, the one ``preload``
       *         uses when it names none. False, and no change, when it
       *         cannot be opened. Blocks on the disk. */
      bool open(const std::string& file);
      /** @brief The open file, empty for none. */
      std::string file() const;

      /**
       *  @brief Define cue @p number as @p file from @p startMs to @p endMs
       *         at @p speed, replacing a cue of that number. An empty
       *         @p file is the open file.
       *
       *  An end of 0 or below is the end of the file. With @p normalized,
       *  the start and end are fractions of the file (0 to 1; an end of 0 is
       *  the end). The span is held inside the file. False, and no change,
       *  when the number is outside ``FIRST_CUE .. LAST_CUE``, the file
       *  cannot be opened, the span holds nothing (an end at or before the
       *  start), or the speed is not a number above 0. Blocks on the disk.
       */
      bool preload(Int number, const std::string& file, Flt startMs = 0.f, Flt endMs = 0.f,
                   Flt speed = 1.f, bool normalized = false);
      /** @brief Remove cue @p number; false when there is none. */
      bool remove(Int number);
      /** @brief Remove every cue of @p file, and forget it as the open
       *         file; the number of cues removed. */
      UInt close(const std::string& file);
      /** @brief Remove every cue; the number removed. */
      UInt clear();

      /** @brief The cues, by number. A copy. */
      std::vector<cue> cues() const;
      /** @brief How many cues there are. */
      UInt count() const;
      /** @brief Counts the changes: one more after each that changed
       *         something. Wait-free; any thread. */
      std::uint64_t version() const {
        return changes.load(std::memory_order_acquire);
      }

    private:
      explicit fileStreamCues(std::string name);
      void Changed();

      const std::string tableName;
      mutable std::mutex mutex;
      std::vector<cue> table; // by number
      std::string openFile;
      std::atomic<std::uint64_t> changes{0};
    };

    /**
     *  @brief Plays sound files streamed from disk, without loading them
     *         into memory: cues, speed, looping, pausing, pitch shifting and
     *         time stretching. What the patcher's ``~sfplay`` does.
     *
     *  ### The stream is the engine's
     *
     *  Each cue plays through the same stream a streaming ``YSE::sound``
     *  uses (``INTERNAL::abstractSoundFile``): two buffers of
     *  ``STREAM_BUFFERSIZE`` frames, one played on the audio thread while
     *  the slow pool fills the other from disk and hands it over without a
     *  lock. The audio thread never touches the disk. Opening a cue opens
     *  the file and fills its first buffer from the cue's start (what Max
     *  calls preloading), so a cue starts at once. A stream reads
     *  forward, without interpolation, and fades in over 200 samples when it
     *  starts or resumes and out over 200 when it is stopped or paused, as a
     *  ``YSE::sound`` does.
     *
     *  ### Cues
     *
     *  Cue 1 is the file ``open`` opened, played whole (or the span a
     *  ``seek`` gave). Cues 2 and up come from a ``fileStreamCues`` table:
     *  the player's own unless ``cueList`` gave it a shared one. ``preload``
     *  and ``clear`` edit that table and ``sync`` it; a table edited by
     *  somebody else is followed by calling ``sync`` (``stale()`` tells).
     *  ``sync`` opens a stream for each new or changed cue and keeps the
     *  others, so a cue that plays goes on playing.
     *
     *  The control calls (``open``, ``close``, ``preload``, ``clear``,
     *  ``cueList``, ``sync``, ``collect``, ``channels``) open files and
     *  allocate: control and background threads only, never the audio
     *  thread. They take one lock among themselves. What they make is handed
     *  to the audio thread whole, through one atomic slot, and taken at the
     *  top of the next block; the streams the audio thread let go are freed
     *  by the next control call or by ``collect``.
     *
     *  ### Playing (the audio thread)
     *
     *  ``play(cue)`` starts a cue from its start: 0 stops, 1 is the open
     *  file, and a cue the player has no stream for is ignored. A list plays
     *  its cues one after the other, skipping the ones it has no stream for,
     *  each from the block after the last one ended. Switching cues fades
     *  the old one out over 200 samples while the new one fades in;
     *  restarting the cue that plays starts it again once its stream has
     *  read its start back from disk (a block or two of silence).
     *  ``seek(start, end)`` plays the open file from @p start to @p end ms;
     *  it is silent until the slow pool has read from there. ``pause`` and
     *  ``resume`` hold and go on. All of these take effect at the top of the
     *  next block, after a new set of streams is taken.
     *
     *  - **The end.** A cue that reaches its end stops, or with loop on goes
     *    on from its start without a gap (the disk fill wraps). ``loop``
     *    loops every cue, ``loopOne`` only the cue of that number.
     *    ``done()`` is true for the block in which playback stopped: a cue
     *    or the last cue of a list reached its end, or ``stop`` faded it
     *    out. Not when another cue took over, and not on ``pause``.
     *  - **Speed.** ``speed`` times the cue's own speed, at the file's rate
     *    against the live ``SAMPLERATE``, read every block. A stream does not
     *    play backward: a speed below 0 is 0, which holds the sample it is
     *    on.
     *  - **Channels.** Output ``k`` plays file channel ``k``. File channels
     *    past the last output are not played. Outputs past the file's
     *    channels are silent, or with ``modout`` on repeat them: output
     *    ``k`` plays file channel ``k % channels``.
     *  - **Pitch.** ``pitchShift`` (a ratio) and ``pitchShiftCents`` shift
     *    every output with a ``DSP::pitchShifter`` in ``quality``, keeping
     *    the duration. With ``timeStretch`` on the shifter also undoes the
     *    speed's change of pitch, so the speed changes only the tempo (up to
     *    two octaves either way, the shifter's range). While none of the
     *    three is set the shifters are off: no latency, the stream as it is.
     *    Turning them on starts them afresh, silent for their latency.
     *  - ``position()`` is where the cue that plays is, in ms into its file,
     *    per sample of the last block (0 while stopped).
     *
     *  .. code-block:: cpp
     *
     *     // control thread
     *     DSP::fileStreamPlayer player(2);
     *     player.open("speech.wav");
     *     player.preload(2, "speech.wav", 1500.f, 3000.f);
     *     player.play(2);
     *
     *     // audio thread, once per block
     *     player();
     *     DSP::buffer& left = player.out(0);
     *     if (player.done()) { ... }
     *
     *  Every audio-thread call allocates nothing, takes no lock and blocks
     *  on no I/O. The outputs are ``STANDARD_BUFFERSIZE`` long; a shorter
     *  block resizes them down without allocating. Not thread-safe beyond
     *  what is said above: the audio-thread calls belong to one thread at a
     *  time.
     */
    class API fileStreamPlayer {
    public:
      /** @brief The most outputs. */
      static constexpr UInt MAX_CHANNELS = playerOutputs::MAX_CHANNELS;
      /** @brief The most cues a list plays. */
      static constexpr UInt MAX_LIST = 128;

      /** @brief A stopped player with @p outputs outputs (1 to
       *         ``MAX_CHANNELS``) and a cue table of its own. Allocates. */
      explicit fileStreamPlayer(UInt outputs = 1);
      /** @brief Frees every stream. Not on the audio thread. */
      ~fileStreamPlayer();
      fileStreamPlayer(const fileStreamPlayer&) = delete;
      fileStreamPlayer& operator=(const fileStreamPlayer&) = delete;
      fileStreamPlayer(fileStreamPlayer&&) = delete;
      fileStreamPlayer& operator=(fileStreamPlayer&&) = delete;

      // ─── control and background threads ──────────────────────────────────

      /** @brief The number of outputs, 1 to ``MAX_CHANNELS``. Allocates:
       *         before the audio thread sees the player. */
      fileStreamPlayer& channels(UInt outputs);
      UInt channels() const {
        return outs.channels();
      }

      /** @brief Open @p file as cue 1, from its start, replacing the open
       *         file. False, and no change, when it cannot be opened. Blocks
       *         on the disk. */
      bool open(const std::string& file);
      /** @brief Close @p file: cue 1 if it is the open file, and every cue
       *         of it in the cue table. An empty name closes the open file
       *         only. False when nothing was closed. */
      bool close(const std::string& file = std::string());
      /** @brief The open file, empty for none. */
      std::string file() const;

      /** @brief Define cue @p number in the cue table
       *         (``fileStreamCues::preload``) and ``sync``. An empty
       *         @p file is the open file. */
      bool preload(Int number, const std::string& file, Flt startMs = 0.f, Flt endMs = 0.f,
                   Flt speed = 1.f, bool normalized = false);
      /** @brief Remove every cue from the cue table, and ``sync``. The open
       *         file stays. */
      void clear();
      /** @brief Remove cues @p numbers from the cue table, and ``sync``. */
      void clear(const std::vector<Int>& numbers);

      /** @brief Play the cues of @p list (null: a table of the player's
       *         own), and ``sync``. */
      void cueList(std::shared_ptr<fileStreamCues> list);
      /** @brief The cue table. */
      std::shared_ptr<fileStreamCues> cueList() const;

      /** @brief Open streams for the cue table as it is now, keeping those
       *         whose cue is unchanged, and hand them to the audio thread.
       *         Blocks on the disk; a cue whose file no longer opens has no
       *         stream. False when one did not open. */
      bool sync();
      /** @brief Free the streams the audio thread let go. */
      void collect();

      // ─── the audio thread ─────────────────────────────────────────────────

      /** @brief Play cue @p number from its start: 0 stops, 1 is the open
       *         file. */
      fileStreamPlayer& play(Int number);
      /** @brief Play @p count cues one after the other (at most
       *         ``MAX_LIST``). */
      fileStreamPlayer& play(const Int* numbers, UInt count);
      /** @brief Play the open file from @p startMs to @p endMs (0 or below:
       *         its end). */
      fileStreamPlayer& seek(Flt startMs, Flt endMs = 0.f);
      /** @brief Fade out and stop; ``done()`` once it has. */
      fileStreamPlayer& stop();
      /** @brief Fade out and hold the position. */
      fileStreamPlayer& pause();
      /** @brief Fade back in from where ``pause`` held. */
      fileStreamPlayer& resume();

      /** @brief Whether every cue loops. */
      fileStreamPlayer& loop(bool on) {
        looping = on;
        return *this;
      }
      bool loop() const {
        return looping;
      }
      /** @brief The one cue that loops (0: none). */
      fileStreamPlayer& loopOne(Int number) {
        loopCue = number;
        return *this;
      }
      Int loopOne() const {
        return loopCue;
      }
      /** @brief The playback speed, 1 normal; below 0 (or not a number) is
       *         0. */
      fileStreamPlayer& speed(Flt value);
      Flt speed() const {
        return rate;
      }
      /** @brief Whether outputs past the file's channels repeat them. */
      fileStreamPlayer& modout(bool on) {
        repeating = on;
        return *this;
      }
      bool modout() const {
        return repeating;
      }
      /** @brief The pitch shift as a ratio, 1 none; not a finite number
       *         above 0: ignored. */
      fileStreamPlayer& pitchShift(Flt ratio);
      Flt pitchShift() const {
        return shiftRatio;
      }
      /** @brief The pitch shift in cents, on top of ``pitchShift``; not
       *         finite: ignored. */
      fileStreamPlayer& pitchShiftCents(Flt cents);
      Flt pitchShiftCents() const {
        return shiftCents;
      }
      /** @brief Whether the speed changes only the tempo. */
      fileStreamPlayer& timeStretch(bool on) {
        stretching = on;
        return *this;
      }
      bool timeStretch() const {
        return stretching;
      }
      /** @brief The pitch shifters' algorithm. */
      fileStreamPlayer& quality(pitchShifter::QUALITY value);
      pitchShifter::QUALITY quality() const {
        return shiftQuality;
      }

      /** @brief One block of @p length samples, up to
       *         ``STANDARD_BUFFERSIZE``. Returns output 0. */
      buffer& operator()(UInt length = STANDARD_BUFFERSIZE);

      /** @brief Output @p channel of the last block (from 0); the last
       *         output past the count. */
      buffer& out(UInt channel = 0) {
        return outs.out(channel);
      }
      /** @brief Where the playing cue was at each sample of the last block,
       *         in ms into its file; 0 while stopped. */
      buffer& position() {
        return where;
      }

      /** @brief Whether playback stopped in the last block. */
      bool done() const {
        return finished;
      }
      /** @brief Whether a cue plays, paused or not. */
      bool playing() const;
      /** @brief Whether the cue that plays is paused. */
      bool paused() const;
      /** @brief The cue that plays, 0 for none. */
      Int cue() const;
      /** @brief Whether the player has a stream for cue @p number (1: an
       *         open file), as of the last block. */
      bool hasCue(Int number) const;
      /** @brief Whether the cue table changed since the streams the last
       *         block used were opened: time to ``sync``. Wait-free. */
      bool stale() const;
      /** @brief Whether streams let go by the audio thread wait for
       *         ``collect``. Any thread. */
      bool releasePending() const {
        return retired.load(std::memory_order_acquire) != nullptr;
      }

    private:
      struct stream;
      struct streamSet;

      // Control side: build and publish a set. Under `edit`.
      bool Rebuild();
      void Publish(streamSet* next);
      void CollectLocked();

      // Audio side.
      void Take();
      void Start(stream* s, Int number);
      void Retire(stream* s);
      void Cut(stream* s);
      void Render(stream* s, UInt length, bool withPosition);
      stream* Find(Int number) const;
      bool Loops(Int number) const;

      // The control side's lock and its record of what it last built.
      mutable std::mutex edit;
      std::shared_ptr<fileStreamCues> list;
      std::shared_ptr<stream> openStream;
      std::vector<std::shared_ptr<stream>> built; // the cue streams of the last set

      // The handoff: `current` is the audio thread's; `incoming` is filled
      // by the control side and taken with one exchange; `retired` holds the
      // set the audio thread let go until the control side frees it.
      streamSet* current = nullptr;
      std::atomic<streamSet*> incoming{nullptr};
      std::atomic<streamSet*> retired{nullptr};

      // The audio thread's.
      playerOutputs outs;
      buffer where;
      std::vector<std::unique_ptr<pitchShifter>> shifters;
      stream* active = nullptr;
      stream* fading = nullptr;
      Int activeCue = 0;
      Int sequence[MAX_LIST] = {};
      UInt sequenceLength = 0;
      UInt sequenceNext = 0;
      bool finished = false;
      bool looping = false;
      Int loopCue = 0;
      Flt rate = 1.f;
      bool repeating = false;
      Flt shiftRatio = 1.f;
      Flt shiftCents = 0.f;
      bool stretching = false;
      pitchShifter::QUALITY shiftQuality = pitchShifter::BASIC;

      // A transport call, applied at the top of the next block.
      enum class REQUEST : std::uint8_t { NONE, PLAY, LIST, SEEK, STOP, PAUSE, RESUME };
      REQUEST request = REQUEST::NONE;
      Int requestCue = 0;
      Int requestList[MAX_LIST] = {};
      UInt requestListLength = 0;
      Flt requestStart = 0.f;
      Flt requestEnd = 0.f;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_SAMPLING_FILESTREAMPLAYER_HPP
