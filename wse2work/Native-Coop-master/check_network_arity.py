"""Validate fixed-width multiplayer send operations without importing the module."""
from __future__ import print_function
import ast
import glob
import re
import sys

pattern = re.compile(r'^multiplayer_send_(?:(\d+)_int|int)_to_(server|player)$')
errors = []
checked = 0
for path in glob.glob('module_*.py'):
    with open(path, 'rb') as source:
        tree = ast.parse(source.read(), filename=path)
    for node in ast.walk(tree):
        if not isinstance(node, (ast.Tuple, ast.List)) or not node.elts:
            continue
        op = node.elts[0]
        if not isinstance(op, ast.Name):
            continue
        match = pattern.match(op.id)
        if not match:
            continue
        checked += 1
        expected = int(match.group(1) or 1) + 1 + (match.group(2) == 'player')
        actual = len(node.elts) - 1
        if actual != expected:
            errors.append('%s:%s: %s expects %s arguments, got %s' %
                          (path, node.lineno, op.id, expected, actual))
for error in errors:
    print(error)
print('Checked %d network sends; %d arity errors.' % (checked, len(errors)))
sys.exit(bool(errors))
