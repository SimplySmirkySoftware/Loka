#!/usr/bin/env python3
"""Document vocabulary mutations; a build failure never counts as a kill."""
import argparse
from pathlib import Path
import signal
import subprocess

ROOT = Path(__file__).resolve().parents[2]
MUTATIONS = [
    ('a', 'common/app/doc/Heading.hpp', 'styles[props.level() - 1] + props.content',
     '(void(styles), props.content)', 'testDocumentVocabularyLowering'),
    ('b', 'common/app/doc/Paragraph.hpp', 'doc::Lower(props).applyPropsToNode(leaf);',
     '(void)leaf;', 'testDocumentVocabularyRetainedApply'),
    ('c', 'common/app/doc/Heading.hpp', '.activatedLink(props.activatedLink_)',
     '.activatedLink(scene::NodeState<LinkTarget>())', 'testDocumentVocabularyActivationAndLayout'),
    ('d', 'common/app/doc/Heading.hpp', 'return level < 1 ? 1 : (level > 6 ? 6 : level);',
     'return level;', 'release'),
    ('e', 'common/app/doc/Heading.hpp', 'FontSize<12>() + Bold',
     'FontSize<10>() + Bold', 'testDocumentVocabularyLowering'),
    ('f', 'common/app/doc/Heading.hpp', 'doc::Lower(props).applyPropsToNode(leaf);',
     '{ const AttributedTextDefinitionWithAttr lowered = doc::Lower(props); '
     'if (lowered.props.text_->get().valid()) lowered.applyPropsToNode(leaf); }',
     'testDocumentVocabularyFailure'),
    ('g', 'common/app/nodes/controls/Card.hpp',
     'return content && content->asCardBandNode() ? 0 : content;',
     'return content && content->asCardBandNode() ? 0 : column;', 'testDocumentVocabularyLowering'),
    ('h', 'common/app/doc/CodeBlock.hpp',
     'scene::NoteDefinitionCaptureRefusal();\n            return;', 'scene::NoteDefinitionCaptureRefusal();',
     'testDocumentVocabularyInitialRefusalAndFor'),
    ('i', 'common/app/doc/Heading.hpp',
     'if (AttributedTextNode *leaf = doc::detail::ProjectionLeaf(node->childrenHead()))\n'
     '            doc::Lower(props).applyPropsToNode(leaf);',
     'const AttributedTextDefinitionWithAttr lowered = doc::Lower(props);\n'
     '          if (AttributedTextNode *leaf = doc::detail::ProjectionLeaf(node->childrenHead()))\n'
     '            lowered.applyPropsToNode(leaf);', 'testDocumentVocabularyChildStates'),
]

def run(command, log):
    result = subprocess.run(command, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    log.write_bytes(result.stdout)
    return result.returncode


def mutation(entry):
    letter, filename, old, new, test = entry
    path = ROOT / filename
    original = path.read_bytes()
    source = original.decode()
    assert source.count(old) == (3 if letter == 'e' else 1), 'anchor mismatch: ' + letter
    logs = ROOT / 'build/document-vocabulary-mutations'
    logs.mkdir(parents=True, exist_ok=True)
    target = 'documentLevelsRelease' if test == 'release' else 'LokaContractTests'
    build = ['cmake', '--build', 'build/Testing', '--target', target, '-j', '4']
    command = (['build/Testing/tests/compile/documentLevelsRelease'] if test == 'release'
               else ['build/Testing/LokaContractTests', test])

    def check(label, green):
        if run(build, logs / f'{letter}-{label}-build.log'):
            raise RuntimeError(f'{letter} {label}: build failure is not a kill')
        result = run(command, logs / f'{letter}-{label}.log')
        if (result == 0) != green:
            raise RuntimeError(f'{letter} {label}: unexpected exit {result}')

    check('baseline', True)
    try:
        path.write_text(source.replace(old, new))
        check('mutant', False)
        print(f'killed ({letter})', flush=True)
    finally:
        path.write_bytes(original)
        check('restored', True)
        assert path.read_bytes() == original


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mutation', nargs='?', default='all', choices=['all'] + [e[0] for e in MUTATIONS])
    args = parser.parse_args()
    def terminate(signum, frame):
        raise SystemExit(128 + signum)
    signal.signal(signal.SIGTERM, terminate)
    for entry in MUTATIONS:
        if args.mutation in ('all', entry[0]):
            mutation(entry)

if __name__ == '__main__':
    main()
