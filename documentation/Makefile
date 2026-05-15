# Convenience targets for building the libYSE documentation.
#
#   make html        # full build: doxygen XML + sphinx HTML
#   make doxygen     # just regenerate Doxygen XML
#   make sphinx      # just rebuild Sphinx HTML (assumes XML is current)
#   make serve       # python http server on http://localhost:8000
#   make clean       # delete build/ and source/_doxygen/

SPHINXBUILD ?= sphinx-build
SOURCEDIR    = source
BUILDDIR     = build

.PHONY: html doxygen sphinx serve clean help

html: doxygen sphinx

doxygen:
	doxygen Doxyfile

sphinx:
	$(SPHINXBUILD) -b html "$(SOURCEDIR)" "$(BUILDDIR)/html"

serve:
	@echo "Serving on http://localhost:8000 (Ctrl-C to stop)"
	@cd $(BUILDDIR)/html && python -m http.server 8000

clean:
	rm -rf "$(BUILDDIR)" "$(SOURCEDIR)/_doxygen"

help:
	@echo "Targets: html, doxygen, sphinx, serve, clean"
