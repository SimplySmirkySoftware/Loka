#!/usr/bin/env python3
"""Run each image mutation against a built testing preset, restoring after each kill."""
import argparse
from pathlib import Path

from document_mutations import mutation


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path, help="repository root")
    root = parser.parse_args().root.resolve()
    doc = root / "common/app/doc"
    image = doc / "Image.hpp"
    frames = doc / "detail/ContainerWriter.hpp"
    edits = [
        ("skip-image-dispatch", image, "class ImageNode : public BlockNode<ImageProps>",
         "class ImageNode : public scene::ComponentNodeWithProps<ImageProps>", "Markdown",
         (": BlockNode<ImageProps>(props)", ": scene::ComponentNodeWithProps<ImageProps>(props)")),
        ("retain-old-alt", image, "doc::Lower(props).applyPropsToNode(leaf);", "(void)leaf;", "Screen"),
        ("allow-list-image", frames, "!this->accepts(false) || !CollectDocumentLine(p.source",
         "!CollectDocumentLine(p.source", "Html"),
        ("body-escape-alt", doc / "Html.hpp", "AppendHtmlByte(this->fragment, alt[i], true);",
         "AppendHtmlByte(this->fragment, alt[i], false);", "Html"),
        ("literal-destination", doc / "Markdown.hpp", "CloseMarkdownLink(body, source.c_str());",
         'body += "](<" + source + ">)";', "Markdown"),
        ("allow-empty-source", frames, " || source.empty()", "", "Markdown"),
    ]
    for edit in edits:
        mutation(root, "Image", edit)


if __name__ == "__main__":
    main()
