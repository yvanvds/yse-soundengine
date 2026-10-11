"""Sphinx configuration for the libYSE documentation.

The build flow is:

    1. ``doxygen Doxyfile`` (run from ``documentation/``) emits XML into
       ``source/_doxygen/xml``.
    2. Sphinx + Breathe consume that XML and render reStructuredText pages
       using the sphinx-book-theme.

Additionally, the ``builder-inited`` hook below renders the per-category
patcher object reference under ``source/patcher/objects/`` from
``source/_data/patcher_objects.json`` via Jinja templates — see
:func:`_render_patcher_objects` and issues #103 / #870. Each object's
long-form notes come from ``source/_data/patcher_notes/<slug>.rst`` (#997,
see :func:`_load_patcher_notes`).

If the Doxygen XML directory is missing, Sphinx will emit empty API
pages — run Doxygen first.
"""

import json
import re
from pathlib import Path

# -- Project information -----------------------------------------------------

project = "libYSE"
author = "Yvan Vander Sanden"
copyright = "2014-2026, Yvan Vander Sanden"


def _read_engine_version():
    """Parse the canonical VERSION literal out of YseEngine/system.hpp.

    Single source of truth for the docs banner. ``yse.py release`` writes
    to ``system.hpp`` only; this picks it up at Sphinx-build time so the
    docs never lag a release. See issue #89.
    """
    engine_hpp = (
        Path(__file__).resolve().parent.parent.parent / "YseEngine" / "system.hpp"
    )
    m = re.search(
        r'VERSION\s*=\s*"(\d+)\.(\d+)\.(\d+)"',
        engine_hpp.read_text(encoding="utf-8"),
    )
    if not m:
        raise RuntimeError(
            f"VERSION literal not found in {engine_hpp}; docs build cannot "
            f"determine the release banner."
        )
    return m.group(1), m.group(2), m.group(3)


_major, _minor, _patch = _read_engine_version()
# Sphinx convention: ``version`` is short (X.Y), ``release`` is full (X.Y.Z).
version = f"{_major}.{_minor}"
release = f"{_major}.{_minor}.{_patch}"

# -- General configuration ---------------------------------------------------

extensions = [
    "breathe",
    "myst_parser",
]

source_suffix = {
    ".rst": "restructuredtext",
    ".md": "markdown",
}

# ``_data`` holds inputs to the patcher reference renderer (the JSON snapshot
# and the per-category notes files under ``_data/patcher_notes/``), not pages.
exclude_patterns = ["_build", "_doxygen", "_data", "Thumbs.db", ".DS_Store"]

# Breathe re-emits the ``YSE`` namespace target from every header processed by
# a ``doxygenfile`` directive. Sphinx then floods the build with duplicate-
# target / duplicate-declaration warnings — known limitation, no impact on
# the rendered HTML. Suppress them so real warnings stay visible.
suppress_warnings = [
    "docutils",
    "duplicate_declaration.cpp",
]

# -- Breathe configuration ---------------------------------------------------

# Doxygen >= 1.9.7 wraps markdown headings that skip levels (e.g. a `###`
# with no enclosing `#`/`##`) in anonymous, title-less <sect1>/<sect2>
# elements. Breathe (<= 4.36.0) crashes on the title-less <sect2>: its
# generated parser never sets `.title` on docSect2Type when the element has
# no <title> child, so visit_docsectN raises AttributeError. Patch the
# renderer to hoist the children of anonymous sections instead of emitting
# a section with an empty heading — the same flat structure older Doxygen
# produced. Drop this once breathe handles anonymous sections upstream.


def _patch_breathe_anonymous_sections():
    from docutils import nodes
    from breathe.renderer.sphinxrenderer import SphinxRenderer

    def visit_docsectN(self, node):
        title = getattr(node, "title", "") or ""
        if not title:
            return self.render_iterable(node.content_)
        section = nodes.section()
        section["ids"].append(self.get_refid(node.id))
        section += nodes.title(title, title)
        section += self.create_doxygen_target(node)
        section += self.render_iterable(node.content_)
        return [section]

    SphinxRenderer.visit_docsectN = visit_docsectN
    # The renderer dispatches through the `methods` dict, which captured the
    # original function at class-definition time — repoint those entries too.
    for key in ("docsect1", "docsect2", "docsect3"):
        SphinxRenderer.methods[key] = visit_docsectN


_patch_breathe_anonymous_sections()

# Path is relative to this conf.py.
_doxygen_xml = Path(__file__).parent / "_doxygen" / "xml"

breathe_projects = {"libYSE": str(_doxygen_xml)}
breathe_default_project = "libYSE"
# Only documented members appear. The Doxyfile is the upstream filter; this
# keeps breathe from re-emitting anything Doxygen happened to extract anyway.
breathe_default_members = ("members",)
breathe_show_include = False

# -- HTML output -------------------------------------------------------------

html_theme = "sphinx_book_theme"
html_title = f"libYSE {version}"
html_static_path = ["_static"]

# Logo and favicon are sourced from the repo-root `logo/` directory so the
# README and the docs share a single canonical asset. Sphinx resolves the
# path relative to this conf.py and copies the file into `_static/` at
# build time.
html_logo = "../../logo/yse-logo.svg"
html_favicon = "../../logo/yse-icon.svg"

html_theme_options = {
    "repository_url": "https://github.com/yvanvds/yse-soundengine",
    "use_repository_button": True,
    "use_issues_button": True,
    "use_edit_page_button": False,
    "path_to_docs": "documentation/source",
    "home_page_in_toc": True,
    "show_navbar_depth": 2,
}


# -- Auto-generated patcher reference -----------------------------------------
#
# Issue #103: the snapshot at _data/patcher_objects.json is produced by the
# ``dump_patcher_meta`` C++ tool (``python yse.py dump-patcher-meta``); this
# hook turns it into the reStructuredText pages consumed by Sphinx.  Running
# the render inside ``builder-inited`` keeps every documentation build —
# local ``make html``, CI's documentation.yml workflow — in lockstep with
# the committed JSON without anyone needing to remember to invoke a
# generator step.
#
# Issue #870: one page per category under ``patcher/objects/``, plus an
# index page listing the categories.  The whole directory is generated and
# gitignored; it is cleared before each render so a category that empties
# out does not leave an orphaned page behind.
#
# Categories are listed in a fixed reading order (key, page slug, title,
# one-line summary).  The keys are the ``YsePCategory`` names the engine
# emits.  ``UNSET`` is a fallback that should never reach the docs because
# the failsafe doctest in Tests/patcher/test_doc_coverage.cpp rejects it;
# an unknown key (an engine category this table does not know yet) is
# reported as a warning and rendered under "Uncategorised".

_PATCHER_CATEGORIES = [
    ("OSC", "signal_generators", "Signal generators",
     "Audio-rate sources: oscillators, noise and ramps."),
    ("FILTER", "filters", "Filters",
     "Audio-rate filters."),
    ("DELAY", "delay", "Delay",
     "Delay lines, taps and feedback structures."),
    ("DYNAMICS", "dynamics", "Dynamics",
     "Compressors, limiters, gates and envelope followers."),
    ("SAMPLING", "sampling", "Sampling",
     "Recording audio into buffers and playing it back."),
    ("SPECTRAL", "spectral", "Spectral",
     "FFT and frequency-domain processing."),
    ("ANALYSIS", "analysis", "Analysis",
     "Measuring signals: levels, pitch, zero crossings and snapshots."),
    ("MATH", "math", "Math",
     "Arithmetic, comparison, conversion, scaling and analysis of numbers."),
    ("RANDOM", "random", "Random",
     "Random numbers, random walks and probabilistic choices."),
    ("ROUTING", "routing", "Routing",
     "Steering messages between inlets and outlets."),
    ("CONTROL", "control_flow", "Control flow",
     "Ordering, looping, conditions and load-time messages."),
    ("LIST", "lists", "Lists",
     "Building lists, taking them apart and processing them."),
    ("STRING", "strings", "Strings and symbols",
     "Text, symbols and character codes."),
    ("COLLECTION", "collections", "Collections",
     "Stores that hold numbers or messages for later recall."),
    ("DICT", "dictionaries", "Dictionaries",
     "The ``.dict`` family: keyed, nested data."),
    ("ARRAY", "arrays", "Arrays",
     "The ``.array`` family: ordered data and the operations on it."),
    ("TIME", "time", "Time",
     "Clocks, delays, ramps, rate limiting and tempo."),
    ("SEQUENCE", "sequencing", "Sequencing",
     "Recording messages and playing them back in time."),
    ("MIDI", "midi", "MIDI",
     "Formatting, parsing and note handling for MIDI, and MIDI device I/O."),
    ("GUI", "gui", "GUI controls",
     "User-facing controls and message boxes, driven by the GUI value protocol."),
    ("IO", "input_output", "Input and output",
     "Audio in and out of the graph, named send/receive, and host-facing I/O."),
    ("ENCAPSULATION", "subpatchers", "Subpatchers",
     "Subpatchers and the inlets and outlets that form their boundary."),
    ("GENERIC", "other", "Other",
     "Objects that fit no other category."),
    ("UNSET", "uncategorised", "Uncategorised",
     "Objects without a category. This page should be empty."),
]

# Shown on every object flagged ``requires_midi_device`` in the snapshot.
_MIDI_DEVICE_NOTE = (
    "Only available when libYSE is built with ``YSE_ENABLE_MIDI_DEVICE`` "
    "(on by default on Windows and Linux desktop builds). On macOS and "
    "Android the option is off and this object is not registered, so a "
    "patch that uses it is not portable to those platforms."
)


# Issue #997: an object's ``ADD_DESCRIPTION`` is a short summary (the C API
# and editor tooltips show it too). Longer reference notes live in
# ``_data/patcher_notes/<slug>.rst``, one file per category slug above. Each
# object's notes start with a marker line naming the object, and run to the
# next marker or the end of the file:
#
#     .. patcher-object:: .zl
#
#     Ordinary reStructuredText: paragraphs, lists, code blocks.
#
# The body is rendered under the object's summary, before its pin and
# parameter tables. Notes are keyed by the marker text, never by a filename,
# because object names hold characters such as ``~*``, ``.|`` and ``.!-``.
# README.md in that directory documents the format for contributors.

_NOTES_MARKER = ".. patcher-object::"
# Case-sensitive on purpose: "max" (a maximum) and "Maximum" are fine.
_NOTES_FORBIDDEN_WORD = re.compile(r"\bMax\b")


def _is_rst_comment_line(line):
    """True for a blank line or a line that can belong to an RST comment."""
    return not line.strip() or line.startswith("..") or line[:1].isspace()


def _split_notes_file(path, rel, logger):
    """Split one notes file into ``[(name, marker line, body text)]``.

    Anything before the first marker is the file header, which may only hold
    comments so that no text is silently dropped. A marker must name exactly
    one object; a malformed marker is reported and its section dropped.
    """
    sections = []
    for lineno, line in enumerate(
        path.read_text(encoding="utf-8").splitlines(), start=1
    ):
        loc = f"{rel}:{lineno}"
        if line.startswith(_NOTES_MARKER):
            words = line[len(_NOTES_MARKER):].split()
            if len(words) != 1:
                logger.warning(
                    "a patcher-object marker names exactly one object.",
                    location=loc,
                )
            sections.append([words[0] if len(words) == 1 else "", lineno, []])
        elif sections:
            if _NOTES_FORBIDDEN_WORD.search(line):
                logger.warning(
                    "patcher notes for %s name Max; describe YSE's behaviour "
                    "on its own terms.",
                    sections[-1][0],
                    location=loc,
                )
            sections[-1][2].append(line)
        elif not _is_rst_comment_line(line):
            logger.warning(
                "text before the first patcher-object marker is not "
                "rendered; make it a comment or move it under a marker.",
                location=loc,
            )
    return [
        (name, lineno, "\n".join(body).strip("\n"))
        for name, lineno, body in sections
        if name
    ]


def _notes_problem(name, text, data, where):
    """Why a notes section cannot be used, or ``None`` if it can."""
    if not text.strip():
        return f"patcher notes for {name} are empty."
    if name not in data:
        return (
            f"patcher notes for {name}, which is not in the metadata "
            f"snapshot (_data/patcher_objects.json)."
        )
    if name in where:
        return f"duplicate patcher notes for {name}; the first is at {where[name]}."
    return None


def _load_patcher_notes(notes_dir, data, logger):
    """Return ``{object name: notes body}`` from ``notes_dir/*.rst``.

    Warns (which fails a ``-W`` build) on a note for an object that is not
    in the snapshot, on a second note for the same object, on a note filed
    under another category's file, on a file whose name is not a category
    slug, on a malformed marker, on text before the first marker that is not
    a comment, and on the word "Max" in a body.
    """
    slug_of = {key: slug for key, slug, *_ in _PATCHER_CATEGORIES}
    notes = {}
    where = {}
    if not notes_dir.is_dir():
        return notes

    for path in sorted(notes_dir.glob("*.rst")):
        rel = path.relative_to(notes_dir.parent.parent).as_posix()
        if path.stem not in slug_of.values():
            logger.warning(
                "patcher notes file %s is not named after a category slug in "
                "_PATCHER_CATEGORIES.",
                rel,
                location=f"{rel}:1",
            )

        for name, lineno, text in _split_notes_file(path, rel, logger):
            loc = f"{rel}:{lineno}"
            problem = _notes_problem(name, text, data, where)
            if problem:
                logger.warning(problem, location=loc)
                continue
            expected = slug_of.get(data[name].get("category", "UNSET"))
            if expected is not None and expected != path.stem:
                logger.warning(
                    "patcher notes for %s belong in %s.rst, the file for its "
                    "category.",
                    name,
                    expected,
                    location=loc,
                )
            notes[name] = text
            where[name] = loc

    return notes


def _render_patcher_objects(app):
    """Render ``patcher/objects/*.rst`` from the JSON snapshot."""
    import jinja2  # Sphinx already depends on Jinja2; no extra requirement.
    from sphinx.util import logging as sphinx_logging

    logger = sphinx_logging.getLogger(__name__)

    src_dir = Path(app.srcdir)
    data_path = src_dir / "_data" / "patcher_objects.json"
    notes_dir = src_dir / "_data" / "patcher_notes"
    template_dir = src_dir / "_templates"
    out_dir = src_dir / "patcher" / "objects"

    if not data_path.exists():
        logger.warning(
            "patcher metadata snapshot not found at %s; "
            "run `python yse.py dump-patcher-meta` to generate it.",
            data_path,
        )
        return

    with data_path.open(encoding="utf-8") as f:
        data = json.load(f)

    notes = _load_patcher_notes(notes_dir, data, logger)
    for name, obj in data.items():
        obj["notes"] = notes.get(name, "")

    known ={key for key, *_ in _PATCHER_CATEGORIES}
    grouped = {key: [] for key in known}
    for name in sorted(data.keys()):
        obj = data[name]
        key = obj.get("category", "UNSET")
        if key not in known:
            logger.warning(
                "patcher object %s has category %s, which conf.py does not "
                "know; add it to _PATCHER_CATEGORIES.",
                name,
                key,
            )
            key = "UNSET"
        grouped[key].append(obj)

    # Only categories that hold objects get a page, in reading order.
    categories = [
        {
            "key": key,
            "slug": slug,
            "title": title,
            "summary": summary,
            "objects": grouped[key],
            "has_midi_device": any(
                o.get("requires_midi_device") for o in grouped[key]
            ),
        }
        for key, slug, title, summary in _PATCHER_CATEGORIES
        if grouped[key]
    ]

    env = jinja2.Environment(
        loader=jinja2.FileSystemLoader(str(template_dir)),
        keep_trailing_newline=True,
        trim_blocks=False,
        lstrip_blocks=False,
    )

    out_dir.mkdir(parents=True, exist_ok=True)
    for stale in out_dir.glob("*.rst"):
        stale.unlink()

    index = env.get_template("patcher_objects_index.rst.j2")
    (out_dir / "index.rst").write_text(
        index.render(categories=categories, total=len(data)), encoding="utf-8"
    )
    page = env.get_template("patcher_objects_category.rst.j2")
    for category in categories:
        (out_dir / f"{category['slug']}.rst").write_text(
            page.render(category=category, midi_device_note=_MIDI_DEVICE_NOTE),
            encoding="utf-8",
        )


def setup(app):
    app.connect("builder-inited", _render_patcher_objects)
    return {"version": "1.0", "parallel_read_safe": True}
