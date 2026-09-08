"""Static checks for the dedicated campaign network protocol.

Checks channel direction, missing receive branches, inconsistent payload widths,
and duplicate numeric subtype IDs.  Python 2.7 compatible for the module build.
"""
from __future__ import print_function

import ast
import glob
import re
import sys


SERVER_CHANNEL = 'multiplayer_event_multiplayer_campaign_server_events'
CLIENT_CHANNEL = 'multiplayer_event_multiplayer_campaign_client_events'
SERVER_HANDLER = 'multiplayer_campaign_server_events'
CLIENT_HANDLER = 'multiplayer_campaign_client_events'


def name(node):
    if isinstance(node, ast.Name):
        return node.id
    # Module-system condition modifiers are written as this_or_next|eq and
    # parse as a Python bitwise-or expression; the operation is on the right.
    if isinstance(node, ast.BinOp) and isinstance(node.op, ast.BitOr):
        return name(node.right)
    return None


def string(node):
    return node.s if isinstance(node, ast.Str) else None


def find_script(tree, script_name):
    for node in ast.walk(tree):
        if isinstance(node, (ast.Tuple, ast.List)) and len(node.elts) >= 2:
            if string(node.elts[0]) == script_name:
                return node.elts[1]
    return None


def handler_events(tree, script_name):
    body = find_script(tree, script_name)
    result = set()
    if body is None:
        return result
    for node in ast.walk(body):
        if not isinstance(node, (ast.Tuple, ast.List)) or len(node.elts) < 3:
            continue
        if name(node.elts[0]) != 'eq' or string(node.elts[1]) != ':event_type':
            continue
        event = name(node.elts[2])
        if event:
            result.add(event)
    return result


def load_subtypes():
    server = {}
    client = {}
    pattern = re.compile(r'^(multiplayer_event_multiplayer_campaign_[A-Za-z0-9_]+)\s*=\s*(\d+)')
    with open('header_common.py', 'rb') as source:
        for lineno, raw in enumerate(source, 1):
            match = pattern.match(raw.decode('latin1'))
            if not match:
                continue
            event, value = match.group(1), int(match.group(2))
            if '_server_event_' in event:
                server[event] = (value, lineno)
            elif event not in (SERVER_CHANNEL, CLIENT_CHANNEL):
                client[event] = (value, lineno)
    return server, client


def duplicate_errors(definitions, label):
    by_value = {}
    errors = []
    for event, data in definitions.items():
        by_value.setdefault(data[0], []).append((event, data[1]))
    for value, entries in sorted(by_value.items()):
        if len(entries) > 1:
            errors.append('%s subtype %d is duplicated: %s' %
                          (label, value, ', '.join(x[0] for x in entries)))
    return errors


def collect_sends():
    sends = {SERVER_CHANNEL: {}, CLIENT_CHANNEL: {}}
    errors = []
    dynamic = 0
    pattern = re.compile(r'^multiplayer_send_(\d+)_int_to_(server|player)$')
    one_pattern = re.compile(r'^multiplayer_send_int_to_(server|player)$')
    for path in glob.glob('module_*.py'):
        with open(path, 'rb') as source:
            tree = ast.parse(source.read(), filename=path)
        for node in ast.walk(tree):
            if not isinstance(node, (ast.Tuple, ast.List)) or not node.elts:
                continue
            operation = name(node.elts[0])
            match = pattern.match(operation or '')
            one = one_pattern.match(operation or '')
            if not match and not one:
                continue
            direction = (match or one).group(2 if match else 1)
            width = int(match.group(1)) if match else 1
            channel_index = 2 if direction == 'player' else 1
            subtype_index = channel_index + 1
            if len(node.elts) <= subtype_index:
                continue
            channel = name(node.elts[channel_index])
            if channel not in sends:
                continue
            subtype = name(node.elts[subtype_index])
            if not subtype:
                dynamic += 1
                continue
            payload = width - 1
            sends[channel].setdefault(subtype, set()).add(payload)
    for channel, events in sends.items():
        for subtype, widths in events.items():
            if len(widths) > 1:
                errors.append('%s has inconsistent payload widths %s' %
                              (subtype, sorted(widths)))
    return sends, errors, dynamic


def main():
    errors = []
    server_defs, client_defs = load_subtypes()
    errors.extend(duplicate_errors(server_defs, 'server'))
    errors.extend(duplicate_errors(client_defs, 'client'))
    sends, send_errors, dynamic = collect_sends()
    errors.extend(send_errors)

    with open('module_coop_scripts.py', 'rb') as source:
        coop_tree = ast.parse(source.read(), filename='module_coop_scripts.py')
    server_receives = handler_events(coop_tree, SERVER_HANDLER)
    client_receives = handler_events(coop_tree, CLIENT_HANDLER)

    for subtype in sorted(sends[SERVER_CHANNEL]):
        if subtype not in server_receives:
            errors.append('server->client %s has no client receive branch' % subtype)
    for subtype in sorted(sends[CLIENT_CHANNEL]):
        if subtype not in client_receives:
            errors.append('client->server %s has no server receive branch' % subtype)

    for error in errors:
        print(error)
    checked = sum(len(events) for events in sends.values())
    print('Checked %d campaign subtypes; %d protocol errors; %d dynamic send skipped.' %
          (checked, len(errors), dynamic))
    return bool(errors)


if __name__ == '__main__':
    sys.exit(main())
