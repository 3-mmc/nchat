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
- `inline_images`, `inline_image_lines` – inline previews in the history.
- `inline_audio` – play voice notes inline (`play_audio`).

key.conf:

- `cycle_list_view` (alt-f) – All / per protocol / Archived.
- `toggle_pinned_only` (alt-o), `play_audio`.

Updating from upstream
----------------------

    git fetch upstream && git rebase upstream/master
