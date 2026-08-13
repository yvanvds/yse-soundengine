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
   *    ``G_MULTISLIDER``, ``G_MATRIXCTRL``, ``G_KSLIDER``, ``G_NSLIDER``,
   *    ``G_DIAL``, ``G_INCDEC``, ``G_RANDOM``.
   *  - Indexed selection: ``G_UMENU``, ``G_RADIOGROUP``, ``G_TAB``.
   *  - Labelled switches: ``G_LED``, ``G_TEXTBUTTON``.
   *  - Text entry: ``G_TEXTEDIT``.
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
   *  - Dictionaries: ``G_DICT``.
   *  - Arrays: ``G_ARRAY``.
   *  - Debugging: ``G_PRINT``.
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
    DEFOBJ(G_MULTISLIDER, ".multislider");
    DEFOBJ(G_MATRIXCTRL, ".matrixctrl");
    DEFOBJ(G_KSLIDER, ".kslider");
    DEFOBJ(G_NSLIDER, ".nslider");
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
