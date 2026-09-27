MIDI
====

The patcher has 45 MIDI objects. They fall into five groups:

- **Input** objects receive MIDI from a hardware port: ``.midiin``,
  ``.notein``, ``.ctlin`` and the rest of the ``*in`` family.
- **Senders** turn numbers into MIDI messages: ``.noteon``, ``.bendout``,
  ``.xctlout``, ``.rpnout`` and more. ``.midiout`` sends the result to a
  hardware port.
- **Codecs** take a byte stream apart and put it back together:
  ``.midiparse`` / ``.midiformat``, ``.sxformat``, and the MPE trio.
- **Note handling** objects keep a patch's notes in order: ``.makenote``,
  ``.stripnote``, ``.flush``, ``.sustain``, ``.poly``, ``.borax``,
  ``.offer`` and ``.midiflush``.
- **Port lookup**: ``.midiinfo`` lists the ports a machine has.

Only the objects that hold a hardware port depend on the platform. Everything
else works everywhere, also in a build without MIDI device support. This page
explains how MIDI moves into and out of a patch, which objects exist where,
and how the groups fit together. :doc:`objects/midi` has the full reference
for every object.

.. _midi-platforms:

Platform availability
---------------------

MIDI device I/O uses RtMidi and is controlled by the ``YSE_ENABLE_MIDI_DEVICE``
CMake option.

.. list-table::
   :header-rows: 1
   :widths: 30 20 50

   * - Platform
     - Default
     - Notes
   * - Windows
     - ``ON``
     -
   * - Linux (desktop)
     - ``ON``
     -
   * - macOS
     - ``OFF``
     - The MIDI device backend is not supported on macOS.
   * - Android
     - ``OFF``
     - RtMidi is not built for Android.

You can switch the option off on Windows and Linux with
``-DYSE_ENABLE_MIDI_DEVICE=OFF``. That build does not need RtMidi at all.

When the option is off, the 18 objects that hold a port are **not
registered**. The other 27 MIDI objects are always registered.

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Availability
     - Objects
   * - Only with ``YSE_ENABLE_MIDI_DEVICE``
     - ``.midiin``, ``.notein``, ``.ctlin``, ``.bendin``, ``.pgmin``,
       ``.touchin``, ``.polyin``, ``.rtin``, ``.sysexin``, ``.xbendin``,
       ``.xbendin2``, ``.xctlin``, ``.xnotein``, ``.xmidiin``, ``.rpnin``,
       ``.nrpnin``, ``.midiinfo``, ``.midiout``
   * - Every build, every platform
     - ``.noteon``, ``.noteoff``, ``.controlchange``, ``.polypressure``,
       ``.channelpressure``, ``.programchange``, ``.bendout``,
       ``.xbendout``, ``.xbendout2``, ``.xctlout``, ``.xnoteout``,
       ``.rpnout``, ``.nrpnout``, ``.midiparse``, ``.midiformat``,
       ``.sxformat``, ``.mpeconfig``, ``.mpeformat``, ``.mpeparse``,
       ``.midiflush``, ``.makenote``, ``.stripnote``, ``.flush``,
       ``.sustain``, ``.poly``, ``.borax``, ``.offer``

The senders used to be Windows-only. Since 3.0 they are registered
everywhere, because they only build bytes and open nothing (issue #746). A
patch that formats MIDI and sends it somewhere other than a port (to ``.seq``,
to a file, or to your own code through a ``.s``) now loads on every platform.

What happens to an object that is not registered:

- ``CreateObject(".notein")`` returns ``nullptr`` and logs an error (see
  :doc:`building`).
- ``ParseJSON`` skips the object and every cord to or from it, logs it, and
  loads the rest of the patch (see :doc:`file_format`). The patch still
  plays, but without MIDI input or output.

To check this ahead of time, use the C API's
``yse_patcher_get_metadata_json()``. Each object in it has a
``"requires_midi_device"`` field, which is true for exactly the 18 objects
above. A patch editor can use it to mark those objects as not portable. The
flag is the same in every build, so metadata taken on Windows also tells you
what will be missing on Android.

In C++, the port-listing functions ``YSE::System().getNumMidiInDevices()``,
``getMidiInDeviceName()`` and their output counterparts are only declared when
the option is on. Guard such code with ``#if YSE_ENABLE_MIDI_DEVICE``.

Two things to know about numbers
--------------------------------

Channels are not numbered the same way everywhere
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. list-table::
   :header-rows: 1
   :widths: 50 25 25

   * - Objects
     - Channel
     - Program
   * - Input objects, ``.midiparse``, ``.midiformat``, ``.mpeparse``,
       ``.mpeformat``
     - 1-16
     - 1-128
   * - The single-message senders (``.noteon``, ``.controlchange``,
       ``.programchange``, ``.bendout``, the ``.x*out`` family, ``.rpnout``,
       ``.nrpnout``)
     - 0-15
     - 0-127

The input objects and the codecs count the way hardware displays it, from 1,
as in Max. The single-message senders take their channel as a creation
argument that is added to the status byte, so ``.noteon 0`` sends on the
channel hardware calls 1. ``.programchange`` sends the program number as
given, 0-127. ``.midiformat`` takes 1-128 and subtracts one.

Two spellings of a MIDI message
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

A MIDI message travels between objects as a list, in one of two spellings:

**Numeric** (``144 60 100``)
   Each byte is written as its decimal number. ``.midiformat``,
   ``.sxformat``, ``.mpeconfig``, ``.mpeformat``, ``.xmidiin`` and ``.seq``
   send this spelling. It is the only one that ``.midiparse``, ``.mpeparse``
   and ``.seq`` read.

**Binary**
   The characters of the list *are* the bytes. The single-message senders
   (``.noteon``, ``.noteoff``, ``.controlchange``, ``.polypressure``,
   ``.channelpressure``, ``.programchange``, ``.bendout``, the ``.x*out``
   family, ``.rpnout`` and ``.nrpnout``) send this spelling.

``.midiout`` and ``.midiflush`` read both. They tell them apart by the first
character: a MIDI message starts with a status byte of 128 or more, which is
never a decimal digit. Any other object only understands the numeric
spelling. To record or decode what you build, use ``.midiformat`` (or
``.mpeformat``, ``.sxformat``) rather than the single-message senders.

MIDI input
----------

How input reaches a patch
~~~~~~~~~~~~~~~~~~~~~~~~~

MIDI arrives on RtMidi's own thread, one per open port. The patch runs on the
audio thread. The two never touch directly. An engine-wide hub
(``MIDI::inHub``) sits between them:

1. The first input object that asks for a port opens it. This happens on the
   control thread, when the object joins its patcher. Later objects on the
   same port share the open device, and each one gets **a queue of its own**.
2. When a message arrives, RtMidi's thread copies it into the queue of every
   object listening to that port. The copy is wait-free and allocates
   nothing. A message longer than 8 bytes (a SysEx dump) is split into
   8-byte chunks, in order.
3. At the start of every block the patcher renders, each input object empties
   its queue and sends what it found out of its outlets. What that triggers is
   rendered in the same block.

So a MIDI message reaches the patch at the start of the next block after it
arrived. Latency is at most one block, plus whatever the driver adds.

The input objects have no inlets. The patcher finds them anyway: they ask to
be polled once per block. This means that **input only arrives while the
patcher renders**. A patcher that is not attached to a sound or an insert
(see :doc:`index`) does not drain its queues, and they fill up.

When the last object on a port is deleted, the port is closed.

Limits
~~~~~~

.. list-table::
   :header-rows: 1
   :widths: 55 45

   * - Limit
     - Value
   * - Input ports open at once (``port`` argument)
     - 8 (ports 0-7)
   * - Input objects listening to one port
     - 8
   * - Events one object can hold between two blocks
     - 63 (each up to 8 bytes)

These are fixed so that RtMidi's thread can walk the table without a lock.

- An object whose ``port`` is 8 or higher, or the ninth object on one port,
  gets no subscription. The refusal is logged. The object is still valid, but
  it never receives anything.
- When an object's queue is full, the new events are **dropped**. Each drop is
  counted, and a warning is logged once per episode, not once per message. A
  full queue normally means the audio thread has stalled or the patcher is not
  rendering, since a controller does not send 63 messages in a few
  milliseconds.

.. warning::

   A SysEx dump arrives as one message and is split into 8-byte chunks all
   at once. A dump longer than about 500 bytes does not fit into an empty
   queue, so its tail is dropped. Tracked in
   `#950 <https://github.com/yvanvds/yse-soundengine/issues/950>`_.

Ports that do not open
~~~~~~~~~~~~~~~~~~~~~~

The ``port`` argument is an index into
``YSE::System().getMidiInDeviceName(...)``, and 0 by default. If the port
does not open (nothing is plugged in, or the backend allows one client per
port and the host already has it open), the object stays valid and simply
receives nothing. The failure is logged. This way a patch loads the same on a
machine without a controller.

Changing the ``port`` argument of a live object rebuilds the object, because
the subscription is taken when the object joins its patcher.

Finding a port: ``.midiinfo``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Every MIDI object addresses a port by a bare index, and indexes differ between
machines. ``.midiinfo`` tells a patch what each index is. A bang sends:

1. the number of ports out of the right outlet,
2. then, for each port in order, its index (middle outlet) followed by its
   name (left outlet).

Input and output ports are numbered separately. The ``direction`` argument
(``input``, the default, or ``output``) selects the set, and the messages
``input`` and ``output`` switch it. A patch can match a name and send the
index it found to the object that should open it.

The port list is read once, when the object joins its patcher, because
listing ports allocates and talks to the operating system. A device plugged
in later appears after a ``refresh`` message. The scan runs on a background
thread, and the whole list is sent again once it is done, at the start of a
later block. At most 32 ports are listed, and names are cut at 63
characters.

The input objects
~~~~~~~~~~~~~~~~~

All of them take ``port`` as the first argument. Most take ``channel`` as the
second one (1-16, or 0 for all channels). Unlike Max, a ``channel`` argument
does **not** remove the channel outlet. The outlet stays and reports the
filtered channel, so the outlet numbering never depends on the arguments and
saved cords stay valid.

.. list-table::
   :header-rows: 1
   :widths: 18 42 40

   * - Object
     - Receives
     - Outlets, left to right
   * - ``.midiin``
     - every byte, undecoded
     - byte (one int per byte)
   * - ``.xmidiin``
     - every message, framed
     - one numeric list per whole message
   * - ``.notein``
     - note-on and note-off
     - pitch, velocity, channel
   * - ``.xnotein``
     - notes with release velocity
     - pitch, velocity, release velocity, channel
   * - ``.ctlin``
     - control change
     - value, controller, channel
   * - ``.xctlin``
     - 14-bit control change (controllers 0-31 + 32-63)
     - value 0-16383, controller, channel
   * - ``.bendin``
     - pitch bend, coarse byte
     - bend 0-127 (64 = centre), channel
   * - ``.xbendin``
     - pitch bend, 14 bits
     - bend 0-16383 (8192 = centre), channel
   * - ``.xbendin2``
     - pitch bend, as two bytes
     - MSB, LSB, channel
   * - ``.pgmin``
     - program change
     - program 1-128, channel
   * - ``.touchin``
     - channel aftertouch
     - pressure, channel
   * - ``.polyin``
     - polyphonic key pressure
     - pitch, pressure, channel
   * - ``.rtin``
     - system real-time
     - status byte (248 clock, 250 start, 251 continue, 252 stop, 255 reset)
   * - ``.sysexin``
     - system exclusive only
     - byte (one int per byte, from 240 to 247)
   * - ``.rpnin`` / ``.nrpnin``
     - registered / non-registered parameter writes
     - value 0-16383, parameter number, channel

Behaviour they share:

- **Outlets fire right to left.** The channel goes out first and the leftmost
  value last. Whatever the leftmost outlet triggers already has the other
  values.
- **A note-off is velocity 0.** ``.notein`` reports both spellings of a
  release (a note-off message, or a note-on with velocity 0) as the pitch
  with velocity 0. Test for it once, with ``.sel 0`` on the velocity.
  ``.xnotein`` also reports the release velocity that a note-off message
  carries.
- **Active sensing (254) is filtered out** by the backend. Timing clock and
  SysEx are passed on.
- ``.ctlin``'s optional third argument filters by controller number. Its
  default is -1 (all), because controller 0 is a real controller (Bank
  Select).

Decoding and encoding a stream
------------------------------

``.midiparse`` and ``.midiformat`` are exact inverses. ``.midiparse`` takes
raw bytes (one int at a time, as ``.midiin`` sends them, or a numeric list)
and sends each complete message out of the outlet for its type:

.. list-table::
   :header-rows: 1
   :widths: 10 30 60

   * - Pin
     - ``.midiparse`` outlet / ``.midiformat`` inlet
     - Contents
   * - 0
     - note
     - ``pitch velocity`` (a release is velocity 0)
   * - 1
     - poly
     - ``pitch pressure``
   * - 2
     - control
     - ``controller value``
   * - 3
     - program
     - 1-128
   * - 4
     - aftertouch
     - 0-127
   * - 5
     - bend
     - 0-127, coarse byte only
   * - 6
     - channel
     - 1-16. On ``.midiformat`` this inlet is cold.
   * - 7
     - raw
     - everything else, as a byte list: SysEx, song position, time code,
       real-time bytes

Wire outlet *n* of ``.midiparse`` to inlet *n* of ``.midiformat`` and the
stream passes through. In between, a patch can change the part it cares
about. ``.midiformat`` sends one whole message per numeric list, on the
channel its channel inlet holds (1 by default).

``.midiparse`` handles the parts of MIDI that make a byte search go wrong:

- **Running status.** A chord is often sent as one status byte followed by
  several pitch/velocity pairs. Each pair decodes as a note.
- **Interleaving.** A real-time byte (a clock) may arrive in the middle of
  another message. It goes out of the raw outlet at once, and the interrupted
  message continues.
- **Long messages.** A SysEx dump longer than 128 bytes leaves the raw outlet
  in consecutive chunks.

Two readings are lossy, as in Max: pitch bend keeps only the coarse byte, and
a note-off message comes back from ``.midiformat`` as a note-on with velocity
0. For full-resolution bend use ``.xbendin`` / ``.xbendout``.

``.xmidiin`` does the framing part of this on a port: it sends every whole
message as one numeric list, with running status expanded.

Sending MIDI
------------

The senders
~~~~~~~~~~~

Each single-message sender builds one kind of message. The left inlet is hot
and sends; the other inlets store a value for the next message.

.. list-table::
   :header-rows: 1
   :widths: 22 38 40

   * - Object
     - Inlets
     - Arguments
   * - ``.noteon`` / ``.noteoff``
     - pitch, velocity
     - channel (0-15)
   * - ``.xnoteout``
     - pitch, velocity, release velocity
     - channel
   * - ``.controlchange``
     - value
     - channel, controller
   * - ``.xctlout``
     - value 0-16383, controller 0-31
     - channel
   * - ``.polypressure``
     - pitch, pressure
     - channel
   * - ``.channelpressure``
     - pressure
     - channel
   * - ``.programchange``
     - program 0-127
     - channel
   * - ``.bendout``
     - bend 0-127 (64 = centre)
     - channel
   * - ``.xbendout``
     - bend 0-16383 (8192 = centre)
     - channel
   * - ``.xbendout2``
     - MSB, LSB
     - channel
   * - ``.rpnout`` / ``.nrpnout``
     - value 0-16383, parameter 0-16383
     - channel

Values out of range are clamped, not refused. A patch that scales a
controller past 127 plays at the top of the range instead of falling silent.

``.midiformat`` does the work of ``.noteon``, ``.controlchange``,
``.polypressure``, ``.channelpressure``, ``.programchange`` and ``.bendout``
in one object, on one channel setting. It sends a release as a note-on with
velocity 0; use ``.noteoff`` or ``.xnoteout`` for a real note-off message.

``.midiout``
~~~~~~~~~~~~

``.midiout <port>`` sends every list it receives to a hardware output port,
as one MIDI message, at the length it has. It reads both spellings (see
above). A numeric list with a value outside 0-255, or longer than 256 bytes,
is dropped whole. It also understands the messages ``allnotesoff``,
``reset``, ``omni on``, ``omni off``, ``poly on``, ``poly off``,
``local control on`` and ``local control off``.

The port opens on the first message, not when the object is created, so a
patch loads on a machine with different devices. Opening a port can block,
and the first message may well arrive on the audio thread, so the open runs
on a background thread (issue #759). **That first message is dropped**, and so
is anything else that arrives before the port is open, usually just the
next few milliseconds. If the very first message matters, send something
harmless first, such as a controller the device ignores.

The send is not made on the thread that delivered the message either
(issue #949). ``.midiout`` puts the bytes on a queue, and a dedicated MIDI
sender thread passes them to the device, usually within a millisecond.
Messages sent from one thread keep their order. If the queue is ever full,
the message is dropped and counted, not sent late.

Extended precision
~~~~~~~~~~~~~~~~~~

Plain MIDI values have 7 bits. The ``x`` objects use the extra bits that the
protocol has room for:

- **Pitch bend** is really 14 bits. ``.bendin`` / ``.bendout`` use only the
  coarse byte (0-127), as Max does. Over a two-semitone range that is a step
  of about three cents, which is audible on a slow bend. ``.xbendin`` /
  ``.xbendout`` use all 14 bits (0-16383). ``.xbendin2`` / ``.xbendout2``
  keep the two bytes apart, so a value passes through unchanged.
- **Controllers 0-31** each have a fine partner 32 higher.
  ``.xctlin`` / ``.xctlout`` treat the pair as one 14-bit value.
  ``.xctlout`` sends the coarse message first, as the MIDI specification
  requires. ``.xctlin`` sends a value when the coarse byte arrives (with the
  fine byte as 0) and again when the fine byte follows, so devices that
  only send the coarse byte still work.
- **Release velocity**: a note-off message has a velocity byte of its own.
  ``.xnotein`` reports it and ``.xnoteout`` sends it.

RPN and NRPN
~~~~~~~~~~~~

Registered and non-registered parameter numbers reach parameters that no
controller number covers. One write is four control changes: two select a
14-bit parameter number (controllers 101/100 for RPN, 99/98 for NRPN), and
two carry a 14-bit value (controllers 6 and 38).

- ``.rpnout`` / ``.nrpnout`` send all four for every value, and each goes
  out as its own list, because ``.midiout`` sends every list as a single
  message. The selection is sent again with every value, because anything
  else on the cable may have changed it. The closing RPN Null is not
  appended, as in Max. Send parameter 16383 yourself if you want it.
- ``.rpnin`` / ``.nrpnin`` remember the selected parameter **per channel**
  and report each write as value, parameter, channel. Both kinds move the
  same selection, so each object only reports its own kind. ``.rpnin``
  treats parameter 16383 (the RPN Null) as "nothing selected".

Registered numbers mean the same on every device: 0 is pitch-bend range, 1
fine tuning, 2 coarse tuning. Non-registered numbers are defined by the
manufacturer.

System exclusive
----------------

``.sysexin <port>`` sends every byte of every SysEx message, from 240 to 247,
and nothing else. It is the receiving half of a patch dump: request a patch
with ``.sxformat``, and the answer arrives here without notes or clocks mixed
in. A real-time byte in the middle of a dump is dropped (``.rtin`` gets it).
Any other status byte ends the dump, because hardware that is interrupted
simply stops sending. The object stores nothing, so collect the dump
downstream and watch for the 247. See the warning under *Limits* above for
long dumps.

``.sxformat`` builds a SysEx message from a template in its arguments, one
token per byte:

.. list-table::
   :header-rows: 1
   :widths: 25 75

   * - Token
     - Meaning
   * - ``0``-``255``
     - a constant byte
   * - ``$i1`` … ``$i9``
     - the value held by that inlet. ``$i1`` is the left, hot inlet. The
       object gets one inlet per placeholder, up to the highest one used.
   * - ``sumstart``
     - emits nothing and marks where the checksum region starts
   * - ``sum``
     - the Roland checksum of the region: the byte that makes its total a
       multiple of 128. Without ``sumstart`` the region is everything so
       far, except a leading 240.

For example, a Roland-style parameter change whose last data byte comes from
the left inlet:

.. code-block:: text

   .sxformat 240 65 16 66 18 sumstart 64 0 4 $i1 sum 247

A number, a list (which fills ``$i1``, ``$i2`` … from left to right) or a bang
on the left inlet sends the whole message as one numeric list. A negative
value leaves its byte out, so one template can cover a short and a long form.
A value above 127 is clamped, because a byte with the top bit set would end
the message early at the device. Templates are limited to 256 tokens.

MPE
---

MIDI Polyphonic Expression gives every sounding note its own channel, so
pitch bend, channel pressure and controller 74 ("slide") work per note. A
zone has a master channel (1 for the lower zone, 16 for the upper zone) and a
number of member channels, one note each.

``.mpeconfig <zone>``
   A number on the inlet (0-15, default 15) sends the MPE Configuration
   Message: three control changes, each its own list, on the master channel.
   0 turns the zone off. Only the two zones that MPE 1.0 defines are
   supported (``0`` lower, ``1`` upper), not Max's seven.

``.mpeformat``
   Builds note, bend, pressure and slide messages for one member channel.
   Inlets: note (``pitch velocity``), bend (0-16383, 14 bits, because MPE
   devices use a 48-semitone range), pressure, slide, and a cold channel
   inlet (1-16). Unlike Max it has one channel inlet rather than one inlet
   per member channel.

``.mpeparse <zone> <members>``
   The inverse. Outlets: note, bend, pressure, slide, channel, and role
   (2 master, 1 member, 0 not in this zone). The role goes out first. A
   Configuration Message on the zone's master channel updates the member
   count as it passes, and is not reported. Program changes, other
   controllers and SysEx are not decoded here; wire a ``.midiparse`` to the
   same source for those.

All three are codecs and work on every platform.

Handling notes
--------------

The senders do not remember anything. A patch that sends a note-on has to
send the note-off itself, and a note-off that never arrives is a hanging
note. These objects keep track of notes. Apart from ``.midiflush`` they work
on pitch/velocity pairs, before a channel is chosen, and they send the same
kind of pairs.

.. list-table::
   :header-rows: 1
   :widths: 18 82

   * - Object
     - What it does
   * - ``.makenote <velocity> <duration>``
     - Plays a note: sends the pair now, and the same pitch with velocity 0
       after the duration (ms). Up to 64 notes per object. ``stop`` releases
       all of them now, ``clear`` forgets them.
   * - ``.stripnote``
     - Passes note-ons and drops every pair with velocity 0. Use it when only
       attacks matter, so a key does not trigger twice.
   * - ``.flush``
     - Passes pairs through and remembers which pitches are sounding. A bang
       releases them all.
   * - ``.sustain``
     - A sustain pedal. While the pedal is down (right inlet non-zero), it
       holds note-offs back and sends them when the pedal lifts.
   * - ``.poly <voices> <steal>``
     - Gives each note a voice number (outlets: voice, pitch, velocity,
       overflow), so a ``.route`` can send each note to one of N voice chains.
       The release comes out with the same voice number. Up to 128 voices;
       stealing (oldest first) is off by default.
   * - ``.borax``
     - Only measures: note serial number, voice, how many notes are
       sounding, durations and time between attacks. It sends no notes.
   * - ``.offer``
     - Stores a pair and returns the second value once, when the first is
       asked for. A transposer uses it to find the pitch it sounded when the
       note-off for the original pitch arrives.
   * - ``.midiflush``
     - Works on a MIDI byte stream instead of pairs. Passes everything
       through, remembers which notes on which channels are sounding, and
       sends a note-off for each on a bang.

All pair objects share a few rules:

- **Velocity is sent before pitch.** Outlets fire right to left, so the
  velocity reaches a sender's cold inlet before the pitch reaches its hot
  one. Wire outlet 0 to the pitch inlet and outlet 1 to the velocity inlet.
- **A list on the left inlet** is ``pitch velocity``, which is what
  ``.midiparse``'s note outlet sends. So ``.midiparse`` → ``.stripnote`` is
  a single cord.
- **Velocity 0 is a release**, everywhere.

A typical order is: note source → ``.sustain`` → ``.flush`` → a sender
(``.noteon``, ``.midiformat``) → ``.midiflush`` → ``.midiout``. ``.flush``
sees what is really sounding, pedal included, and ``.midiflush`` sees what
reached the wire. ``.flush`` and ``.midiflush`` are complements, not
alternatives: one works before the channel is decided, the other after.

``.makenote`` waits on the patcher's deferred-message scheduler (see
:doc:`time`), so its timing has a resolution of one block, and a paused
engine holds its pending releases. Besides its own limit of 64 notes, its
pending releases count toward the patcher-wide limit of 128 pending messages
that it shares with ``.delay``, ``.pipe``, ``.qlist``, ``.mtr`` and ``.seq``.
When its own 64 are taken, a new note is refused whole, attack included. When
the patcher-wide budget is full, the note is released at once rather than
never.

Held notes on teardown
~~~~~~~~~~~~~~~~~~~~~~

Since 3.0, a patch that goes away does not leave notes hanging (issue
#758). When a patcher is cleared or destroyed, or a single object is deleted,
the patcher first lets every object stop, and only then removes the cords.
The objects that hold notes release them in that step:

- ``.makenote`` releases every pending note (its ``stop``).
- ``.flush`` and ``.midiflush`` send their note-offs (a bang).
- ``.sustain`` sends the note-offs it was holding back.
- ``.poly`` releases every voice (its ``stop``).

Because the cords are still there, the releases travel down the patch and
reach ``.midiout`` like any other message. ``.borax``, ``.stripnote`` and
``.offer`` have nothing to release. A process that is killed outright can
release nothing, so a ``.midiflush`` before ``.midiout`` is still the safety
net during normal use.

Examples
--------

A patch played from a keyboard on input port 0:

.. code-block:: cpp

   YSE::patcher patch;
   patch.create(2);

   YSE::pHandle* keys = patch.CreateObject(".notein", "0");  // port 0, all channels
   YSE::pHandle* mtof = patch.CreateObject(".mtof");
   YSE::pHandle* osc  = patch.CreateObject("~sine");
   YSE::pHandle* dac  = patch.CreateObject("~dac");

   patch.Connect(keys, 0, mtof, 0);  // pitch
   patch.Connect(mtof, 0, osc, 0);   // frequency
   patch.Connect(osc, 0, dac, 0);
   patch.Connect(osc, 0, dac, 1);

   // keys is nullptr in a build without YSE_ENABLE_MIDI_DEVICE.

A generative line sent to hardware, with a safety net:

.. code-block:: cpp

   YSE::pHandle* note  = patch.CreateObject(".makenote", "100 250");  // velocity, ms
   YSE::pHandle* on    = patch.CreateObject(".noteon", "0");          // channel 1
   YSE::pHandle* guard = patch.CreateObject(".midiflush");
   YSE::pHandle* out   = patch.CreateObject(".midiout", "0");         // output port 0

   patch.Connect(note, 0, on, 0);    // pitch (hot)
   patch.Connect(note, 1, on, 1);    // velocity (cold, arrives first)
   patch.Connect(on, 0, guard, 0);
   patch.Connect(guard, 0, out, 0);

   // Anything that sends pitches into note's left inlet plays; a bang on
   // guard releases whatever is still sounding.

See also
--------

- :doc:`objects/midi`: the reference for every MIDI object.
- :doc:`messages`: hot and cold inlets and the order in which outlets fire.
- :doc:`time`: the deferred-message scheduler and its limits.
- :doc:`files`: reading and writing Standard MIDI Files with ``.seq``.
