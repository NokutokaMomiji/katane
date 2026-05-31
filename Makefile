.PHONY: all debug clean rebuild

PYTHON ?= python

all:
	$(PYTHON) build.py --mode release

debug:
	$(PYTHON) build.py --mode debug

clean:
	$(PYTHON) build.py --clean

rebuild:
	$(PYTHON) build.py --rebuild --mode release
