#!/usr/bin/env python3
"""Mutate the numbering, export grammar, marker spelling and retained identity independently."""
from pathlib import Path
from document_mutations import main

ROOT = Path(__file__).resolve().parents[2]
DOC = ROOT / "common/app/doc"
LIST = DOC / "List.hpp"
FRAMES = DOC / "detail/ContainerWriter.hpp"
MD = DOC / "Markdown.hpp"
HTML = DOC / "Html.hpp"
MUTATIONS = [
    ("constant-ordinal", LIST, "item.props, static_cast<unsigned>(this->childrenCount()) + 1", "item.props, 1", "Screen"),
    ("allow-gap", FRAMES, "&& ordinal == this->ordinal() + 1", "", "Markdown"),
    ("allow-unordered-item", FRAMES, ": list.kind != LIST)", ": !list.isList())", "Html"),
    ("unordered-tag", HTML, '"<ol>\\n"', '"<ul>\\n"', "Html"),
    ("fixed-indent", MD, "marker.size() + 1", "marker.size() > 1 ? 3 : 2", "Markdown"),
    ("repeat-delimiter", FRAMES, "this->previous == '.' ? ')' : '.'", "'.'", "Markdown"),
    ("drop-empty", FRAMES, "numbered || !body.empty()", "!body.empty()", "Markdown"),
    ("drop-cap", LIST, "this->childrenCount() >= NumberedItemProps::MaximumOrdinal", "false", "Cap"),
    ("ignore-ordinal-order", LIST, "return this->ordinal_ < other.ordinal_;", "return false;", "Retention"),
    ("ignore-ordinal-compatibility", LIST, "return node->props.ordinal() == props.ordinal();",
     "(void)node; (void)props; return true;", "Retention"),
]

if __name__ == "__main__":
    main(ROOT, "NumberedList", MUTATIONS)
