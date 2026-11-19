# tuipro

`tuipro` is a terminal UI container in the spirit of Neovim's TUI: a small C
core owns the terminal and a diffing cell grid, while LuaJIT builds
applications, layouts, widgets, plugins and scripts on top.

- ANSI/VT output, no ncurses.
- Double-buffered cell grid with dirty-region diffing, truecolor and 256-color
  fallback, wide characters and combining marks.
- Raw mode, alternate screen, mouse, bracketed paste, focus events and resize
  handling.
- Lua widget toolkit: boxes, lists, menus, tables, inputs, tabs, buttons,
  progress bars, spinners, popups, separators, scrollable viewports,
  scrollbars and status lines.
- Optional terminal-transparent surfaces: neutral panels and controls use the
  terminal background while focus and selection colors stay visible. The
  bundled demos enable this by default.
- Focus traversal, window-style directional focus, modal popups, command line,
  timers, key bindings and commands.
- Native HTTP/HTTPS client (`fetch`) built on libuv and OpenSSL, with promises,
  redirects, abort support and gzip/deflate decoding.
- Headless mode for snapshots, scripts and CI.
- LuaJIT 2.1 / Lua 5.1 API only (no Lua 5.3 syntax).

The project is intentionally small enough to read in one sitting. Start with
[examples/minimal.lua](examples/minimal.lua), then try the dashboard:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
./build/tuipro examples/dashboard.lua
```

## Requirements

- Linux (or another POSIX system with `poll`, `termios` and `ioctl`).
- C11 compiler and CMake 3.16 or newer.
- LuaJIT 2.1 development files (`luajit`, `lua.h`, `libluajit-5.1`).
  If pkg-config cannot find LuaJIT, point CMake at a source checkout:

  ```sh
  cmake -S . -B build -DTUIPRO_LUAJIT_ROOT=/path/to/LuaJIT
  cmake --build build -j
  ```

- libuv development files and OpenSSL headers/libraries.
- zlib is optional; when found, gzip and deflate response bodies are decoded.
- Python 3 is optional; it is used by the PTY smoke test and as the local
  HTTP/HTTPS fixture server for the fetch tests.

## Building and installing

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
./build/tuipro --help
```

Install with the usual CMake flow:

```sh
cmake --install build --prefix /usr/local
```

The install includes the `tuipro` executable, the Lua runtime under
`share/tuipro/lua`, examples, plugins, the C header and the documentation.
The executable also finds the Lua tree from a development checkout, so you do
not need to install while working on tuipro.

## Command line

```text
tuipro [options] [script.lua | module] [args...]
tuipro --plugin plugins/hello
tuipro --demo
tuipro --demo-coding
tuipro --demo-api
```

Common options:

| Option | Meaning |
| --- | --- |
| `--demo` | Run the built-in dashboard demo. |
| `--demo-coding` | Run the built-in opencode-style coding demo. |
| `--demo-api` | Run the built-in HTTP client (`fetch`) demo. |
| `--plugin PATH` | Run a plugin directory, `init.lua`, or plugin name. |
| `-l, --load MODULE` | Require a module and call its entry point. |
| `-e, --eval CODE` | Evaluate Lua code. |
| `--headless` | No terminal I/O; useful for tests and snapshots. |
| `--size WxH` | Initial size in headless mode (default `80x24`). |
| `--fps N` | Maximum redraw rate (default 60). |
| `--frames N` | Exit after N rendered frames. |
| `--truecolor`, `--no-truecolor` | Force or disable 24-bit colors. |
| `--mouse`, `--no-mouse` | Enable or disable mouse reporting. |
| `--no-paste` | Disable bracketed paste. |

The built-in demo has five pages: Dashboard, Tasks, Widgets, Inputs and
Dialogs. The Dialogs page demonstrates a basic confirm dialog, a scrollable
form with a viewport and scrollbar, and a block-art `example.jpg` preview; all
of them close with Esc.

`--demo-coding` opens a second, self-contained demo that mimics a terminal
coding agent: a session sidebar, a transcript with tool cards and diffs, a
prompt box and a status footer. Sessions switch from the sidebar, `Enter`
submits a prompt and plays a simulated reply, `Ctrl-S` toggles the sidebar,
`Ctrl-P` opens a command palette and `Ctrl-O` starts a new session. Both demos
use terminal transparency by default.

`--demo-api` is a third demo for the native HTTP client. It starts with a
built-in response so it can be rendered without network access; press `Enter`
or click **Send** to fetch the URL in the input field. The response panel shows
the status, headers and body, while the history panel keeps recent requests.
**Abort** cancels an in-flight request. The client is built on libuv and
OpenSSL, supports HTTP and HTTPS, redirects, gzip/deflate and does not block
the UI.

The Lua API is a Node.js-style `fetch`:

```lua
local res = fetch("https://example.com"):await()
print(res.status, res.headers:get("content-type"))
print(res:text():await())

-- Inside an App, promise callbacks run from the normal event loop:
fetch(url):then_(function(r) return r:json() end)
          :then_(function(data) app:set_message(data.title) end)
          :catch(function(err) app:error(tostring(err)) end)
```

`fetch` returns a `Promise`; responses expose `status`, `statusText`, `ok`,
`url`, `redirected`, `headers`, `text()`, `json()` and `arrayBuffer()`.
`AbortController`/`AbortSignal`, `Promise.all`, timeouts and
`reject_unauthorized = false` are supported. See
[docs/lua-api.md](docs/lua-api.md#http-client-fetch) for the full reference.

Useful environment variables:

| Variable | Meaning |
| --- | --- |
| `TUIPRO_RUNTIME` | Directory containing the Lua modules (the repository root or an install prefix). |
| `TUIPRO_TRUECOLOR=0` | Disable 24-bit color. |
| `TUIPRO_SYNC=0` | Disable synchronized-output escape sequences. |
| `TUIPRO_HEADLESS=1` | Default new applications to headless mode. |

The demo and the plugin can be smoke-tested without a terminal:

```sh
./build/tuipro --headless --size 80x24 --frames 1 --demo
./build/tuipro --headless --size 80x24 --frames 1 --demo-coding
./build/tuipro --headless --size 80x24 --frames 1 --demo-api
./build/tuipro --headless --size 80x24 --frames 1 --plugin plugins/hello
```

## A minimal application

```lua
local tuipro = require("tuipro")
local ui = tuipro.ui

local app = tuipro.App.new({
  title = "Hello",
  footer_hint = "q quit",
})

app:set_body(ui.center({
  content = ui.box({
    width = 36,
    height = 7,
    title = "tuipro",
    padding = 1,
    content = ui.vbox({
      gap = 1,
      children = {
        ui.text({ text = "Hello, terminal!", hl = "Title" }),
        ui.button({ label = "Quit", primary = true,
                    on_press = function() app:quit() end }),
      },
    }),
  }),
}))

app:bind("q", function(a) a:quit() end)
app:run()
```

Save it as `hello.lua` and run `./build/tuipro hello.lua`.

## Layout and widgets

Layouts are ordinary widgets. A `VBox`/`HBox` stretches children that set
`flex`; fixed `width`/`height` values are respected. Use `ui.spacer()` for a
flexible gap and `ui.spacer(height)` for a fixed vertical gap.

```lua
ui.hbox({
  gap = 1,
  children = {
    ui.box({ width = 24, title = "Sidebar", content = sidebar }),
    ui.box({ flex = 1, title = "Main", content = main }),
  },
})
```

Widgets receive a 0-based `(x, y)` cell coordinate. Public callbacks use:

- `on_select(item, index, self)` and `on_activate(item, index, self)` for
  lists and tables.
- `on_press(button, app)` for buttons (popup buttons receive the popup).
- `on_submit(text, self, app)`, `on_cancel(text, self, app)` and
  `on_change(text, self)` for inputs.
- `on_change(item, index, self)` for tabs.

See [docs/widgets.md](docs/widgets.md) for every widget and option, and
[docs/lua-api.md](docs/lua-api.md) for the application and plugin APIs.

## Headless scripts and snapshots

Headless mode still builds a real grid, so scripts can render and inspect it:

```lua
local tuipro = require("tuipro")
local core = tuipro.core

core.init({ headless = true, width = 40, height = 8, truecolor = true })
core.put(0, 0, "hello")
core.flush()
for _, row in ipairs(core.snapshot()) do
  print(row)
end
core.shutdown()
```

`core.rows()` preserves trailing blanks; `core.snapshot()` trims them. Use
`core.cell(x, y)` for style and width metadata. Applications can render one
frame without entering the event loop:

```lua
local app = tuipro.App.new({ headless = true, width = 80, height = 24 })
-- ... build the app ...
app:layout()
app:render()
local rows = tuipro.core.snapshot()
app:shutdown()
```

## Plugins

A plugin is a Lua chunk returning a function or a table. Functions are called
as `f(tuipro, cli)`. Tables may provide `run(app)`, `setup(tuipro, cli, app)`,
`mount(app)`, `commands` and `bindings`.

```lua
local M = {}

function M.run()
  local tuipro = require("tuipro")
  local app = tuipro.App.new({ title = "Hello plugin" })
  app:set_body(tuipro.ui.text("Hello from a plugin"))
  app:bind("q", function(a) a:quit() end)
  app:run()
end

function M.setup(_, _, app)
  app:command("hello", function(_, a) a:success("Hello") end)
end

return M
```

Run it with `tuipro --plugin path/to/plugin`, or load it from Lua with
`tuipro.plugins.load()` and `tuipro.plugins.install()`.

## Examples

| File | What it shows |
| --- | --- |
| [examples/minimal.lua](examples/minimal.lua) | Smallest complete app. |
| [examples/dashboard.lua](examples/dashboard.lua) | Cards, progress, table, timers. |
| [examples/widgets.lua](examples/widgets.lua) | Widget gallery and focus. |
| [examples/file_manager.lua](examples/file_manager.lua) | Split view, lists and preview. |
| [examples/split_panes.lua](examples/split_panes.lua) | Three-pane layout and focus. |

## Tests

```sh
make test                 # runs tests/run.sh through the Makefile
./tests/run.sh            # Lua suites + CLI + PTY smoke test
ctest --test-dir build --output-on-failure
```

Understand the internals in [docs/architecture.md](docs/architecture.md), or
browse the installed C API in [include/tuipro.h](include/tuipro.h).

## License

MIT. See [LICENSE](LICENSE).
