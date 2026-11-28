#!/usr/bin/env python3
"""Check thematic break spelling, placement, admission and screen projection."""
from pathlib import Path

from document_mutations import main

ROOT = Path(__file__).resolve().parents[2]
DOC = ROOT / "common/app/doc"
MD = DOC / "Markdown.hpp"
HTML = DOC / "Html.hpp"
ADMISSION = "if (!this->acceptsBlock())\n              return false;\n            static_cast<Writer &>(*this).rule();"
MUTATIONS = [
    ("omit-markdown-rule", MD, 'this->block("***");', '', "Blocks"),
    ("omit-html-rule", HTML, 'this->beginLeaf();\n            this->fragment += "<hr>\\n";', '', "Blocks"),
    ("allow-item-rule", DOC / "detail/ContainerWriter.hpp", ADMISSION,
     ADMISSION.replace("acceptsBlock()", "acceptsListOrCodeBlock()"), "Grammar"),
    ("unquote-rule", MD, 'this->block("***");', 'this->beginLeaf(); this->fragment += "***\\n";', "Quotes"),
    ("dash-marker", MD, 'this->block("***");', 'this->block("---");', "Blocks"),
    ("remove-screen-text", DOC / "Rule.hpp", 'Text("* * *")', 'Text("")', "Screen"),
]

if __name__ == "__main__":
    main(ROOT, "Rule", MUTATIONS)
