#!/usr/bin/env python3
"""Run the image dispatch, projection, grammar and spelling mutations."""
from pathlib import Path

from document_mutations import main

ROOT = Path(__file__).resolve().parents[2]
DOC = ROOT / "common/app/doc"
IMAGE = DOC / "Image.hpp"
TAG = DOC / "detail/HtmlImage.hpp"
FRAMES = DOC / "detail/ContainerWriter.hpp"
MUTATIONS = [
    ("skip-image-dispatch", IMAGE, "class ImageNode : public BlockNode<ImageProps>",
     "class ImageNode : public scene::ComponentNodeWithProps<ImageProps>", "Markdown",
     (": BlockNode<ImageProps>(props)", ": scene::ComponentNodeWithProps<ImageProps>(props)")),
    ("retain-old-alt", IMAGE, "doc::Lower(props).applyPropsToNode(leaf);", "(void)leaf;", "Screen"),
    ("allow-list-image", FRAMES, "!this->accepts(false) || !CollectDocumentLine(p.source",
     "!CollectDocumentLine(p.source", "Html"),
    ("body-escape-alt", TAG, "AppendHtmlByte(out, alt[i], true);",
     "AppendHtmlByte(out, alt[i], false);", "Html"),
    ("literal-destination", DOC / "Markdown.hpp", "CloseMarkdownLink(body, source.c_str());",
     'body += "](<" + source + ">)";', "Markdown"),
    ("html-omit-size", DOC / "Html.hpp", "if (!AppendHtmlImage(this->fragment, source, alt, size))",
     "(void)size; if (!AppendHtmlImage(this->fragment, source, alt, 0))", "Html"),
    ("markdown-omit-size", DOC / "Markdown.hpp", "AppendHtmlImage(tag, source, alt, size)",
     "AppendHtmlImage(tag, source, alt, 0)", "Markdown"),
    ("allow-zero-size", TAG, "if (size && (!size->width() || !size->height()))", "if (false)", "Render"),
    ("discard-hook-result", FRAMES, "return static_cast<Writer &>(*this).image(source, alt, p.size.get());",
     "static_cast<Writer &>(*this).image(source, alt, p.size.get()); return true;", "Html"),
    ("ignore-size-order", IMAGE, "label ? label < 0 : this->size < other.size", "label < 0", "RetainedSize"),
    ("allow-empty-source", FRAMES, " || source.empty()", "", "Markdown"),
]

if __name__ == "__main__":
    main(ROOT, "Image", MUTATIONS)
