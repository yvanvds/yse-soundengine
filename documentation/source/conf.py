"""Sphinx configuration for the libYSE documentation.

The build flow is:

    1. ``doxygen Doxyfile`` (run from ``documentation/``) emits XML into
       ``source/_doxygen/xml``.
    2. Sphinx + Breathe consume that XML and render reStructuredText pages
       using the sphinx-book-theme.

Additionally, the ``builder-inited`` hook below renders
``source/api/patcher_objects.rst`` from
``source/_data/patcher_objects.json`` via a Jinja template — see
:func:`_render_patcher_objects` and issue #103.

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
# hook turns it into the reStructuredText page consumed by Sphinx.  Running
# the render inside ``builder-inited`` keeps every documentation build —
# local ``make html``, CI's documentation.yml workflow — in lockstep with
# the committed JSON without anyone needing to remember to invoke a
# generator step.  The rendered ``patcher_objects.rst`` is gitignored.
#
# Category headings are listed in a fixed order; ``UNSET`` is a fallback
# that should never reach the docs because the failsafe doctest in
# Tests/patcher/test_doc_coverage.cpp rejects it at build time.

_PATCHER_CATEGORY_ORDER = [
    ("OSC", "Oscillators"),
    ("FILTER", "Filters"),
    ("MATH", "Math"),
    ("GENERIC", "Generic / routing"),
    ("GUI", "GUI controls"),
    ("TIME", "Time"),
    ("MIDI", "MIDI"),
    ("UNSET", "Uncategorised"),
]


def _render_patcher_objects(app):
    """Render ``api/patcher_objects.rst`` from the JSON snapshot."""
    import jinja2  # Sphinx already depends on Jinja2; no extra requirement.
    from sphinx.util import logging as sphinx_logging

    logger = sphinx_logging.getLogger(__name__)

    src_dir = Path(app.srcdir)
    data_path = src_dir / "_data" / "patcher_objects.json"
    template_dir = src_dir / "_templates"
    out_path = src_dir / "api" / "patcher_objects.rst"

    if not data_path.exists():
        logger.warning(
            "patcher metadata snapshot not found at %s; "
            "run `python yse.py dump-patcher-meta` to generate it.",
            data_path,
        )
        return

    with data_path.open(encoding="utf-8") as f:
        data = json.load(f)

    # Group by category, preserving the lexicographic order the dumper
    # already emits inside each group. Categories themselves render in
    # the canonical order above so the table of contents is stable.
    grouped = {label: [] for _, label in _PATCHER_CATEGORY_ORDER}
    category_label = dict(_PATCHER_CATEGORY_ORDER)
    for name in sorted(data.keys()):
        obj = data[name]
        label = category_label.get(obj.get("category", "UNSET"), "Uncategorised")
        grouped[label].append(obj)

    # Drop empty categories so we don't emit a heading with no body.
    by_category = {k: v for k, v in grouped.items() if v}

    env = jinja2.Environment(
        loader=jinja2.FileSystemLoader(str(template_dir)),
        keep_trailing_newline=True,
        trim_blocks=False,
        lstrip_blocks=False,
    )
    template = env.get_template("patcher_objects.rst.j2")
    rendered = template.render(by_category=by_category)

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(rendered, encoding="utf-8")


def setup(app):
    app.connect("builder-inited", _render_patcher_objects)
    return {"version": "1.0", "parallel_read_safe": True}
