Fork notes
==========

Build
-----

    mkdir -p build && cd build
    cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$HOME/.local \
             -DHAS_SIGNAL=ON -DHAS_INSTAGRAM=ON
    make -j$(nproc) && make install

Optional runtime tools: `chafa` (inline images), `ffmpegthumbnailer` (video
thumbnails), `ffplay`/`ffprobe` from ffmpeg (voice notes).

Preferences and previews
------------------------

Open Preferences with `Alt-g`. Up/Down selects an option (skipping section
headings); Left/Right changes it, as do Enter and Space. Home/End and
PageUp/PageDown navigate the list. Esc or Ctrl-C closes the dialog and applies
changes. Configured navigation bindings in `key.conf` also work. On an account,
Enter toggles pinned-only chats and `+`/`-` changes its order.

Under Media, **Preview style** selects blocks (the existing default), ASCII,
Braille dots, half blocks, quadrant blocks, sextant blocks, stipple/shading or
mixed symbols. **Preview colours** selects colour, foreground-only colour
(leaving the terminal background untouched), or monochrome. Choose ASCII and
monochrome for traditional text art. Styles also apply to video thumbnails.
Changes refresh previews when Preferences closes, including images already
cached in the current session. Unicode styles need a font with those glyphs;
ASCII works with ordinary terminal fonts. Chafa must be installed to render
previews; monochrome previews also work without a 256-colour terminal.

These use Chafa's supported [symbol classes and colour modes](https://github.com/hpjansson/chafa/blob/master/docs/chafa.xml).
Images remain ordinary character cells, so previews work inside ncurses panes.

Tabs show compact names for navigation. A single header above each conversation
shows the full contact name and optional number/@handle on the left, with the
coloured account label aligned right. This header also appears in each split
pane, using the existing padding row. The bottom bar shows chat activity/status
and the send/reply/save hint, without repeating contact details.

The sidebar defaults to 22 columns and narrows automatically in small terminals
to leave at least 20 columns for the conversation. `Alt-,` / `Alt-.` adjusts the
preferred width; it is restored after widening the terminal again.

Pane layout and composing
-------------------------

The sidebar spans the terminal's full height. Tabs, history, status, message
input and shortcut help occupy only the conversation area to its right.

`Alt-v` opens/closes a split view with two different chats. Each pane has its own
message field and per-chat draft. The highlighted **To name** label and cursor
identify the focused field; the other field shows **Draft for name**. Press
`F6` to move focus without moving the conversations. Sending uses only the
focused chat's draft and account, and an empty focused draft sends nothing.
`Alt-x` swaps the conversations and their drafts between panes. Selecting a tab
for an already visible chat focuses that pane rather than showing it twice.

Narrow windows temporarily show the focused conversation alone; widening the
window restores both panes and drafts. `F6` does not switch to a hidden pane.
Closing split view keeps the focused conversation open. Message editing keeps
focus on its recipient until the edit is saved or cancelled.

Xfce Terminal shortcuts
-----------------------

Xfce Terminal's menu access keys and Alt-number terminal tab bindings can
intercept nchat shortcuts. Disable **menu access keys** in Terminal Preferences
(`shortcuts-no-mnemonics=true` in the `xfce4-terminal` xfconf channel), and clear
the `goto-tab-1` through `goto-tab-9` bindings in its `accels.scm`. Terminal tabs
can still use Ctrl-Tab / Ctrl-Shift-Tab. Restart the terminal after editing the
accelerator file. See Xfce's [shortcut definitions](https://github.com/xfce-mirror/xfce4-terminal/blob/master/terminal/terminal-window.c)
and [menu mnemonic preference](https://github.com/xfce-mirror/xfce4-terminal/blob/master/terminal/terminal-preferences.c).

Within nchat, the default Cut and Paste bindings are Ctrl-Alt-x and Ctrl-Alt-v,
leaving Alt-x and Alt-v for split view. Existing custom key bindings are kept.

Activity indicators
-------------------

A fresh unread incoming message in a chat that is not visible pulses its
sidebar/tab marker for six seconds. The usual unread marker remains afterwards.
Opening the chat stops the pulse, as does revealing it in the other split pane.
Cached history, old backfill, edits and duplicate events do not start a pulse.

For WhatsApp, Signal and Instagram, a send still awaiting its backend
acknowledgement after 1.5 seconds shows a spinner and **Sending** in that chat's
status line and a marker in the sidebar/tab. After 15 seconds the label becomes
**Still sending**. There is no guessed timeout or automatic resend. Acknowledged
slow sends briefly show **Sent**; explicit failures show **Send failed** and a
`!` marker until that failed send is retried. These indicate backend acceptance, while
delivery/read receipts keep their existing meaning.

Concurrent sends are tracked separately by request ID, including attachments
and forwarded messages. Indicators stay with their chat when switching focus,
swapping panes or migrating chat IDs. The dummy protocol also supports send
acknowledgements; protocols without this capability do not show guessed progress.

Turn off **Activity animations** in `Alt-g` → Chats for reduced motion. The
ordinary unread markers and static sending/failure labels remain available.
Timing uses a monotonic clock, and animation ticks redraw only indicator rows.

Saved drafts and retrying sends
-------------------------------

Unsent message text and cursor positions save per account and chat, including
both split panes. Drafts are checkpointed once a second while nchat runs and
flushed on normal exit. Sending removes the saved draft immediately. Existing
message edits are excluded from new-message draft saves. Text is restored when
you reopen the chat; reply selection and message-edit mode are not restored.

Drafts live in `drafts.txt` in nchat's configuration directory. Saves replace the
file atomically and use owner-only permissions. The contents are local,
unencrypted text (hex encoded to preserve newlines and Unicode). A crash may
lose up to the last second of typing; normal exit flushes the latest changes.

For WhatsApp, Signal and Instagram, **F7** (`retry_send`) retries the oldest failed
send in the focused chat. Its original text, reply, mentions and attachment
metadata are retained separately from your current draft. Each press retries
one failure; pressing again cannot duplicate an already pending retry. A failure
shows the configured retry shortcut in the status line. A failed retry returns
to the queue, and unrelated successful sends do not dismiss other failures.

Retry queues last for the current nchat session. Pending sends without a backend
answer cannot be retried with F7, and nchat never resends automatically. A retry
uses the original attachment path, so keep the source file until it succeeds.

Local chat pins
---------------

Press `Alt-p` while viewing a chat to pin or unpin it locally. Pinned chats
appear first in the sidebar with a pin marker. `Alt-f` cycles through All,
Pinned, account views and Archived; Pinned collects pins across accounts.
Per-account pinned-only settings in Preferences also use local pin choices.

Local chat pinning is enabled by default and works with every protocol. Choices
are saved immediately in `local_pins.txt` in nchat's configuration directory and
survive restarts. Existing service pins are shown until you override a chat
locally; an explicit local unpin remains unpinned even if the service still pins
it. Service refreshes do not overwrite local choices. Pins follow chat-ID
migrations and are removed when a chat is deleted.

In `Alt-g` → Chats → **Pin chats**, choose **on the service** to use the original
service pinning behaviour. Local choices stay saved while this mode is off.
When a message is selected, `Alt-p` still pins the message on the service as
before; the local option applies to chat pins.

Instagram
---------

`nchat --setup` → Instagram, log in with password (+2FA) or by pasting a cURL
command / cookie header copied from a logged-in browser session.

Added config options
--------------------

ui.conf:

- `pinned_only_profiles` – comma separated profile ids (e.g. `Signal`) that
  only list pinned chats; toggle with `toggle_pinned_only` (alt-o).
- `archive_local` – archive chats locally only (`archive_chat`, alt-a).
- `pin_local` – pin/unpin chats locally (default `1`); set to `0` for service pins.
- `activity_animations` – pulse fresh activity and animate slow sends (default `1`).
- `media_display` – `preview` (default), `name` or `path` for attachments.
- `inline_image_lines` – maximum preview height (default `10`).
- `inline_image_style` – `blocks` (default), `ascii`, `braille`, `half`,
  `quadrants`, `sextants`, `stipple` or `mixed`.
- `inline_image_color` – `color` (default), `foreground` or `mono`.
- `inline_audio` – play voice notes inline (`play_audio`).

key.conf:

- `cycle_list_view` (alt-f) – All / Pinned / per protocol / Archived.
- `pin` (alt-p) – pin/unpin the current chat; pins a message when one is selected.
- `toggle_pinned_only` (alt-o), `play_audio`.
- `preferences` (alt-g) – media, chats and accounts settings.
- `retry_send` (F7) – retry one failed send in the focused chat.
- `focus_pane` (F6), `toggle_split` (alt-v), `swap_split` (alt-x).

UI regression test
------------------

With a Unix Makefiles build and `HAS_TESTS=ON`, `ctest --test-dir build` includes
the headless `test_ui_panes` test when Python 3 is available. It checks sidebar
geometry, drafts, cursor focus, account/chat send routing, tabs, pane swaps,
resizing, deletion, activity indicators, send retries and draft restart recovery
with fake protocols. It never loads real accounts.
Run it directly with `python3 tests/run_ui_regression.py build` after building.

Updating from upstream
----------------------

    git fetch upstream && git rebase upstream/master
