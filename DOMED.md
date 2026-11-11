# DomeD Discord

A native Linux/Wayland Discord client forked from MaiCord, using DomeD's bitmap
font collection, appearance and tray conventions. Fork repository:
https://github.com/jebot-git/maicord-dome (`main`). Upstream source:
https://github.com/maj113/MaiCord at `a906bd7c58a3ebe40310f49cace894f7d2e67b62`.
MPL-2.0 continues to apply; upstream and third-party notices are retained.

## Run

```sh
git clone https://github.com/jebot-git/maicord-dome.git
cd maicord-dome
bash dome/build.sh
./dome/run.sh
```

The executable is `../maicord-dome-build/MaiCord`. Always use `dome/run.sh`, which
prepares the bitmap catalog and isolates the profile. Sign in through MaiCord's
normal account, QR or browser flows. This build does not embed a browser engine.
To add an applications-menu entry and a `~/.local/bin/dome-discord` launcher:

```sh
python3 dome/install-user.py
```

No system privileges are needed. The installer audits the prepared executable
and refuses test-hook builds. It does not register itself as the default Discord
URL handler. That remains an explicit choice in the client's System settings.
The handler and autostart entries re-enter the wrapper, so they receive the same
bitmap catalog and private profile.

## DomeD integration

- Wayland app identity: `org.domed.Discord`; requests server-side window
  decorations so DomeD supplies the frame.
- Shared background/text palette, square controls, and session accent. Light/dark
  and accent changes follow the desktop settings portal while running. When that
  portal is absent, startup uses `$XDG_CONFIG_HOME/dome/appearance.json`.
- The selected BDF family, pixel size and integer enlargement are read from the
  same appearance file. Font changes take effect on the next launch. PCF-only
  selections currently fall back to Unifont; the native BDF collection is required.
- Universal menu exported as a separate hierarchical `com.canonical.dbusmenu`
  object, associated with the native window using `org_kde_kwin_appmenu`.
  File/Edit/View/Voice/Window/Help appear in DomeD's top bar; the window does not
  duplicate a menu bar. File supplies Close Window, View supplies Preferences,
  and the desktop owns About/Hide/Quit in the application menu. Actions use the existing edit, settings, zoom and voice
  handlers, with disconnected voice commands disabled and shortcuts exported.
- StatusNotifierItem and `com.canonical.dbusmenu` use the native MaiCord tray
  implementation: open, unread/mention state, voice mute/deafen/disconnect and quit.
  Closing hides the window when the tray host is available, according to the
  client's existing minimize-to-tray preference. A second launch reveals it.
- Desktop notification identity, launcher badges, single-instance activation and
  protocol integration use the DomeD identity. Notification and voice behavior
  remain MaiCord's; they were not exercised against a real account in this run.

The data, state and cache bases are each namespaced under `dome-discord`.
MaiCord's own subdirectory remains inside that namespace, for example
`~/.local/share/dome-discord/maicord`. Existing official MaiCord data is untouched.
The launcher never reads another client's tokens. No account credentials or
message contents are included in validation artifacts.

Wallet requests use a persistent private D-Bus connection, serialized across
threads and separate from the UI's desktop-service connection. Each operation
opens a fresh Secret Service session bound to the provider that replied, so
caller approval survives repeated operations while provider restarts do not
reuse stale sessions. Reading/storing allows Dome Wallet's three-minute access
dialog to finish; routine calls retain their short deadlines. Locked-wallet
unlock prompts are still reported as locked without invoking a prompt.

## Bitmap-only text

The fork rejects fonts containing `glyf`, `CFF `, `CFF2` or `COLR` tables, including
hybrid bitmap/outline faces. Outline parsing, TrueType instruction execution,
CFF/COLR font rendering and the font-outline GPU path are removed from the build;
their implementation sources and bundled outline-font assets are deleted.

`dome/bitmap_font.py` wraps installed Unicode BDF glyphs in SFNT/CBLC/CBDT bitmap
containers, with cmap and metric tables. Despite their `.ttf` filename, these files
contain no outline tables. Their PNG pixels are exact binary BDF pixels. Text uses
nearest-neighbor sampling and snapped device-pixel placement; cached white masks
are tinted at draw time, preserving theme, role and ANSI colors. No TrueType/CFF
outline fallback is available, including to plugins. Outline-based text effects
are not retained; plugin glyph textures are generated from bitmap masks. Missing characters use the
BDF-derived Unifont and supplementary-plane bitmap faces.

MaiCord's Unicode layout, bidi, text input, selection and OpenType layout-table
machinery remain. Non-font vector UI icons, regional flag illustrations, Lottie
and background effects remain; they are independent of font loading/rendering.
The plugin effect glyph grid is built from bitmap glyphs too.

## MaiCord compatibility

Messaging, channels, friends, media, voice/DAVE, account storage and the native
plugin host use the upstream implementation. Linux ELF plugin loading and the
plugin SDK ABI are retained and tested. Windows-only plugin binaries require a
Linux build. Plugins/themes that request unavailable font families cannot enable
outline fonts; the text system uses bitmap fallbacks. DomeD's palette overrides
imported theme colors while this launcher is used. Individual catalog plugins
and live Discord voice calls have not been independently qualified here.

Upstream's self-updater is disabled in `dome/build.sh` so it cannot replace this
fork. The signed plugin catalog and its verification are retained.

## Validation

The private-bus `maicord_dbus_services` regression verifies caller reuse across
lookups/stores and worker threads, access approval taking longer than four
seconds, and reconnecting with a new Secret Service session. Together with
`maicord_dbus_secure_buffer`, it passes against the mock provider and a bare
private bus. These tests require the private-bus harness; normal test runs skip
them to avoid accessing the user's wallet.

`bash dome/build.sh --test` selects the supported bitmap and plugin contracts by
default. The legacy upstream suite assumes outline-font support and is not this
fork's default suite.

```sh
python3 -m unittest discover -s tests/dome -p 'test_*.py'
bash dome/build.sh --test --skip-client --keep-intermediates \
  --test-names domed_bitmap_native,maicord_native_cbdt_fixture,maicord_plugin_abi,maicord_plugin_loader_elf
# Always return to a shipping build without test hooks:
bash dome/build.sh
uv run --no-project --with pillow --with python-xlib python dome/test_ui.py
uv run --no-project --with pillow --with python-xlib python dome/test_ui.py --light
```

Native tests check exact BDF pixels, binary pixels at fractional sizes, cmap and
metrics, rejection of outline fonts, existing bitmap decoding, and plugin ABI/ELF
loading. Adapter tests cover SFNT checksums, absence of outline tables, appearance
validation and private profile paths. The disposable native Wayland test checks a
real DomeD window, tray menu, Wayland-associated universal menu, menu-driven editing/zoom/close, the application/File/Window convention,
single instance and clean exit, without an account.
Reports and the screenshot are under `dome/validation`. No real headset or live
account authentication was tested. Upstream tests specifically requiring outline
fonts are obsolete for this bitmap-only fork.

The shipping ELF is approximately 6.70 MB, above upstream's 6.50 MB size target.
