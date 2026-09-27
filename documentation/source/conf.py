"""Sphinx configuration for the libYSE documentation.

The build flow is:

    1. ``doxygen Doxyfile`` (run from ``documentation/``) emits XML into
       ``source/_doxygen/xml``.
    2. Sphinx + Breathe consume that XML and render reStructuredText pages
       using the sphinx-book-theme.

Additionally, the ``builder-inited`` hook below renders the per-category
patcher object reference under ``source/patcher/objects/`` from
``source/_data/patcher_objects.json`` via Jinja templates — see
:func:`_render_patcher_objects` and issues #103 / #870.

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

exclude_patterns = ["_build", "_doxygen", "Thumbs.db", ".DS_Store"]

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


def _render_patcher_objects(app):
    """Render ``patcher/objects/*.rst`` from the JSON snapshot."""
    import jinja2  # Sphinx already depends on Jinja2; no extra requirement.
    from sphinx.util import logging as sphinx_logging

    logger = sphinx_logging.getLogger(__name__)

    src_dir = Path(app.srcdir)
    data_path = src_dir / "_data" / "patcher_objects.json"
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

    known = {key for key, *_ in _PATCHER_CATEGORIES}
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
