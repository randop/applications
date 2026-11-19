# Lua API

`tuipro` embeds LuaJIT 2.1 and exposes the terminal through a small C core. All
application code is plain Lua 5.1 (the LuaJIT dialect): use `unpack` instead of
`table.unpack`, and the bundled `bit` library instead of Lua 5.3 bitwise
operators.

```lua
local tuipro = require("tuipro")
local ui = tuipro.ui

local app = tuipro.App.new({ title = "Hello" })
app:set_body(ui.text("Hello, terminal!"))
app:bind("q", function(a) a:quit() end)
app:run()
```

When a script returns a number, the launcher exits with that status. This makes
test scripts easy to run:

```lua
return 0 -- success
```

## Modules

| Module | Purpose |
| --- | --- |
| `tuipro` | Public front-end and convenience constructors. |
| `tuipro.core` | Embedded C core: grid, terminal, polling, snapshots. |
| `tuipro.app` | `App` class: event loop, layout, focus, popups, bindings. |
| `tuipro.widget` | `Widget` base class. |
| `tuipro.container` | `Container`, `VBox`, `HBox`, `Padding`, `Center`, `Stack`, `Split`. |
| `tuipro.canvas` | Clipped drawing surface. |
| `tuipro.ui` | Widget constructors (`ui.text`, `ui.list`, ...). |
| `tuipro.hl` | Highlight groups and style resolution. |
| `tuipro.theme` | Default color palette and group definitions. |
| `tuipro.keys` | Key notation parsing, normalization and pretty-printing. |
| `tuipro.util` | UTF-8, width, wrapping, path and generic helpers. |
| `tuipro.cli` | Command-line options exposed by the launcher. |
| `tuipro.plugins` | Plugin discovery and installation. |
| `tuipro.http` | `fetch`, `Promise`, `Headers`, `AbortController`, `AbortSignal`. |
| `tuipro.http_native` | Low-level libuv/OpenSSL request binding. |
| `tuipro.json` | Small dependency-free JSON encoder/decoder. |
| `tuipro.demo` | Built-in dashboard (`tuipro --demo`). |
| `tuipro.demo_coding` | Built-in coding demo (`tuipro --demo-coding`). |
| `tuipro.demo_api` | Built-in HTTP client demo (`tuipro --demo-api`). |

## `require("tuipro")`

| Field | Description |
| --- | --- |
| `version`, `_VERSION` | Version string (`"0.1.0"`). |
| `core` | The `tuipro.core` module. |
| `cli` | The `tuipro.cli` module. |
| `ui` | Widget constructors. |
| `App` | Application class. |
| `Widget`, `widget` | Base widget class. |
| `container` | Container classes and `parse_padding`. |
| `canvas` | Canvas class. |
| `hl`, `highlight` | Highlight API. |
| `theme` | Default theme. |
| `keys` | Key helpers. |
| `util` | Utility helpers. |
| `plugins` | Plugin loader. |
| `http` | HTTP client module (`fetch`, `Promise`, ...). |
| `json` | JSON codec (`tuipro.http.json` is the same table). |
| `fetch` | Global-style `fetch(url, opts)` shorthand for `http.fetch`. |

Convenience functions:

| Function | Description |
| --- | --- |
| `tuipro.app(opts)` | Create an `App`; calls `opts.setup(app)` before returning. |
| `tuipro.application(opts)` | Alias for `tuipro.app`. |
| `tuipro.run(opts)` | Create an app, run it and return it. |
| `tuipro.demo(opts)` | Run the built-in dashboard. |
| `tuipro.demo_coding(opts)` | Run the built-in coding demo. |
| `tuipro.demo_api(opts)` | Run the built-in HTTP client demo. |
| `tuipro.fetch(url, opts)` | Start an HTTP(S) request; returns a `Promise`. |
| `tuipro.info()` | `core.info()` when initialized, otherwise `{}`. |
| `tuipro.quit()` | Request exit from the core event loop. |

`tuipro.app` and `tuipro.run` differ from `tuipro.App.new`: only the
convenience functions call `opts.setup`.

## Application

### Creating an application

```lua
local app = tuipro.App.new({
  title = "My app",                 -- terminal title
  width = 80, height = 24,          -- headless size (terminal size otherwise)
  fps = 60,                         -- maximum redraw rate
  frames = 1,                       -- stop after N rendered frames
  headless = false,                 -- no terminal I/O
  mouse = true,                     -- mouse reporting
  truecolor = true,                 -- nil = auto-detect
  background = "Normal",            -- style used to clear the root; false disables
  cmdline = true,                   -- show the bottom command/message line
  statusline = nil,                 -- nil = default, false = hidden, or widget
  tabline = nil,                    -- nil = hidden, or widget
  footer_hint = "? help   : command   C-q quit",
  command_prompt = ":",
  body = widget,                    -- initial root widget (default empty VBox)
  children = { ... },               -- appended to the root
  focus = widget,                   -- initial focus
  bindings = { ["q"] = fn, ["x"] = { fn, "description" } },
  commands = { hello = fn },        -- callback(arg, app)
})
```

`App.new` sets up a default statusline that displays mode, title, focused
widget label and `footer_hint`. Pass `statusline = false` to hide it, or a
widget to replace it. The command line is one row at the bottom; hide it with
`cmdline = false`.

### Structure

| Method | Description |
| --- | --- |
| `app:set_body(widget)` | Replace the root body widget. Detaches the old root. |
| `app:add(child[, opts])` | Add a child to the root (the root must support `add`). |
| `app:set_statusline(widget)` | Set the statusline; `nil` restores the default, `false` hides it. |
| `app:set_tabline(widget)` | Set the tabline; `false` hides it. |
| `app:set_title(title)` | Update the terminal title. |
| `app.root`, `app.tabline`, `app.statusline` | Current widgets. |
| `app.overlays` | Stack of open popups, bottom first. |
| `app.focus` | Currently focused widget or `nil`. |
| `app.mode` | `"normal"` or `"command"`. |
| `app.frames` | Number of frames rendered so far. |
| `app.should_quit` | Mutable quit flag. Prefer `app:quit()`. |

### Running and rendering

| Method | Description |
| --- | --- |
| `app:run()` | Enter the event loop. Initializes the core if needed and shuts it down on exit. |
| `app:render()` | Lay out and render exactly one frame. Leaves the core initialized. |
| `app:layout([w, h])` | Recompute layout, optionally updating the size first. |
| `app:resize(w, h)` | Change the size and resize the core when initialized. |
| `app:shutdown()` | Cancel timers and shut down the core if the app owns it. |
| `app:invalidate()` | Request a redraw. |
| `app:layout_invalidate()` | Request a layout pass and redraw. |

`App:render()` is the headless workhorse: it creates the core when necessary,
lays out chrome and body, renders the body, statusline, command line and
popups, flushes the diff, then returns the app. Call `app:shutdown()` when you
are done. `App:run()` performs that cleanup itself.

```lua
local app = tuipro.App.new({ headless = true, width = 60, height = 16 })
app:set_body(tuipro.ui.text("snapshot"))
app:layout()
app:render()
local rows = tuipro.core.snapshot()
app:shutdown()
```

### Focus

| Method | Description |
| --- | --- |
| `app:set_focus(widget)` | Focus a widget, firing `on_blur`/`on_focus`. |
| `app:focus_next(dir)` | Cycle focus; `dir` is `1` (default) or `-1`. |
| `app:focus_direction(dir)` | Move to the nearest widget in `"left"`, `"right"`, `"up"` or `"down"`. |
| `app:focusables([scope])` | List focusable widgets in the current scope. |
| `app:ensure_focus()` | Keep focus valid, or focus the first candidate. |
| `app:current_scope()` | Top popup widget, or the root body. |
| `widget:focus()` | Ask the app to focus this widget. |

Focus traversal is depth-first in child order. Directional focus scores
candidate centers by primary distance plus twice the cross-axis distance.
`Tab` and `Shift-Tab` cycle focus unless a widget consumes the key. `C-w` is a
prefix: `C-w h/j/k/l` moves directionally and `C-w w` cycles. The prefix expires
after 900 ms.

### Events

Applications receive events from `core.poll()` and dispatch them:

```lua
app:handle_event({ type = "key", key = "enter", mods = {} })
app:handle_event({ type = "mouse", action = "press", button = "left",
                   x = 4, y = 2, mods = {} })
app:handle_event({ type = "paste", text = "pasted text" })
app:handle_event({ type = "resize", width = 100, height = 30 })
app:handle_event({ type = "signal", name = "INT" })
app:handle_event({ type = "focus", focused = true })
```

Key dispatch order:

1. Command-line editing when `app.mode == "command"`.
2. `Esc` closes the top popup when `close_on_esc` is true.
3. A pending `C-w` window prefix.
4. The focused widget, then each ancestor, until one returns `true`.
5. The global `C-w` prefix.
6. Global key bindings registered with `app:bind`.
7. `Tab` / `Shift-Tab` focus cycling.

Mouse dispatch gives the top popup priority. Clicks outside a popup close it
when `close_on_click_outside` is true. Otherwise the tabline, statusline,
command line and body are tested in that order. A press focuses the nearest
focusable ancestor of the hit widget, then the event bubbles from the hit
widget up through its ancestors.

### Bindings

```lua
app:bind("C-s", function(app, ev)
  app:success("saved")
end, "Save")

app:unbind("C-s")
```

Callbacks receive `(app, event)`. Bindings use canonical key names from
`tuipro.keys`; `<C-w>`, `ctrl-w`, `C-w` and `C-W` are equivalent. `app:bind`
replaces an existing binding with the same canonical name.

Default bindings:

| Key | Action |
| --- | --- |
| `C-q` | Quit. |
| `F1`, `?` | Show help. |
| `:` | Enter the command line. |
| `C-l` | Invalidate and redraw. |
| `Esc` | Close the top popup, or clear the current message. |
| `Tab` / `S-Tab` | Next / previous focus. |
| `C-w h/j/k/l` | Directional focus. |
| `C-w w` | Cycle focus. |

### Commands

```lua
app:command("greet", function(arg, app)
  app:info("hello " .. arg)
end, "Greet someone")
```

Callbacks receive `(argument_string, app)`. Returning a non-empty string shows
it as an info message. Built-in commands: `:q`, `:quit`, `:qa`, `:qall`,
`:help`, `:echo`, `:redraw`, `:version`, `:messages` and `:commands`.
`:redraw` requests a full layout pass.

| Method | Description |
| --- | --- |
| `app:enter_command([prompt])` | Start editing the command line. |
| `app:exit_command()` | Leave command mode. |
| `app:execute_command(line)` | Run a command line directly. |
| `app:show_help([filter])` | Open a help popup; `filter` may be `"keys"` or `"commands"`. |
| `app:show_messages()` | Open the message history in a popup. |

The command line supports UTF-8 editing, history (`Up`/`Down`), `C-a`/`C-e`,
`C-b`/`C-f`, `C-u`, `C-k`, `C-w`, `C-d`, `Backspace`, `Delete`, and `Tab`
completion. `Enter` executes, `Esc` cancels, and `C-q` quits. Pasted text has
control characters removed and newlines/tabs converted to spaces.

### Messages

```lua
app:info("Loaded")             -- default 4 s
app:success("Saved")           -- 4 s
app:warn("Disk almost full")   -- 6 s
app:error("Failed")            -- 8 s, also written to stderr
app:set_message(text, level, timeout_ms)
app:clear_message()
```

`app.message` is `{ text, level, expires }`; `app.message_history` keeps the
last 100 messages. Levels are `"info"`, `"warn"`, `"error"` and `"success"`.
`app.message_style()` returns the highlight group used for the message line.

### Timers

```lua
local once = app:after(500, function() app:info("later") end)
local tick = app:every(1000, function() ... end)
app:cancel(tick)
```

Durations are milliseconds. A repeating timer reschedules from the current
time after each run. Cancel by setting `timer.active = false` or calling
`app:cancel(timer)`. Timers are processed in the event loop and can call
`invalidate()` freely.

### Errors

`app:protect(fn, ...)` calls a function with a traceback-protected `xpcall` and
returns a boolean. On failure it calls `app:report_error(err)`, which increments
`app.error_count`, stores `app.last_error`, prints the first line to stderr,
invokes `app.on_error(app, err)` when set, shows an error message, and quits
after more than 40 errors. Bindings, timers and popup close callbacks go through
this protection.

### Popups

```lua
local popup = ui.popup({
  title = "Confirm",
  width = 40,
  height = 10,
  content = ui.text("Delete the file?"),
  buttons = {
    { label = "Cancel" },
    { label = "Delete", danger = true, on_press = function(p) ... end },
  },
})

app:open_popup(popup, {
  width = 40,
  height = 10,
  x = nil, y = nil,                -- nil = centered
  focus = nil,                     -- default: first focusable in the popup
  close_on_esc = true,
  close_on_click_outside = false,
  on_close = function(widget) ... end,
})
```

`open_popup` returns an overlay table. Popups are stacked; input goes to the
topmost overlay first. Closing restores the previously focused widget when
possible. `popup:close()` closes itself, and `app:close_popup(widget)` or
`app:close_all_popups()` close programmatically. Overlay sizes default to the
widget's preferred size, clamped to the screen. A popup's buttons receive the
popup as their `on_press` argument.

### Cursor

`app:set_cursor(x, y[, shape])` places the terminal cursor at 0-based screen
coordinates. Shapes are `"block"`, `"underline"` / `"under"` and `"bar"` /
`"line"`. Widgets should use `Widget:set_cursor()` with coordinates relative to
the window; it adds nothing automatically, so pass absolute screen cells.
`App:render()` clears the cursor request at the start of each frame.

## HTTP client (`fetch`)

The native client uses libuv for DNS and TCP and OpenSSL for TLS. It speaks
HTTP/1.1 over HTTP or HTTPS, follows redirects, decodes chunked and
connection-close bodies, and decompresses gzip/deflate when zlib is available.
Requests and response bodies are buffered in memory, so this API targets API
calls and small-to-medium payloads rather than streaming downloads.

```lua
local tuipro = require("tuipro")

local res = tuipro.fetch("https://httpbin.org/json"):await()
print(res.status, res.ok, res.headers:get("content-type"))
print(res:text():await())

local data = tuipro.fetch("https://httpbin.org/json"):await():json():await()
print(data.slideshow.title)
```

`fetch` is also installed as a global, matching browsers and Node.js. In an
application the event loop drives requests automatically, so callbacks are the
natural style:

```lua
fetch(url)
  :then_(function(res) return res:json() end)
  :then_(function(data)
    app:set_message(data.title)
    app:invalidate()
  end)
  :catch(function(err)
    app:error(tostring(err))
  end)
```

Nothing blocks the UI: the terminal poll and the network loop share the same
libuv wait, and promise callbacks run from the normal app loop.

### `fetch(url, opts)`

Returns a `Promise` that resolves to a `Response`. The URL must use `http://`
or `https://`.

| Option | Default | Meaning |
| --- | --- | --- |
| `method` | `"GET"` | HTTP method. |
| `headers` | `nil` | Table, `Headers`, or array of `{ name, value }` pairs. |
| `body` | `nil` | String request body; rejected for GET and HEAD. |
| `redirect` | `"follow"` | `"follow"`, `"manual"` or `"error"`. |
| `max_redirects` | `32` | Redirect limit (`0` disables following). |
| `timeout_ms` | `30000` | Whole-request timeout; `0` disables it. |
| `reject_unauthorized` | `true` | Verify TLS certificates. `rejectUnauthorized` is accepted too. |
| `decompress` | `true` | Decode `Content-Encoding: gzip`/`deflate`. |
| `signal` | `nil` | An `AbortSignal` that cancels the request. |

The client adds `Host`, `User-Agent: tuipro/0.1.0`, `Accept: */*`,
`Accept-Encoding: gzip, deflate`, `Connection: close` and `Content-Length`
unless the caller supplied them. Redirects to a different origin drop
`Authorization`, `Cookie`, `Proxy-Authorization` and `Host` headers. The
timeout covers the entire redirect chain.

### Promises

| Method | Description |
| --- | --- |
| `then_(on_fulfilled, on_rejected)` | Chain a handler; returns a new promise. |
| `catch(on_rejected)` | Shorthand for `then_(nil, fn)`. |
| `finally(fn)` | Run `fn` on either outcome. |
| `next(timeout_ms)` | Pump the native loop until settled. Returns `state, value` (`"pending"` on timeout). |
| `await(timeout_ms)` | Pump until settled, then return the value or raise the rejection. |
| `abort()` | Abort the underlying request, if any. |
| `Promise.resolve(v)`, `Promise.reject(e)`, `Promise.all(list)` | Standard helpers. |

`await()` is convenient in scripts and tests. Do not call it from inside a
fetch callback: the loop is already running. Use `then_` there instead.

### `Response`

| Field / method | Description |
| --- | --- |
| `status`, `statusText`, `ok` | HTTP status information. |
| `url`, `redirected`, `type` | Final URL and redirect state. |
| `headers` | A `Headers` object. |
| `header(name)` | Shorthand for `headers:get(name)`. |
| `text()` | Promise for the decoded body string. |
| `json()` | Promise for the body decoded with `tuipro.json`. |
| `arrayBuffer()`, `bytes()` | Promise for the binary-safe body string. |

### `Headers`

`Headers.new(init)` accepts a table, another `Headers`, or an array of
`{ name, value }` pairs. Names are case-insensitive.

| Method | Description |
| --- | --- |
| `get(name)` | Joined value (`", "`), or `nil`. |
| `getAll(name)` | Array of values. |
| `has(name)` | Boolean. |
| `append(name, value)` | Add a value without replacing existing ones. |
| `set(name, value)` | Replace all values for a name. |
| `delete(name)` | Remove a name. |
| `entries()` / `each()` | Iterator over `name, value` pairs. |
| `keys()`, `values()` | Iterators over names or values. |
| `forEach(fn)` | Call `fn(value, name, headers)`. |
| `to_table()` | Plain table with joined values. |

Invalid names or values (control characters, newlines) raise an error.

### `AbortController`

```lua
local controller = require("tuipro.http").AbortController.new()
local promise = fetch(url, { signal = controller.signal })
controller:abort()          -- rejects with AbortError
```

`signal.aborted` and `signal.reason` are available; `addEventListener`,
`removeEventListener` and the `onabort` field are supported.

### Errors

Failures reject with a `FetchError` carrying `name`, `message` and `code`.
Names include `TypeError` (network/protocol/validation failures),
`AbortError` and `TimeoutError`. `tostring(err)` yields
`"Name: message"`.

### JSON

`tuipro.json` provides `encode(value)`, `decode(text)` and the `null` sentinel.
Encoding sorts object keys and rejects cycles, non-string keys and non-finite
numbers. Arrays are detected from consecutive integer keys; an empty table
encodes as `[]`. Decoding rejects trailing data and malformed escapes, and
represents JSON `null` with `json.null` (object members set to null are
omitted, array slots keep the sentinel).

## Core (`tuipro.core`)

The C core is a process-wide singleton. Coordinates are 0-based; colors are
`0xRRGGBB` numbers, with `-1`/`nil` meaning the terminal default.

```lua
local core = require("tuipro.core")

core.init({
  headless = true,
  width = 80, height = 24,
  truecolor = true,
  mouse = false,
  paste = true,
  title = "app",
})
```

| Function | Returns / description |
| --- | --- |
| `core.init(opts)` | Initialize the terminal or headless grid. |
| `core.shutdown()` | Restore the terminal and mark the core inactive. |
| `core.initialized()` | Whether the core is active. |
| `core.size()` | `width, height`. |
| `core.put(x, y, text[, fg, bg, attrs])` | Draw text; returns the x coordinate after the last cell. |
| `core.fill(x, y, w, h, text[, fg, bg, attrs])` | Fill a rectangle. |
| `core.clip(x, y, w, h)` / `core.clip_reset()` | Set or clear the clipping rectangle. |
| `core.clear()` | Blank the back buffer. |
| `core.flush()` | Diff the back buffer against the front buffer and write the terminal changes. |
| `core.cursor(x, y[, visible, shape])` | Position/show the cursor; `-1, -1` hides it. |
| `core.rows()` | All rows, preserving trailing blanks. |
| `core.snapshot()` | All rows with trailing blanks trimmed. |
| `core.cell(x, y)` | `text, fg, bg, attrs, width`, or `nil` outside the grid. |
| `core.text_width(text)` | Display width in cells (UTF-8, wide chars, combining marks). |
| `core.poll(timeout_ms)` | Next event, or `nil` on timeout/EOF. `-1` blocks. |
| `core.exit()` / `core.should_exit()` | Request/test process exit. |
| `core.title(text)` | Set the terminal title. |
| `core.bell()` | Ring the terminal bell. |
| `core.copy(text)` | Copy text through OSC 52 when supported. |
| `core.set_mouse(bool)` / `core.set_paste(bool)` | Toggle mouse / bracketed paste reporting. |
| `core.ticks()` | Monotonic milliseconds. |
| `core.resize(w, h)` | Resize the grid directly. |
| `core.info()` | `{ version, lua_version, truecolor, width, height, term, term_program }`. |

`core.version` and `core.engine` (`"LuaJIT"`) are available as fields.

### Event tables

`core.poll()` returns tables shaped like these:

```lua
{ type = "key", key = "a", text = "a",
  mods = { ctrl = false, alt = false, shift = false, super = false, value = 0 } }

{ type = "mouse", button = "left", action = "press", x = 4, y = 2, mods = { ... } }

{ type = "resize", width = 100, height = 30 }

{ type = "paste", text = "..." }

{ type = "signal", name = "INT" }

{ type = "focus", focused = true }
```

Mouse actions are `"press"`, `"release"`, `"move"` and `"wheel"`; buttons are
`"left"`, `"middle"`, `"right"`, `"wheel-up"`, `"wheel-down"`, `"wheel-left"`
and `"wheel-right"`. Key names are canonical lowercase names such as `"a"`,
`"up"`, `"f1"`, `"enter"`, `"esc"`, `"space"`, `"tab"`, `"backspace"` and
`"delete"`. Printable keys also carry UTF-8 `text`.

## Keys

`tuipro.keys` normalizes user notation into canonical names. Modifiers are `C`
(Ctrl), `M` (Alt/Meta), `S` (Shift) and `D` (Super/Cmd), in that order.

```lua
local keys = require("tuipro.keys")

keys.parse("ctrl-shift-left").name   -- "C-S-left"
keys.parse("<C-w>").name             -- "C-w"
keys.name(ev)                        -- canonical name for an event
keys.match("C-q", ev)                -- boolean
keys.pretty("C-S-left")              -- "Ctrl-Shift-Left"
```

Named keys include `esc`, `enter`, `tab`, `backtab`, `space`, `backspace`,
`delete`, `insert`, `up`, `down`, `left`, `right`, `home`, `end`, `pageup`,
`pagedown`, `begin` and `f1`–`f24`. Single punctuation characters are accepted
directly. Uppercase letters parse as `S-<letter>`.

## Highlights

A style is a group name, a table, an inline derived style, or `nil` for
`Normal`. Colors may be `#rrggbb` strings, theme palette names (`"accent"`,
`"red"`, ...), decimal numbers or 24-bit integers. `false`, `"default"` and
`"none"` mean the terminal default. Attributes are `bold`, `dim`, `italic`,
`underline`, `blink`, `reverse`, `strike` and `overline`.

```lua
local hl = require("tuipro.hl")

hl.define("WarningBox", {
  fg = "yellow",
  bg = "surface",
  bold = true,
})

hl.define("WarningTitle", { link = "WarningBox", italic = true })

local inline = hl.derive("ListItem", { fg = "#ff8800" })
hl.get("Normal")        -- fg, bg, attrs
hl.style(inline)        -- { fg = ..., bg = ..., attrs = ... }
hl.attrs("Error")       -- attribute bitmask
hl.names()              -- sorted group names
```

`hl.define` caches nothing; `hl.get` caches resolved string and table styles, so
reuse style tables rather than rebuilding them every frame when possible.
`hl.DEFAULT` is `-1`. The default theme is `tuipro.theme`; override individual
groups or replace `hl.palette` entries before defining groups to recolor the
whole UI. Unknown group names warn once on stderr and resolve as the current
base style.

### Transparency

By default the theme paints neutral surfaces (`Normal`, `NormalFloat`, borders,
menus, lists, inputs, buttons, statusline, tabline) with opaque dark colors.
Enable terminal transparency so those backgrounds fall through to the
terminal's own background:

```lua
hl.set_transparent(true)   -- neutral surfaces become hl.DEFAULT
hl.set_transparent(false)  -- restore the opaque theme
```

Colored backgrounds are kept, so focus rings, selected rows, active tabs and
badges stay visible. The setting also affects named and inline styles and can
be toggled at any time; resolved-style caches are discarded. The bundled demos
call `hl.set_transparent(true)` by default; pass `{ transparent = false }` to
`tuipro.demo.run()` or `tuipro.demo_coding.run()` for the opaque look.
Individually, a widget background can be made transparent with `bg = false`
(or `"default"` / `"none"`).

## Plugins

A plugin is a Lua chunk that returns a function or a table.

```lua
-- plugins/hello/init.lua
local M = {}

function M.setup(tuipro, cli, app)
  app:command("hello", function(_, a)
    a:success("Hello from hello")
  end, "Say hello")
end

function M.mount(app)
  -- adjust the app's widget tree
end

M.commands = {
  wave = { function(_, a) a:info("wave") end, "Wave" },
}

M.bindings = {
  ["C-h"] = { function(a) a:info("hotkey") end, "Hotkey help" },
}

return M
```

Function entries are called as `f(tuipro, cli)`. Table entries may expose:

| Field | Called as | Purpose |
| --- | --- | --- |
| `setup` | `setup(tuipro, cli, app)` | Register commands/bindings before mounting. |
| `mount` | `mount(app)` | Modify the application. |
| `commands` | `fn(arg, app)` | Added with `app:command`. |
| `bindings` | `fn(app, ev)` | Added with `app:bind`. |

`tuipro.plugins.load(name[, tuipro])` resolves and loads a plugin. It returns
the entry plus the resolved path, or `nil` plus an error. Table entries are
returned without being installed. `tuipro.plugins.install(entry, app[, tuipro])`
applies a loaded entry. `tuipro.plugins.load_all(dir, app[, tuipro])` discovers
and installs every plugin under a directory.

Resolution order for a name:

1. `name/init.lua`, then `name` itself.
2. `plugins/<name>` relative to the current directory.
3. `$TUIPRO_RUNTIME/plugins/<name>` and `$TUIPRO_RUNTIME/<name>`.
4. Each directory in `$TUIPRO_PLUGIN_PATH` (colon-separated).
5. `require(name)` as a final fallback.

The `--plugin` launcher resolves direct paths and directories plus the same
development and installed plugin roots, and adds the plugin's directory to
`package.path` before executing it. Installed plugins live under
`share/tuipro/plugins`, next to the installed Lua tree.

## Writing widgets

```lua
local Widget = require("tuipro.widget")

local Badge = Widget:extend("Badge")

function Badge.new(opts)
  opts = opts or {}
  local self = setmetatable({}, Badge)
  Widget.init(self, opts)
  self.label = opts.label or ""
  self.height = 1
  return self
end

function Badge:render(canvas)
  local style = self.focused and "ButtonFocus" or "Badge"
  canvas:fill(0, 0, self.w, 1, " ", style)
  canvas:text(1, 0, self.label, self.w - 2, style, "center")
end
```

`Widget:extend(name)` also installs a default `.new(opts)` that calls
`cls.init(self, opts)`, so subclasses without a custom constructor work as
long as `init` is defined.

Base fields set by `Widget:init`:

| Field | Meaning |
| --- | --- |
| `x`, `y`, `w`, `h` | Absolute screen geometry (0-based cells). |
| `width`, `height` | Requested sizes used by layout. |
| `flex` | Share of leftover space on the main axis. |
| `padding` | Optional padding metadata (widget-specific). |
| `visible` | Hidden widgets are skipped by layout and input. |
| `focusable`, `focused`, `contains_focus` | Focus state. |
| `parent`, `app` | Tree links. |
| `name`, `hl`, `tooltip` | Metadata. |

Base methods:

| Method | Description |
| --- | --- |
| `widget:resize(x, y, w, h)` | Set geometry and call `layout(w, h)`. |
| `widget:layout(w, h)` | Lay out children (containers implement this). |
| `widget:render(canvas)` | Draw; leaf widgets must implement it. |
| `widget:preferred_width(avail)` / `preferred_height(avail)` | Intrinsic size. |
| `widget:set_app(app)` / `widget:detach()` | Attach/detach the widget tree. |
| `widget:invalidate()` / `invalidate_layout()` | Request repaint / relayout. |
| `widget:handle(ev)` | Dispatch to `on_key`, `on_mouse`, `on_paste`, `on_resize`. |
| `widget:find_widget_at(x, y)` / `find_focusables()` | Hit testing / focus discovery. |
| `widget:contains(x, y)` / `hit(x, y)` | Bounds checks. |
| `widget:is_focusable()` | Focusable, visible and not disabled. |
| `widget:focus()` | Focus through the app. |
| `widget:is_descendant_of(ancestor)` | Tree query. |
| `widget:get_label()` / `label()` | `label` field, then `name`, then class name. |
| `widget:set_cursor(x, y[, shape])` | Ask the app to show the cursor. |

Event hooks return `true` to consume an event:

```lua
function Badge:on_key(ev) ... end
function Badge:on_mouse(ev) ... end
function Badge:on_paste(ev) ... end
function Badge:on_resize(ev) ... end
function Badge:on_focus() ... end
function Badge:on_blur() ... end
function Badge:on_attach(app) ... end
function Badge:on_detach(app) ... end
```

See [widgets.md](widgets.md) for the complete widget reference and
[architecture.md](architecture.md) for how layout, rendering and input fit
together.

## Canvas

A canvas is a clipped drawing surface with local coordinates. Widgets receive a
canvas whose origin is their own top-left corner. All writes are clipped to the
canvas rectangle.

```lua
local Canvas = require("tuipro.canvas")
local canvas = Canvas.new(x, y, w, h)

canvas:put(lx, ly, "text", "Title")
canvas:fill(lx, ly, w, h, " ", "NormalFloat")
canvas:text(lx, ly, "fit me", maxw, "Muted", "center")
canvas:hline(lx, ly, w, "─", "Border")
canvas:vline(lx, ly, h, "│", "Border")
canvas:spans(lx, ly, { { text = "a", hl = "Accent" },
                       { text = "b", hl = "Muted" } }, maxw, "Normal")
canvas:put_right(ly, "right", "Muted", 1)
canvas:clear("Normal")
```

`Canvas:text` pads and truncates with `util.fit`; `spans` draws styled runs and
stops at `maxw`. `Canvas:put` returns nothing; use `util.width` for layout
math. The underlying C core has a single clip rectangle, so canvases apply
their clip immediately before each write.

## CLI module

`tuipro.cli` mirrors the launcher options and environment:

| Field | Meaning |
| --- | --- |
| `headless` | Headless mode (also `TUIPRO_HEADLESS`). |
| `width`, `height` | From `--size` or `TUIPRO_WIDTH` / `TUIPRO_HEIGHT`; defaults 80×24. |
| `fps` | Redraw rate; default 60. |
| `frames` | Frame limit; `0` means unlimited. |
| `truecolor` | `nil` auto, `false` or `true`. |
| `mouse`, `paste` | Input features. |
| `plugin`, `script` | Selected entry point. |
| `demo`, `demo_coding`, `demo_api` | Built-in demo selection. |
| `args` | Script arguments after the entry point. |
| `is_headless()` | Boolean helper. |

See the README for the full command-line and environment reference.

## LuaJIT and portability notes

- The embedded runtime is LuaJIT 2.1 implementing Lua 5.1. Avoid `goto`,
  integer division (`//`), bitwise operators and `table.unpack`.
- Use `unpack` (or `local unpack = unpack or table.unpack`) and
  `require("bit")`.
- Lua strings are byte strings. Cursor positions in `Input` and the command
  line are 1-based byte indices with `#s + 1` as the end position. Use
  `tuipro.util` helpers for UTF-8 editing and `util.width` for display width.
- Only POSIX systems with `poll`, `termios` and `ioctl` are supported by the
  terminal backend.
- The C core is global to the process: one active terminal/grid at a time.
  Creating a second app while the core is initialized reuses that core.
