#!/usr/bin/env python3
"""Exercise the real --serve executable with isolated Dummy profiles, no TTY."""
import fcntl
import json
import os
from pathlib import Path
import queue
import signal
import subprocess
import sys
import tempfile
import threading
import time


class Client:
    def __init__(self, binary, conf):
        self.process = subprocess.Popen([str(binary), '--serve', '-d', str(conf)],
                                        stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        stderr=subprocess.PIPE)
        self.lines = queue.Queue()
        self.saved = []
        def read():
            for line in self.process.stdout:
                try:
                    self.lines.put(json.loads(line))
                except Exception as error:
                    self.lines.put(error)
            self.lines.put(EOFError('server closed stdout'))
        self.thread = threading.Thread(target=read, daemon=True)
        self.thread.start()
        self.hello = self.next()
        assert self.hello['event'] == 'hello', self.hello

    def next(self):
        value = self.lines.get(timeout=10)
        if isinstance(value, Exception):
            raise value
        return value

    def until(self, predicate):
        for index, value in enumerate(self.saved):
            if predicate(value):
                return self.saved.pop(index)
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            value = self.next()
            if predicate(value):
                return value
            self.saved.append(value)
        raise AssertionError('missing reply/event')

    def send(self, value):
        self.process.stdin.write((json.dumps(value, ensure_ascii=False) + '\n').encode())
        self.process.stdin.flush()

    def request(self, value, ok=True):
        self.send(value)
        reply = self.until(lambda event: event.get('id') == value['id'])
        assert reply['ok'] is ok, reply
        return reply

    def finish(self, how='eof'):
        if how == 'quit':
            self.request({'id': 999, 'op': 'quit'})
        elif how == 'eof':
            self.process.stdin.close()
        else:
            self.process.send_signal(how)
        assert self.process.wait(timeout=10) == 0
        assert self.process.stderr.read() == b''
        if not self.process.stdin.closed:
            self.process.stdin.close()
        self.thread.join(timeout=2)
        self.process.stdout.close()
        self.process.stderr.close()


def main():
    binary = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix='nchat-serve-') as scratch:
        conf = Path(scratch)
        profile = conf / 'profiles' / 'Dummy_1'
        profile.mkdir(parents=True)
        # Existing confdirs have a version marker, as regular setup creates.
        (conf / 'version').write_text('31')
        (conf / 'profiles' / 'version').write_text('31')
        client = Client(binary, conf)
        hello = client.hello
        assert hello['version'] == 1 and hello['nchat']
        assert hello['skipped'] == []
        assert len(hello['profiles']) == 1
        info = hello['profiles'][0]
        assert info['profile'] == 'Dummy_1' and info['protocol'] == 'Dummy'
        assert 'send_acknowledgement' in info['features']
        base = {'profile': 'Dummy_1'}
        client.request(dict(base, id=1, op='get_chats'))
        chats = client.until(lambda e: e.get('event') == 'chats')['chats']
        assert chats and isinstance(chats, list)
        chat = chats[0]['id']
        client.request(dict(base, id=2, op='get_messages', chat=chat, **{'from': '', 'limit': 50}))
        event = client.until(lambda e: e.get('event') == 'messages')
        messages = event['messages']
        assert event['chat'] == chat and messages
        assert 'quoted' in messages[0] and 'reactions' in messages[0]
        text = 'Hello 🌍\n"quoted" \\ line\t\x01'
        client.request(dict(base, id=3, op='send_message', chat=chat, text=text))
        sent = client.until(lambda e: e.get('event') == 'sent')
        assert sent['ok'] and sent['request'] == 3 and sent['message']['text'] == text
        # Wait for async cache persistence, then history must come from sqlite.
        for attempt in range(20):
            client.request(dict(base, id=100 + attempt, op='get_messages', chat=chat, **{'from': '', 'limit': 1}))
            event = client.until(lambda e: e.get('event') == 'messages')
            if event['cached']:
                break
            time.sleep(.02)
        assert event['cached']
        client.request(dict(base, id=4, op='get_message', chat=chat, message=messages[0]['id']))
        assert client.until(lambda e: e.get('event') == 'messages')['messages'][0]['id'] == messages[0]['id']
        client.process.stdin.write(b'not json\n')
        client.process.stdin.flush()
        bad = client.until(lambda e: 'id' in e and e['id'] is None)
        assert not bad['ok']
        for request in [dict(base, id=5, op='unknown'),
                        {'id': 6, 'op': 'get_chats', 'profile': 'missing'},
                        dict(base, id=7, op='get_messages', chat=chat),
                        dict(base, id=8, op='send_typing', chat=chat, typing=1),
                        dict(base, id=9, op='get_messages', chat=chat, **{'from': '', 'limit': -1}),
                        {'id': -1, 'op': 'quit'}, {'id': 'bad', 'op': 'quit'}]:
            assert client.request(request, False)['error']
        for invalid in [b'[' * 2000 + b']' * 2000 + b'\n', b'[]\n', b'{"op":"get_chats"}\n', b'{"id":50,"op":"get_chats","profile":"\xff"}\n',
                        b'x' * (1024 * 1024 + 1) + b'\n']:
            client.process.stdin.write(invalid)
            client.process.stdin.flush()
            assert not client.until(lambda e: 'id' in e and e['id'] is None)['ok']
        client.request(dict(base, id=10, op='get_chats'))
        assert client.until(lambda e: e.get('event') == 'chats')['chats']
        # Same confdir held by another frontend: hello reports its profile lock.
        second = Client(binary, conf)
        assert second.hello['profiles'] == []
        assert {'profile': 'Dummy_1', 'why': 'locked'} in second.hello['skipped']
        second.finish('quit')
        client.finish()
        for how in ['quit', signal.SIGTERM, signal.SIGINT]:
            restarted = Client(binary, conf)
            assert restarted.hello['profiles'][0]['profile'] == 'Dummy_1'
            restarted.finish(how)
        fd = os.open(profile, os.O_RDONLY)
        try:
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            locked = Client(binary, conf)
            assert locked.hello['profiles'] == []
            assert locked.hello['skipped'] == [{'profile': 'Dummy_1', 'why': 'locked'}]
            locked.finish()
        finally:
            os.close(fd)
        empty = conf / 'empty'
        no_profiles = Client(binary, empty)
        assert no_profiles.hello['profiles'] == []
        no_profiles.finish()
        assert not (conf / 'ui.conf').exists()
        assert not (conf / 'key.conf').exists()


if __name__ == '__main__':
    main()
    print('serve regression passed')
