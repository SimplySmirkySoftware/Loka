#!/usr/bin/env python3
"""Check item code placement, grammar and byte-level spacing independently."""
from pathlib import Path

from document_mutations import main

ROOT = Path(__file__).resolve().parents[2]
DOC = ROOT / "common/app/doc"
MD = DOC / "Markdown.hpp"
HTML = DOC / "Html.hpp"
FRAMES = DOC / "detail/ContainerWriter.hpp"
MUTATIONS = [
    ("omit-current-indent", MD, "i <= level; ++i)\n              if (this->frames[i].kind == NESTED_ITEM)",
     "i < level; ++i)\n              if (this->frames[i].kind == NESTED_ITEM)", "Indent"),
    ("indent-empty-line", MD, "if (!blank)\n                  this->fragment.append(indent, ' ');",
     "this->fragment.append(indent, ' ');", "Payload"),
    ("end-empty-head-line", MD, "if (!withChildren || !value.empty())", "if (true)", "Indent"),
    ("newline-after-pre", HTML, 'this->fragment += "</code></pre>";\n            if (this->parentNeedsNewline())',
     'this->fragment += "</code></pre>";', "Order"),
    ("allow-item-paragraph", MD, "!this->acceptsBlock() || !WriteInline(p.content, false, body)",
     "!this->acceptsListOrCodeBlock() || !WriteInline(p.content, false, body)", "Grammar"),
    ("allow-code-before-head", FRAMES, "frame.kind == NESTED_ITEM && !frame.needsHead()",
     "frame.kind == NESTED_ITEM", "Grammar"),
    ("bare-code", FRAMES, "this->beginLeaf(this->depth, false);", "this->beginLeaf(this->depth, true);", "Order"),
]

if __name__ == "__main__":
    main(ROOT, "ItemBlocks", MUTATIONS)
