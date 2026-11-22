#!/usr/bin/env python3
"""Run the image dispatch, projection, grammar and spelling mutations."""
from pathlib import Path

from document_mutations import main

ROOT = Path(__file__).resolve().parents[2]
DOC = ROOT / "common/app/doc"
IMAGE = DOC / "Image.hpp"
FRAMES = DOC / "detail/ContainerWriter.hpp"
MUTATIONS = [
    ("skip-image-dispatch", IMAGE, "class ImageNode : public BlockNode<ImageProps>",
     "class ImageNode : public scene::ComponentNodeWithProps<ImageProps>", "Markdown",
     (": BlockNode<ImageProps>(props)", ": scene::ComponentNodeWithProps<ImageProps>(props)")),
    ("retain-old-alt", IMAGE, "doc::Lower(props).applyPropsToNode(leaf);", "(void)leaf;", "Screen"),
    ("allow-list-image", FRAMES, "!this->accepts(false) || !CollectDocumentLine(p.source",
     "!CollectDocumentLine(p.source", "Html"),
    ("body-escape-alt", DOC / "Html.hpp", "AppendHtmlByte(this->fragment, alt[i], true);",
     "AppendHtmlByte(this->fragment, alt[i], false);", "Html"),
    ("literal-destination", DOC / "Markdown.hpp", "CloseMarkdownLink(body, source.c_str());",
     'body += "](<" + source + ">)";', "Markdown"),
    ("allow-empty-source", FRAMES, " || source.empty()", "", "Markdown"),
]

if __name__ == "__main__":
    main(ROOT, "Image", MUTATIONS)
