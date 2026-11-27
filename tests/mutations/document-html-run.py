#!/usr/bin/env python3
"""Run the HTML spelling, projection and failure-atomicity mutations."""
from pathlib import Path
from document_mutations import main

ROOT = Path(__file__).resolve().parents[2]
HTML = ROOT / "common/app/doc/Html.hpp"
PROJECTION = HTML.parent / "detail/Projection.hpp"
ESCAPE = HTML.parent / "detail/HtmlImage.hpp"
FRAMES = HTML.parent / "detail/ContainerWriter.hpp"
MUTATIONS = [
    ("empty-container", FRAMES, "if (frame.emitted())",
     "if (!frame.emitted()) static_cast<Writer &>(*this).open(frame.kind);\n            if (true)", "Containers"),
    ("item-outside-list", FRAMES, "return item ? parent.kind == LIST : !parent.isList();",
     "return item || !parent.isList();", "Containers"),
    # Move the quote check. Valid lists still pass, exposing the illegal List(Quote) admission.
    ("validate-after-push", FRAMES, "!this->accepts(false) || this->depth == MaxDepth",
     "(kind == LIST && !this->accepts(false)) || this->depth == MaxDepth", "Containers",
     ("frame.reset(kind);", "frame.reset(kind); if (kind == QUOTE && !this->accepts(false)) return false;")),
    # Enlarge the mutant's backing array so accepting depth 17 tests grammar without corrupting memory.
    ("drop-depth-bound", FRAMES, " || this->depth == MaxDepth", "", "Containers",
     ("Frame frames[MaxDepth + 1]", "Frame frames[MaxDepth + 2]")),
    ("ampersand", ESCAPE, 'out += "&amp;";', "out += c;", "Inline"),
    ("less-than", ESCAPE, 'out += "&lt;";', "out += c;", "Inline"),
    ("greater-than", ESCAPE, 'out += "&gt;";', "out += c;", "Inline"),
    ("attribute-quote", ESCAPE, 'out += "&quot;";', "out += c;", "Inline"),
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
