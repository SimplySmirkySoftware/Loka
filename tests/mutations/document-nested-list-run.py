#!/usr/bin/env python3
"""Mutate nested item capture, layout, grammar, and each export spelling independently."""
from pathlib import Path

from document_mutations import main

ROOT = Path(__file__).resolve().parents[2]
DOC = ROOT / "common/app/doc"
LIST = DOC / "List.hpp"
FRAMES = DOC / "detail/ContainerWriter.hpp"
MD = DOC / "Markdown.hpp"
HTML = DOC / "Html.hpp"

MUTATIONS = [
    ("short-indent", MD, "indent += this->itemMarker(i - 1).size() + 1;",
     "indent += this->itemMarker(i - 1).size();", "Markdown"),
    ("quote-shell", MD, "if (this->frames[i].kind == QUOTE)",
     "if (this->frames[i].kind == QUOTE || this->frames[i].kind == NESTED_ITEM)", "Markdown"),
    ("close-head-before-children", HTML, "if (!withChildren)", "if (true || !withChildren)", "Html"),
    ("newline-before-child-list", HTML, "this->fragment += body;",
     "this->fragment += body; if (withChildren) this->fragment += '\\n';", "Html"),
    ("no-head-first-rule", FRAMES, "parent.kind == NESTED_ITEM && !parent.needsHead()", "parent.kind == NESTED_ITEM", "Grammar"),
    ("advance-ordinal-again", FRAMES, "static_cast<signed char>(this->ordinal());",
     "static_cast<signed char>(this->ordinal() + (child == NESTED_ITEM ? 1 : 0));", "Ordinal"),
    ("empty-head-own-line", MD, "if (!withChildren || !value.empty())", "if (true)", "SetextHeading"),
    ("omit-bare-marker-blank", MD, "&& firstLineBare)", "&& firstLineBare && false)", "LostOrderedList"),
    # Apply the exception to the first bare line of each child list, including later lists.
    ("always-bare-marker-blank", MD, "&& firstLineBare)", "&& firstLineBare && false)", "LooseSeparator",
     ("size_t indent = 0;",
      "if (level && this->frames[level].kind == NUMBERED_LIST && this->frames[level].ordinal() == 1"
      " && this->frames[level - 1].kind == NESTED_ITEM && !this->frames[level - 1].head.empty()"
      " && value.empty() && !withChildren) this->separator(quotes);\n            size_t indent = 0;")),
    ("admit-refused-value", LIST, "scene::NoteDefinitionCaptureRefusal();\n          return false;",
     "return true;", "CaptureRefusal"),
    ("use-default-row-gap", LIST, "row->gap(0) << Box().width(indent)", "(*row) << Box().width(indent)", "Screen"),
    ("admit-list-under-list", FRAMES, ": this->acceptsBlock() ||",
     ": (parent.isList() && (kind == LIST || kind == NUMBERED_LIST)) || this->acceptsBlock() ||",
     "Grammar"),
]

if __name__ == "__main__":
    main(ROOT, "NestedList", MUTATIONS)
