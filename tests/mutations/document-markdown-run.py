#!/usr/bin/env python3
"""Run the document Markdown mutations independently. Build failures are not kills."""
from pathlib import Path
from document_mutations import main

ROOT = Path(__file__).resolve().parents[2]
MARKDOWN = ROOT / "common/app/doc/Markdown.hpp"
PROJECTION = MARKDOWN.parent / "detail/Projection.hpp"
WALK = MARKDOWN.parent / "BlockWalk.hpp"
FRAMES = MARKDOWN.parent / "detail/ContainerWriter.hpp"
MUTATIONS = [
    ("drop-marker", MARKDOWN, "this->fragment += this->frames[this->depth - 1].previous;", "", "Containers"),
    ("drop-prefix-level", MARKDOWN, "i < quotes;", "i + 1 < quotes;", "Containers"),
    ("ignore-sibling-marker", FRAMES, "(parent.previous == '-' ? '*' : '-')", "'-'", "Containers"),
    ("skip-leave", WALK, "return reader.leave(props) ? scene::DOCUMENT_BLOCK_READ : scene::DOCUMENT_BLOCK_REFUSED;",
     "return scene::DOCUMENT_BLOCK_READ;", "Containers"),
    ("prune", WALK, "case scene::DOCUMENT_BLOCK_READ:\n            return true;",
     "case scene::DOCUMENT_BLOCK_READ: break;", "Walk"),
    ("parked", WALK, "scene::INestable *nest = node->asNestable();",
     "for (unsigned i = 0; scene::Node *branch = node->retainedLifecycleBranch(i); ++i)\n"
     "            if (!WalkDocumentBlocks(branch, reader)) return false;\n"
     "          scene::INestable *nest = node->asNestable();", "Walk"),
    ("escape-heading", MARKDOWN, "[]<&#~", "[]<&~", "PhysicalLines"),
    ("unsafe-stars", MARKDOWN, "if (stars)", "if (stars || marks)", "Inline"),
    ("ignore-flanking", MARKDOWN, "if (stars)", "if (stars || (!touching && !literal))", "Inline"),
    ("partial-body", PROJECTION, "!CollectDocumentText(seg.text, this->bytes)",
     "(CollectDocumentText(seg.text, this->bytes), false)", "Failure"),
    ("partial-destination", PROJECTION, "!CollectDocumentLine(seg.link.text(), target)",
     "(CollectDocumentLine(seg.link.text(), target), false)", "Failure"),
    ("edge-emphasis", PROJECTION, "AppendRun(projected, plain, run.first, ink.first);", "AppendRun(projected, run.style, run.first, ink.first);", "Inline"),
    ("short-fence", MARKDOWN, "Backticks(body, 0, body.size(), 3)", 'std::string(3, \'`\')', "Blocks"),
    ("partial-commit", MARKDOWN, "if (!root || !detail::WalkDocumentBlocks(root, reader))\n          return false;",
     "if (!root || !detail::WalkDocumentBlocks(root, reader)) { out += reader.fragment; return false; }", "Failure"),
    ("split-run", PROJECTION, "if (runs.empty() || !(runs.back().style == style))",
     "if (first < end)", "Inline"),
    ("split-link", PROJECTION, "if (target != this->targets.c_str() + link)", "if (!target.empty())", "Inline"),
    ("physical-digits", MARKDOWN, "(digits && (v == '.' || v == ')'))", "(digits && v == ')')", "PhysicalLines"),
    ("image", MARKDOWN, "(v == '!' && beforeLink)", "(v == '?' && beforeLink)", "PhysicalLines"),
    ("detached", WALK, "if (node->lifecycleFact() != scene::NODE_FACT_ATTACHED)",
     "if (!node)", "Seats"),
    ("code-padding", MARKDOWN, "if (i == end && !onlySpaces)\n                return false;",
     "if (i == end) break;", "Failure"),
    ("bare-strip-space", MARKDOWN, "&& ExtraStripSpace(v))", "&& v == 0)", "Failure"),
    ("leading-signature", MARKDOWN, "else if (v == 0xfeff && pos == first)", "else if (v == 0xfeff && pos == end)", "PhysicalLines"),
]


if __name__ == "__main__":
    main(ROOT, "Markdown", MUTATIONS)
