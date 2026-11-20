# Architecture

`tuipro` is split into two layers with a deliberately narrow boundary:

```text
┌──────────────────────────────────────────────────────────────┐
│ LuaJIT                                                       │
│   App event loop · layout containers · widgets · plugins      │
│   Canvas · highlights · keys · snapshots                     │
├──────────────────────────────────────────────────────────────┤
│ C core (embedded as tuipro.core)                             │
│   terminal lifecycle · cell grid · diff flush · input parser  │
│   polling · signals · cursor · snapshots                     │
└──────────────────────────────────────────────────────────────┘
```

The C layer never knows what a widget is. It owns a rectangular grid of cells,
draws UTF-8 text into it, flushes differences to the terminal and turns bytes
into events. The Lua layer owns all composition, focus, application state and
policy.

## Project layout

```text
include/tuipro.h          public C API and data structures
src/main.c                launcher, package path, script/module/plugin dispatch
src/term.c                terminal lifecycle, raw mode, polling, signals
src/grid.c                UTF-8 cells, clipping, diffing, snapshots, colors
src/input.c               keyboard/mouse/paste/focus parser
src/lua_core.c            Lua bindings for the C core
src/tuipro_internal.h     private terminal state and shared helpers

lua/tuipro.lua            public Lua front-end
lua/tuipro/app.lua        event loop, layout, focus, chrome, popups, commands
lua/tuipro/widget.lua     Widget base class
lua/tuipro/container.lua  VBox/HBox/Padding/Center/Stack/Split
lua/tuipro/canvas.lua     clipped drawing surface
lua/tuipro/hl.lua         highlight groups, glyphs and theme switching
lua/tuipro/theme.lua      theme registry, palettes, groups and glyphs
lua/tuipro/keys.lua       key notation parsing and normalization
lua/tuipro/util.lua       UTF-8, width, wrapping and generic helpers
lua/tuipro/ui/            concrete widgets
lua/tuipro/plugins.lua    plugin discovery and installation
lua/tuipro/demo.lua       built-in dashboard
lua/tuipro/demo_theme.lua built-in theme showcase

examples/                 minimal, dashboard, gallery, file manager, splits
plugins/hello/            sample plugin
plugins/aurora/           sample theme plugin with glyph overrides
plugins/opencode/         OpenCode dark theme plugin with exported palette colors
plugins/tokyo-night/      TokyoNight Night theme plugin with upstream colors and flat showcase styling
tests/                    Lua suites, CLI checks, PTY smoke test
docs/                     this documentation
```

## C core

### Terminal lifecycle (`term.c`)

`tp_term_init()` configures either a headless grid or a real terminal:

- In headless mode it creates a grid at the requested size and performs no I/O.
  This is the basis for snapshots, scripts and CI.
- In terminal mode it requires `stdin` and `stdout` to be TTYs, enters raw mode
  with `cfmakeraw`, saves termios, installs signal handlers, reads the window
  size and switches to the alternate screen (`?1049h`). It hides the cursor,
  disables line wrapping, enables bracketed paste (`?2004h`) and, when enabled,
  mouse reporting (`?1000h`, `?1002h`, `?1006h` for SGR coordinates).
- `tp_term_shutdown()` disables mouse and paste reporting, restores wrapping,
  resets SGR and cursor shape, shows the cursor, leaves the alternate screen,
  restores termios and reinstalls the old signal handlers. `TP_EVENT_SIGNAL`
  events request a clean exit.

Truecolor detection checks `TUIPRO_TRUECOLOR` first, then `COLORTERM`
(`truecolor`/`24bit`), `TERM` containing `direct`, and a list of known terminal
programs (`iTerm.app`, WezTerm, kitty, ghostty, VS Code, Alacritty, ...).
When truecolor is off, colors are quantized to the xterm-256 palette.

Output is buffered in a growable byte buffer and written with retry-on-`EINTR`
semantics. Synchronized output (`?2026h` / `?2026l`) wraps a flush when
supported; disable it with `TUIPRO_SYNC=0`.

Signals are handled minimally: `SIGWINCH` sets a flag that becomes a resize
event, and `SIGINT`, `SIGTERM`, `SIGHUP` and `SIGQUIT` set the exit flag.
`SIGPIPE` is ignored so a lost terminal does not kill the process abruptly.

### Grid (`grid.c`)

A `tp_term` owns two cell arrays: `front` (what the terminal currently shows)
and `back` (what widgets draw for the next frame). A cell stores:

```c
uint32_t codepoint;      /* 0 = wide continuation */
uint32_t combining[2];
uint32_t fg, bg;         /* 0xRRGGBB or TP_DEFAULT_COLOR */
uint16_t attrs;
uint8_t width;           /* 0, 1 or 2 */
```

`tp_grid_put()` walks UTF-8 codepoints and places them in cells. Wide
codepoints occupy a base cell with `width = 2` plus a continuation cell with
`width = 0`; a wide glyph that would cross the right edge is dropped rather
than split. Combining marks (up to two per cell) attach to the preceding base
cell. `tp_text_width()` performs the same width calculation without drawing,
using a compact Unicode width table with emoji/wide fallbacks.

`tp_grid_fill()` fills a rectangle and respects clipping. `tp_grid_set_clip()`
and `tp_grid_reset_clip()` implement the single clip rectangle used by all
canvases. Writes outside the grid are discarded. When a write overwrites half
of a wide glyph, the other half is sanitized to a blank so the front buffer
never contains a dangling continuation.

`tp_grid_flush()` is the display core:

1. In headless mode, copy `back` to `front` and return.
2. If a full redraw is pending, emit clear-screen and invalidate cached output
   state.
3. For each row, skip identical cells, then emit a run that tolerates up to
   eight unchanged cells between changes. Long unchanged spans break the run.
4. Expand runs outward to include both halves of wide glyphs.
5. Move the cursor, emit SGR only when the style actually changes, and write
   UTF-8 cells (base plus combining marks).
6. Copy `back` to `front`.

This is a line-run diff, not a minimal-edit algorithm. It is fast, easy to
verify, and keeps typical redraws proportional to what changed. The terminal
state cache (`out_x`, `out_y`, current SGR, cursor state) avoids redundant
escapes between runs.

Snapshots are generated from the grid: `tp_grid_row_alloc(row, trim)` rebuilds
a UTF-8 row, optionally trimming trailing blanks. This is what
`core.rows()`/`core.snapshot()` expose to Lua and tests, and why headless output
can be asserted as plain strings.

### Input (`input.c`)

Input bytes accumulate in a fixed 16 KiB buffer. `tp_term_poll()` waits for
readability (and a timeout), reads available bytes, then asks the parser for
one event. Parsing is incremental and returns `0` for incomplete sequences.

Supported input:

- UTF-8 text, including multi-byte characters and modified printable keys.
- Control keys: Ctrl-A..Ctrl-Z, Tab, Enter, Backspace, Esc.
- CSI and SS3 sequences for arrows, Home/End, PageUp/PageDown, Insert/Delete,
  function keys and keypad keys.
- SGR mouse (`ESC [ < b ; x ; y M/m`): press, release, motion and wheel with
  Shift/Alt/Ctrl modifiers. Coordinates are converted to 0-based cells.
- Bracketed paste (`ESC [ 200 ~ ... ESC [ 201 ~`) into a growable buffer.
- Focus in/out events.
- Kitty keyboard protocol codepoints for special keys and CSI-u style
  modifiers.

An escape-only sequence is ambiguous. The parser waits up to 30 ms
(`TP_ESC_TIMEOUT_MS`) for more bytes; after the deadline a lone `ESC` becomes a
key event. This keeps Escape responsive without splitting real escape sequences.

The resulting `tp_event` contains a type, key name or mouse button/action,
modifier bits, 0-based coordinates, resize dimensions, paste payload or signal
name. `tp_event_release()` frees owned payloads.

### Lua bridge (`lua_core.c`)

The core is preloaded as `tuipro.core` in the Lua state; no separate shared
library is required. The bridge:

- Converts Lua tables into `tp_term_init()` options.
- Converts colors (`-1`/`nil` → terminal default) and attribute masks.
- Exposes drawing, clipping, cursor and snapshot functions.
- Pushes events as Lua tables and releases native payloads after conversion.
- Keeps a single global `tp_term *` and force-shuts it down on process exit.

Because the terminal is global, only one app can own the core at a time. App
creation does not initialize it; `App:render()`/`App:run()` do so lazily. An app
that initialized the core shuts it down in `App:shutdown()`.

## Lua layer

### Application event loop (`app.lua`)

`App` holds the body tree, optional tabline/statusline, overlay stack, timers,
bindings, commands, focus and messages. The main loop:

```text
render initial frame
while not should_quit:
    timeout = min(1s, next timer, frame pacing if dirty)
    event = core.poll(timeout)
    dispatch event
    process due timers
    if dirty and enough time passed: render
    stop if core.should_exit() or frame limit reached
```

Frame pacing comes from `fps` (default 60). `frames`/`options.frames` limits
the number of rendered frames for headless smoke tests. Timers are sorted only
by scanning the timer list for the nearest deadline; the list is small and this
keeps cancellation trivial. The loop uses monotonic `core.ticks()` rather than
wall-clock time.

`App:render()`:

1. Initialize the core if needed and pick up a changed terminal size.
2. Lay out chrome and body.
3. Ensure a valid focus target.
4. Clear the back buffer and paint the background (skipped when the resolved
   background is the terminal default or `background = false`).
5. Render tabline, body, statusline, command line and popups into clipped
   canvases.
6. Set or hide the cursor.
7. Flush the grid diff and increment `frames`.

The app does not render continuously when nothing is dirty. Widgets call
`invalidate()` after state changes, timers do the same, and the loop waits for
input, the next timer or the next allowed frame.

### Layout

Layout is top-down and immediate. `Widget:resize()` stores absolute geometry,
then calls `layout(w, h)`. Containers distribute their children:

- **VBox/HBox**: fixed children use explicit or preferred size; `flex` children
  split the remainder by weight; the last flex child absorbs rounding. `gap`
  is inserted between visible children. `justify` handles leftover space when
  there is no flex child. `align`/`valign` position fixed cross-axis sizes.
- **Padding**: subtracts per-side padding and resizes its single child.
- **Center**: uses fixed or preferred size, clamps to the container, and
  centers the remainder.
- **Stack**: gives every visible child the full rectangle.
- **Split**: reserves one cell per separator, assigns fixed `size` panes, then
  distributes the rest by `weight`. Separator positions are recorded for
  rendering.
- **Box**: subtracts border and padding, then gives the content rectangle to
  its child.

Only widgets with explicit `flex` stretch. This avoids the common surprise
where every child grows because it happens to be a container. `ui.spacer()` is
the explicit flexible gap.

Preferred sizes are computed recursively and consult the available axis where
needed (text wrapping, list content, table intrinsic columns, popup content).
Layout is recomputed whenever `layout_dirty` is set or the core size changes.

### Rendering and clipping

Every widget renders through a `Canvas` whose origin is the widget's absolute
position and whose size is its rectangle. `Canvas:put/fill` apply the canvas
clip in the C core before drawing, so a widget cannot paint outside its
allocation even if its drawing math is wrong. This is the main correctness
invariant of the toolkit. Composite widgets create child canvases with
`Canvas:sub()`, which intersects the child rectangle with the parent clip; this
keeps deeply nested and scrolled content (see `ui.viewport`) inside every
ancestor rectangle.

The app paints in this order: tabline, body, statusline, command line, then
popups bottom-to-top. Popup assets are not shadowed; the popup fill and border
provide separation. The command line is part of the main canvas; focused
inputs place a real terminal cursor through `app:set_cursor()`.

`hl.get(style)` resolves a group or table to `fg, bg, attrs` and caches string
and table styles. `Normal` is the implicit base for every style. Unknown group
names warn once and fall back to the base style.

### Focus

`find_focusables()` walks visible children depth-first. `ensure_focus()` keeps
the current widget if it is still focusable, otherwise selects the first one.
Tab/Shift-Tab cycle. Directional focus compares widget centers: a candidate
must be on the requested side, and the score is primary distance plus twice
the cross-axis distance. This gives Neovim-like behavior without a window
manager.

Input is dispatched to the focused widget and then up through its ancestors,
which lets containers intercept keys before global bindings. Mouse input hit
tests the tree in reverse draw order, focuses the nearest focusable ancestor on
press, and bubbles from the hit widget. Popups get priority and can close on
Escape or outside clicks.

### Popups

An overlay stores the widget, requested size/position, focus to restore,
closing policy and optional `on_close`. Popups are laid out centered (or at
explicit coordinates), clamped to the screen, and stacked. Key input reaches
the focused widget inside the top popup; mouse input outside the popup may
close it. Closing restores focus if the previous widget is still attached and
focusable, otherwise focuses the next popup or the body.

### Commands and bindings

Bindings are matched by canonical key names from `keys.parse()`. Commands are
stored by name and executed with the text after the first whitespace.
Command-line editing mirrors the Input widget's UTF-8 cursor logic, with
history and tab completion. Bindings, commands, timers and close callbacks run
under `App:protect`, so a Lua error is reported without unwinding the event
loop.

### Plugins

The launcher (`src/main.c`) resolves `--plugin` paths against the working
directory, `TUIPRO_RUNTIME`, and the installed tree, adds the plugin directory
to `package.path`, and executes the chunk. The Lua plugin loader in
`plugins.lua` provides the same resolution for programmatic use, plus
`discover`, `load`, `install` and `load_all`.

A plugin is intentionally just a Lua chunk. It can return a function, a table
with `setup`/`mount`/`commands`/`bindings`, or any value it likes. This keeps
the loading contract small and lets plugins act as either entry points or
libraries.

## Process and runtime paths

The executable finds the Lua tree in this order:

1. `$TUIPRO_RUNTIME` and `$TUIPRO_RUNTIME/lua`.
2. `<exe_dir>/../share/tuipro/lua` (relocatable install).
3. `<exe_dir>/../lua` and `<exe_dir>/lua` (build tree layouts).
4. `<cwd>/lua` (development checkout).
5. The compile-time `TUIPRO_LUA_DIR` (default
   `/usr/local/share/tuipro/lua`).

Plugin resolution uses `<cwd>/plugins`, `$TUIPRO_RUNTIME/plugins`, and the
tree's own `plugins/` directory (derived from the Lua module path) for the
installed layout. CMake installs the executable, Lua tree, examples, plugins,
header, README, LICENSE and docs.

The launcher supports scripts, module names (`require` + entry point), `-e`
code, `-l` modules, the built-in demos and plugins. A script returning a number
becomes the process exit status; errors print a traceback and exit nonzero.

## Testing

Tests are plain Lua scripts run by the real executable in headless mode:

```sh
./build/tuipro --headless --size 100x30 tests/test_widgets.lua
```

`tests/helper.lua` provides assertions, app/event helpers, headless rendering
and snapshot assertions. Tests end with `return H.finish()`, which prints a
summary and returns a nonzero status on failure.

`tests/run.sh` runs every `tests/test_*.lua` file, CLI smoke checks (demo,
plugin, exit codes), all examples, and a Python PTY smoke test. The PTY test
verifies alternate-screen entry, raw mode, mouse/paste modes, keyboard input,
SIGWINCH resize and clean restoration on exit. Set `TUIPRO_PTY=0` to skip it or
`TUIPRO_BIN` to point at another build. CTest runs `tests/run.sh` with the
target binary.

Headless tests can inspect exact cell contents:

```lua
local rows = core.snapshot()        -- trailing blanks trimmed
local full = core.rows()            -- fixed-width rows
local text, fg, bg, attrs, w = core.cell(x, y)
```

## Design notes and limits

- **No ncurses.** ANSI/VT output keeps dependencies to LuaJIT and libc/POSIX.
- **Line-run diffing.** Simple and predictable; very large sparse changes may
  emit more escapes than a minimal-edit renderer. A full redraw is always
  available by invalidating the grid.
- **Single global terminal.** The C core is process-wide. Multi-app support
  would require per-app cores or a terminal multiplexer layer.
- **No text shaping.** Width handles wide and combining codepoints, but there
  is no grapheme cluster segmentation, bidi or font shaping.
- **Alternate screen only.** The app does not support running inline in the
  normal screen buffer.
- **Lua 5.1/LuaJIT API.** The code deliberately avoids newer syntax and uses
  the `bit` library.
- **POSIX terminal I/O.** Windows terminals are not supported by the current
  backend.
- **Plugins are trusted Lua.** There is no sandbox; loading a plugin executes
  arbitrary code with full process access.
