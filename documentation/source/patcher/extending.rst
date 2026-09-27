Writing a patcher object
========================

This page is for contributors who want to add an object type to the engine. It
walks through how the existing objects are built: the class and its macros,
inlets and outlets, creation arguments, the documentation every object
carries, signal objects, the optional hooks, registration, and the tests. It
uses two real objects as examples: ``.swap`` (a control object, ``gSwap``) and
``~saw`` (a signal object, ``dSaw``).

Read :doc:`realtime` first. It explains which thread a handler runs on and the
rules that follow from that. This page does not repeat them. It shows how an
object meets them.

The files you touch
-------------------

Adding ``.swap`` (issue `#476
<https://github.com/yvanvds/yse-soundengine/issues/476>`_) changed these
files. A new object touches the same set:

.. list-table::
   :header-rows: 1
   :widths: 44 56

   * - File
     - What you add
   * - ``YseEngine/patcher/<folder>/gSwap.h`` and ``.cpp``
     - The object itself. The folder is the family: ``math``,
       ``genericObjects``, ``guiObjects``, ``midi``, ``time``, ``filters``,
       ``generatorObjects``.
   * - ``YseEngine/patcher/pObjectList.hpp``
     - The type name constant: ``DEFOBJ(G_SWAP, ".swap");``. Also add the
       constant to the grouped list in the comment at the top of the file.
   * - ``YseEngine/patcher/pRegistry.cpp``
     - An ``#include`` of the header and one ``Add(...)`` line.
   * - ``YseEngine/CMakeLists.txt``
     - The ``.cpp`` in ``YSE_SRCS`` **and** in the ``PATCHER_SUPPRESS``
       ``set_source_files_properties`` list.
   * - ``Tests/patcher/test_patcher_swap.cpp``
     - The object's own test file.
   * - ``Tests/CMakeLists.txt``
     - The test file in ``YSE_TEST_SOURCES`` **and** in the
       ``_patcher_test_suppress`` ``set_source_files_properties`` list.
   * - ``documentation/source/_data/patcher_objects.json``
     - Regenerated with ``python yse.py dump-patcher-meta``. Never edit it by
       hand.
   * - ``PROJECT_OVERVIEW.md``
     - A mention in the patcher node list.

Two CMake lists per side is easy to get wrong. The second list gives patcher
files the warning flags they need (``-Wno-unused-parameter`` and the
``json.hpp`` suppressions). Leave a file out of it and it still builds, but
with a page of warnings.

You do **not** touch the C API. It reads the registry, so a registered object
can be created with ``yse_patcher_create_object`` and is described by the
metadata calls without any change there (see `The C API`_).

Names
-----

Four names identify an object, and they follow fixed patterns:

.. list-table::
   :header-rows: 1
   :widths: 22 30 48

   * - Name
     - Example
     - Rule
   * - Type name
     - ``.swap``, ``~saw``
     - ``~`` for a signal object, ``.`` for a control object. This is the
       string patches, ``DumpJSON`` and the C API use, so it can never change
       once released.
   * - ``YSE::OBJ`` constant
     - ``G_SWAP``, ``D_SAW``
     - ``D_`` for signal objects, ``G_`` for control objects, ``M_`` for MIDI
       objects.
   * - Class
     - ``gSwap``, ``dSaw``
     - ``g`` for control, ``d`` (or the older ``p``) for signal, ``m`` for
       MIDI.
   * - Files
     - ``gSwap.h``, ``gSwap.cpp``
     - The class name.

Most objects follow a Max or Pd object. Keep Max's name and behaviour unless
there is a reason not to, and write the reason down in the header.

The class
---------

An object is a subclass of ``YSE::PATCHER::pObject``, declared with the
macros at the bottom of ``YseEngine/patcher/pObject.h``. This is the
declaration of ``.swap``, from ``math/gSwap.h`` (the long doc comment and the
test accessors are left out):

.. code-block:: cpp

   #pragma once
   #include "../pObject.h"
   #include <cstddef>
   #include <string>

   namespace YSE {
     namespace PATCHER {

       PATCHER_CLASS(gSwap, YSE::OBJ::G_SWAP)
       _NO_MESSAGES
       _NO_CALCULATE

       _BANG_IN(SetLeftBang)
       _INT_IN(SetLeftInt)
       _FLOAT_IN(SetLeftFloat)
       _LIST_IN(SetLeftList)

       _INT_IN(SetRightInt)
       _FLOAT_IN(SetRightFloat)

       _PARM_CLEAR
       _PARM_PARSE

       // ... accessors the tests read ...

     private:
       // ... helpers ...
       std::string argument;
       Value left;
       Value right;
     };

   } // namespace PATCHER
   } // namespace YSE

What the macros do:

``PATCHER_CLASS(className, name)``
   Opens ``class className : public pObject`` with a ``public:`` section,
   declares the constructor, a ``Type()`` that returns ``name``, and a static
   ``Create()`` for the registry. You close the class with ``};`` yourself.
``_NO_MESSAGES`` / ``_DO_MESSAGES``
   Whether the object takes word commands through ``SetMessage``. Most
   objects parse words in their list handler instead and use
   ``_NO_MESSAGES``. ``_DO_MESSAGES`` also makes ``HandlesMessages()`` answer
   true, which changes how a message box's text reaches the object (see
   :doc:`messages`).
``_NO_CALCULATE`` / ``_DO_CALCULATE``
   Whether ``Calculate()`` does anything (see `When an object computes`_).
``_BANG_IN(f)``, ``_INT_IN(f)``, ``_FLOAT_IN(f)``, ``_LIST_IN(f)``, ``_BUFFER_IN(f)``
   Declare one message handler each. Signatures:
   ``(int inlet, THREAD)`` for a bang, ``(value, int inlet, THREAD)`` for the
   others, with ``const std::string&`` for a list and ``DSP::buffer*`` for a
   buffer.
``_PARM_CLEAR`` / ``_PARM_PARSE``
   Declare the two argument callbacks (see `Creation arguments`_).
``_DO_RESET``
   Declares ``ResetDSP()`` for a signal object that must forget its input
   buffer every block (see `Signal objects`_).

The ``.cpp`` defines ``className`` before it uses the definition macros:

.. code-block:: cpp

   #include "gSwap.h"
   #include "../pListArgs.h"
   #include "../pObjectList.hpp"
   #include "gExprEval.h"
   #include <cstddef>
   #include <string>

   using namespace YSE::PATCHER;

   #define className gSwap

After that, ``CONSTRUCT()`` (or ``CONSTRUCT_DSP()`` for a signal object) opens
the constructor, and ``BANG_IN(f)``, ``INT_IN(f)``, ``FLOAT_IN(f)``,
``LIST_IN(f)``, ``BUFFER_IN(f)``, ``CALC()``, ``RESET()``, ``PARM_CLEAR()``
and ``PARM_PARSE()`` open the matching definitions. They expand to ordinary
member function headers, so the parameter names ``value``, ``inlet``,
``buffer`` and ``thread`` are fixed.

The constructor
---------------

The constructor builds the object's whole shape: pins, arguments and
documentation. This is ``.swap``'s, with the comments and the long text
strings shortened:

.. code-block:: cpp

   CONSTRUCT() {
     ADD_IN_0;
     REG_BANG_IN(SetLeftBang);
     REG_INT_IN(SetLeftInt);
     REG_FLOAT_IN(SetLeftFloat);
     REG_LIST_IN(SetLeftList);

     ADD_IN_1;
     REG_INT_IN(SetRightInt);
     REG_FLOAT_IN(SetRightFloat);

     ADD_OUT_ANY;
     ADD_OUT_ANY;

     REG_PARM_CLEAR;
     REG_PARM_PARSE;
     ADD_PARAM(argument);

     ApplyArgument();

     ADD_DESCRIPTION(
         "Reverses the order of a pair of numbers — Max's swap, ... ");
     ADD_CATEGORY(pCategory::MATH);
     INLET_DOC(0, "left",
               "Hot. An int or float is stored and the pair is then released; ... ",
               "any number");
     INLET_DOC(1, "right",
               "Cold — the value is stored for the next release and nothing is sent. ... ",
               "any number");
     OUTLET_DOC(0, "fromRight",
                "The number stored from inlet 1 (or the creation argument), ... ",
                "any number");
     OUTLET_DOC(1, "fromLeft",
                "The number that arrived at inlet 0, as the kind it was given in. ... ",
                "any number");
     PARAM_DOC("right", "0",
               "Max's argument: the initial value of the right slot, ... ",
               "any number");
   }

The constructor runs on the control thread, before the object is part of any
graph, so it may allocate. When a patch creates an object, the patcher calls,
in this order: the constructor, ``SetParams`` with the creation arguments,
then ``SetParent`` with the patcher.

Inlets and outlets
------------------

``ADD_IN_0`` to ``ADD_IN_5`` add an inlet. Inlet 0 is **hot** and the others
are **cold** (see :doc:`messages`). Add them in order. Each ``REG_*_IN``
line that follows attaches a handler to the inlet added last. An inlet takes
exactly the message types you register on it. The others are ignored, and
the metadata reports the set as the inlet's ``accepts`` list.

One handler function may be registered on several inlets. That is why every
handler gets the inlet number, and why ``.swap``'s handlers start with a
check:

.. code-block:: cpp

   INT_IN(SetLeftInt) {
     if (inlet != 0) return;
     left.kind = Kind::INT;
     left.intValue = value;
     Emit(thread);
   }

   INT_IN(SetRightInt) {
     if (inlet != 1) return;
     right.kind = Kind::INT;
     right.intValue = value;
   }

``ADD_OUT_BANG``, ``ADD_OUT_INT``, ``ADD_OUT_FLOAT``, ``ADD_OUT_LIST``,
``ADD_OUT_BUFFER`` and ``ADD_OUT_ANY`` add an outlet of that type. Use
``ANY`` when the outlet can carry more than one type, as ``.swap``'s do: it
forwards an int as an int and a float as a float. Send with
``outputs[i].SendBang(thread)``, ``SendInt``, ``SendFloat``, ``SendList`` or
``SendBuffer``, and always pass on the ``thread`` you were given.

A send is synchronous: it returns only after everything downstream has run.
So:

- **Send right to left.** When one message fires several outlets, send the
  rightmost first, as Max does (see :doc:`messages`).
- **Copy your state before the first send.** A cord from your outlet back to
  your own inlet runs your handler again, inside the send. ``.swap`` copies
  both values into locals before it sends either of them:

  .. code-block:: cpp

     void gSwap::Emit(YSE::THREAD thread) {
       const Value fromLeft = left;
       const Value fromRight = right;

       Send(1, fromLeft, thread);
       Send(0, fromRight, thread);
     }

When an object computes
~~~~~~~~~~~~~~~~~~~~~~~

After a **hot** inlet's handler returns, the inlet calls the object's
``Calculate()`` (through ``CalculateIfReady``). Objects use this in one of two
ways:

- **Emit from the handler**, and declare ``_NO_CALCULATE``. This is what
  ``.swap`` does, and what most recent objects do. Each handler decides for
  itself whether its message produces output, so a bad list can be ignored
  without sending anything.
- **Store in the handler and emit from** ``CALC()``. The older objects work
  this way. ``.+`` (``gAdd``) stores both operands in its handlers and its
  ``CALC()`` sends the sum. Every message on the hot inlet then produces
  output.

For a control object, ``Calculate()`` has no other caller, unless the object
is a poller (see `Optional hooks`_).

Pins that depend on the arguments
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Objects like ``.gate``, ``.route`` and ``.trigger`` take their pin count from
a creation argument. They add the pins from ``PARM_PARSE()`` and remove
them in ``PARM_CLEAR()``. The constructor still builds the shape the object
has **without** arguments, and documents every pin of that shape. That shape
is what the metadata describes.

Creation arguments
------------------

``ADD_PARAM(field)`` registers a member as the next creation argument. The
argument string is split on whitespace and the tokens are written into the
registered fields from left to right. A field can be an ``int``, ``float``,
``std::atomic<int>``, ``std::atomic<float>``, ``std::string`` (one token) or
``std::vector<std::string>`` (the rest of the tokens, so it must come last).
Set the field's default before or after ``ADD_PARAM``. Either works, because
the arguments are only parsed after the constructor. Tokens beyond the last
parameter are ignored.

The patcher stores the argument string as given. ``DumpJSON`` writes that
string, not the values you parsed from it, so a save and a load give the
object back the same arguments.

``REG_PARM_CLEAR`` and ``REG_PARM_PARSE`` register the two callbacks. The
parse callback runs after the tokens are written, so it can turn them into
state or pins. The clear callback runs before, and is the whole of what
``SetParams("")`` does, so it must put the object back in its no-argument
shape. ``.swap`` needs both, because the argument's spelling decides whether
the right slot holds an int or a float:

.. code-block:: cpp

   PARM_CLEAR() {
     argument.clear();
     ApplyArgument();
   }

   PARM_PARSE() {
     ApplyArgument();
   }

Your choice here also decides what a **live** ``SetParams`` does to a running
object (see :doc:`building`):

- If the object registers only numeric fields and no callbacks, the new
  numbers are queued and written into the running object at the start of the
  next block. Its state is kept. Those fields are read on the audio thread,
  so make them atomic if a control thread can write them some other way too.
- If it registers a callback, or a text or list parameter, the patcher builds
  a **new** object with the new arguments and swaps it in. Your object does
  not have to handle a change of arguments while it runs, but it does lose its
  state.

Documentation
-------------

Every object documents itself in its constructor. This text becomes the
object's entry in :doc:`objects/index`, the C API metadata and the JSON
snapshot, so write it for a patch author, not for a reviewer.

``ADD_DESCRIPTION(text)``
   What the object does and how it behaves at its edges.
``ADD_CATEGORY(pCategory::...)``
   The reference page and palette group it goes in: ``OSC``, ``FILTER``,
   ``MATH``, ``GUI``, ``TIME``, ``MIDI``, ``ROUTING``, ``CONTROL``, ``LIST``,
   ``STRING``, ``COLLECTION``, ``DICT``, ``ARRAY``, ``RANDOM``, ``IO``,
   ``ENCAPSULATION`` or ``SEQUENCE``. ``GENERIC`` is refused by a test. Pick
   the closest real category instead.
``INLET_DOC(index, label, doc, range)`` and ``OUTLET_DOC(...)``
   One for every pin the constructor creates. The label is a short name
   (``"left"``, ``"fromRight"``). ``range`` is free text such as
   ``"0-127"``, ``"any number"`` or ``"bang"``, and may be ``""``.
``PARAM_DOC(name, default, doc, range)``
   One for every ``ADD_PARAM``, in the same order. The name is the one a
   patch author sees, and need not be the field's name: ``.swap`` registers
   ``argument`` and documents it as ``"right"``.

Every ``INLET_DOC`` and ``OUTLET_DOC`` indexes into the pins, so call them
after the pins exist.

.. note::

   The tests check that every pin and every documented parameter has a label
   and a text, but not yet that there is one ``PARAM_DOC`` for every
   ``ADD_PARAM``. Count them yourself. Tracked in `#954
   <https://github.com/yvanvds/yse-soundengine/issues/954>`_.

Rules for the handlers
----------------------

Any handler can run on the audio thread (see :doc:`realtime`), so every
message handler, and ``Calculate()``, follows the audio thread's rules: no
allocation, no lock, no I/O, no log line. In practice:

- **Size storage on the control thread.** Allocate buffers in the
  constructor or in ``PARM_PARSE()``, and refuse input that does not fit.
- **Parse without allocating.** Walk a list's ``const std::string&`` in place.
  ``.swap`` reads its two numbers with ``ReadNumericToken`` and
  ``TokenLooksLikeFloat`` from ``pListArgs.h``, into locals on the stack.
- **Guard shared state without waiting.** If two threads can reach the same
  state (a host ``pHandle::SetFloatData`` while the audio thread delivers a
  queued value), protect it with a test-and-set flag, and drop the message
  when the flag is taken. The stateful objects each declare a small
  ``storeGuard`` class for this (``.coll``, ``.funbuff``, ``.table`` and
  others). State that a GUI polls from the host must be atomic.
- **Count what you refuse.** A refusal on the audio thread cannot log. Keep a
  counter that the tests can read (see :doc:`realtime`).
- **Hand slow work to the engine.** Waiting and scheduling go through
  ``Scheduler()`` (:doc:`time`), files through ``FileIO()`` (:doc:`files`),
  and domain clocks through ``Clocks()``. The recipes are in the headers of
  ``time/messageScheduler.h``, ``io/fileScheduler.h`` and
  ``time/clockBridge.h``.

A ``THREAD`` argument of ``T_GUI`` does not mean "control thread". The engine
also delivers ``T_GUI`` on the audio thread (see :doc:`realtime`), so never
use it as permission to lock or allocate.

Signal objects
--------------

A signal object passes a buffer of 128 samples along each signal cord, once
per block. ``~saw`` is the smallest complete one. Its header:

.. code-block:: cpp

   PATCHER_CLASS(dSaw, YSE::OBJ::D_SAW)
   _NO_MESSAGES
   _DO_CALCULATE
   _DO_RESET

   _FLOAT_IN(SetFrequency)
   _BUFFER_IN(SetFrequencyBuffer)

   private:
     DSP::saw saw;

     aFlt frequency;
     DSP::buffer* freqBuffer;
   };

And its source:

.. code-block:: cpp

   CONSTRUCT_DSP() {
     frequency = 440.f;
     freqBuffer = nullptr;

     ADD_IN_0;
     REG_BUFFER_IN(SetFrequencyBuffer);
     REG_FLOAT_IN(SetFrequency);

     ADD_OUT_BUFFER;

     ADD_PARAM(frequency);

     ADD_DESCRIPTION("Audio-rate sawtooth oscillator. Frequency can be set with a float or modulated "
                     "with a DSP buffer.");
     ADD_CATEGORY(pCategory::OSC);
     INLET_DOC(0, "freq", "Oscillator frequency in Hz. Accepts a DSP buffer (FM) or a float.",
               "0-20000 Hz");
     OUTLET_DOC(0, "out", "Sawtooth wave audio output.", "-1.0 to 1.0");
     PARAM_DOC("frequency", "440", "Initial frequency in Hz.", "0-20000 Hz");
   }

   RESET() // {
   freqBuffer = nullptr;
   }

   FLOAT_IN(SetFrequency) {
     frequency = value;
   }

   BUFFER_IN(SetFrequencyBuffer) {
     freqBuffer = buffer;
   }

   CALC() {
     // DSP::saw is a phasor (0..1). Rescale it in place to a bipolar -1..1 saw.
     DSP::buffer& out = freqBuffer != nullptr ? saw(*freqBuffer) : saw(frequency);
     Flt* ptr = out.getPtr();
     for (UInt i = 0; i < out.getLength(); ++i)
       ptr[i] = 2.f * ptr[i] - 1.f;
     outputs[0].SendBuffer(&out, thread);
   }

How the render drives it:

- ``CONSTRUCT_DSP()`` marks the object as a signal object.
- At the start of every block the patcher calls ``ResetDSP()`` on every
  object. The base version marks each input buffer as not yet arrived.
  ``RESET()`` adds your own reset to it. It already contains the opening
  brace and the call to the base, which is why ``dSaw`` writes ``// {``
  after it and ends with a plain ``}``.
- A signal object with no signal cord into it is a **start point**, and the
  patcher calls its ``Calculate()`` directly. Any other signal object
  computes when its last signal input has arrived. Either way it runs once
  per block, on the audio thread, and it must send a buffer on every signal
  outlet.
- A buffer inlet that also takes a float is the usual pattern: with a cord,
  the buffer wins; without one, the float is used.
- A number that arrives with ``T_GUI`` on a signal object's hot inlet only
  stores. It does not compute a block early. The next block uses it.
- ``frequency`` is an ``aFlt`` (an atomic float), because a control thread
  writes it while the audio thread reads it.
- The output buffer belongs to the object (here, inside ``DSP::saw``). It is
  allocated once, when the object is built.

Optional hooks
--------------

``pObject`` has more virtual functions. The defaults do nothing, so an object
overrides only the ones it needs. Each one is documented in full in
``pObject.h``.

.. list-table::
   :header-rows: 1
   :widths: 30 32 38

   * - Override
     - Called
     - Used by
   * - ``DumpState`` / ``RestoreState``
     - On save and load, on the control thread
     - Objects whose contents are worth saving: ``.coll``, ``.table``,
       ``.funbuff``, ``.dict``, ``.array`` (see :doc:`file_format`)
   * - ``Loadbang``
     - Once, after ``ParseJSON`` has published the whole loaded patch
     - ``.loadbang``, ``.loadmess``
   * - ``Teardown``
     - Once, before the object is deleted or the patcher cleared, while every
       cord still exists
     - ``.makenote``, ``.midiflush``, ``.poly``, ``.sustain``, ``.metro``
   * - ``WantsBlockPoll`` (return ``true``)
     - ``Calculate()`` once per block, before the render
     - The MIDI input objects, whose input arrives on another thread
   * - ``SetParent``
     - When the object joins a patcher
     - File objects call ``EnableFileIO()`` here
   * - ``DeliverDeferred`` / ``DeliverFileResult``
     - When a message the object scheduled, or a file it asked for, is ready
     - ``.delay``, ``.pipe``, ``.coll``, ``.textfile``
   * - ``OnPatcherRenamed``
     - After the patcher's name changes
     - Objects that address a shared store by a name scoped to the patcher,
       such as the ``.array`` family (see :doc:`data`)

Saving state
~~~~~~~~~~~~

``DumpState`` writes whatever the object holds beyond its arguments into the
object's ``state`` record, and ``RestoreState`` reads it back into a new
object before the audio thread can see it. Write nothing when there is
nothing to save, so an unused object saves as it always did.

A message may be running on another thread while the patch is saved. So
``DumpState`` takes the object's guard through ``saveGuard``, which waits a
short, bounded time for the message to finish. If the guard is still taken,
return ``false``: the patcher then retries the whole save instead of writing
a file with this object's contents missing. ``.funbuff``:

.. code-block:: cpp

   bool gFunbuff::DumpState(nlohmann::json::value_type& json) {
     const saveGuard guard(busy);
     if (!guard.Held()) return false;

     if (!embed) return true;

     json["embed"] = true;
     for (std::size_t i = 0; i < count; i++) {
       nlohmann::json pair;
       pair["x"] = pairs[i].x;
       pair["y"] = pairs[i].y;
       json["pairs"].push_back(pair);
     }
     return true;
   }

Then add the object's ``state`` format to the table in :doc:`file_format`.

Load and teardown
~~~~~~~~~~~~~~~~~

``Loadbang`` and ``Teardown`` both run on the control thread, outside the
patcher's lock, with ``T_GUI``. Send from your outlets there as from any
handler. ``.loadbang``'s is one line of work:

.. code-block:: cpp

   void gLoadbang::Loadbang(YSE::THREAD thread) {
     fired.fetch_add(1, std::memory_order_relaxed);
     outputs[0].SendBang(thread);
   }

``Loadbang`` only runs after a load. An object created with ``CreateObject``
never gets one.

GUI objects
-----------

A control that a host draws exposes its state through the GUI value protocol.
:doc:`gui` describes it from the host's side. On the object's side there are
three macros:

``_HAS_GUI``
   Declares ``GetGuiValue()``, which you define with ``GUI_VALUE()``. The
   host can read the value but not write it back.
``_HAS_GUI_SETTABLE``
   The same, plus the promise that inlet 0 accepts, as a list, the exact
   string ``GetGuiValue()`` returns, and ``set 0 <value>``. ``.dial``,
   ``.slider`` and ``.i`` use it.
``_HAS_GUI_CELLS``
   For a control with several cells (``.multislider``, ``.matrixctrl``).
   Adds ``GetGuiValueCount()`` and ``GetGuiValueAt()`` (``GUI_VALUE_COUNT()``,
   ``GUI_VALUE_AT()``), and the same promise with ``set <index> <value>``.

The host polls these on its own thread while the audio thread runs your
handlers. Every field they read must be atomic, a read that also clears must
be one ``exchange``, and ``GetGuiValueAt`` must range-check. The full
contract is the "GUI value protocol" comment in ``pObject.h``. Read it before
you write a GUI object. A settable object is what ``.preset`` can store and
recall.

Registering
-----------

Three edits make the type exist.

1. **The constant**, in ``pObjectList.hpp``, next to its family:

   .. code-block:: cpp

      DEFOBJ(G_SWAP, ".swap");

2. **The registry entry**, in ``pRegistry.cpp``. Include the header with the
   others, and add one line in the constructor:

   .. code-block:: cpp

      #include "math/gSwap.h"

   .. code-block:: cpp

      Add(OBJ::G_SWAP, gSwap::Create);

   An object that opens a MIDI device port goes inside the
   ``#if YSE_ENABLE_MIDI_DEVICE`` block and uses ``AddMidiDevice`` instead.
   The metadata then marks it ``requires_midi_device``. An object that only
   formats or parses MIDI bytes uses ``Add``, outside the guard, so it exists
   on every platform.

3. **The two CMake lists** in ``YseEngine/CMakeLists.txt`` (see
   `The files you touch`_).

The registry key and the second argument of ``PATCHER_CLASS`` must be the
same string. ``Type()`` is what ``DumpJSON`` writes and ``ParseJSON`` looks
up, so if they differ the object saves as one type and loads as another. Use
the ``OBJ::`` constant in both places and they cannot differ.

The C API
---------

There is nothing to add. The C API creates objects by type name
(``yse_patcher_create_object(p, ".swap", "5")``) and enumerates the registry
for its metadata calls (``yse_patcher_get_type_count``,
``yse_patcher_get_inlet_info``, ``yse_patcher_get_metadata_json`` and the
rest; see :doc:`c_api`). Your object appears there as soon as it is
registered, with the documentation from its constructor.

Only a new **category** needs C API work: ``pCategory`` has a mirror in the
C header (``YsePCategory``), a name in ``tools/dump_patcher_metadata`` and a
page in ``documentation/source/conf.py``. Adding a category is a change of its
own, not part of adding an object.

Tests
-----

Two kinds of test apply to a new object: the registry-wide tests, which
check every registered object without knowing about yours, and the object's
own test file.

What the registry-wide tests catch
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. list-table::
   :header-rows: 1
   :widths: 38 62

   * - Test
     - Fails when
   * - ``test_doc_coverage.cpp``: "every registered object documents itself"
     - The description is empty, the category is unset, a pin created by the
       constructor has no ``INLET_DOC`` / ``OUTLET_DOC`` label or text, or a
       ``PARAM_DOC`` has an empty name or text.
   * - ``test_doc_coverage.cpp``: "no registered object falls back to
       GENERIC"
     - The category is ``GENERIC``.
   * - ``test_doc_coverage.cpp``: "every registered object reports its own
       name"
     - ``Type()`` differs from the registry key.
   * - ``test_c_api_metadata.cpp``: "bulk JSON matches dump_patcher_meta
       snapshot per-object"
     - ``patcher_objects.json`` was not regenerated, or was regenerated before
       the object's last documentation or pin change. A newly registered
       object that is missing from the snapshot fails here too.
   * - ``test_c_api_metadata.cpp``: the other cases
     - The C API's view of the registry disagrees with the engine's. These
       pass for any correctly registered object.

These tests only see objects that are registered, and the build only sees
files that are in the CMake lists. So forgetting the registry line or the
CMake entry does not fail them: it fails your own test file, which cannot
create the object or link.

The object's own test file
~~~~~~~~~~~~~~~~~~~~~~~~~~

Name it ``Tests/patcher/test_patcher_<name>.cpp``, and put every case in
``TEST_SUITE("patcher")`` with the object's name and issue number in its
title (``"swap: a bang is a replay, not an exchange of the slots (#476)"``).
The file needs no audio device. ``test_patcher_swap.cpp`` is a good model. It
covers, in order:

- **Registry and shape**: the object can be created through a patcher by its
  type name, is in ``AllNames()``, and has the pins and outlet types it
  should.
- **Behaviour**: every message on every inlet, the arguments (including
  ``SetParams("")`` and a bad or surplus argument), and the edge cases. Most
  cases build the object directly, without a patcher, and wire its outlets to
  the sink objects in ``Tests/patcher/sinks.hpp`` (``OrderSink`` records
  which outlet fired in which order, ``MultiSink`` records the type that
  arrived). Deliver messages with ``GetInlet(n)->SetInt(v, YSE::T_GUI)`` and
  the other setters.
- **Ordering**: right-to-left outlet order, and state copied before the first
  send.
- **Round trip**: build it in a patcher, ``DumpJSON``, ``ParseJSON`` into a
  second patcher, and check it behaves the same. Include ``state`` if the
  object has any.
- **Documentation**: the category, the pin labels and the parameter names,
  which a binding generator keys on.
- **Real-time safety**: drive the message paths with ``YSE::T_DSP`` inside a
  ``TestHelpers::ProbeScope`` (``Tests/support/alloc_probe.hpp``) and check
  that ``TestHelpers::g_alloc_count.load() == 0``. The probe counts only the
  thread that opened it, so drive the object on that thread. Read the notes
  in that header on what it can see on each platform before relying on it.
- **In a real patch**: at least one case that uses the object the way a patch
  would, through ``YSE::patcher``, ``CreateObject`` and ``Connect``.

An object with a GUI value also belongs in
``test_patcher_gui_protocol.cpp``. An object that two threads can reach at
once belongs in ``test_patcher_object_races.cpp``, which the sanitizer
builds also run (``python yse.py test --sanitizer asan`` or ``tsan``).

While you work on the object, run only its cases:

.. code-block:: console

   python yse.py test                                  # configure and build (also runs ctest)
   build-tests/bin/yse_tests --test-case="swap: *"     # then just this object's cases
   build-tests/bin/yse_tests --test-case="doc coverage*,registry:*,c-api metadata*"

Regenerate the snapshot
-----------------------

``documentation/source/_data/patcher_objects.json`` holds the metadata of
every registered object. The object reference pages are generated from it,
and the parity test compares it with the live registry. After every change to
an object's pins or documentation, regenerate it and commit the result:

.. code-block:: console

   python yse.py dump-patcher-meta

This builds the ``dump_patcher_meta`` tool in its own ``build-tools/``
directory (it needs Ninja) and writes the file. The output is sorted, so an
unchanged engine gives an identical file and the diff shows only your object.
Generate it on Windows, where every object is registered. Builds without the
MIDI device backend (macOS, Android) register fewer objects.

Before you commit
-----------------

- ``python yse.py format`` and ``python yse.py analyze <your files>``, and
  clear new findings (see ``CLAUDE.md``).
- Your test file and the registry-wide tests pass.
- ``patcher_objects.json`` is regenerated.
- The object counts at the top of :doc:`index` are updated.
- If the object saves state, its format is in :doc:`file_format`.
- ``PROJECT_OVERVIEW.md`` mentions the object in the patcher node list.

Checklist
---------

- [ ] ``gX.h`` / ``gX.cpp`` in the right family folder, with
  ``PATCHER_CLASS(gX, YSE::OBJ::G_X)`` and ``#define className gX``.
- [ ] Every pin added in the constructor, every inlet with its handlers, and
  every pin with an ``INLET_DOC`` or ``OUTLET_DOC``.
- [ ] ``ADD_DESCRIPTION``, ``ADD_CATEGORY`` (not ``GENERIC``), and one
  ``PARAM_DOC`` per ``ADD_PARAM`` in the same order.
- [ ] No allocation, lock, I/O or log line in any handler or ``Calculate()``.
- [ ] ``DEFOBJ`` in ``pObjectList.hpp``, ``#include`` and ``Add`` in
  ``pRegistry.cpp``.
- [ ] The ``.cpp`` in both lists in ``YseEngine/CMakeLists.txt``.
- [ ] ``test_patcher_x.cpp`` in both lists in ``Tests/CMakeLists.txt``.
- [ ] ``python yse.py dump-patcher-meta`` run and the JSON committed.
