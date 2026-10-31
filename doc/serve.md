# Headless stdio server (version 1)

Run `nchat --serve [-d CONFDIR]`. Configure accounts first with `nchat --setup`
in a terminal. Serve accepts only the confdir and verbose logging options in
addition to `--serve`; combining it with setup, export, keydump, developer mode,
help or version is an error. The mode is built by default, using the already
vendored RapidJSON implementation, with no additional dependency.

The terminal frontend and its configuration are not initialized. Protocol stdin
and stdout use private descriptors; ordinary library stdout is discarded and
ordinary library stdin sees EOF. Incidental library stderr is also suppressed
(the protocols already log their warnings to the log file). This prevents a library diagnostic or unexpected
prompt from corrupting the client stream. Runtime authentication is explicitly
declined before protocols display prompts or QR codes. Logs use the normal nchat
log file; log dumping to the console is disabled. stderr is reserved for fatal
startup errors. No account setup, sockets, or multiple clients are supported.

## Framing, replies, and shutdown

Both directions are UTF-8 JSON, one object per newline. Strings use JSON escaping,
so embedded newlines do not split a record. Requests must contain `id`, an unsigned
64-bit integer (0 through 18446744073709551615), and `op`, a string. Clients should
use distinct IDs. IDs above 2^53-1 need an integer-aware JSON parser in JavaScript.
Extra fields are ignored. JSON nesting is limited to 64 levels. Required string fields may be empty where meaningful;
NUL characters in fields are rejected. Boolean fields require JSON booleans.

Every input record receives exactly one reply:

```json
{"id":7,"ok":true}
{"id":8,"ok":false,"error":"no profile Telegram_+49999"}
```

`ok` means accepted by the local protocol/cache, not delivered by the service.
Validation failures identify the unknown operation/profile or missing/invalid
field. Malformed JSON, non-object input, or a missing ID produces `id: null`.
An invalid ID, if present in otherwise valid JSON, is echoed in the error reply.
The next valid record still works. Input records are limited to 1 MiB; oversized
records are discarded through the next newline and receive a JSON error with
`id: null`. A final unterminated record is processed on EOF.

Notifications are events and have `event` rather than `id`. All profile events
include `profile`. Each callback is queued safely; the server processes events
between requests. A request's reply precedes events caused by that request,
including synchronous protocol callbacks. Unrelated events may precede any reply.
One mutex protects complete writes. Consume stdout continuously to avoid pipe
backpressure. A closed stdout is treated as a disconnect, without SIGPIPE exit.

EOF, `quit`, SIGINT, SIGTERM and SIGHUP stop the loop, join login, log out and close
loaded profiles, stop the cache, and release directory locks. Exit status is 0.
Events already queued during logout may follow a quit reply. The `exit` event from
one profile is advisory: it does not terminate other profiles or the server.

## Startup and locking

The first output is `hello`, after profiles have loaded and **before Login is
called**:

```json
{"event":"hello","version":1,"nchat":"5.20.15","profiles":[{"profile":"Dummy_1","protocol":"Dummy","name":"","self":"Stanley_0","features":["auto_get_contacts_on_login","send_acknowledgement"]}],"skipped":[]}
```

`name` and `self` are the protocol's values at load time and may be empty. The
profile key is the directory name. Feature names are the complete set of enabled
`ProtocolFeature` flags, without `Feature`, in snake_case:

- `auto_get_chats_on_login`, `typing_timeout`
- `edit_messages_within_two_days`, `edit_messages_within_fifteen_mins`
- `limited_reactions`, `mark_read_every_view`, `auto_get_contacts_on_login`
- `pin_chat`, `archive_chat`, `pin_message`, `presence_requires_online`
- `send_acknowledgement`

Profiles use the normal `ScopedDirLock`; a locked profile appears in `skipped` as
`{"profile":"Dummy_1","why":"locked"}`. Other skip reasons are `invalid name`,
`unsupported or load failed`, and `multiple profiles disabled` for a build without
multiprotocol support. No profiles is a valid server state.

nchat also locks the whole configuration directory. When another frontend owns
that lock, serve reports the profile locks in `hello` without touching its log,
config, temporary directory, or cache. Unlocked profiles in that confdir have the
skip reason `config directory locked`; the server loads none until restarted
after the other frontend exits. This preserves the existing exclusive confdir
ownership while still exposing locked profiles to clients. Locks are never stolen.
Invalid existing configuration versions remain fatal startup errors, as in normal
nchat. Use a confdir prepared by setup; do not construct version files by hand.

## Requests

Every operation except `quit` requires `profile`. All operations except
`get_chats`, `get_contacts`, and `quit` also require `chat`.

| op | Additional fields | Request class |
| --- | --- | --- |
| `get_chats` | — | GetChatsRequest |
| `get_contacts` | — | GetContactsRequest |
| `get_messages` | `from` string (empty = newest), `limit` integer, 1–1000 | GetMessagesRequest |
| `get_message` | `message` ID | GetMessageRequest |
| `set_current_chat` | `chat` may be empty to clear | SetCurrentChatRequest |
| `send_message` | `text`; optional `quoted` message ID, optional `file` local path | SendMessageRequest |
| `edit_message` | `message`, `text` | EditMessageRequest |
| `delete_message` | `message` | DeleteMessageRequest |
| `mark_read` | `message` | MarkMessageReadRequest |
| `send_typing` | `typing` boolean | SendTypingRequest |
| `send_reaction` | `message`, `emoji` (empty removes reaction) | SendReactionRequest |
| `download_file` | `message` | DownloadFileRequest |
| `get_members` | — | GetGroupMembersRequest |
| `pin_chat` | `on` boolean | PinChatRequest |
| `archive_chat` | `on` boolean | ArchiveChatRequest |
| `quit` | — | Local shutdown |

`get_messages` and `get_message` try MessageCache first, falling back to the
protocol when it cannot satisfy a fetch, using the cache's existing sequence and
pagination rules. A cached page may contain fewer than `limit` messages. `from`
is echoed even for cached pages (the cache's internal callback omits that cursor,
so serve retains it). History is newest first. Network pagination/limits ultimately
depend on the protocol; Dummy ignores both. Chats and contacts request the protocol;
cached contacts are also emitted automatically at startup for name resolution.

`send_message` sets `clientRequestId` to `id`, sender to the protocol self ID, and
time to current milliseconds. The optional `file` must exist locally; MIME type is
detected as in the terminal UI. Quotes are expanded from known messages/cache.
Editing preserves the original message's metadata. WhatsApp deletion/read receipts
need the original sender; reaction requests need sender and previous self emoji;
downloads need the file ID. Serve fills these from observed messages or sqlite.
If required metadata is unavailable it replies with an error asking the client to
fetch the message first. `get_message`/`get_messages` should therefore precede these
operations. Download uses `DownloadFileActionNone` and never opens an external app.

Operations are mapped even when a protocol does not implement them. Feature flags
help identify capabilities; acceptance does not promise a later event. A profile
that has requested terminal control is marked unavailable; further operations for
it fail with `profile needs setup ...`. Other profiles continue normally. Exit and
run `nchat --setup` in a terminal, then restart serve.

## Events

Each service notification produces one event. List fields always contain arrays.
`ok` on notification events reflects the service's success flag, separate from
request acceptance; inspect it before applying deletion or chat state changes.

| event | Fields besides `profile` | Service message |
| --- | --- | --- |
| `connected` | `ok` | ConnectNotify |
| `chats` | `ok`, `chats`: [chat] | NewChatsNotify |
| `contacts` | `contacts`: [contact] | NewContactsNotify |
| `messages` | `ok`, `cached`, `chat`, `from`, `messages`: [message] | NewMessagesNotify |
| `sent` | `chat`, `ok`, optional `request`, optional `message` | SendMessageNotify |
| `message_status` | `chat`, `message`, `read` | NewMessageStatusNotify |
| `message_file` | `chat`, `message`, `file` | NewMessageFileNotify |
| `deleted` | `ok`, `chat`, `message` | DeleteMessageNotify |
| `deleted` | `ok`, `chat` (no `message`) | DeleteChatNotify |
| `typing` | `chat`, `user`, `typing` | ReceiveTypingNotify |
| `status` | `user`, `online`, `last_seen` | ReceiveStatusNotify |
| `reactions` | `chat`, `message`, `reactions` | NewMessageReactionsNotify |
| `pinned` | `chat`, `message`, `on` | NewMessageIsPinnedNotify |
| `pinned` | `ok`, `chat`, `on` | UpdatePinNotify |
| `muted` | `ok`, `chat`, `on` | UpdateMuteNotify |
| `archived` | `ok`, `chat`, `on` | UpdateArchivedNotify |
| `members` | `chat`, `members`: [contact] | NewGroupMembersNotify |
| `needs_setup` | — | ProtocolUiControlNotify |
| `exit` | — | RequestAppExitNotify |
| `other` | `type`: numeric MessageType | Every other service message |

`sent.request` is included only for profiles advertising `send_acknowledgement`;
otherwise no correlation is invented. `sent.message` appears on success or when a
message ID is available. Dummy acknowledges the submitted object without assigning
an ID, so its successful message can have `id: ""`.

Events reflect callbacks, including callbacks from the cache; applications should
upsert by profile/chat/message ID rather than append duplicates blindly. Partial
reaction changes are consolidated with previously observed state. Unrecognized
notifications are never silently dropped; `other.type` is the enum number in
`protocol.h` for this nchat version, not a stable cross-version wire enum.

## Objects

- **chat:** `id`, `name`, `unread` (boolean, not a count), `unread_mention`, `muted`,
  `pinned`, `archived`, `last_message_time`.
- **contact:** `id`, `name`, `phone` (without a leading `+`, as stored), `self`.
- **message:** `id`, `sender`, `text`, `time`, `outgoing`, `read`, `edited`,
  `deleted`, `pinned`, `quoted`, `file`, `reactions`, `mentions`.
- **quoted:** `null`, or `{ "id": "...", "text": "...", "sender": "..." }`.
- **file:** `null`, or `{ "path": "...", "status": 0, "id": "...", "type": "..." }`.
  Status follows FileStatus: -1 none, 0 not downloaded, 1 downloaded,
  2 downloading, 3 download failed. `id` is a protocol token, not a path;
  `type` is MIME type. Paths may be empty until downloaded. No bytes cross stdio.
- **reactions:** `{ "sender_emojis": { "sender ID": "emoji" },
  "emoji_counts": { "emoji": 2 } }`. The self sender key is `You`.
- **mentions:** object mapping display names to user IDs, possibly empty.

Chat names use known contacts, with `You` for self and the chat ID as fallback,
matching the UI. Contacts can arrive after chats: clients should update displayed
names by joining subsequent contacts on ID. All lists may be empty. `time` and
`last_message_time` use milliseconds since the epoch, with -1 for unset values.
`last_seen` also preserves nchat's TimeSeen sentinels: -1 unknown/recently/offline,
0 reserved, 1 last month, 2 last week; values above these are epoch milliseconds.
Mentions and some transient metadata are not persisted by nchat's cache.

## Protocol authentication boundary

The original UI-control notification could only retry or grant terminal ownership;
it could not decline authentication. `ProtocolUiControlNotify.isUiAvailable` now
lets a handler decline, defaulting to true for the terminal frontend. Telegram stops
interactive authentication after a denial; WhatsApp and Signal return failure from
their console-acquisition callbacks before prompting/provisioning. Those narrow
protocol changes are isolated in a separate commit. No upstream merge is needed.

## Verification and limitations

With `HAS_TESTS=ON`, `ctest` runs `test_serve` against real Dummy profiles (when
`HAS_DUMMY=ON`) and `test_serve_events` against a deterministic protocol fixture.
The former covers startup, history/cache, send correlation, malformed input,
restart, exclusive profile/confdir locks and all shutdown paths. The latter covers
request metadata, events Dummy does not generate, reauthentication denial,
incidental stdout suppression, and synchronous callback ordering.

Dummy does not implement contacts requests, editing, deletion, reactions, read
receipts, member lookup, pinning, archiving, or file downloads. Tests of those
mappings use the fixture. Authenticated account/network behavior still requires
manual verification with actual services. The interface is stdio-only; reconnecting
requires launching a new child process.
