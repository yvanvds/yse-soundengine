/*
  ==============================================================================

    filePlaylist.hpp
    A list of sound files played from disk, one clip at a time (issue #1158).

  ==============================================================================
*/

#ifndef YSE_DSP_SAMPLING_FILEPLAYLIST_HPP
#define YSE_DSP_SAMPLING_FILEPLAYLIST_HPP

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
#include "fileStreamPlayer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief A list of sound files, each a clip with a selection and
     *         settings of its own, played from disk one at a time. What the
     *         patcher's ``~playlist`` does; the engine holds the list and
     *         the playback, a host draws them.
     *
     *  ### Clips
     *
     *  A clip is a sound file, numbered by its place in the list from 1.
     *  ``append`` adds one at the end or at a place, ``remove`` takes one
     *  out, ``move`` puts one at another place (what a host does when the
     *  user drags a clip), ``clear`` empties the list. A clip plays its
     *  selection: ``selection`` (fractions of the file, 0 to 1) or
     *  ``selectionMs`` set it, ``clearSelection`` makes it the whole file
     *  again. Clip number 0 in those three means every clip.
     *
     *  Each clip has the playlist's settings (``loop``, ``speed``,
     *  ``pitchShift``, ``pitchShiftCents``, ``timeStretch``, ``quality``)
     *  unless ``setting`` gave it one of its own, which then wins for that
     *  clip. Setting a clip's value to NaN makes it follow the playlist
     *  again.
     *
     *  Every clip plays through a ``fileStreamPlayer`` the playlist owns,
     *  as one of its cues: the engine's own stream, read from disk in the
     *  background, so the audio thread never touches the disk. Opening a
     *  clip reads its first stream buffer, so a clip starts at once.
     *
     *  The control calls (``append``, ``remove``, ``clear``, ``move``,
     *  ``selection``, ``selectionMs``, ``clearSelection``, ``setting``,
     *  ``collect``, ``channels``) open files and allocate: control and
     *  background threads only, never the audio thread. They take one lock
     *  among themselves and hand the audio thread a new copy of the list
     *  through one atomic slot, taken at the top of the next block; the
     *  copy the audio thread let go is freed by the next control call or by
     *  ``collect``. ``clips``, ``count`` and ``version`` may be called from
     *  any thread but the audio thread: a host reads the list with them.
     *
     *  ### Playing (the audio thread)
     *
     *  ``play(n)`` plays clip ``n`` from the start of its selection: 0
     *  stops, a number past the list is ignored. ``next`` plays the clip
     *  after the one that plays or last played (the first when none has),
     *  and the first after the last. A clip plays to the end of its
     *  selection and stops; with its loop on it goes on from the start
     *  without a gap. A clip does not go on to the next by itself: a host
     *  or patch sends ``next`` when it is done. ``pause`` and ``resume``
     *  hold and go on. Starting, stopping, pausing and switching clips fade
     *  over 200 samples, as a ``fileStreamPlayer`` does. All of these take
     *  effect at the top of the next block.
     *
     *  The list may change while a clip plays: a clip moved keeps playing
     *  under its new number; the clip that plays, removed or cleared, falls
     *  silent at once; its selection changed, it plays again from the new
     *  selection's start (silent for a block or two while the disk reads
     *  it).
     *
     *  Each block reports, for the clip that plays (numbered as the list is
     *  now): ``started()`` the clip that started, ``done()`` the clip that
     *  stopped (it reached its end, or ``stop`` faded it out; not when
     *  another clip took over, not on ``pause``), ``looped()`` the clip
     *  that went on from its start, and, with ``reportProgress`` on,
     *  ``progressDue()`` every ``PROGRESS_MS`` of play with ``progress()``
     *  how far through its selection the clip is (0 to 1). Each is 0 or
     *  false when it did not happen.
     *
     *  ``position()`` is the playhead, per sample of the last block: the
     *  clip number plus how far into its file the clip is (0 to below 1),
     *  so 2.5 is halfway through clip 2's file; 0 while nothing plays.
     *  ``playing()``, ``paused()`` and ``where()`` give the same for the
     *  last sample, from any thread, for a host drawing a playhead.
     *
     *  .. code-block:: cpp
     *
     *     // control thread
     *     DSP::filePlaylist list(2);
     *     list.append("intro.wav");
     *     list.append("theme.wav");
     *     list.selectionMs(2, 500.f, 4500.f);
     *     list.play(2);
     *
     *     // audio thread, once per block
     *     list();
     *     DSP::buffer& left = list.out(0);
     *     if (list.done() != 0) list.next();
     *
     *  Every audio-thread call allocates nothing, takes no lock and blocks
     *  on no I/O, and reads the live ``SAMPLERATE`` every block. The
     *  outputs are ``STANDARD_BUFFERSIZE`` long; a shorter block resizes
     *  them down without allocating. Output ``k`` plays file channel ``k``;
     *  outputs past a file's channels are silent. The audio-thread calls
     *  belong to one thread at a time.
     */
    class API filePlaylist {
    public:
      /** @brief The most clips a list holds. */
      static constexpr UInt MAX_CLIPS = 1024;
      /** @brief The most outputs. */
      static constexpr UInt MAX_CHANNELS = fileStreamPlayer::MAX_CHANNELS;
      /** @brief How often ``progressDue()`` comes while a clip plays, in
       *         milliseconds. */
      static constexpr Flt PROGRESS_MS = 50.f;

      /** @brief The settings a clip has of its own or from the playlist. */
      enum SETTING : std::uint8_t {
        LOOP, ///< 1 loops, 0 does not.
        SPEED, ///< The playback speed, 1 normal, below 0 backward.
        PITCHSHIFT, ///< The pitch shift as a ratio, above 0.
        PITCHSHIFTCENT, ///< The pitch shift in cents, on top.
        TIMESTRETCH, ///< 1: the speed changes only the tempo.
        QUALITY, ///< The pitch shifter's quality, 0 to 3.
        SETTINGS, ///< How many there are.
      };

      /** @brief One clip, as ``clips()`` and ``clipAt()`` give it. */
      struct clip {
        /** @brief The file it plays. */
        std::string file;
        /** @brief The name it is reported by. */
        std::string name;
        /** @brief The length of the file, in ms. */
        Flt lengthMs = 0.f;
        /** @brief The selection it plays, in ms into the file: the whole
         *         file without one. */
        Flt startMs = 0.f;
        Flt endMs = 0.f;
        /** @brief Whether a selection was set. */
        bool selected = false;
        /** @brief Its own settings, by ``SETTING``; NaN follows the
         *         playlist. */
        Flt settings[SETTINGS];
        clip();
      };

      /** @brief An empty list with @p outputs outputs (1 to
       *         ``MAX_CHANNELS``). Allocates. */
      explicit filePlaylist(UInt outputs = 2);
      /** @brief Frees every stream. Not on the audio thread. */
      ~filePlaylist();
      filePlaylist(const filePlaylist&) = delete;
      filePlaylist& operator=(const filePlaylist&) = delete;
      filePlaylist(filePlaylist&&) = delete;
      filePlaylist& operator=(filePlaylist&&) = delete;

      // ─── control and background threads ──────────────────────────────────

      /** @brief The number of outputs, 1 to ``MAX_CHANNELS``. Allocates:
       *         before the audio thread sees the playlist. */
      filePlaylist& channels(UInt outputs);
      UInt channels() const {
        return player.channels();
      }

      /**
       *  @brief Add @p file as a clip at place @p at (from 1; 0 or past
       *         the end: at the end), the clips from there on moving up one.
       *
       *  @p name is what the clip is reported by; empty, the last part of
       *  the file's path. Returns the clip's number, or 0, and no change,
       *  when the file cannot be opened or the list holds ``MAX_CLIPS``.
       *  Blocks on the disk.
       */
      Int append(const std::string& file, Int at = 0, const std::string& name = std::string());
      /** @brief Take clip @p number out; false when there is none. */
      bool remove(Int number);
      /** @brief Take every clip out; how many there were. */
      UInt clear();
      /** @brief Put clip @p from at place @p to, the clips between moving
       *         over; false when either is not a clip. */
      bool move(Int from, Int to);
      /**
       *  @brief Make clip @p number (0: every clip) play from @p start to
       *         @p end, as fractions of its file (0 to 1).
       *
       *  An end of 0 or below is the end of the file; the selection is held
       *  inside the file. False when the number is not a clip or 0, or the
       *  selection holds nothing (an end at or before the start); every
       *  clip it could set is set. Blocks on the disk.
       */
      bool selection(Int number, Flt start, Flt end);
      /** @brief The same in milliseconds into the file. */
      bool selectionMs(Int number, Flt startMs, Flt endMs);
      /** @brief Make clip @p number (0: every clip) play its whole file
       *         again. */
      bool clearSelection(Int number);
      /**
       *  @brief Give clip @p number (0: every clip) a setting of its own;
       *         NaN makes it follow the playlist again.
       *
       *  ``LOOP`` and ``TIMESTRETCH`` take 0 or not 0, ``SPEED`` and
       *  ``PITCHSHIFTCENT`` any finite number, ``PITCHSHIFT`` a number
       *  above 0, ``QUALITY`` 0 to 3. False, and no change, for another
       *  value or a number that is not a clip.
       */
      bool setting(Int number, SETTING which, Flt value);

      /** @brief The clips, in order. A copy. */
      std::vector<clip> clips() const;
      /** @brief How many clips there are. */
      UInt count() const;
      /** @brief Counts the changes to the list: one more after each that
       *         changed something. Wait-free; any thread. */
      std::uint64_t version() const {
        return changes.load(std::memory_order_acquire);
      }

      /** @brief Free the copies and streams the audio thread let go. */
      void collect();
      /** @brief Whether something the audio thread let go waits for
       *         ``collect``. Any thread. */
      bool releasePending() const {
        return retired.load(std::memory_order_acquire) != nullptr || player.releasePending();
      }

      // ─── the audio thread ─────────────────────────────────────────────────

      /** @brief Play clip @p number from the start of its selection: 0
       *         stops. */
      filePlaylist& play(Int number);
      /** @brief Play the clip after the one that plays or last played. */
      filePlaylist& next();
      /** @brief Fade out and stop; ``done()`` once it has. */
      filePlaylist& stop();
      /** @brief Fade out and hold the position. */
      filePlaylist& pause();
      /** @brief Fade back in from where ``pause`` held. */
      filePlaylist& resume();

      /** @brief Whether the clips loop. */
      filePlaylist& loop(bool on) {
        shared[LOOP] = on ? 1.f : 0.f;
        return *this;
      }
      bool loop() const {
        return shared[LOOP] != 0.f;
      }
      /** @brief The playback speed, 1 normal, below 0 backward; not a
       *         finite number: ignored. */
      filePlaylist& speed(Flt value);
      Flt speed() const {
        return shared[SPEED];
      }
      /** @brief The pitch shift as a ratio; not a finite number above 0:
       *         ignored. */
      filePlaylist& pitchShift(Flt ratio);
      Flt pitchShift() const {
        return shared[PITCHSHIFT];
      }
      /** @brief The pitch shift in cents; not finite: ignored. */
      filePlaylist& pitchShiftCents(Flt cents);
      Flt pitchShiftCents() const {
        return shared[PITCHSHIFTCENT];
      }
      /** @brief Whether the speed changes only the tempo. */
      filePlaylist& timeStretch(bool on) {
        shared[TIMESTRETCH] = on ? 1.f : 0.f;
        return *this;
      }
      bool timeStretch() const {
        return shared[TIMESTRETCH] != 0.f;
      }
      /** @brief The pitch shifters' algorithm. */
      filePlaylist& quality(pitchShifter::QUALITY value);
      pitchShifter::QUALITY quality() const {
        return static_cast<pitchShifter::QUALITY>(static_cast<int>(shared[QUALITY]));
      }
      /** @brief Whether ``progressDue()`` reports. */
      filePlaylist& reportProgress(bool on) {
        reporting = on;
        return *this;
      }
      bool reportProgress() const {
        return reporting;
      }

      /** @brief One block of @p length samples, up to
       *         ``STANDARD_BUFFERSIZE``. Returns output 0. */
      buffer& operator()(UInt length = STANDARD_BUFFERSIZE);

      /** @brief Output @p channel of the last block (from 0); the last
       *         output past the count. */
      buffer& out(UInt channel = 0) {
        return player.out(channel);
      }
      /** @brief The playhead at each sample of the last block: the clip
       *         number plus how far into its file it is; 0 while nothing
       *         plays. */
      buffer& position() {
        return head;
      }

      /** @brief The clip that started in the last block, 0 for none. */
      Int started() const {
        return startedClip;
      }
      /** @brief The clip that stopped in the last block, 0 for none. */
      Int done() const {
        return doneClip;
      }
      /** @brief The clip that looped in the last block, 0 for none. */
      Int looped() const {
        return loopedClip;
      }
      /** @brief Whether a progress report is due after the last block. */
      bool progressDue() const {
        return dueProgress;
      }
      /** @brief How far through its selection the clip that plays was at
       *         the end of the last block, 0 to 1. */
      Flt progress() const {
        return through;
      }

      /** @brief The clips as the last block saw them. Audio thread. */
      UInt clipCount() const;
      /** @brief Clip @p number as the last block saw it, or null. Audio
       *         thread. */
      const clip* clipAt(Int number) const;

      /** @brief The clip that plays, paused or not, 0 for none, as of the
       *         last block. Any thread. */
      Int playing() const {
        return shownClip.load(std::memory_order_relaxed);
      }
      /** @brief Whether the clip that plays is paused. Any thread. */
      bool paused() const {
        return shownPaused.load(std::memory_order_relaxed);
      }
      /** @brief How far into its file the clip that plays was at the end
       *         of the last block, 0 to below 1. Any thread. */
      Flt where() const {
        return shownWhere.load(std::memory_order_relaxed);
      }

    private:
      struct slot {
        Int cue = 0;
        clip c;
      };
      struct order;

      // Control side, under `edit`.
      Int FreeCue() const;
      bool Span(Int number, Flt start, Flt end, bool normalized, bool whole);
      bool Read(slot& s) const;
      void Publish();
      void CollectLocked();

      // Audio side.
      void Take();
      Int Index(Int cue) const; // from 0, -1 when not listed
      void Apply(Int cue);

      fileStreamPlayer player;

      // The control side's lock, the list it edits, and a lock only for
      // reading and replacing that list, so a host reading it never waits
      // on the disk.
      mutable std::mutex edit;
      mutable std::mutex view;
      std::vector<slot> slots;
      std::atomic<std::uint64_t> changes{0};

      // The handoff of the list, as fileStreamPlayer hands its streams.
      order* current = nullptr;
      std::atomic<order*> incoming{nullptr};
      std::atomic<order*> retired{nullptr};

      // The audio thread's.
      buffer head;
      Flt shared[SETTINGS] = {0.f, 1.f, 1.f, 0.f, 0.f, 0.f};
      bool reporting = false;
      Int pendingCue = 0; // a clip to start, until its stream is there
      Int soundingCue = 0; // the clip that played after the last block
      Int lastCue = 0; // the clip that played last, for next()
      Int startedClip = 0;
      Int doneClip = 0;
      Int loopedClip = 0;
      bool dueProgress = false;
      Flt through = 0.f;
      Dbl progressWait = 0.0; // samples to the next progress report

      enum class REQUEST : std::uint8_t { NONE, PLAY, NEXT, STOP, PAUSE, RESUME };
      REQUEST request = REQUEST::NONE;
      Int requestClip = 0;

      // What a host reads.
      std::atomic<Int> shownClip{0};
      std::atomic<bool> shownPaused{false};
      std::atomic<Flt> shownWhere{0.f};
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_SAMPLING_FILEPLAYLIST_HPP
