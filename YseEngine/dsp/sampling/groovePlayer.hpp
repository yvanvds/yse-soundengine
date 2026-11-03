/*
  ==============================================================================

    groovePlayer.hpp
    Variable-rate looping playback of a sample buffer (issue #1141).

  ==============================================================================
*/

#ifndef YSE_DSP_SAMPLING_GROOVEPLAYER_HPP
#define YSE_DSP_SAMPLING_GROOVEPLAYER_HPP

#include "../../headers/constants.hpp"
#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../buffer.hpp"
#include "playerOutputs.hpp"

namespace YSE {
  namespace DSP {

    class sampleView;

    /** @brief How a ``groovePlayer`` treats its loop points. */
    enum class GROOVE_LOOP {
      /** Play through the loop points, to an end of the buffer. */
      OFF,
      /** Jump back to the other loop point on reaching one. */
      ON,
      /** Turn round at each loop point: back and forth. */
      PENDULUM,
    };

    /**
     *  @brief Plays a ``sampleBuffer`` at the speed of a signal, between
     *         loop points, with a sync output: what the patcher's
     *         ``~groove`` does (Max's ``groove~``).
     *
     *  It reads through the ``sampleView`` a ``sampleBufferReader`` pins for
     *  the block, with the 4-point cubic kernel of ``interpolate4``
     *  (``sampleView::cubic``). Positions are milliseconds into the buffer,
     *  at the buffer's own sample rate.
     *
     *  - **Speed.** Each sample of the speed signal is how fast playback
     *    moves at that sample: 1 plays at normal speed, 2 twice as fast,
     *    0.5 at half speed, -1 backward at normal speed. At 0 the position
     *    stands still and the output holds the sample there. Normal speed
     *    takes the buffer's sample rate and the engine's into account.
     *  - **Position.** ``position(ms)`` jumps there; it is where playback
     *    starts. ``startLoop`` jumps to the loop start, ``endLoop`` to the
     *    loop end. Each of the three also plays after a ``stop`` or after
     *    playback ran off the buffer. Positions are kept inside the buffer.
     *  - **Loop points** (``loopPoints``, ``loopStart``, ``loopEnd``, in
     *    ms). An end of 0 is the end of the buffer (``sampleRegion``), so
     *    the default loop is the whole buffer; ``reset`` goes back to it.
     *    Points past the buffer are its end, as long as it is in that
     *    block, so they follow a buffer that is resized or reloaded. A loop
     *    whose end is not after its start holds nothing, and playback goes
     *    through it as if the loop were off.
     *  - **Loop mode** (``loop``). ``OFF`` plays through the loop points;
     *    reaching either end of the buffer stops playback (silence) until
     *    the next ``position``, ``startLoop`` or ``endLoop``. ``ON``:
     *    playing forward, a position at or past the loop end goes on from
     *    the loop start by as much as it overshot; playing backward, a
     *    position before the loop start goes on from the loop end. A
     *    position before the loop start, playing forward (or after the loop
     *    end, backward), plays into the loop. ``PENDULUM`` turns round at
     *    the loop points instead of jumping, so a positive speed plays the
     *    loop forward, then backward, and so on (a negative one starts
     *    backward).
     *  - **Loop crossfade** (``loopInterp``, ``interpTime`` in ms, 50 by
     *    default). With loop ``ON`` and loop interpolation on, the last
     *    ``interpTime`` ms before the loop jumps (in the direction it plays)
     *    are crossfaded with the sound on the other side of the point it
     *    jumps to (``loopCrossfade``), while the position is in the loop. A
     *    crossfade longer than the loop is the loop's length.
     *  - **Sync** (``sync()``). Where in the loop each sample was read: 0
     *    at the loop start rising to 1 at the loop end
     *    (``sampleRegion::phase``), 0 outside the loop, for an empty loop,
     *    and while stopped.
     *  - **Channels.** As ``playerOutputs``: output ``k`` plays buffer
     *    channel ``k``, missing channels are silent, extra ones mixed in.
     *  - **Following a phase** (``phase``, issue #1142). The other way to
     *    drive it: instead of a speed, each sample of a signal says where in
     *    the loop to read, 0 at the loop start rising to 1 at the loop end,
     *    so a ramp plays the loop once per cycle whatever the loop's length:
     *    what the patcher's ``~phasegroove`` does. See ``phase``.
     *
     *  With no buffer bound, or an empty one, the outputs and sync are
     *  silence and the position stays where it was.
     *
     *  .. code-block:: cpp
     *
     *     // control thread
     *     DSP::sampleBufferReader reader(DSP::sampleBufferRegistry::acquire("drums"));
     *     DSP::groovePlayer groove(2);
     *     groove.loop(DSP::GROOVE_LOOP::ON).loopPoints(250.f, 750.f);
     *
     *     // audio thread, once per block
     *     DSP::sampleView view = reader.pin();
     *     groove(view, speed);              // a buffer, or a number
     *     DSP::buffer& left = groove.out(0);
     *     DSP::buffer& ramp = groove.sync();
     *
     *  The speed of normal playback comes from ``SAMPLERATE``, read every
     *  block, and the loop from the view, so it follows the live sample rate.
     *  The outputs are sized to ``STANDARD_BUFFERSIZE`` when the channel
     *  count is set; a shorter block resizes them down without allocating,
     *  and a longer one is cut to ``STANDARD_BUFFERSIZE``. Processing
     *  allocates nothing and takes no lock. Not thread-safe: one thread at a
     *  time, and the setters on that thread too; ``channels`` allocates, so
     *  set it before the audio thread sees the object.
     */
    class API groovePlayer {
    public:
      /** @brief The most outputs a player has. */
      static constexpr UInt MAX_CHANNELS = playerOutputs::MAX_CHANNELS;
      /** @brief The default loop crossfade, in milliseconds. */
      static constexpr Flt DEFAULT_INTERP_TIME = 50.f;

      /** @brief A player at position 0 with @p outputs outputs (1 to
       *         ``MAX_CHANNELS``), playing as soon as its speed is not 0.
       *         Allocates. */
      explicit groovePlayer(UInt outputs = 1);

      /** @brief The number of outputs, 1 to ``MAX_CHANNELS``. Allocates:
       *         not on the audio thread. */
      groovePlayer& channels(UInt outputs);
      UInt channels() const {
        return outputs.channels();
      }

      /** @brief Jump to @p ms into the buffer and play from there. Below
       *         0, or not a number, is 0. */
      groovePlayer& position(Flt ms);
      /** @brief Where playback is, in ms into the buffer: the position the
       *         next sample is read at. */
      Dbl position() const {
        return cursor;
      }

      /** @brief Jump to the loop start and play from there. */
      groovePlayer& startLoop();
      /** @brief Jump to the loop end and play from there. */
      groovePlayer& endLoop();
      /** @brief Stop playback until the next ``position``, ``startLoop`` or
       *         ``endLoop``. The position stays. */
      groovePlayer& stop();
      /** @brief Whether playback runs: false after ``stop``, or once it ran
       *         off an end of the buffer with the loop off. */
      bool playing() const {
        return isPlaying;
      }

      /** @brief Both loop points, in ms; an end of 0 is the end of the
       *         buffer. */
      groovePlayer& loopPoints(Flt startMs, Flt endMs) {
        loopStartMs = startMs;
        loopEndMs = endMs;
        return *this;
      }
      groovePlayer& loopStart(Flt ms) {
        loopStartMs = ms;
        return *this;
      }
      Flt loopStart() const {
        return loopStartMs;
      }
      groovePlayer& loopEnd(Flt ms) {
        loopEndMs = ms;
        return *this;
      }
      Flt loopEnd() const {
        return loopEndMs;
      }
      /** @brief Both loop points back to 0: the whole buffer. */
      groovePlayer& reset() {
        return loopPoints(0.f, 0.f);
      }

      /** @brief What reaching a loop point does. */
      groovePlayer& loop(GROOVE_LOOP mode) {
        loopMode = mode;
        return *this;
      }
      GROOVE_LOOP loop() const {
        return loopMode;
      }

      /** @brief Whether a loop (``ON``) crossfades across its jump. */
      groovePlayer& loopInterp(bool on) {
        crossfading = on;
        return *this;
      }
      bool loopInterp() const {
        return crossfading;
      }

      /** @brief The loop crossfade, in milliseconds. Below 0, or not a
       *         number, is 0. */
      groovePlayer& interpTime(Flt ms) {
        fadeMs = ms > 0.f ? ms : 0.f;
        return *this;
      }
      Flt interpTime() const {
        return fadeMs;
      }

      /** @brief One block at the speeds of @p speed; as long as it, up to
       *         ``STANDARD_BUFFERSIZE``. Returns output 0. */
      buffer& operator()(const sampleView& view, buffer& speed);

      /** @brief One block of @p length samples, up to
       *         ``STANDARD_BUFFERSIZE``, at the constant @p speed. Returns
       *         output 0. */
      buffer& operator()(const sampleView& view, Flt speed, UInt length = STANDARD_BUFFERSIZE);

      /**
       *  @brief One block read where @p phases says, as a fraction of the
       *         loop; as long as it, up to ``STANDARD_BUFFERSIZE``. Returns
       *         output 0.
       *
       *  Sample ``i`` is read at ``start + phase * (end - start)`` of the
       *  loop points, for this block's buffer, with the same 4-point read as
       *  a speed. A rising ramp plays the loop forward, a falling one
       *  backward, a held value holds the sample there, and how fast the
       *  ramp runs is how fast the loop plays: neither the engine's sample
       *  rate nor the buffer's enters into it, except that the loop points
       *  are milliseconds of the buffer.
       *
       *  - A phase outside 0 to 1 wraps round the loop, so 1.25 and -0.75
       *    read where 0.25 does, and an offset ramp shifts the loop. 1 itself
       *    is the loop end, where a one-shot ramp comes to rest. A phase
       *    that is not a number holds the last one.
       *  - ``loop`` plays no part: the ramp decides what happens at the
       *    points. With ``loopInterp`` on, the last ``interpTime`` ms before
       *    the point the ramp wraps from are crossfaded with the sound on the
       *    far side of the point it wraps to (``loopCrossfade``), in the
       *    direction the ramp is moving. Its direction is taken the short way
       *    round from one phase to the next, so a wrap does not reverse it.
       *  - ``sync()`` is the phase read, kept in 0 to 1.
       *  - A loop that holds nothing plays silence, as no buffer or an empty
       *    one does. So does a player stopped by ``stop`` or by a speed that
       *    ran off the buffer, until the next ``position``, ``startLoop`` or
       *    ``endLoop``; those three only start it again here, since the
       *    phase says where it reads.
       *  - ``position()`` gives where the block's last sample was read, so
       *    a speed can take over from there.
       */
      buffer& phase(const sampleView& view, buffer& phases);

      /** @brief One block of @p length samples, up to
       *         ``STANDARD_BUFFERSIZE``, at the constant phase @p at: holds
       *         the sample there. Returns output 0. */
      buffer& phase(const sampleView& view, Flt at, UInt length = STANDARD_BUFFERSIZE);

      /** @brief Output @p channel of the last block (from 0); the last
       *         output past the count. */
      buffer& out(UInt channel = 0) {
        return outputs.out(channel);
      }

      /** @brief The sync output of the last block: 0 to 1 over the loop. */
      buffer& sync() {
        return syncOut;
      }

    private:
      // The block, at the speed of `speeds[i]` (or `constant` when null).
      void Process(const sampleView& view, const Flt* speeds, Flt constant, UInt length);
      // The block, read at the phase `phases[i]` (or `constant` when null).
      void ProcessPhase(const sampleView& view, const Flt* phases, Flt constant, UInt length);
      // The outputs and sync silent and `length` samples long, or as much of
      // it as they hold. Returns that length.
      UInt Begin(UInt length);

      // A jump to a loop point, waiting for a block with something to play
      // (the points are frames of the buffer as it is then).
      enum class JUMP { NONE, LOOP_START, LOOP_END };

      // Keeps `frame` on the loop as playback moving by `step` reaches a
      // loop point: a jump (ON) or a turn (PENDULUM, flips `direction`).
      void Wrap(Dbl& frame, Dbl step, Dbl low, Dbl high);
      // With the loop off: whether `frame` moving by `step` ran off an end
      // of the buffer (`last` frames), and if so stops playback there.
      bool OffTheEnd(Dbl& frame, Dbl step, Dbl last);

      playerOutputs outputs;
      buffer syncOut;
      bool isPlaying = true;
      JUMP jump = JUMP::NONE;
      GROOVE_LOOP loopMode = GROOVE_LOOP::OFF;
      bool crossfading = false;
      Flt fadeMs = DEFAULT_INTERP_TIME;
      Flt loopStartMs = 0.f;
      Flt loopEndMs = 0.f;
      Dbl cursor = 0.0; // ms
      Dbl direction = 1.0; // -1 on the way back in a pendulum loop
      Dbl lastPhase = 0.0; // the phase last read, in 0 to 1
      bool phaseFalling = false; // the way the phase was last moving
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_SAMPLING_GROOVEPLAYER_HPP
