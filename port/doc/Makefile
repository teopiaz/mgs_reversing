PYTHON ?= python3

.PHONY: html clean deps

html:
	$(PYTHON) build_html.py

deps:
	$(PYTHON) -m pip install markdown

clean:
	rm -rf html