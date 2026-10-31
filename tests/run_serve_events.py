#!/usr/bin/env python3
"""Validate protocol mappings using a deterministic synchronous protocol peer."""
from pathlib import Path
import sys
import tempfile
sys.dont_write_bytecode = True
from run_serve_regression import Client

with tempfile.TemporaryDirectory(prefix='nchat-serve-events-') as scratch:
    client = Client(Path(sys.argv[1]).resolve(), scratch)
    assert len(client.hello['profiles']) == 2
    assert len(client.hello['profiles'][0]['features']) == 12
    setup = client.until(lambda e: e.get('event') == 'needs_setup')
    assert setup['profile'] == 'Fixture_2'
    client.request({'id': 1, 'op': 'get_chats', 'profile': 'Fixture_2'}, False)
    base = {'profile': 'Fixture_1', 'chat': 'chat'}
    client.request(dict(base, id=2, op='get_chats'))
    assert not any(e.get('event') == 'chats' for e in client.saved), 'event preceded acceptance reply'
    chats = client.until(lambda e: e.get('event') == 'chats')['chats']
    assert chats[0]['name'] == 'Chat name' and chats[0]['unread']
    client.request(dict(base, id=3, op='get_messages', **{'from': '', 'limit': 20}))
    msg = client.until(lambda e: e.get('event') == 'messages')['messages'][0]
    assert msg['file']['id'] == 'download-id' and msg['file']['status'] == 0
    assert msg['quoted'] == {'id': 'q', 'text': 'quote', 'sender': 'quoted sender'}
    assert msg['mentions'] == {'Name': 'user'}
    assert msg['reactions']['sender_emojis'] == {'You': 'old'}
    # Cache reads preserve the requested pagination cursor, even though the
    # cache API itself does not set it on the callback.
    for attempt in range(30):
        client.request(dict(base, id=100 + attempt, op='get_messages', **{'from': 'message', 'limit': 1}))
        page = client.until(lambda e: e.get('event') == 'messages')
        if page['cached']:
            break
    assert page['cached'] and page['from'] == 'message' and page['messages'][0]['id'] == 'older'
    client.request(dict(base, id=4, op='send_message', text='reply', quoted='message'))
    sent = client.until(lambda e: e.get('event') == 'sent')
    assert sent['request'] == 4 and sent['message']['quoted']['sender'] == 'sender'
    client.request(dict(base, id=5, op='edit_message', message='message', text='changed'))
    assert client.until(lambda e: e.get('event') == 'messages')['messages'][0]['edited']
    client.request(dict(base, id=6, op='send_reaction', message='message', emoji='new'))
    assert client.until(lambda e: e.get('event') == 'reactions')['reactions']['emoji_counts'] == {'new': 1}
    client.request(dict(base, id=7, op='download_file', message='message'))
    assert client.until(lambda e: e.get('event') == 'message_file')['file']['path'] == '/tmp/download'
    client.request(dict(base, id=8, op='get_members'))
    assert client.until(lambda e: e.get('event') == 'members')['members'][0]['name'] == 'Member'
    for index, (op, fields, event, key) in enumerate([
        ('send_typing', {'typing': True}, 'typing', 'typing'),
        ('pin_chat', {'on': True}, 'pinned', 'on'),
        ('archive_chat', {'on': True}, 'archived', 'on'),
        ('mark_read', {'message': 'message'}, 'message_status', 'read'),
        ('delete_message', {'message': 'message'}, 'deleted', 'ok'),
    ], 20):
        client.request(dict(base, id=index, op=op, **fields))
        assert client.until(lambda e: e.get('event') == event)[key]
    client.request(dict(base, id=30, op='set_current_chat'))
    client.request(dict(base, id=31, op='get_contacts'))
    assert client.until(lambda e: e.get('event') == 'status')['last_seen'] == 1700000000123
    assert client.until(lambda e: e.get('event') == 'muted')['on']
    assert client.until(lambda e: e.get('event') == 'pinned')['message'] == 'message'
    assert 'message' not in client.until(lambda e: e.get('event') == 'deleted')
    assert client.until(lambda e: e.get('event') == 'exit')['profile'] == 'Fixture_1'
    assert isinstance(client.until(lambda e: e.get('event') == 'other')['type'], int)
    client.request(dict(base, id=32, op='set_current_chat'))
    client.finish('quit')
print('serve event mappings passed')
