"""Sphinx configuration for the libYSE documentation.

The build flow is:

    1. ``doxygen Doxyfile`` (run from ``documentation/``) emits XML into
       ``source/_doxygen/xml``.
    2. Sphinx + Breathe consume that XML and render reStructuredText pages
       using the sphinx-book-theme.

If the XML directory is missing, Sphinx will emit empty API pages — run
Doxygen first.
"""

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
