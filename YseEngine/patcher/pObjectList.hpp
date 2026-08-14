#pragma once

#define DEFOBJ(name, tag) static constexpr char const* name = tag

namespace YSE {
  /**
   *  @brief Compile-time string identifiers for patcher object types.
   *
   *  Pass any of these constants to ``patcher::CreateObject`` instead of the
   *  raw string literal. The ``~`` prefix marks DSP / audio-rate objects,
   *  ``.`` marks control-rate objects.
   *
   *  - DSP generators: ``D_SINE``, ``D_SAW``, ``D_NOISE``.
   *  - DSP math: ``D_ADD``, ``D_MULTIPLY``, ``D_CLIP``.
   *  - Control math: ``G_ADD``, ``G_DIVIDE``, ``G_REVERSESUBSTRACT``,
   *    ``G_REVERSEDIVIDE``, ``G_MODULO``, ``G_INTDIVIDE``.
   *  - Operand order: ``G_SWAP``.
   *  - Elementary math: ``G_ABS``, ``G_SQRT``, ``G_POW``, ``G_ROUND``.
   *  - Range mapping: ``G_SCALE``, ``G_ZMAP``, ``G_LINEDRIVE``.
   *  - Range limiting: ``G_CLIP``, ``G_PONG``.
   *  - Range routing: ``G_SPLIT``.
   *  - Smoothing: ``G_SLIDE``.
   *  - Expressions: ``G_EXPR``, ``G_VEXPR``.
   *  - Trigonometry / hyperbolics: ``G_SIN``, ``G_COS``, ``G_TAN``,
   *    ``G_ASIN``, ``G_ACOS``, ``G_ATAN``, ``G_ATAN2``, ``G_SINH``,
   *    ``G_COSH``, ``G_TANH``, ``G_ASINH``, ``G_ACOSH``, ``G_ATANH``.
   *  - Comparison / logic: ``G_EQUAL``, ``G_LESS``, ``G_LOGICALAND``, ...
   *  - Running comparison: ``G_MAXIMUM``, ``G_MINIMUM``.
   *  - Running extremes: ``G_PEAK``, ``G_TROUGH``.
   *  - Threshold crossing: ``G_PAST``.
   *  - Change detection: ``G_CHANGE``.
   *  - Zero-crossing edges: ``G_TOGEDGE``.
   *  - Bitwise: ``G_BITAND``, ``G_BITOR``, ``G_SHIFTLEFT``, ``G_SHIFTRIGHT``.
   *  - DSP filters: ``D_LOWPASS``, ``D_HIGHPASS``, ``D_BANDPASS``, ``D_VCF``.
   *  - I/O: ``D_DAC``, ``D_ADC``, ``D_LINE``.
   *  - Control: ``G_INT``, ``G_FLOAT``, ``G_SLIDER``, ``G_RSLIDER``,
   *    ``G_XYSLIDER``, ``G_MULTISLIDER``, ``G_MATRIXCTRL``, ``G_FUNCTION``,
   *    ``G_KSLIDER``, ``G_NSLIDER``, ``G_DIAL``, ``G_INCDEC``, ``G_RANDOM``.
   *  - Proximity fields: ``G_NODES``.
   *  - Indexed selection: ``G_UMENU``, ``G_RADIOGROUP``, ``G_TAB``.
   *  - Labelled switches: ``G_LED``, ``G_TEXTBUTTON``.
   *  - Text entry: ``G_TEXTEDIT``.
   *  - Presets: ``G_PRESET``.
   *  - Timing: ``G_METRO``, ``G_DELAY``, ``G_PIPE``, ``G_TRANSPORT``,
   *    ``G_SETCLOCK``,
   *    ``G_WHEN``, ``G_TRANSLATE``, ``G_TIMEPOINT``, ``G_TEMPO``,
   *    ``G_CLOCKER``, ``G_TIMER``,
   *    ``G_SPEEDLIM``, ``G_QLIM``, ``G_THRESH``, ``G_QUICKTHRESH``,
   *    ``G_LINE``, ``G_BLINE``.
   *  - Registers: ``G_COUNTER``, ``G_ACCUM``.
   *  - Randomness: ``G_RANDOM``, ``G_DRUNK``, ``G_URN``, ``G_DECIDE``,
   *    ``G_PROB``, ``G_ANAL``, ``G_HISTO``.
   *  - Statistics: ``G_ANAL``, ``G_HISTO``, ``G_MEAN``.
   *  - Shared state: ``G_VALUE``.
   *  - Collections: ``G_COLL``, ``G_BAG``, ``G_CAPTURE``, ``G_FUNBUFF``,
   *    ``G_TABLE``, ``G_TEXTFILE``, ``G_QLIST``, ``G_MTR``, ``G_SEQ``.
   *  - Dictionaries: ``G_DICT``, ``G_DICT_COMPARE``, ``G_DICT_DESERIALIZE``,
   *    ``G_DICT_GROUP``, ``G_DICT_ITER``, ``G_DICT_JOIN``, ``G_DICT_PACK``,
   *    ``G_DICT_PRINT``, ``G_DICT_ROUTE``, ``G_DICT_SERIALIZE``,
   *    ``G_DICT_SLICE``, ``G_DICT_STRIP``, ``G_DICT_UNPACK``.
   *  - Arrays: ``G_ARRAY``, ``G_ARRAY_AT``, ``G_ARRAY_LENGTH``,
   *    ``G_ARRAY_PUSH``, ``G_ARRAY_POP``, ``G_ARRAY_SHIFT``,
   *    ``G_ARRAY_UNSHIFT``, ``G_ARRAY_INSERT``, ``G_ARRAY_REMOVE``,
   *    ``G_ARRAY_INDEXOF``, ``G_ARRAY_INDEX``, ``G_ARRAY_INDEXMAP``,
   *    ``G_ARRAY_REVERSE``, ``G_ARRAY_ROTATE``, ``G_ARRAY_SCRAMBLE``,
   *    ``G_ARRAY_SHUFFLE``, ``G_ARRAY_SORT``, ``G_ARRAY_MIN``,
   *    ``G_ARRAY_MAX``, ``G_ARRAY_MEAN``, ``G_ARRAY_MEDIAN``,
   *    ``G_ARRAY_MODE``, ``G_ARRAY_STDDEV``, ``G_ARRAY_SLICE``,
   *    ``G_ARRAY_SUBARRAY``, ``G_ARRAY_SUB``, ``G_ARRAY_SPLIT``,
   *    ``G_ARRAY_UNION``, ``G_ARRAY_SECT``, ``G_ARRAY_UNIQUE``.
   *  - Debugging: ``G_PRINT``, ``G_DICT_PRINT``.
   *  - Initialisation: ``G_LOADBANG``, ``G_LOADMESS``.
   *  - Encapsulation: ``PATCHER``, ``G_INLET``, ``G_OUTLET``, ``D_INLET``,
   *    ``D_OUTLET``.
   *  - Messaging: ``G_SEND``, ``G_RECEIVE``, ``G_FORWARD``, ``G_ROUTE``,
   *    ``G_ROUTEPASS``,
   *    ``G_SEL``, ``G_TRIGGER``, ``G_BANGBANG``, ``G_ONEBANG``, ``G_NEXT``,
   *    ``G_MATCH``,
   *    ``G_BONDO``, ``G_BUDDY``, ``G_CYCLE``, ``G_BUCKET``, ``G_SPRAY``,
   *    ``G_FUNNEL``, ``G_DECODE``, ``G_GATE``, ``G_SWITCH``, ``G_ROUTER``,
   *    ``G_MATRIX``, ``G_IF``, ``G_REGEXP``.
   *  - Message construction: ``G_PREPEND``, ``G_APPEND``, ``G_SUBSTITUTE``,
   *    ``G_SPRINTF``, ``G_TOSYMBOL``, ``G_FROMSYMBOL``, ``G_COMBINE``,
   *    ``G_SPELL``, ``G_ATOI``, ``G_ITOA``, ``G_TOLOWER``, ``G_TOUPPER``.
   *  - List processing: ``G_ZL``, ``G_PACK``, ``G_PAK``, ``G_UNPACK``,
   *    ``G_JOIN``, ``G_UNJOIN``, ``G_LISTFUNNEL``.
   *  - Iteration: ``G_UZI``, ``G_ITER``.
   *  - MIDI out: ``M_OUT``, ``M_NOTEON``, ``M_NOTEOFF``, ``M_CONTROL``,
   *    ``M_BENDOUT``.
   *  - MIDI in: ``M_IN``, ``M_NOTEIN``, ``M_CTLIN``, ``M_BENDIN``,
   *    ``M_PGMIN``, ``M_TOUCHIN``, ``M_POLYIN``, ``M_RTIN``.
   *  - Extended-precision MIDI: ``M_XBENDIN``, ``M_XBENDIN2``, ``M_XCTLIN``,
   *    ``M_XNOTEIN``, ``M_XMIDIIN``, ``M_XBENDOUT``, ``M_XBENDOUT2``,
   *    ``M_XCTLOUT``, ``M_XNOTEOUT``.
   *  - Parameter numbers: ``M_RPNIN``, ``M_NRPNIN``, ``M_RPNOUT``,
   *    ``M_NRPNOUT``.
   *  - MIDI polyphonic expression: ``M_MPECONFIG``, ``M_MPEFORMAT``,
   *    ``M_MPEPARSE``.
   *  - MIDI codec: ``M_PARSE``, ``M_FORMAT``.
   *  - MIDI system exclusive: ``M_SYSEXIN``, ``M_SXFORMAT``.
   *  - MIDI devices: ``M_MIDIINFO``.
   *  - MIDI hygiene: ``M_MIDIFLUSH``, ``M_MAKENOTE``, ``M_STRIPNOTE``,
   *    ``M_FLUSH``, ``M_SUSTAIN``.
   *  - Voice allocation: ``M_POLY``.
   *  - Note analysis: ``M_BORAX``.
   *  - Note bookkeeping: ``M_OFFER``.
   *  - Conversion: ``MIDITOFREQUENCY``, ``FREQUENCYTOMIDI``, ``G_ATODB``,
   *    ``G_DBTOA``, ``G_CARTOPOL``, ``G_POLTOCAR``.
   */
  struct API OBJ {
    // Encapsulation (issue #545). ``PATCHER`` names two things on purpose: the
    // type a ``YSE::patcher`` itself reports, and the type of a *subpatcher*
    // object created inside one. A subpatcher is a patcher, and the object that
    // stands for it is deliberately not given a second name — Max ships ``p``
    // as an alias for ``patcher`` and one name is enough. The two never collide
    // in practice because the root patcher is not one of its own objects.
    //
    // ``G_INLET`` / ``G_OUTLET`` are the boundary of a subpatcher: the parent's
    // cords land on them, and inside the subpatcher they are ordinary
    // pass-through objects. See genericObjects/gSubpatcher.h for the model.
    //
    // ``D_INLET`` / ``D_OUTLET`` are the audio-rate half of that boundary
    // (issue #764) — the same pass-through with the same index, carrying a
    // buffer instead of a message. They are separate objects rather than a
    // dual-natured `.inlet` because ``IsDSPObject()`` is what the graph
    // compiler, every palette and every binding read to decide what an object
    // *is*, and one object cannot answer both honestly. **The index space is
    // shared**: a subpatcher has one set of pins, so `~inlet 2` and `.inlet 2`
    // both claim boundary inlet 2 and ``Connect(source, 0, sub, 2)`` resolves to
    // whichever of them exists. See genericObjects/dInlet.h.
    DEFOBJ(PATCHER, "patcher");
    DEFOBJ(G_INLET, ".inlet");
    DEFOBJ(G_OUTLET, ".outlet");
    DEFOBJ(D_INLET, "~inlet");
    DEFOBJ(D_OUTLET, "~outlet");

    DEFOBJ(D_DAC, "~dac");
    DEFOBJ(D_ADC, "~adc");
    DEFOBJ(G_RECEIVE, ".r");
    DEFOBJ(G_SEND, ".s");

    DEFOBJ(D_LINE, "~line");

    DEFOBJ(D_SINE, "~sine");
    DEFOBJ(D_SAW, "~saw");
    DEFOBJ(D_NOISE, "~noise");

    DEFOBJ(G_INT, ".i");
    DEFOBJ(G_FLOAT, ".f");
    DEFOBJ(G_SLIDER, ".slider");
    DEFOBJ(G_RSLIDER, ".rslider");
    // A two-dimensional control pad (issue #563): one gesture carrying two
    // correlated axes, x and y over independently settable ranges. Max's
    // pictslider under an honest name — the picture is a host rendering
    // concern. See guiObjects/gXYSlider.h, including why a position is never
    // sorted the way .rslider's span is.
    DEFOBJ(G_XYSLIDER, ".xyslider");
    DEFOBJ(G_MULTISLIDER, ".multislider");
    DEFOBJ(G_MATRIXCTRL, ".matrixctrl");
    // A breakpoint function editor as one control (issue #561): a bounded,
    // sorted store of (x, y, curve) breakpoints that answers any x with the
    // curved interpolation between its neighbours and bangs out the whole
    // envelope as a ramp list `.line` and `.bline` consume. See
    // guiObjects/gFunction.h — including why it deliberately does not share
    // `.funbuff`'s integer store.
    DEFOBJ(G_FUNCTION, ".function");
    DEFOBJ(G_KSLIDER, ".kslider");
    DEFOBJ(G_NSLIDER, ".nslider");
    // A field of circular nodes a cursor is weighed against (issue #562) — the
    // patcher's morph controller, and the first control whose *output* is a
    // computation over its own state rather than the state itself. One XY
    // position in, per-node distance and normalised weight out. See
    // guiObjects/gNodes.h.
    DEFOBJ(G_NODES, ".nodes");
    // The indexed selector family (issue #556) — one implementation of "a
    // bounded index over a named item list" under the three names a host draws
    // differently. See guiObjects/gItemList.h.
    DEFOBJ(G_UMENU, ".umenu");
    DEFOBJ(G_RADIOGROUP, ".radiogroup");
    DEFOBJ(G_TAB, ".tab");
    DEFOBJ(G_DIAL, ".dial");
    DEFOBJ(G_INCDEC, ".incdec");
    DEFOBJ(G_BUTTON, ".b");
    DEFOBJ(G_TOGGLE, ".t");
    // The labelled switch family (issue #557) — one implementation of "an on/off
    // that carries a name" under the two names a host draws differently. `.b`
    // and `.t` hold the same value and cannot say what they are, which leaves a
    // host rendering a headless patch with a grid of anonymous squares. See
    // guiObjects/gLabelSwitch.h.
    DEFOBJ(G_LED, ".led");
    DEFOBJ(G_TEXTBUTTON, ".textbutton");
    DEFOBJ(G_MESSAGE, ".m");
    DEFOBJ(G_LIST, ".l");
    DEFOBJ(G_TEXT, ".text");
    // An editable string (issue #560) — the one GUI value that is genuinely a
    // mutable string, where `.text` is a fixed label and every other control's
    // string is an immutable creation argument. The input side of the symbol
    // family (`.sprintf`, `.combine`, `.tosymbol`), which until now had no way
    // to receive text from outside a patch. See guiObjects/gTextEdit.h.
    DEFOBJ(G_TEXTEDIT, ".textedit");
    // Snapshot and recall of the patch's control values (issue #564) — Max's
    // preset: numbered slots each holding the captured GUI value of every
    // settable control, restored through the ordinary control-thread message
    // path. Participation is issue #551's settable promise, which is what
    // makes capture and restore the same contract. See guiObjects/gPreset.h.
    DEFOBJ(G_PRESET, ".preset");
    DEFOBJ(G_COUNTER, ".counter");
    DEFOBJ(G_ACCUM, ".accum");
    DEFOBJ(G_SWITCH, ".switch");
    DEFOBJ(G_GATE, ".gate");
    DEFOBJ(G_ROUTE, ".route");
    DEFOBJ(G_ROUTEPASS, ".routepass");
    DEFOBJ(G_ROUTER, ".router");
    DEFOBJ(G_MATRIX, ".matrix");
    DEFOBJ(G_FORWARD, ".forward");
    DEFOBJ(G_VALUE, ".value");
    DEFOBJ(G_SEL, ".sel");
    DEFOBJ(G_TRIGGER, ".trigger");
    DEFOBJ(G_BANGBANG, ".bangbang");
    DEFOBJ(G_ONEBANG, ".onebang");
    DEFOBJ(G_NEXT, ".next");
    DEFOBJ(G_MATCH, ".match");
    DEFOBJ(G_UZI, ".uzi");
    DEFOBJ(G_ITER, ".iter");
    DEFOBJ(G_BONDO, ".bondo");
    DEFOBJ(G_BUDDY, ".buddy");
    DEFOBJ(G_CYCLE, ".cycle");
    DEFOBJ(G_BUCKET, ".bucket");
    DEFOBJ(G_SPRAY, ".spray");
    DEFOBJ(G_FUNNEL, ".funnel");
    DEFOBJ(G_DECODE, ".decode");
    DEFOBJ(G_IF, ".if");
    DEFOBJ(G_REGEXP, ".regexp");
    DEFOBJ(G_PREPEND, ".prepend");
    DEFOBJ(G_APPEND, ".append");
    DEFOBJ(G_SUBSTITUTE, ".substitute");
    DEFOBJ(G_SPRINTF, ".sprintf");
    DEFOBJ(G_TOSYMBOL, ".tosymbol");
    DEFOBJ(G_FROMSYMBOL, ".fromsymbol");
    DEFOBJ(G_COMBINE, ".combine");
    DEFOBJ(G_SPELL, ".spell");
    DEFOBJ(G_ATOI, ".atoi");
    DEFOBJ(G_ITOA, ".itoa");
    // ASCII case folding (issue #810) — the one operation the symbol family was
    // missing. Every comparison in this patcher is exact, so folding both sides
    // to one case is what makes a match case-insensitive. See
    // genericObjects/gCase.h.
    DEFOBJ(G_TOLOWER, ".tolower");
    DEFOBJ(G_TOUPPER, ".toupper");

    DEFOBJ(G_ZL, ".zl");
    DEFOBJ(G_PACK, ".pack");
    DEFOBJ(G_PAK, ".pak");
    DEFOBJ(G_UNPACK, ".unpack");
    DEFOBJ(G_JOIN, ".join");
    DEFOBJ(G_UNJOIN, ".unjoin");
    DEFOBJ(G_LISTFUNNEL, ".listfunnel");

    DEFOBJ(G_COLL, ".coll");
    DEFOBJ(G_BAG, ".bag");
    DEFOBJ(G_CAPTURE, ".capture");
    DEFOBJ(G_FUNBUFF, ".funbuff");
    DEFOBJ(G_TABLE, ".table");
    DEFOBJ(G_TEXTFILE, ".textfile");
    DEFOBJ(G_QLIST, ".qlist");
    DEFOBJ(G_MTR, ".mtr");
    DEFOBJ(G_SEQ, ".seq");

    // A nested key/value dictionary shared by name (issue #550), and the
    // patcher's answer to the reference-passed value question the array (#548),
    // string (#549) and dict epics all asked: an ``OUT_TYPE`` carries a value,
    // never an identity, so a dictionary is **addressed by name** and what
    // travels down a cord is the message ``dictionary <name>``. Storage lives in
    // the ``namedStore.h`` registry (#684) and is resolved once, on the control
    // thread. The twelve ``dict.*`` operations are separate objects written
    // against ``dictStore``; see genericObjects/gDict.h for the whole model.
    DEFOBJ(G_DICT, ".dict");

    // The first of the dict.* operations written against that model (issue
    // #770): both dictionaries are bound from the creation arguments —
    // ``.dict.compare <left> <right>`` — because a dictionary is addressed by
    // name and never passed down a cord. A bang, or the left dictionary's
    // ``dictionary <name>`` reference, reports whether the two hold the same
    // entries.
    DEFOBJ(G_DICT_COMPARE, ".dict.compare");

    // Builds a dictionary from serialised text (issue #771): the target
    // dictionary is bound from the creation argument —
    // ``.dict.deserialize <name>`` — and a list message holding one JSON
    // object (exactly what ``.dict.serialize`` emits) replaces it whole,
    // then announces ``dictionary <name>`` out the outlet. The read half of
    // the interchange pair. The parse runs on the background pool — nlohmann
    // allocates, and the inlet may be the audio thread — and the result is
    // installed by the patcher's block poll one block later.
    DEFOBJ(G_DICT_DESERIALIZE, ".dict.deserialize");

    // Groups a dictionary's entries by a value (issue #772): the source and
    // target dictionaries are bound from the creation arguments —
    // ``.dict.group <source> <target> [<key>]`` — and every grouped entry is
    // written into the target as ``<groupValue>::<originalPath>``, the
    // dictionary of dictionaries the flat store expresses as a path prefix.
    DEFOBJ(G_DICT_GROUP, ".dict.group");

    // Outputs a dictionary's key/value pairs one at a time (issue #773): the
    // dictionary is bound from the creation argument — ``.dict.iter <name>``
    // — and a bang, or the dictionary's ``dictionary <name>`` reference,
    // streams one ``<path> <value...>`` list per entry in storage order,
    // then a done bang. The walk is a snapshot taken at the trigger, so a
    // mutation arriving mid-walk changes the dictionary but not the walk.
    DEFOBJ(G_DICT_ITER, ".dict.iter");

    // Merges two dictionaries into one (issue #774): all three dictionaries
    // are bound from the creation arguments — ``.dict.join <left> <right>
    // <target>`` — and a bang, or the left dictionary's ``dictionary <name>``
    // reference, replaces the target with the left dictionary's entries
    // overlaid by the right's. On a colliding key path the right overwrites
    // the left — Max's own rule — so the left is the base and the right the
    // override, which is the preset-over-defaults layering the object exists
    // for.
    DEFOBJ(G_DICT_JOIN, ".dict.join");

    // Builds a dictionary from a list of named inlets (issue #775): the
    // dictionary is bound from the first creation argument and every
    // argument after it is a key path declaring one inlet — ``.dict.pack
    // <name> <key> [<key> ...]`` — the dictionary counterpart of ``.pack``,
    // with its hot/cold rule: only inlet 0 releases. A trigger replaces the
    // bound dictionary whole with one entry per key path and sends its
    // ``dictionary <name>`` reference, the unit the rest of the family
    // consumes.
    DEFOBJ(G_DICT_PACK, ".dict.pack");

    // Prints a dictionary's contents to the engine log (issue #776): the
    // dictionary is bound from the creation argument — ``.dict.print
    // <name>`` — and a bang, or the dictionary's ``dictionary <name>``
    // reference, dumps it as a nested multi-line JSON document through
    // ``.print``'s lock-free log on-ramp. The debugging instrument for
    // structured data: a dictionary is the one patcher value a patch cannot
    // see by wiring it to a sink, because what a cord carries is only its
    // name.
    DEFOBJ(G_DICT_PRINT, ".dict.print");

    // Routes a dictionary by the keys it holds (issue #777): the dictionary
    // is bound from the first creation argument and every argument after it
    // is a key declaring one outlet, plus a rightmost reject —
    // ``.dict.route <name> <key> [<key> ...]``. A bang, or the dictionary's
    // ``dictionary <name>`` reference, sends the reference — never the
    // contents — out the outlet of the leftmost key present, so a patch
    // that receives dictionaries of several shapes dispatches each to the
    // part of the graph that understands it: what ``.route`` does for list
    // text, for dictionaries. ``gRoute`` is the model, including its
    // rightmost-outlet-is-the-reject rule.
    DEFOBJ(G_DICT_ROUTE, ".dict.route");

    // Serialises a dictionary to text (issue #778): the dictionary is bound
    // from the creation argument — ``.dict.serialize <name>`` — and a bang,
    // or the dictionary's ``dictionary <name>`` reference, sends the whole
    // dictionary out the outlet as one list message holding a single-line
    // compact JSON object — the same document ``DictToJson`` builds for a
    // saved patch, and exactly what ``.dict.deserialize`` parses back. The
    // write half of the pair that makes a dictionary the patcher's
    // interchange format rather than only its store. A document longer than
    // the patcher's list payload bound is refused whole, never truncated.
    DEFOBJ(G_DICT_SERIALIZE, ".dict.serialize");

    // Splits a dictionary at a key path (issue #779): all three dictionaries
    // are bound from the creation arguments —
    // ``.dict.slice <source> <slice> <remainder> [<path>]`` — and a bang, or
    // the source's ``dictionary <name>`` reference, partitions the source:
    // every entry under the path replaces the slice target with the prefix
    // stripped, so the sub-tree becomes a dictionary rooted at itself, and
    // every other entry replaces the remainder target unchanged. Each
    // target's reference then leaves its outlet, remainder first. The
    // operation the flat store makes explicit — a sub-tree is not a value,
    // so extracting one is a bounded prefix scan rather than a lookup.
    DEFOBJ(G_DICT_SLICE, ".dict.slice");

    // Removes a dictionary's entries under a key path, in place (issue
    // #780): the dictionary is bound from the creation argument —
    // ``.dict.strip <name> [<path>]`` — and a bang, or the dictionary's
    // ``dictionary <name>`` reference, erases every entry whose key begins
    // ``<path>::``, leaving everything else untouched, including a leaf
    // stored at exactly the path. The dictionary's reference then leaves the
    // outlet. The branch removal ``.dict``'s ``delete`` cannot spell — one
    // path, not a sub-tree — and the in-place half of ``.dict.slice``'s
    // partition: what a slice leaves in its remainder is what a strip of the
    // same path leaves behind.
    DEFOBJ(G_DICT_STRIP, ".dict.strip");

    // Outputs a dictionary's values on separate outlets (issue #781): the
    // dictionary is bound from the first creation argument and every
    // argument after it is a key path declaring one outlet —
    // ``.dict.unpack <name> <path> [<path> ...]`` — the dictionary
    // counterpart of ``.unpack`` and ``.dict.pack``'s inverse, sharing its
    // key-path conventions so the pair reads as a pair. A bang, or the
    // dictionary's ``dictionary <name>`` reference, snapshots the bound
    // dictionary and sends each path's value out its outlet right to left
    // through ``SendAtom``, so a number leaves as a number; a path the
    // dictionary does not hold sends nothing rather than a zero, ``.dict``'s
    // miss rule.
    DEFOBJ(G_DICT_UNPACK, ".dict.unpack");

    // An ordered, index-addressed sequence shared by name (issue #548), the
    // second type built on the value model ``.dict`` settled: an ``OUT_TYPE``
    // carries a value, never an identity, so an array is **addressed by name**
    // and what travels down a cord is the message ``array <name>``. Storage
    // lives in the ``namedStore.h`` registry (#684) and is resolved once, on the
    // control thread. An element is one atom, so an array and the list text it
    // spells are the same thing seen twice. The ``array.*`` operations are
    // separate objects written against ``arrayStore``; see
    // genericObjects/gArray.h for the whole model.
    DEFOBJ(G_ARRAY, ".array");

    // The first of the array.* operations written against that model (issue
    // #782): the array is bound from the creation argument —
    // ``.array.at <name> [<index>]`` — because an array is addressed by name
    // and never passed down a cord. An int fetches the element at that
    // position, a bang re-fetches at the stored index, and a list of indices
    // is answered whole, as one list in the order asked; a position the
    // array does not have bangs the miss outlet instead. The object a
    // running patch wires an index into, where ``.array``'s own ``get``
    // needs the index inside the message text.
    DEFOBJ(G_ARRAY_AT, ".array.at");

    // The bound array's length, asked for with a bang and answered as one
    // int — ``store->count`` as it stood at the trigger (issue #783). The
    // number every ``.uzi``-driven walk over an array needs before it can
    // start, and the one ``.zl len`` gives for a list. Asked, never
    // announced: a write to the array emits nothing, ``.value``'s rule that
    // an object driven by its inlet does not emit on its own. Zero is a
    // length, not a miss — an empty or unnamed array answers 0 — so there
    // is no miss outlet where ``.array.at`` needs one.
    DEFOBJ(G_ARRAY_LENGTH, ".array.length");

    // The end-mutators (issue #784): the stack and queue operations that turn
    // a shared array into the buffer a generative patch pushes events onto
    // and pops them off. Each binds the array from its creation argument and
    // acts under one hold of the store's guard. ``.array.push`` adds at the
    // end and ``.array.unshift`` at the front — an int, float or symbol as
    // one element, a list whole in the order sent or refused whole — and both
    // emit the array's reference after an add that lands, so the family
    // chains. ``.array.pop`` removes from the end and ``.array.shift`` from
    // the front; both **emit the element they removed** — the difference from
    // ``.array``'s own ``delete`` — and bang a second outlet when the array
    // is empty, a queue-draining loop's exit condition.
    DEFOBJ(G_ARRAY_PUSH, ".array.push");
    DEFOBJ(G_ARRAY_POP, ".array.pop");
    DEFOBJ(G_ARRAY_SHIFT, ".array.shift");
    DEFOBJ(G_ARRAY_UNSHIFT, ".array.unshift");

    // The position-mutators (issue #785): the middle-of-the-array
    // counterparts of the end-mutators, for a position that arrives on a
    // cord — the edit ``.array``'s own ``insert`` / ``delete`` messages
    // cannot take from a patch. Each binds the array from its creation
    // argument and applies at a stored, per-object position under one hold
    // of the store's guard. ``.array.insert`` adds an element — or a list,
    // whole in the order sent or refused whole — shifting the rest up, and
    // emits the array's reference after an insert that lands; a position
    // past the end is a counted refusal. ``.array.remove`` drops the element
    // at the position and **emits it**, the difference from ``.array``'s own
    // ``delete``; a position the array does not have bangs the miss outlet
    // instead. Exact inverses over ``ArrayInsertAt`` / ``ArrayEraseAt``.
    DEFOBJ(G_ARRAY_INSERT, ".array.insert");
    DEFOBJ(G_ARRAY_REMOVE, ".array.remove");

    // The search objects (issue #786): both are one ``ArrayFind`` — the
    // position of the first element spelling a value, scanned under one hold
    // of the store's guard — and they differ only in how the answer leaves.
    // Each binds the array from its creation argument; a value on the hot
    // inlet searches and stores, a bang re-searches with the stored value,
    // seeded by the second creation argument. ``.array.indexof`` answers
    // in-band: one int, the first matching position or -1 on a miss — Max's
    // own answer, the one a patch can test with ``.sel -1``.
    // ``.array.index`` answers on the family's split: the position out one
    // outlet on a hit, a bang out the miss outlet otherwise, so membership
    // is a cord choice rather than a comparison.
    DEFOBJ(G_ARRAY_INDEXOF, ".array.indexof");
    DEFOBJ(G_ARRAY_INDEX, ".array.index");

    // The reordering primitive (issue #787): Max's ``array.indexmap``, the
    // object every custom permutation is built from and the one ``.zl
    // indexmap`` already gives for a list — with the family's zero-based
    // positions. Binds the array from its creation argument; a bang applies
    // the stored map, seeded by the trailing arguments and replaced by a
    // list on the map inlet, and a list on the trigger is applied at the
    // moment it arrives. Each entry picks the element at that position, in
    // map order — entries may repeat, an index naming no element
    // contributes nothing (``AssignOrder``'s rule), and a negative index
    // refuses the whole map. The whole reorder is one hold of the store's
    // guard, through a scratch table the object owns; a reorder that lands
    // emits the array's reference, so the family chains.
    DEFOBJ(G_ARRAY_INDEXMAP, ".array.indexmap");

    // The four permutations (issue #788): each is an index order plus the
    // shared "apply this order to the store" helper, exactly as ``.zl``'s
    // reordering modes each reduce to ``AssignOrder``. All bind the array
    // from the creation argument, permute it under one hold of the store's
    // guard through a scratch table the object owns, and emit the array's
    // reference after a permutation that lands, so the family chains.
    // ``reverse`` counts the order down; ``rotate`` is where the wrapping
    // the base type refuses actually lives, so it takes a signed amount —
    // any magnitude modulo the length, positive toward the end, negative
    // toward the start; ``scramble`` and ``shuffle`` are one Fisher-Yates
    // body under Max's two names for it, seedable per object, publishing
    // the applied order for ``.array.indexmap`` to put a parallel array
    // into the same new order.
    DEFOBJ(G_ARRAY_REVERSE, ".array.reverse");
    DEFOBJ(G_ARRAY_ROTATE, ".array.rotate");
    DEFOBJ(G_ARRAY_SCRAMBLE, ".array.scramble");
    DEFOBJ(G_ARRAY_SHUFFLE, ".array.shuffle");

    // The fifth permutation (issue #789), and the one with a decision the
    // other four do not have: what the comparison is. ``.zl sort``'s
    // ordering over the store's elements — numbers before symbols in both
    // directions, numbers by value, symbols by their characters — through a
    // stable bounded merge sort, under one hold of the store's guard.
    // Negative direction sorts descending, anything else ascending; a sort
    // that lands publishes the applied zero-based order before the array's
    // reference, so ``.array.indexmap`` can put a parallel array into the
    // same new order.
    DEFOBJ(G_ARRAY_SORT, ".array.sort");

    // The six statistics (issue #790): the read-only reducers, each one read
    // of the store under one hold of its guard, answered as a scalar after
    // the guard is released — ``gArrayLength``'s kin, never the mutating
    // permute base. Five reduce the **numeric** elements (a symbol is
    // skipped, not part of the population) and ``mode`` counts every element
    // by its spelling. ``min``/``max``/``mode`` answer with the element
    // itself, typed the way the patcher spells it; ``mean``/``median``/
    // ``stddev`` answer one float. An empty population bangs the empty
    // outlet — the minimum of nothing does not exist, and a sentinel would
    // be indistinguishable from a real answer. ``median`` and ``mode`` sort
    // a scratch the object owns, never the shared store.
    DEFOBJ(G_ARRAY_MIN, ".array.min");
    DEFOBJ(G_ARRAY_MAX, ".array.max");
    DEFOBJ(G_ARRAY_MEAN, ".array.mean");
    DEFOBJ(G_ARRAY_MEDIAN, ".array.median");
    DEFOBJ(G_ARRAY_MODE, ".array.mode");
    DEFOBJ(G_ARRAY_STDDEV, ".array.stddev");

    // The range readers (issue #791): each outputs a *piece* of the array —
    // as the list text it spells, never as a new named array, since creating
    // one would resolve a name on a message path — collected under one hold
    // of the store's guard. Bounds are zero-based and refused negative; past
    // the end they are bounds of a range, so the piece is the intersection
    // with the live elements, and an ask that selects nothing bangs the
    // empty outlet. ``slice`` is JS's exclusive end (0 or absent extends to
    // the array's end, no reverse); ``subarray`` is the inclusive end with
    // the reversed piece permitted, and ``sub`` is its second Max name over
    // one implementation — scramble/shuffle's arrangement. ``split`` cuts
    // head from tail at a boundary, tail sent first, an empty half silent.
    DEFOBJ(G_ARRAY_SLICE, ".array.slice");
    DEFOBJ(G_ARRAY_SUBARRAY, ".array.subarray");
    DEFOBJ(G_ARRAY_SUB, ".array.sub");
    DEFOBJ(G_ARRAY_SPLIT, ".array.split");

    // The set operations (issue #792): read-only, ``.zl``'s semantics for
    // lists — a set operation produces a set, each element once at its
    // first occurrence, equality by the spelling. ``union`` and ``sect``
    // bind **two** names at creation (gDictCompare's arrangement, snapshot
    // included so no two guards are ever held at once — the same-store
    // case would trip over its own try-lock); ``unique`` thins one array,
    // ``.zl thin``'s selection under Max's array.unique name. The result
    // leaves as the list it spells, never as a new named array, and an
    // empty result bangs the empty outlet.
    DEFOBJ(G_ARRAY_UNION, ".array.union");
    DEFOBJ(G_ARRAY_SECT, ".array.sect");
    DEFOBJ(G_ARRAY_UNIQUE, ".array.unique");

    DEFOBJ(G_PRINT, ".print");

    DEFOBJ(G_LOADBANG, ".loadbang");
    DEFOBJ(G_LOADMESS, ".loadmess");

    DEFOBJ(G_ADD, ".+");
    DEFOBJ(G_SUBSTRACT, ".-");
    DEFOBJ(G_MULTIPLY, ".*");
    DEFOBJ(G_DIVIDE, "./");

    DEFOBJ(G_REVERSESUBSTRACT, ".!-");
    DEFOBJ(G_REVERSEDIVIDE, ".!/");
    DEFOBJ(G_SWAP, ".swap");
    DEFOBJ(G_MODULO, ".%");
    DEFOBJ(G_INTDIVIDE, ".div");

    DEFOBJ(G_SCALE, ".scale");
    DEFOBJ(G_ZMAP, ".zmap");
    DEFOBJ(G_LINEDRIVE, ".linedrive");

    DEFOBJ(G_CLIP, ".clip");
    DEFOBJ(G_PONG, ".pong");

    DEFOBJ(G_SPLIT, ".split");

    DEFOBJ(G_SLIDE, ".slide");

    DEFOBJ(G_EXPR, ".expr");
    DEFOBJ(G_VEXPR, ".vexpr");

    DEFOBJ(G_ABS, ".abs");
    DEFOBJ(G_SQRT, ".sqrt");
    DEFOBJ(G_POW, ".pow");
    DEFOBJ(G_ROUND, ".round");

    DEFOBJ(G_SIN, ".sin");
    DEFOBJ(G_COS, ".cos");
    DEFOBJ(G_TAN, ".tan");
    DEFOBJ(G_ASIN, ".asin");
    DEFOBJ(G_ACOS, ".acos");
    DEFOBJ(G_ATAN, ".atan");
    DEFOBJ(G_ATAN2, ".atan2");
    DEFOBJ(G_SINH, ".sinh");
    DEFOBJ(G_COSH, ".cosh");
    DEFOBJ(G_TANH, ".tanh");
    DEFOBJ(G_ASINH, ".asinh");
    DEFOBJ(G_ACOSH, ".acosh");
    DEFOBJ(G_ATANH, ".atanh");

    DEFOBJ(G_EQUAL, ".==");
    DEFOBJ(G_NOTEQUAL, ".!=");
    DEFOBJ(G_LESS, ".<");
    DEFOBJ(G_LESSEQUAL, ".<=");
    DEFOBJ(G_GREATER, ".>");
    DEFOBJ(G_GREATEREQUAL, ".>=");
    DEFOBJ(G_LOGICALAND, ".&&");
    DEFOBJ(G_LOGICALOR, ".||");

    DEFOBJ(G_MAXIMUM, ".maximum");
    DEFOBJ(G_MINIMUM, ".minimum");

    DEFOBJ(G_PEAK, ".peak");
    DEFOBJ(G_TROUGH, ".trough");

    DEFOBJ(G_PAST, ".past");

    DEFOBJ(G_CHANGE, ".change");
    DEFOBJ(G_TOGEDGE, ".togedge");

    DEFOBJ(G_BITAND, ".&");
    DEFOBJ(G_BITOR, ".|");
    DEFOBJ(G_SHIFTLEFT, ".<<");
    DEFOBJ(G_SHIFTRIGHT, ".>>");

    DEFOBJ(G_RANDOM, ".random");
    DEFOBJ(G_DRUNK, ".drunk");
    DEFOBJ(G_URN, ".urn");
    DEFOBJ(G_DECIDE, ".decide");
    DEFOBJ(G_PROB, ".prob");
    DEFOBJ(G_ANAL, ".anal");
    DEFOBJ(G_HISTO, ".histo");
    DEFOBJ(G_MEAN, ".mean");
    DEFOBJ(G_METRO, ".metro");
    DEFOBJ(G_DELAY, ".delay");
    DEFOBJ(G_TRANSPORT, ".transport");
    DEFOBJ(G_SETCLOCK, ".setclock");
    DEFOBJ(G_WHEN, ".when");
    DEFOBJ(G_TRANSLATE, ".translate");
    DEFOBJ(G_TIMEPOINT, ".timepoint");
    DEFOBJ(G_TEMPO, ".tempo");
    DEFOBJ(G_CLOCKER, ".clocker");
    DEFOBJ(G_TIMER, ".timer");
    DEFOBJ(G_PIPE, ".pipe");
    DEFOBJ(G_SPEEDLIM, ".speedlim");
    DEFOBJ(G_QLIM, ".qlim");
    DEFOBJ(G_THRESH, ".thresh");
    DEFOBJ(G_QUICKTHRESH, ".quickthresh");
    DEFOBJ(G_LINE, ".line");
    DEFOBJ(G_BLINE, ".bline");

    DEFOBJ(D_ADD, "~+");
    DEFOBJ(D_SUBSTRACT, "~-");
    DEFOBJ(D_MULTIPLY, "~*");
    DEFOBJ(D_DIVIDE, "~/");

    DEFOBJ(D_CLIP, "~clip");

    DEFOBJ(MIDITOFREQUENCY, ".mtof");
    DEFOBJ(FREQUENCYTOMIDI, ".ftom");

    DEFOBJ(G_ATODB, ".atodb");
    DEFOBJ(G_DBTOA, ".dbtoa");
    DEFOBJ(G_CARTOPOL, ".cartopol");
    DEFOBJ(G_POLTOCAR, ".poltocar");

    DEFOBJ(D_LOWPASS, "~lp");
    DEFOBJ(D_HIGHPASS, "~hp");
    DEFOBJ(D_BANDPASS, "~bp");
    DEFOBJ(D_VCF, "~vcf");

    DEFOBJ(M_OUT, ".midiout");
    DEFOBJ(M_IN, ".midiin");
    DEFOBJ(M_NOTEIN, ".notein");
    DEFOBJ(M_CTLIN, ".ctlin");
    DEFOBJ(M_BENDIN, ".bendin");
    DEFOBJ(M_PGMIN, ".pgmin");
    DEFOBJ(M_TOUCHIN, ".touchin");
    DEFOBJ(M_POLYIN, ".polyin");
    DEFOBJ(M_RTIN, ".rtin");
    DEFOBJ(M_NOTEON, ".noteon");
    DEFOBJ(M_NOTEOFF, ".noteoff");
    DEFOBJ(M_CONTROL, ".controlchange");
    DEFOBJ(M_POLYPRESS, ".polypressure");
    DEFOBJ(M_CHANPRESS, ".channelpressure");
    DEFOBJ(M_PROGCHANGE, ".programchange");
    DEFOBJ(M_BENDOUT, ".bendout");
    DEFOBJ(M_PARSE, ".midiparse");
    DEFOBJ(M_FORMAT, ".midiformat");
    DEFOBJ(M_SYSEXIN, ".sysexin");
    DEFOBJ(M_SXFORMAT, ".sxformat");
    DEFOBJ(M_XBENDIN, ".xbendin");
    DEFOBJ(M_XBENDIN2, ".xbendin2");
    DEFOBJ(M_XCTLIN, ".xctlin");
    DEFOBJ(M_XNOTEIN, ".xnotein");
    DEFOBJ(M_XMIDIIN, ".xmidiin");
    DEFOBJ(M_XBENDOUT, ".xbendout");
    DEFOBJ(M_XBENDOUT2, ".xbendout2");
    DEFOBJ(M_XCTLOUT, ".xctlout");
    DEFOBJ(M_XNOTEOUT, ".xnoteout");
    DEFOBJ(M_RPNIN, ".rpnin");
    DEFOBJ(M_NRPNIN, ".nrpnin");
    DEFOBJ(M_RPNOUT, ".rpnout");
    DEFOBJ(M_NRPNOUT, ".nrpnout");
    DEFOBJ(M_MPECONFIG, ".mpeconfig");
    DEFOBJ(M_MPEFORMAT, ".mpeformat");
    DEFOBJ(M_MPEPARSE, ".mpeparse");
    DEFOBJ(M_MIDIINFO, ".midiinfo");
    DEFOBJ(M_MIDIFLUSH, ".midiflush");
    DEFOBJ(M_MAKENOTE, ".makenote");
    DEFOBJ(M_STRIPNOTE, ".stripnote");
    DEFOBJ(M_FLUSH, ".flush");
    DEFOBJ(M_SUSTAIN, ".sustain");
    DEFOBJ(M_POLY, ".poly");
    DEFOBJ(M_BORAX, ".borax");
    DEFOBJ(M_OFFER, ".offer");
  };
} // namespace YSE
