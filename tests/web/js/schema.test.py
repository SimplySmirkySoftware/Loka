#!/usr/bin/env python3
"""Canonical schema identity, independent of JSON formatting and object order."""
import copy
import importlib.util
import json
from pathlib import Path
ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location('generator', ROOT / 'tools/ci/gen_style_vocab.py')
g = importlib.util.module_from_spec(spec)
spec.loader.exec_module(g)
vocab = json.loads((ROOT / 'tools/style-vocab.json').read_text())
fingerprint = lambda v: g.fingerprint(g.resolved_schema(v))
original = fingerprint(vocab)
def reverse_objects(value):
    if isinstance(value, dict):
        return {k: reverse_objects(v) for k, v in reversed(list(value.items()))}
    if isinstance(value, list):
        return [reverse_objects(v) for v in value]
    return value
assert fingerprint(json.loads(json.dumps(reverse_objects(vocab), indent=4))) == original
remap = copy.deepcopy(vocab)
remap['weights']['NORMAL'], remap['weights']['BOLD'] = 1, 0
assert fingerprint(remap) != original
reorder = copy.deepcopy(vocab)
fields = reorder['wire']['textDescriptor']['fields']
fields[0], fields[1] = fields[1], fields[0]
assert fingerprint(reorder) != original
assert len(g.resolved_schema(reorder)['fields']) == len(g.resolved_schema(vocab)['fields'])
assert g.fingerprint({}) == 1415952421  # Independent FNV-1a vector for UTF-8 '{}'.
print('schema: formatting/key-order stable; enum-remap/field-reorder sensitive; FNV vector passed')
