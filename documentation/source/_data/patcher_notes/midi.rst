.. Notes for the MIDI category. Format: _data/patcher_notes/README.md.

.. patcher-object:: .bendin

Reads only the coarse byte of the pitch-bend message. Over a two-semitone
bend range that is a step of about three cents, which can be heard on a slow
bend; use ``.xbendin`` for the full resolution. Subtract 64 before scaling
the value into a bend amount.

Port and channel arguments, and the order of the outlets, work as described
under ``.midiin``.

.. patcher-object:: .bendout

The value is sent as the coarse byte of the pitch-bend message, with the fine
byte set to 0. That matches what ``.bendin`` and ``.midiparse`` report, so a
value means the same on the way in and on the way out. ``.xbendout`` sends
the full 14 bits.

The channel argument and the output work as described under ``.noteon``,
except that a channel outside 0-15 is clamped.

.. patcher-object:: .borax

Each pair that arrives is a note-on when its velocity is not 0, and a
note-off when it is 0. The object measures the notes; it does not pass them
on, so wire it beside the path the notes take, not into it.

**Outlets**

Not every outlet fires on every message:

- A note-on sends the delta pair (when there was an earlier note-on), then
  velocity, pitch, polyphony, voice and serial.
- A note-off sends duration and note-off count, then velocity 0, pitch,
  polyphony, voice and serial.
- ``delta`` sends only the delta pair: the time since the last note-on, right
  now. Before the first note-on it sends nothing.

The serial is sent last and is the same at the attack and the release of a
note, so it identifies the note. Voices are the lowest free number from 1,
as ``.poly`` numbers them. A pitch played twice takes two voices, and the
releases close them oldest first. Durations and deltas are measured in
milliseconds with sub-millisecond precision, on the same clock ``.timer``
uses.

**Reset**

A bang on the right inlet sends a full note-off report for every note still
sounding, in voice order, then sets every counter back to 0 and forgets the
last note-on, so the next note-on reports no delta.

**Limits**

At most 128 notes are tracked at once. A note-on with 128 notes sounding is
ignored, and so is a note-off for a pitch no voice holds. Pitches and
velocities are not clamped.

.. patcher-object:: .channelpressure

The value is clamped to 0-127. The channel argument and the output work as
described under ``.noteon``.

.. patcher-object:: .controlchange

The value is clamped to 0-127. The channel and controller number are
creation arguments. The channel argument and the output work as described
under ``.noteon``.

``.xctlout`` sends a controller at 14-bit resolution.

.. patcher-object:: .ctlin

Wire the controller-number outlet into ``.route`` or ``.sel`` to split one
``.ctlin`` across a whole control surface. The default controller filter is
-1, not 0, because controller 0 (bank select) is a real controller.

A switch such as the sustain pedal sends 0 and 127 rather than a sweep.
``.xctlin`` combines the paired controllers 0-31 and 32-63 into 14-bit
values.

Port and channel arguments, and the order of the outlets, work as described
under ``.midiin``.

.. patcher-object:: .flush

Use it between the logic that plays notes and the object that sends them. A
pair with a velocity other than 0 marks the pitch as sounding, a pair with
velocity 0 clears it, and every pair passes through unchanged.

**Messages**

- bang: send pitch and velocity 0 for every note still sounding, from the
  lowest pitch up, and forget them. A second bang sends nothing.
- ``clear``: forget the sounding notes without sending anything.
- ``<pitch> <velocity>``: set the velocity, then play the pitch. This is the
  shape ``.midiparse`` sends from its note outlet.

The object has no channel. To release notes in a MIDI byte stream on the
channels they were played on, use ``.midiflush`` after the formatters.

Pitches outside 0-127 pass through but are not tracked. When the patcher is
cleared or destroyed, or the object is deleted, the object releases its
sounding notes first.

.. patcher-object:: .makenote

The velocity and duration inlets set the values for notes played after them;
a note already sounding keeps its own. A velocity of 0 is a release in MIDI,
so the pair passes through and nothing is scheduled. The velocity starts at
0, so give it as the first argument or send one before playing:

.. code-block:: text

   [.makenote 100 500]   60  =>  60 100 now, 60 0 half a second later

**Messages**

- ``stop``: release every sounding note now, in the order they were played.
- ``clear``: forget the sounding notes without releasing them.
- ``<pitch> <velocity>``: set the velocity, then play the pitch.

The duration is in milliseconds. A duration of 0 releases the note on the
next audio block. Durations follow the engine's audio clock: while the
engine is paused, pending releases wait. Note values such as ``4n`` and tick
counts are not supported; they are ignored and the duration is left as it
was.

A pitch played twice is two notes, each released after its own duration.

**Limits**

One ``.makenote`` holds at most 64 sounding notes. Past that, a new note is
refused completely: nothing is sent. The patcher also holds at most 128
pending timed messages, shared with ``.pipe``, ``.delay`` and the other
timing objects; when that runs out, the note is released at once instead of
after its duration.

When the patcher is cleared or destroyed, or the object is deleted, it
releases its sounding notes first.

.. patcher-object:: .midiflush

Put it in a MIDI byte stream, for example between the formatters and
``.midiout``. Everything that arrives passes out unchanged, in the form it
arrived in. On the way through, the object follows the stream and remembers
which notes are sounding on which channel.

A bang sends a note-off for every note still sounding, as numeric lists such
as ``128 60 0``, ordered by channel and then pitch, and forgets them.

The stream is decoded the way ``.midiparse`` decodes it, so notes sent under
running status are tracked, a clock byte between two data bytes does not
break a note, and bytes inside a system-exclusive dump are never read as
notes. Both forms of a release clear a note: a note-off, and a note-on with
velocity 0.

The inlet reads numeric byte lists, the binary messages the MIDI sender
objects build, and single ints. A numeric list holding a number outside
0-255 is dropped whole.

``.midiout``'s ``allnotesoff`` is a controller message that many devices
ignore; ``.midiflush`` sends a real note-off for each note. When the patcher
is cleared or destroyed, or the object is deleted, it flushes first.

.. patcher-object:: .midiformat

Every inlet except the channel sends a message at once, on the channel the
channel inlet holds (1 until set). The output is a list of byte values, such
as ``144 60 100``, one message per list.

**Inlets**

- note: ``<pitch> <velocity>``. A bare pitch is sent with velocity 0, which is
  a release. Releases are sent as note-ons with velocity 0, never as note-off
  messages.
- poly pressure: ``<pitch> <pressure>``; a bare pitch sends pressure 0.
- control change: ``<controller> <value>``; a bare number sends value 0.
- program change: 1-128, the numbering ``.pgmin`` and ``.midiparse`` report.
- aftertouch: one pressure value for the channel.
- pitch bend: 0-127, the coarse byte, with 64 at rest. The fine byte is sent
  as 0.
- channel: 1-16, stored only.
- raw: byte values passed straight to the outlet, so the system-exclusive and
  clock messages ``.midiparse`` sends from its rightmost outlet can be put
  back.

Values are clamped to the range MIDI allows, except on the raw inlet, where a
value outside 0-255 is dropped. A raw list is cut at 128 bytes.

Because ``.midiparse`` reports a note-off as velocity 0 and pitch bend as the
coarse byte, a stream taken apart and put back together loses note-off
release velocities and the fine bend byte.

.. patcher-object:: .midiin

The bytes arrive exactly as the device sent them, so a note-on is three ints
and a system-exclusive dump is as many ints as it has bytes. To work with
whole messages, use ``.xmidiin``; to decode them, wire ``.midiin`` into
``.midiparse``.

**Every MIDI input object**

The objects that receive from a MIDI device (``.midiin``, ``.xmidiin``,
``.notein``, ``.ctlin``, ``.sysexin`` and the others) share these rules:

- The first argument is the input port, counted from 0. ``.midiinfo`` lists
  the ports by name. Several objects can listen to one port; each receives
  every message.
- A port that cannot be opened, because the device is missing or another
  program holds it, leaves the object silent. The patch still loads.
- Changing the port argument while the patch runs recreates the object.
- Messages that arrive between two audio blocks reach the patch at the start
  of the next block. At most 64 short messages (512 bytes) are handed out per
  block; the rest wait for the next block, so a long dump arrives over
  several blocks.
- These objects exist only in builds with MIDI device support, which
  includes the Windows and Linux builds.

Objects that decode channel messages (``.notein``, ``.ctlin``, ``.bendin``,
``.pgmin``, ``.touchin``, ``.polyin``, ``.rpnin``, ``.nrpnin`` and the ``.x``
input objects) also share these:

- The second argument is the channel to listen to, 1-16. 0, the default,
  listens to all channels.
- Channels are reported as 1-16. The channel outlet is there even when the
  object listens to one channel, and then always reports that channel.
- The outlets fire from right to left, so the channel and the other values
  are out before the leftmost outlet triggers anything.

**Limits**

A message of up to 8192 bytes arrives whole. A longer one is dropped
entirely and logged, never passed on cut short. Messages are also dropped,
and logged, when they arrive faster than the patch takes them. At most 8
input ports can be open at once, and at most 8 objects can listen to one
port.

.. patcher-object:: .midiinfo

Input ports and output ports are numbered separately: input port 1 and
output port 1 are unrelated devices. The ``direction`` argument chooses
which list a bang reports.

**Messages**

- bang: send the number of ports, then for each port its index and then its
  name.
- ``input``, ``output``: switch which list is reported.
- ``refresh``: scan the machine again, for example after a device was plugged
  in. The scan runs in the background, and the full listing is sent again
  when it finishes, from the next audio block.

The ports are scanned when the object is created; a bang reports that scan
until ``refresh`` takes a new one. Names are sent whole, spaces included.

**Limits**

At most 32 ports are listed per direction, and names are cut at 63
characters. At most 16 ``.midiinfo`` objects can use ``refresh`` at the same
time. Only available in builds with MIDI device support.

.. patcher-object:: .midiout

**Input**

Each list is sent as one MIDI message, at its own length, so a program change
goes out as two bytes and a system-exclusive dump as a whole. Two forms are
accepted:

- numeric, ``144 60 100``: what ``.midiformat``, ``.sxformat``, ``.xmidiin``
  and ``.seq`` send;
- binary: what ``.noteon``, ``.bendout``, ``.rpnout`` and the other sender
  objects build.

A numeric list with a number outside 0-255, or longer than 256 bytes, is not
sent.

**Messages**

- ``allnotesoff``: controller 123 (all notes off) on all 16 channels.
- ``reset``: controller 121 (reset all controllers) on all 16 channels.
- ``omni on``, ``omni off``, ``poly on``, ``poly off``, ``local control on``,
  ``local control off``: the matching channel-mode message on channel 1.

**Opening the port**

The port opens when the first message arrives, not when the object is
created, so a patch loads on a machine without its devices. The port is
opened in the background, and messages sent before it is open are dropped,
including the first one. If the port cannot be opened, every message is
dropped.

Sending never blocks the audio thread: messages are handed to a separate
thread, which normally sends them within a millisecond, in order. If that
queue is full, a message is dropped.

At most 64 ``.midiout`` objects can exist at once; a further one never opens
its port. Only available in builds with MIDI device support.

.. patcher-object:: .midiparse

Bytes may arrive one at a time or as numeric lists, in any split: a message
spread over several lists decodes the same as one sent whole.

**Outlets**

- note: ``<pitch> <velocity>``. A note-off, and a note-on with velocity 0,
  both report velocity 0.
- poly pressure: ``<pitch> <pressure>``.
- control change: ``<controller> <value>``.
- program change: 1-128, the value on the wire plus one.
- aftertouch: the channel pressure.
- pitch bend: 0-127, the coarse byte only, with 64 at rest.
- channel: 1-16, sent before the outlet it belongs to.
- raw: everything else, as byte lists: system exclusive, song position, song
  select, time code, tune request, and clock messages (248-255).

The parser understands running status, so a chord sent with one status byte
decodes as separate notes. A clock byte that arrives in the middle of
another message is sent at once and does not disturb it. A
system-exclusive dump longer than 128 bytes leaves in consecutive pieces,
the first starting with 240 and the last ending with 247; a dump cut short
by another status byte ends there.

Data bytes that arrive with no status byte before them are dropped. A value
outside 0-255 is ignored. ``.midiformat`` is the inverse; see its notes for
what does not survive the round trip.

.. patcher-object:: .mpeconfig

MPE gives every sounding note its own MIDI channel, so pitch bend, pressure
and slide apply to one note. Before playing, both ends must agree how many
channels the zone uses; this message sets that.

A value on the inlet sends three control-change messages on the zone's
master channel: channel 1 for the lower zone, channel 16 for the upper.
Wire the outlet to ``.midiout``.

- 15, the default, gives the zone every other channel.
- A smaller number leaves room for a second zone or for ordinary MIDI.
- 0 switches MPE off on the device.

On receiving the message, a device sets its pitch-bend range to 48 semitones
on the member channels and 2 on the master channel, and stops its sounding
notes. MPE defines only the lower and upper zone.

.. patcher-object:: .mpeformat

Set the member channel on the right inlet first; it is stored and sends
nothing. Every other inlet sends one message on that channel at once, as a
numeric byte list. In MPE one channel is one note, so a polyphonic patch
changes the channel for each note.

**Inlets**

- note: ``<pitch> <velocity>``. A bare pitch is sent with velocity 0, a
  release. Releases are sent as note-ons with velocity 0.
- bend: 0-16383, 8192 at rest. The full 14 bits are needed at the 48-semitone
  bend range MPE devices use.
- pressure: sent as channel pressure, which on a member channel belongs to
  that note.
- slide: sent as controller 74.
- channel: 1-16.

Values are clamped to the range MIDI allows. ``.mpeconfig`` tells the device
how many member channels to expect.

.. patcher-object:: .mpeparse

Each message on the four left outlets is preceded by its role, then its
channel. The role says what the channel is doing in the zone:

- 2: the master channel. Its messages apply to every note.
- 1: a member channel. Its messages belong to the one note on that channel.
- 0: a channel outside the zone, such as ordinary MIDI on the same cable.

Pitch bend is reported at the full 14 bits. A release, in either form, is
reported as velocity 0.

The ``zone`` and ``members`` arguments say which channels to expect. An MPE
configuration message on the zone's master channel updates the member count
while the patch runs, and is not reported. The member count only changes
the role reported; nothing is filtered.

Program changes, other controllers, poly pressure and system exclusive are
not reported here. Wire ``.midiparse`` to the same source to read them. Input
is read the same way ``.midiparse`` reads it.

.. patcher-object:: .noteoff

The pitch and velocity are clamped to 0-127. The channel argument and the
output work as described under ``.noteon``.

``.xnoteout`` sends both note-ons and note-offs from one object.

.. patcher-object:: .noteon

A note-on with velocity 0 is a release on most devices, so ``.noteon`` can
end the notes it starts. The pitch and velocity are clamped to 0-127.

**The MIDI sender objects**

``.noteon``, ``.noteoff``, ``.controlchange``, ``.polypressure``,
``.channelpressure``, ``.programchange``, ``.bendout``, ``.xbendout``,
``.xbendout2``, ``.xctlout``, ``.xnoteout``, ``.rpnout`` and ``.nrpnout``
share these rules:

- The channel is a creation argument counted from 0: ``0`` is MIDI channel 1
  and ``15`` is channel 16. This differs from the input objects and
  ``.midiformat``, which number channels 1-16. Keep it within 0-15.
- The leftmost inlet sends the message. The other inlets store a value for
  the next message.
- Values are clamped to the range MIDI allows rather than refused.
- The output is a binary message meant for ``.midiout``. ``.midiflush``
  reads it too, but other objects, including ``.midiparse``, do not; use
  ``.midiformat`` for a message as a list of numbers.

.. code-block:: text

   [.notein] pitch, velocity  ->  [.noteon 0]  ->  [.midiout 1]

.. patcher-object:: .nrpnin

Works like ``.rpnin``, with controllers 99 and 98 selecting the parameter. A
non-registered number means whatever the device's documentation says; it is
where a hardware synthesiser keeps its own settings.

``.rpnin`` and ``.nrpnin`` follow the same selection, so each reports only
the writes to its own kind of parameter. Parameter 16383 is a valid
non-registered number and is reported.

.. patcher-object:: .nrpnout

Works like ``.rpnout``, with controllers 99 and 98 selecting the parameter.
A non-registered number means whatever the receiving device's documentation
says.

.. patcher-object:: .offer

It was made for note streams that are transformed on the way through: store
the original pitch with the pitch actually played, and when the note-off
arrives, its pitch looks up the played one.

**Inlets**

- y (right): arm a y value. Nothing is stored yet; only one y can wait, and a
  second one replaces the first. 0 is a valid y.
- x (left), with a y armed: store the pair x,y and disarm the y.
- x (left), with no y armed: look up x. If a pair matches, send its y and
  delete the pair; otherwise do nothing.

**Messages**

- ``<x> <y>``: arm y, then store the pair. A bare x is a lookup.
- bang: send the y of every stored pair, oldest first, without deleting
  anything.
- ``clear``: delete every pair and disarm a waiting y.

.. code-block:: text

   72 -> right inlet, 60 -> left inlet    stores 60,72
   60 -> left inlet                       sends 72 and deletes the pair

Storing an x that is already there keeps both pairs, and a lookup takes the
newest. A float is truncated to an int. The store holds at most 256 pairs; a
store past that is refused, and the armed y is still used up. Nothing is
saved with the patch.

.. patcher-object:: .pgmin

Programs are reported as 1-128, one more than the value on the wire, which
is how most devices display them. ``.midiformat`` takes the same numbering;
``.programchange`` takes the value on the wire, 0-127.

Port and channel arguments, and the order of the outlets, work as described
under ``.midiin``.

.. patcher-object:: .poly

Wire the voice outlet into ``.route`` or ``.gate`` to send each note to its
own voice chain. The voice number is sent last, after the velocity and
pitch, so it is the value that triggers.

**Allocation**

- A note-on takes the lowest free voice.
- A note-off frees the voice holding that pitch and sends the same voice
  number with velocity 0. A pitch played twice takes two voices; the
  releases free them oldest first.
- When every voice is busy and stealing is off, the note leaves the overflow
  outlet as ``<pitch> <velocity>`` and is not tracked.
- When stealing is on, the note held longest gives up its voice: its release
  is sent first, then the new note on the same voice number.
- A note-off for a pitch no voice holds leaves the overflow outlet as
  ``<pitch> 0``, so the releases of overflowed notes follow them. With
  stealing on this happens when a stolen note's key is released.

**Messages**

- ``stop``: release every sounding voice, in voice order.
- ``<pitch> <velocity>``: set the velocity, then play the pitch.

Pitches and velocities are not clamped. Making the pool smaller while notes
sound strands nothing: notes above the new size still get their releases.
When the patcher is cleared or destroyed, or the object is deleted, it
releases every sounding voice first.

.. patcher-object:: .polyin

Few keyboards send per-key pressure; most send only channel pressure, which
``.touchin`` reads. Match the pitch with the one ``.notein`` reported to
route the pressure to the right voice.

Port and channel arguments, and the order of the outlets, work as described
under ``.midiin``.

.. patcher-object:: .polypressure

The pitch and pressure are clamped to 0-127. The channel argument and the
output work as described under ``.noteon``.

.. patcher-object:: .programchange

Programs are numbered as on the wire, 0-127. Devices that display programs
from 1 show the value plus one, which is also what ``.pgmin`` reports. The
value is clamped to 0-127. The channel argument and the output work as
described under ``.noteon``.

.. patcher-object:: .rpnin

A parameter write is four control changes, not one message: controllers 101
and 100 select a 14-bit parameter number, then controllers 6 and 38 write a
14-bit value. The object remembers the selection for each channel and
reports each value written.

Registered numbers are defined by the MIDI standard: 0 is pitch-bend range,
1 fine tuning, 2 coarse tuning, 3 tuning program, 4 tuning bank.

A value is reported as soon as its coarse byte (controller 6) arrives, with
the fine byte as 0, and again when the fine byte (controller 38) follows.
Devices that send only the coarse byte are therefore reported in steps of
128. Selecting parameter 16383 (null) ends the selection, so later data
bytes are not reported.

Port and channel arguments, and the order of the outlets, work as described
under ``.midiin``.

.. patcher-object:: .rpnout

Each value sends four control-change messages: controllers 101 and 100 select
the parameter number from the right inlet, then controllers 6 and 38 write
the value. The selection is sent again with every value, so another device
on the cable cannot change it in between.

.. code-block:: text

   0 -> right inlet, then 1536 -> left inlet
   pitch-bend range of 12 semitones (12 * 128)

The selection is not reset to null afterwards. Send to parameter 16383 when
a device needs that.

.. patcher-object:: .rtin

A clock sends 24 timing messages per quarter note. Count them with
``.counter`` to follow an external sequencer's tempo, and use start and stop
to drive a ``.transport``. Active sensing (254) is filtered out.

Clock messages can arrive in the middle of other messages, such as a
system-exclusive dump; they reach this object whole without disturbing the
other objects on the port. The port argument works as described under
``.midiin``.

.. patcher-object:: .stripnote

Use it after a note source when only attacks matter, such as triggering a
sample. Every note source reports a release as velocity 0, so without it a
patch fires twice per key.

A list ``<pitch> <velocity>`` sets the velocity and then tests the pitch, so
``.midiparse``'s note outlet can be wired straight in. Nothing is clamped:
any velocity other than 0 passes.

.. patcher-object:: .sustain

A note-on always passes straight through. A note-off (velocity 0) passes
through with the pedal up, and is held back with the pedal down. Lifting the
pedal sends every held note-off, from the lowest pitch up.

**Messages**

- ``sustain 1``, ``sustain 0``: press or lift the pedal, as the right inlet
  does.
- ``flush``: send every held note-off now and leave the pedal where it is.
- ``clear``: forget the held note-offs without sending them.
- ``<pitch> <velocity>``: set the velocity, then play the pitch.

A key played again while its release is held sounds again, and the held
release still goes out when the pedal lifts. Pitches outside 0-127 are never
held.

Placed before a ``.flush``, it leaves the ``.flush`` tracking what is really
sounding. When the patcher is cleared or destroyed, or the object is
deleted, it sends its held note-offs first.

.. patcher-object:: .sxformat

The arguments are the template, one token per byte:

- a number 0-255: a constant byte,
- ``$i1`` to ``$i9``: a byte taken from an inlet; ``$i1`` is the left inlet,
- ``sumstart``: sends nothing, and starts the checksum region,
- ``sum``: a checksum byte that makes the region's total a multiple of 128,
  as Roland devices expect. Without ``sumstart``, the region is everything
  before it except a leading 240.

The object has one inlet for each placeholder up to the highest one used.
The left inlet sends the message; the others only store. A list on the left
inlet fills ``$i1``, ``$i2`` and so on, then sends. A bang sends with the
stored values.

A negative value leaves its byte out, so one template can cover a short and
a long form of a message. A value above 127 is clamped to 127. A
placeholder that has not received a value is 0.

.. code-block:: text

   [.sxformat 240 67 16 0 $i1 $i2 247]   5 99 -> left inlet  =>  240 67 16 0 5 99 247

The message leaves as a list of byte values for ``.midiout``. Start it with
240 and end it with 247; the object does not add them.

**Limits**

A template holds at most 256 tokens; the rest is cut, with an error in the
log. A token that is not a byte, a placeholder, ``sumstart`` or ``sum`` is
skipped, with an error in the log.

.. patcher-object:: .sysexin

Use it to receive the answer to a request built with ``.sxformat``. Nothing
is collected: bytes are sent as they arrive, so collect a whole dump
downstream, for example with ``.zl group``, and watch for the 247 that ends
it.

Clock bytes inside a dump are dropped. A status byte other than 247 ends the
dump without a 247 on the outlet, which happens when a device is interrupted
mid-transfer. A second 240 starts a new dump.

The port argument and the size limit work as described under ``.midiin``.

.. patcher-object:: .touchin

Most keyboards with aftertouch send this kind: one pressure value for the
whole channel, from the hardest-pressed key. Per-key pressure is
``.polyin``'s.

Port and channel arguments, and the order of the outlets, work as described
under ``.midiin``.

.. patcher-object:: .xbendin

Reports both bytes of the pitch-bend message combined. Subtract 8192 before
scaling the value into a bend amount. ``.bendin`` reports only the coarse
byte, and ``.xbendin2`` reports the two bytes separately.

Port and channel arguments, and the order of the outlets, work as described
under ``.midiin``.

.. patcher-object:: .xbendin2

Use it when the two bytes are wanted apart, or to pass a bend through
``.xbendout2`` unchanged. ``.xbendin`` reports the same message as one
number.

Port and channel arguments, and the order of the outlets, work as described
under ``.midiin``.

.. patcher-object:: .xbendout

The value is clamped to 0-16383. The channel argument and the output work as
described under ``.noteon``, except that a channel outside 0-15 is clamped.

.. patcher-object:: .xbendout2

The left inlet takes the coarse byte (64 at rest) and sends; the right inlet
stores the fine byte. Both are clamped to 0-127. The channel argument and the
output work as described under ``.noteon``, except that a channel outside
0-15 is clamped.

.. patcher-object:: .xctlin

MIDI pairs controllers 0-31 with controllers 32-63 as the coarse and fine
halves of one 14-bit value. A value is reported when the coarse controller
arrives, with the fine half as 0, and again when the fine controller
follows. A device that sends only the coarse half is reported in steps of
128.

The coarse value is remembered per channel and controller. Controllers
64-127 have no fine half and are not reported; use ``.ctlin`` for them.

Port and channel arguments, and the order of the outlets, work as described
under ``.midiin``.

.. patcher-object:: .xctlout

Each value sends two control-change messages: the coarse half on the
controller number, then the fine half on that number plus 32. Only
controllers 0-31 have a fine half, so the controller number is clamped to
0-31. The value is clamped to 0-16383.

The channel argument and the output work as described under ``.noteon``,
except that a channel outside 0-15 is clamped.

.. patcher-object:: .xmidiin

Unlike ``.midiin``, which sends single bytes, this object sends each
complete message as one list, ready for ``.midiparse``, ``.seq`` or
``.midiout``.

- Messages sent under running status get their status byte back.
- A clock byte (248-255) is sent at once as a list of its own, without
  disturbing the message it arrived in.
- A new status byte drops an unfinished message.
- A system-exclusive dump longer than 256 bytes leaves in consecutive
  256-byte lists.

The port argument and the size limit work as described under ``.midiin``.

.. patcher-object:: .xnotein

A note-off message carries a release velocity: how fast the key came up.
``.notein`` discards it; this object reports it. The velocity outlet still
reports 0 for every release, so a patch tests for a release the same way as
with ``.notein``. A note-on with velocity 0 has no release velocity and
reports 0 for both.

Port and channel arguments, and the order of the outlets, work as described
under ``.midiin``.

.. patcher-object:: .xnoteout

A velocity above 0 sends a note-on. A velocity of 0 sends a note-off message
carrying the release velocity from the right inlet, which defaults to 64.
All three values are clamped to 0-127. The middle and right inlets store
their value for the next pitch.

The channel argument and the output work as described under ``.noteon``,
except that a channel outside 0-15 is clamped.
