"""Sphinx configuration for the libYSE documentation.

The build flow is:

    1. ``doxygen Doxyfile`` (run from ``documentation/``) emits XML into
       ``source/_doxygen/xml``.
    2. Sphinx + Breathe consume that XML and render reStructuredText pages
       using the sphinx-book-theme.

If the XML directory is missing, Sphinx will emit empty API pages — run
Doxygen first.
"""

from pathlib import Path

# -- Project information -----------------------------------------------------

project = "libYSE"
author = "Yvan Vander Sanden"
copyright = "2014-2026, Yvan Vander Sanden"
release = "2.1"

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
html_title = "libYSE 2.1"
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
