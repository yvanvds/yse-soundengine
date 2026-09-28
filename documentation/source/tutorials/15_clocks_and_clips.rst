Clocks and clips
================

Goal: run a named beat clock, change its tempo while it plays, and loop a
bar of notes against it into a synth or a MIDI output port.

A **domain clock** counts beats at a tempo. A **clip** holds a list of notes
placed in beats and plays them against one clock, on the audio thread. Your
code never schedules a single note: it edits the list and steers the tempo,
and the engine fires each note in the audio block where the clock passes it.

This tutorial has no demo program. The snippets use only the calls shown, and
the C API versions follow at the end.

Mental model
------------

- A clock has a **name**, a **tempo** in BPM and a **beat position**. It
  starts at beat 0 when you create it.
- The beat position is the running total of the tempo over time. A tempo
  change alters how fast the beats go by and never makes the position jump.
- The engine advances every clock once per 128-sample block, on the audio
  thread, while a device is open (or on each ``renderOffline`` block). The
  clocks keep moving when no sound is playing. They do not move before
  ``System().init()`` or while the engine is paused.
- A clip is bound to one clock and reads it at the start of every block. It
  fires the notes whose beat falls between the previous block and this one.
  A clip is therefore accurate to one block: about 2.9 ms at 44.1 kHz.

Clocks are shared by name. The host, any number of clips and every patch in
the process see the same ``"main"`` clock. :doc:`/patcher/time` covers the
patcher side, and :doc:`/intro/threading` says which thread each call belongs
on.

Creating and steering a clock
-----------------------------

.. code-block:: cpp

   #include "yse.hpp"

   YSE::System().init();
   YSE::System().createClock("main", 120.f);   // starts at beat 0, 120 BPM

   YSE::System().setTempo("main", 90.f, 4.f);  // glide to 90 BPM over 4 s
   YSE::System().setTempo("main", 140.f);      // jump to 140 BPM now

   double beat = YSE::System().beatPosition("main");
   float  bpm  = YSE::System().currentTempo("main");

``createClock(name, bpm)`` returns ``false`` when the name is empty or a
clock of that name already exists. The existing clock then stays as it was:
the first registration wins. The tempo defaults to 120 BPM.

``setTempo(name, bpm, rampSeconds)`` changes the tempo in a straight line
over ``rampSeconds``, starting from the tempo the clock has at that moment,
so a new ramp can take over from one still running. With no ramp time, or
0, the change is instant. The engine picks up tempo requests once per block,
so when you call ``setTempo`` twice within one block only the second call
counts.

The tempo is not clamped:

- **0 pauses the clock.** The beat position holds, and the clips on it stop
  firing until the tempo rises again.
- **A negative tempo runs it backwards.** Clips fire nothing while the clock
  goes backwards. See `Timing rules`_.

``beatPosition`` and ``currentTempo`` are cheap reads. You can call them from
a UI thread every frame, for example to draw a playhead. Both return 0 for a
name that has no clock, and ``clockExists(name)`` tells the two cases apart.

Several clocks can run at once, each at its own tempo. They all advance from
the same sample clock, so their ratios stay exact: a clock at 90 BPM and one
at 120 BPM stay at 3:4 for as long as they run.

Playing a clip into a synth
---------------------------

A clip needs a clock, a list of events and at least one target. Here the
target is a synth playing behind a sound, as in :doc:`06_first_synth`:

.. code-block:: cpp

   YSE::SYNTH::sineVoice voice;           // must outlive the synth's setup
   YSE::synth synth;
   synth.create().addVoices(voice, 8);
   YSE::sound sound;
   sound.create(synth);
   sound.play();

   // One bar of four quarter notes.
   //                  startBeat  duration  channel  pitch  velocity  pitchBend
   std::vector<YSE::clipEvent> bar = {
     {0.0, 0.5, 1, 60, 0.8f, 0.f},
     {1.0, 0.5, 1, 64, 0.8f, 0.f},
     {2.0, 0.5, 1, 67, 0.8f, 0.f},
     {3.0, 0.5, 1, 72, 0.8f, 0.f},
   };

   YSE::clip clip;
   if (!clip.create("main")) {
     // no clock called "main"
   }
   clip.setEvents(bar).loopLength(4.0).connect(synth).play();

Each ``clipEvent`` has:

``startBeat``
   The beat within the loop where the note starts.
``durationBeats``
   The note length. The note-off comes this many beats after the note-on.
``channel``
   MIDI channel, 1 to 16.
``pitch``
   MIDI note number, 0 to 127.
``velocity``
   0 to 1.
``pitchBend``
   -1 to 1, 0 for none. A non-zero bend is sent as a pitch-wheel message on
   the note's channel just before the note-on. Use it for microtonal pitches:
   the nearest semitone plus a bend.

``create(clockName)`` binds the clip to a clock. It returns ``false`` if no
clock of that name exists. A failed ``create`` also unbinds the clip from the
clock it had, so it goes silent. You can bind again at any time, to the same
clock or another.

``setEvents``, ``loopLength``, ``connect`` and ``play`` return the clip, so
they chain. All of them are control-thread calls.

Changing a clip while it plays
------------------------------

``setEvents`` replaces the whole list. The clip picks up the new list at the
next block, without locking the audio thread, so you can call it as often as
the music changes:

.. code-block:: cpp

   bar[3].pitch = 71;           // edit your copy of the list ...
   clip.setEvents(bar);         // ... and publish it

A note that is sounding when the list changes still gets its note-off on
time, even if the new list no longer contains it.

``loopLength(beats)`` sets the loop. A length of 0 or less turns looping off,
so each event plays once. ``stop()`` ends playback and sends a note-off for
every note still sounding. ``play()`` starts again. ``isPlaying()`` reports
the state; it turns ``false`` as soon as you call ``stop()``, before the
note-offs have gone out.

Timing rules
------------

Event times are measured on the **clock**, not from the moment you call
``play()``. That keeps clips in step with each other, but it has some
consequences:

- **Loops line up with the clock.** A 4-beat loop plays its beat-0 event at
  clock beats 0, 4, 8 and so on. A clip started at clock beat 5.5 first plays
  its beat-2 event, at clock beat 6. Two clips with the same loop length on
  the same clock are always in phase, however far apart you start them.
- **A clip without a loop plays each event when the clock reaches its
  ``startBeat``.** An event whose ``startBeat`` the clock has already passed
  never plays. To play a phrase once starting "now", add the current
  position: ``startBeat = System().beatPosition("main") + offset``.
- **The first block after** ``play()`` only records where the clock is.
  Firing starts from the next block, so an event exactly on the clock
  position at that moment is skipped.
- **A paused or reversed clock fires nothing.** When the tempo is 0 or
  negative, the clip plays no new notes and sends no note-offs. Notes that are
  sounding wait until the clock passes their end again, or until you call
  ``stop()``.
- **Note-offs go before note-ons** within a block, so a note that ends as the
  same pitch starts again is released first.

A tempo change reaches every clip on the clock in the next block. Nothing is
rescheduled: a glide from 120 to 90 BPM bends every clip on that clock at
once.

Targets
-------

A clip can play into several targets at once. It sends every note to all of
them.

**Synths.** Call ``connect(synth)`` once for each synth, up to 8 per clip.
The notes reach the synth on the audio thread, in the block where they fire.
``disconnect(synth)`` stops the routing. The synth must outlive the
connection: disconnect it, or destroy the clip, before you destroy the synth.

**MIDI output ports.** On Windows and Linux builds with
``YSE_ENABLE_MIDI_DEVICE``, a clip can also play to an external MIDI device,
up to 4 ports per clip:

.. code-block:: cpp

   YSE::midiOut out;
   out.create(0);        // open port 0 first; an unopened midiOut is ignored
   clip.connect(out);

The audio thread still decides when each note fires. It stamps the message
with the time it should be sent and hands it to a separate sender thread,
which makes the actual MIDI call, so the audio callback never waits on the
device. The port stays open inside the engine, so you may destroy the
``midiOut`` object after connecting. If the sender's queue is full, a message
is dropped rather than making the audio thread wait.

Connecting more than 8 synths or 4 ports to one clip does nothing. There is
no error, and the extra targets get no notes.

Lifetime
--------

A clip and its clock can be destroyed in either order.

- ``System().destroyClock(name)`` removes the clock. A clip bound to it keeps
  a hold on the clock, but the clock stops advancing, so the clip stops
  firing. It does not crash and does not read freed memory.
- Creating a new clock with the same name does **not** reconnect old clips.
  Call ``create(name)`` on each clip again to bind it to the new clock.
- Destroying a clip does not send note-offs. Call ``stop()`` and give the
  engine a block to send them (a few milliseconds) before you destroy a
  clip that is playing. Or release the notes on the synth with
  ``allNotesOff()``.
- Destroy your clips before ``System().close()``. Closing the session removes
  every clock and clip from the engine, and the next session starts with
  none.

.. versionchanged:: 3.0
   Clips and patcher objects now share the lifetime of the clock they are
   bound to, so ``destroyClock`` can no longer free a clock that is still in
   use (issue `#707
   <https://github.com/yvanvds/yse-soundengine/issues/707>`_).

Limits
------

- 256 notes sounding at once per clip. Further note-ons are dropped.
- Each event plays at most 64 times in one block. This only matters for a
  very short loop at a very high tempo.
- 8 synths and 4 MIDI ports per clip.

Clocks in a patch
-----------------

Patcher objects use the same clocks by name. ``.transport`` and
``.setclock`` create clocks, and ``.tempo``, ``.metro``, ``.delay`` and the
other timed objects can count beats on them. A clip and a patch on the same
clock fire in the same block. See :doc:`/patcher/time`.

The C API
---------

The C API has the same calls, in ``yse_system.h`` for clocks and
``yse_clip.h`` for clips (see :doc:`/api/c_api`). ``YseClipEvent`` has the
same fields as ``YSE::clipEvent``, in the same order:

.. code-block:: c

   #include "yse_c/yse_all.h"

   YseSystem* sys = yse_system_get();
   yse_system_init(sys);
   yse_system_create_clock(sys, "main", 120.f);

   YseSynth* synth = yse_synth_create();
   yse_synth_add_voices_sine(synth, 8, 0, 0, 127, 0.01f, 0.1f, 0.7f, 0.3f);
   YseSound* sound = yse_sound_create();
   yse_synth_attach_to_sound(synth, sound, NULL, 1.0f);
   yse_sound_play(sound);

   YseClipEvent bar[4] = {
     {0.0, 0.5, 1, 60, 0.8f, 0.f},
     {1.0, 0.5, 1, 64, 0.8f, 0.f},
     {2.0, 0.5, 1, 67, 0.8f, 0.f},
     {3.0, 0.5, 1, 72, 0.8f, 0.f},
   };

   YseClip* clip = yse_clip_create();
   if (yse_clip_bind(clip, "main") != YSE_OK) {
     fprintf(stderr, "%s\n", yse_last_error());
   }
   yse_clip_set_events(clip, bar, 4);
   yse_clip_set_loop_length(clip, 4.0);
   yse_clip_connect_synth(clip, synth);
   yse_clip_play(clip);

   yse_system_set_tempo(sys, "main", 90.f, 4.f);
   double beat = yse_system_beat_position(sys, "main");

   /* teardown: the clip first, then the sound, then the synth */
   yse_clip_stop(clip);
   yse_system_sleep(sys, 20);
   yse_clip_destroy(clip);
   yse_sound_destroy(sound);
   yse_synth_destroy(synth);
   yse_system_destroy_clock(sys, "main");
   yse_system_close(sys);

The differences from C++:

- ``yse_system_create_clock`` and ``yse_clip_bind`` return a ``YseStatus``.
  On failure the reason is in ``yse_last_error()``.
- ``yse_clip_set_events`` copies the array, so you can reuse or free it
  after the call. Pass ``NULL`` with a count of 0 to clear the list.
- ``yse_clip_connect_midi_out`` takes a ``YseMidiOut*`` opened with
  ``yse_midi_out_open``. On builds without MIDI device support it does
  nothing and sets ``yse_last_error()``.

What you learned
----------------

- A domain clock is a named beat counter. Its tempo can jump, glide, pause
  and run backwards, and its beat position never jumps.
- A clip plays a list of beat-timed notes against a clock, on the audio
  thread, accurate to one block.
- Loops line up with the clock's beats, not with the moment you press play.
- ``setEvents`` swaps the list while the clip plays, and sounding notes still
  end on time.
- A clip and its clock can be destroyed in either order, but a clip only
  sends note-offs when you ``stop()`` it.

Next
----

- :doc:`/patcher/time`: beat-synced objects in a patch.
- :doc:`/api/clips`: the ``YSE::clip`` reference.
- :doc:`/api/c_api`: the C clip and clock functions.
