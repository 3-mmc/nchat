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

On a wide terminal, Preferences shows a built-in landscape sample to the right
of the settings. It refreshes immediately when you change Preview style,
Preview colours or Preview height, using the same renderer as chat images.
The sample fits the available panel space; the selected height still applies
in chats. Narrow/short terminals keep the full-width settings list and show a
hint to enlarge the window. Resizing restores the sample automatically.
No personal attachments or downloads are used for the sample.

The character preview uses the terminal's ordinary text grid. Its font size
cannot be changed independently of the rest of nchat in Xfce Terminal. Increase
Preview height or try a denser glyph style to show more detail. Reducing the
terminal font affects all text. The optional Sixel renderer displays pixels
independently of glyph shape (see below).

Under Media, **Preview style** selects blocks (the existing default), ASCII,
Braille dots, half blocks, quadrant blocks, sextant blocks, stipple/shading or
mixed symbols. **Preview colours** selects colour, foreground-only colour
(leaving the terminal background untouched), or monochrome. Choose ASCII and
monochrome for traditional text art. Styles also apply to video thumbnails.
Changes refresh previews when Preferences closes, including images already
cached in the current session. Unicode styles need a font with those glyphs;
ASCII works with ordinary terminal fonts. Chafa must be installed to render
previews; monochrome previews also work without a 256-colour terminal.

The styles choose the **characters**, while Preview colours chooses how their
foreground and background are painted. With foreground + background colour,
several styles look similar because even spaces carry image background colours.
Foreground-only or monochrome makes each style's character shapes more visible.

| Style | What changes | Best starting point |
| --- | --- | --- |
| Blocks | Fractional blocks and borders approximate edges | Colour photo thumbnails |
| ASCII | Ordinary punctuation, letters and digits | Monochrome for classic text art |
| Braille | A 2x4 grid of dots per character | Foreground-only for fine dotted outlines |
| Half blocks | Top/bottom halves, 1x2 samples per character | Colour; simple, widely supported |
| Quadrants | 2x2 block subdivisions | Colour; more edge detail |
| Sextants | 2x3 subdivisions using newer Unicode glyphs | Colour, if your font supports them |
| Stipple | Shading characters simulate tones | Monochrome for a grainy look |
| Mixed | A broad mixture of narrow glyphs | Experimental; more font dependent |

Start with **half blocks**, **foreground + background**, and **16 lines** for
photos. Try **ASCII + monochrome** for text art. More rows preserve more detail;
a photo squeezed into six rows will remain very coarse. The same height is
shared across styles, so finer glyph grids do not make the preview taller.

Preview colour pairs occupy a separate range from UI colours and are never
recycled while cached images reference them. Chafa quantizes to the xterm
240-colour cube/grays before choosing glyphs; nchat remaps palette entries
reserved by theme colours. Theme entries are prepared up front and reused;
group/contact name colours use the existing palette rather than redefining
dozens of image colours. Drawing a new contact cannot recolour an existing preview.
The renderer disables terminal probing, clips to the
pane, and excludes wide/ambiguous glyphs from Mixed. Missing glyph boxes or gaps
in Braille/sextants can still be caused by terminal font fallback; Half blocks
and ASCII are safer choices. Preview rows are character art, not original pixels.

These use Chafa's supported [symbol classes and colour modes](https://github.com/hpjansson/chafa/blob/master/docs/chafa.xml).
Images remain ordinary character cells, so previews work inside ncurses panes.

Optional Sixel graphics
-----------------------

In `Alt-g` → Media → **Preview renderer**, select **Sixel pixels (experimental)**
for actual pixel images, or **characters (portable)** for the existing symbol
styles. Characters remain the default. Sixel uses the existing `chafa` runtime
tool, requires no new build dependency, and also works with video thumbnails.
The live settings sample uses whichever renderer is selected.

Your terminal must support and enable Sixel. Xfce Terminal exposes **Enable sixel
graphics** in its preferences (`/enable-sixel` in the `xfce4-terminal` xfconf
channel); enable it before using this mode. This is an explicit opt-in: nchat
does not send capability probes or consume terminal response bytes. A terminal
that silently ignores Sixel can show a blank preview; switch to Characters.
See Xfce's [Sixel preference and VTE integration](https://github.com/xfce-mirror/xfce4-terminal/blob/master/terminal/terminal-screen.c).

Sixel dimensions come from the terminal's reported pixel/cell geometry. Images
are bounded to the preview rectangle and preserve aspect ratio. Changes in
font size or terminal geometry generate a new raster. Native image colours are
used; character style and Preview colours affect only character previews.
A bigger Preview height increases the image area without shrinking chat text.

Each pane owns its overlays. Switching chats, resizing, opening dialogs,
launching external programs and exiting erase old image rectangles. Nested
dialogs suppress covered images; closing them restores visible chat images.
Partially clipped images use character rows so pixel data never spills into a
header, sidebar, composer or another pane. If output is not a TTY, pixel metrics
are missing, encoding fails, or tmux/screen is detected, characters are used.
Sixel passthrough through multiplexers and Kitty/iTerm graphics are not
implemented. Native image display still needs a live check in your terminal.

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

`F8` / `F9` cycles to the previous / next chat tab, wrapping at the ends.
From a chat outside the tab bar, F8 selects the last tab and F9 selects the first.
`Tab` / `Shift-Tab` still navigates the sidebar chat list. Alt-1 through Alt-9
remains available for direct tab selection when the terminal passes those keys.

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
- `inline_image_backend` – `symbols` (default) or `sixel` (explicit opt-in).
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
- `prev_tab` (F8), `next_tab` (F9) – cycle chat tabs.
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
When Chafa is available, image and preferences tests cover the style/colour
matrix and live settings sample. On Linux, `test_pixel_graphics` runs real Sixel
encoding on a private PTY and checks pixel bounds, fallback, chat switching,
split view, nested dialogs and clipped-image cleanup without real accounts.

Updating from upstream
----------------------

    git fetch upstream && git rebase upstream/master
