.. Notes for the Input and output category. Format: _data/patcher_notes/README.md.

.. patcher-object:: .s

``.s <name>`` delivers each value in two ways:

- **Inside the patcher**, to every ``.r <name>``, including those in
  subpatchers. A value sent while the patch renders arrives in the same
  block; one sent from the control thread arrives at the start of the next.
- **On the named bus**, at ``patcher.<patcherName>.<name>``. A ``.r <name>``
  in another patcher with the same name, a bus subscriber in the host and a
  Python script all receive it there. The patcher's own ``.r`` objects ignore
  this copy, so each gets every value once.

With ``globalOnly`` set to 1 (``.s <name> 1``) the value only goes out on the
bus, and the patcher's own ``.r`` objects get it from there too.

The audio thread can only put ints and floats on the bus. A bang or list sent
while the patch renders reaches the patcher's own ``.r`` objects and nothing
else, and with ``globalOnly`` set it reaches nothing at all. When nothing in
the patcher listens to the name, the host's send callback gets the value
instead, unless it was sent while the patch renders.

:doc:`/patcher/host_io` covers the bus, the send callback, the scope rules
and the length limits.

.. patcher-object:: .r

``.r <name>`` sends out everything sent to that name: by a ``.s`` or
``.forward`` in the same patcher, by one in another patcher with the same
name, by the host's ``PassData`` and ``PassBang``, or by a publish on the bus
address ``patcher.<patcherName>.<name>``. A cord into its inlet is passed
straight through.

Values that come from the bus are sent out on the control thread. The bus
subscription only exists while the engine is running, so create patchers
after ``System().init()``. :doc:`/patcher/host_io` covers when each route
delivers.

.. patcher-object:: .forward

Values go out exactly as from a ``.s`` with the current destination as its
name (see ``.s``).

The right inlet sets the destination. A list sets it to its first word; an
int sets it to the number spelled out, so ``7`` reaches a ``.r 7``. A float or
a bang there is ignored, as is a name longer than 63 characters or an empty
one; the old destination stays. Without a destination, nothing is sent.

.. code-block:: text

   [.forward]   right <- left       then   left <- 3   =>   .r left: 3
                right <- right      then   left <- 4   =>   .r right: 4

.. patcher-object:: .print

Each message becomes one log line: the name, a colon, then the message.

- A bang prints ``bang``. An int prints its digits.
- A float always has a decimal point, so ``440.`` stays visibly a float.
- A list or a message box's text prints as it is.

The line reaches the log on the next ``System::update()`` tick, tagged
``(App Message)``. Catch it with ``YSE::Log().setHandler()``.

**Limits**

- A line longer than about 240 characters is cut and ends in ``...``.
- Each object prints at most 16 lines per update tick, or the number given
  as its second argument (1-256). The first line over the limit in a tick
  logs a notice.
- The queue is shared by every ``.print``. Lines that do not fit are counted,
  and the next update logs how many were lost.

Nothing is dropped silently. ``.print`` has no outlet. See
:doc:`/patcher/host_io`.

.. patcher-object:: .value

Every ``.value`` with the same name in a patcher, and in patchers with the
same name, shares one value. Storing sends nothing and publishes nothing; to
react to changes as well, send the value to a ``.value`` and a ``.s`` from
the same outlet.

The starting value comes from the arguments after the name (``.value tempo
120``, ``.value pos 0 0``). A single number is an int or a float as written,
so ``120`` and ``120.`` differ. Only the ``.value`` that creates the shared
value applies its starting value; a later one adopts what is stored, so adding
a reader never resets it.

See :doc:`/patcher/host_io` for reading and writing a ``.value`` from the
host.

.. patcher-object:: ~adc

There is one outlet per channel of the patcher, as set by
``patcher::create``. The outlets only carry audio when the patcher runs as an
insert effect. When it is a sound source there is no incoming audio and they
stay silent; outlets beyond the host's channel count also stay silent. See
:doc:`/patcher/index`.

.. patcher-object:: ~dac

There is one inlet per channel of the patcher, as set by ``patcher::create``.
A patch can hold several ``~dac`` objects. Their channels are added together
without scaling, so keep the combined level in range. See
:doc:`/patcher/index` for running a patch as a sound or as an insert.
