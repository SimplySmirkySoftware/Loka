#!/usr/bin/env python3
"""Run the HTML spelling, projection and failure-atomicity mutations."""
from pathlib import Path
from document_mutations import main

ROOT = Path(__file__).resolve().parents[2]
HTML = ROOT / "common/app/doc/Html.hpp"
PROJECTION = HTML.parent / "detail/Projection.hpp"
MUTATIONS = [
    ("ampersand", HTML, 'out += "&amp;";', "out += c;", "Inline"),
    ("less-than", HTML, 'out += "&lt;";', "out += c;", "Inline"),
    ("greater-than", HTML, 'out += "&gt;";', "out += c;", "Inline"),
    ("attribute-quote", HTML, 'out += "&quot;";', "out += c;", "Inline"),
    ("heading-level", HTML, "'0' + p.level()", "'1'", "Blocks"),
    ("split-anchor", HTML, "this->slots[keep] == next.slots[keep]",
     "this->slots[keep] == next.slots[keep] && (keep || !next.slots[0])", "Inline"),
    ("break-lf", HTML, 'this->fragment += "<br>";', 'this->fragment += "<br>\\n";', "Blocks"),
    ("edge-spaces", HTML, "input.normalizeEdges();", "", "Inline"),
    ("unstaged", HTML, "std::string fragment;",
     "std::string &fragment; explicit HtmlReader(std::string &out) : fragment(out) {}", "Failure",
     ("detail::HtmlReader reader;", "detail::HtmlReader reader(out);"),
     ("out += reader.fragment;", "")),
    ("code-final-lf", PROJECTION, "if (!body.empty() && body[body.size() - 1] != '\\n')",
     "if (false)", "Blocks"),
]

if __name__ == "__main__":
    main(ROOT, "Html", MUTATIONS)
