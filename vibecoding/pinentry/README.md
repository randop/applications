# pinentry-helper

Barebones C++23 client for `pinentry`, using the Assuan protocol over pipes
(no libassuan, no GPGME — just `fork`/`exec`/`pipe` and line-based I/O).

## Build

```
make
```

## Run

```
./pinentry-helper
```

Needs a real controlling terminal attached (or `$DISPLAY` set, for the GTK/Qt
variants). Running it detached/headless — e.g. inside this sandbox, over SSH
without a pty, under a bare `fork` with no tty — will fail with something
like `ERR ... Inappropriate ioctl for device`, since pinentry has nothing to
draw on. That's expected there; it works normally in a regular terminal.

## Protocol notes

- Commands are sent as lines: `SETDESC ...`, `SETPROMPT ...`, `GETPIN`, `BYE`.
- Responses are lines: `OK`, `ERR <code> <msg>`, `D <data>` (data lines,
  percent-encoded), `S ...` (status, ignored here), `# ...` (comments,
  ignored here).
- `%`, `\r`, `\n` in outgoing parameters must be percent-encoded
  (`percentEncode`); incoming `D` lines must be percent-decoded
  (`percentDecode`).
- `OPTION ttyname=...` / `OPTION ttytype=...` tell `pinentry-curses` /
  `pinentry-tty` which terminal to draw on — they don't use the piped
  stdin/stdout for UI, only for the Assuan protocol itself. GUI backends
  (gtk/qt) ignore these harmlessly.
- Swap the binary via `PinentryClient::create("pinentry-curses")` etc. to
  pin a specific backend instead of whatever `pinentry` resolves to.

## pinentry-raylib

`pinentry_raylib.cpp` is a second, independent program: a `pinentry`
*implementation*, not a client. It speaks the server side of the same
Assuan protocol — reads `SETDESC`/`SETPROMPT`/`GETPIN`/... from stdin,
writes `OK`/`ERR`/`D ...` to stdout — and draws the actual dialog with
raylib + raygui instead of GTK/Qt/curses. `PinentryClient` from the first
half of this repo can drive it exactly like any other pinentry backend:

```cpp
auto client = PinentryClient::create("./pinentry-raylib");
```

or point gpg-agent at it directly (`gpg-agent.conf`):

```
pinentry-program /path/to/pinentry-raylib
```

or run the demo against it:

```
./pinentry-helper ./pinentry-raylib
```

**Masking.** raygui's `GuiTextBox`/`GuiTextInputBox` show the real
characters while a field has edit focus and only mask once it loses
focus — wrong for a passphrase box. `pinentry_raylib.cpp` bypasses that:
it reads `GetCharPressed()`/`KEY_BACKSPACE` itself into a private
`std::string` and only ever draws `'*'` for it, wiped with
`explicit_bzero` before the buffer goes out of scope. Plaintext never
touches the screen or a raygui-owned buffer.

**Window lifecycle.** `InitWindow()`/`CloseWindow()` run fresh for every
`GETPIN`/`CONFIRM`, so the GL context doesn't linger between prompts.
Cancel = Escape, clicking the OS close button, or the Cancel button;
Enter or the OK button submits.

**Dependencies.** raylib isn't in Ubuntu's apt repos, so build it from
source first:

```
git clone --depth 1 --branch 5.5 https://github.com/raysan5/raylib.git
cd raylib && mkdir build && cd build
cmake .. -DPLATFORM=Desktop -DCMAKE_BUILD_TYPE=Release
make -j$(nproc) && make install   # installs to /usr/local
```

`raygui.h` (single header, from raysan5/raygui) is vendored in this repo
already — no separate install needed.

Build deps on Debian/Ubuntu:

```
apt-get install build-essential cmake libgl1-mesa-dev libx11-dev \
    libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev \
    libwayland-dev libxkbcommon-dev pkg-config
```

## Files

- `pinentry_client.hpp/.cpp` — the client (process spawn, pipe wiring,
  protocol read/write, percent encode/decode).
- `main.cpp` — asks for a passphrase via a chosen pinentry backend,
  prints its length, wipes it with `explicit_bzero`.
- `pinentry_raylib.cpp` — the raylib+raygui pinentry implementation
  (Assuan server + GUI).
- `raygui.h` — vendored single-header raygui v5.0.
- `Makefile` — builds `pinentry-helper` and `pinentry-raylib` (`make all`).
