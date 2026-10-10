# Patcher object notes

Long-form reference notes for patcher objects, rendered into the object
reference (`documentation/source/patcher/objects/`) by the `builder-inited`
hook in `documentation/source/conf.py` (issue #997).

An object's documentation has two parts:

- **Summary**: the object's `ADD_DESCRIPTION`, 1-3 sentences and at most
  300 characters. The C API (`yse_patcher_get_type_description`), editor
  tooltips and the top of the reference entry show it.
- **Notes**: everything a patch author needs beyond that (behaviour in
  detail, edge cases, message formats, examples). They live here.

Implementation details that only matter to contributors (RT-safety
reasoning, algorithms, threading) belong in source comments, not in either
part.

## Format

One file per category, named after its slug in `_PATCHER_CATEGORIES` in
`conf.py`: `signal_generators.rst`, `filters.rst`, `math.rst`, `random.rst`,
`routing.rst`, `control_flow.rst`, `lists.rst`, `strings.rst`,
`collections.rst`, `dictionaries.rst`, `arrays.rst`, `time.rst`,
`sequencing.rst`, `midi.rst`, `gui.rst`, `input_output.rst`,
`subpatchers.rst`, `other.rst`. Create a file when its category gets its
first notes.

Each object's notes start with a marker line naming the object, exactly as
it is registered, and run to the next marker or the end of the file:

```rst
.. Notes for the Lists category. Format: _data/patcher_notes/README.md.

.. patcher-object:: .zl

The first word of a message selects the mode. ...

- ``group n``: collects incoming items until ``n`` have arrived ...
- ``slice n``: ...

.. code-block:: text

   [zl slice 2] <- 1 2 3 4   =>   left: 1 2, right: 3 4

.. patcher-object:: .zl.len

...
```

- The body is ordinary reStructuredText (paragraphs, bullet lists, code
  blocks, `.. note::`) at the top level, with no indentation. It is rendered
  under the object's summary, before its inlet, outlet and parameter tables.
- No section headings: each object is already a section of its category
  page. Use `.. rubric:: Title` or a bold paragraph for a sub-heading.
- Before the first marker, a file may only hold RST comments (lines starting
  with `..`, and indented continuations).
- Notes are matched to objects by the marker text, never by filename, so
  names such as `~*`, `.|` or `.!-` need no escaping.
- Don't mention Max. Describe what YSE does on its own terms.

## Checks

The docs build (`sphinx-build -W`) fails when a notes file:

- has notes for an object that isn't in `_data/patcher_objects.json`,
- has a second notes section for the same object (in any file),
- files an object under another category's file,
- isn't named after a category slug,
- has text before the first marker that isn't a comment,
- has an empty section or a marker without a name,
- contains the word `Max` (case-sensitive whole word: `max` and `Maximum`
  are fine).

`Tests/patcher/test_doc_coverage.cpp` enforces the summary side: at most 300
characters, and no `Max` in the description or in any inlet, outlet or
parameter doc. `DocRuleExempt()` in that file is the allow-list for
exceptions. It has been empty since #998 and must stay empty: every object,
including a new one, follows the rule.
