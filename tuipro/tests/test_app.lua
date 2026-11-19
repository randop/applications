-- Application container tests: layout/chrome, focus, bindings, commands,
-- popups, timers and resize handling.
local here = (arg and arg[0] or "tests/test_app.lua"):match("^(.*)/") or "."
local H = dofile(here .. "/helper.lua")
local t = H.t
local ui = H.ui
local A = H.assert
local test = H.test
local core = H.core

test("app renders body, tabline, statusline and command line", function()
  H.with_app({
    width = 40,
    height = 10,
    title = "Snap",
    footer_hint = "HINT",
    body = ui.text({ text = "BODY", height = 1 }),
    tabline = ui.tabs({ "One", "Two" }),
  }, function(app)
    local rows = H.render_app(app)
    A.eq(app.frames, 1)
    A.row(rows, "One")
    A.row(rows, "Two")
    A.row(rows, "BODY")
    A.row(rows, "NORMAL")
    A.row(rows, "HINT")
    A.eq(app._chrome.tab_h, 1)
    A.eq(app._chrome.status_h, 1)
    A.eq(app._chrome.cmd_h, 1)
    local w, h = core.size()
    A.eq(w, 40)
    A.eq(h, 10)
  end)
end)

test("app resize updates the core for height-only changes", function()
  H.with_app({ width = 40, height = 12, body = ui.text("BODY") }, function(app)
    H.render_app(app)
    A.eq(select(2, core.size()), 12)
    app:resize(40, 8)
    local w, h = core.size()
    A.eq(w, 40)
    A.eq(h, 8)
    A.eq(app.width, 40)
    A.eq(app.height, 8)
    local rows = H.render_app(app)
    A.eq(#rows, 8)
  end)
end)

test("app key bindings and quit()", function()
  H.with_app({ body = ui.text("BODY") }, function(app)
    local hits, last_ev = 0, nil
    app:bind("x", function(a, ev)
      hits = hits + 1
      last_ev = ev
    end)
    A.truthy(app:handle_event(H.text_key("x")))
    A.eq(hits, 1)
    A.eq(last_ev.key, "x")

    A.falsy(app.should_quit)
    app:quit()
    A.truthy(app.should_quit)

    app.should_quit = false
    app:handle_event(H.key("C-q"))
    A.truthy(app.should_quit)
  end)
end)

test("app focus traversal and text input dispatch", function()
  local first = ui.input({ value = "" })
  local second = ui.input({ value = "" })
  H.with_app({
    body = ui.vbox({ children = { first, second } }),
  }, function(app)
    app:layout()
    app:ensure_focus()
    A.eq(app.focus, first)
    H.type_text(app, "ab")
    A.eq(first:get(), "ab")

    app:focus_next(1)
    A.eq(app.focus, second)
    A.truthy(first.focused == false)
    A.truthy(second.focused)
    H.type_text(app, "xy")
    A.eq(second:get(), "xy")

    app:focus_next(-1)
    A.eq(app.focus, first)
  end)
end)

test("app command line and commands", function()
  H.with_app({ body = ui.text("BODY") }, function(app)
    app:command("greet", function(arg, a)
      a:info("hello " .. arg)
    end, "Greet")

    app:execute_command("greet world")
    A.eq(app.message.text, "hello world")
    A.eq(app.message.level, "info")

    app:execute_command("missing")
    A.match(app.message.text, "E492")

    app:enter_command(":")
    A.eq(app.mode, "command")
    H.type_text(app, "greet lua")
    app:handle_event(H.key("enter"))
    A.eq(app.mode, "normal")
    A.eq(app.message.text, "hello lua")
    A.eq(app.cmd_history[#app.cmd_history], "greet lua")

    app:enter_command(":")
    H.type_text(app, "discard")
    app:handle_event(H.key("esc"))
    A.eq(app.mode, "normal")
    A.eq(app.cmd_history[#app.cmd_history], "greet lua")
  end)
end)

test("app popups focus, render and close on escape", function()
  local pressed
  local popup = ui.popup({
    title = "Confirm",
    width = 28,
    height = 8,
    content = "Really?",
    buttons = {
      { label = "OK", primary = true, on_press = function(p) pressed = p end },
      { label = "Cancel" },
    },
  })
  H.with_app({ body = ui.text("BODY") }, function(app)
    app:open_popup(popup, { width = 28, height = 8, close_on_esc = true })
    A.eq(#app.overlays, 1)
    A.eq(app.overlays[1].widget, popup)
    A.truthy(popup:find_focusables()[1])

    local rows = H.render_app(app)
    A.row(rows, "Confirm")
    A.row(rows, "Really?")
    A.row(rows, "OK")

    A.truthy(popup.buttons[1]:press())
    A.eq(pressed, popup)
    A.eq(#app.overlays, 1)

    A.truthy(app:handle_event(H.key("esc")))
    A.eq(#app.overlays, 0)
  end)
end)

test("app timers fire, repeat and cancel", function()
  H.with_app({ body = ui.text("BODY") }, function(app)
    local once, ticks = 0, 0
    local one = app:after(0, function() once = once + 1 end)
    local every = app:every(5, function() ticks = ticks + 1 end)
    every.deadline = H.util.now() - 1

    app:_process_timers()
    A.eq(once, 1)
    A.eq(ticks, 1)
    A.falsy(one.active)

    every.deadline = H.util.now() - 1
    app:_process_timers()
    A.eq(ticks, 2)

    app:cancel(every)
    every.deadline = H.util.now() - 1
    app:_process_timers()
    A.eq(ticks, 2)
    A.eq(#app.timers, 0)
  end)
end)

test("app paste reaches the focused input", function()
  local input = ui.input({ value = "" })
  H.with_app({ body = input }, function(app)
    app:layout()
    app:ensure_focus()
    A.truthy(app:handle_event(H.paste("one\ntwo\tthree")))
    A.eq(input:get(), "one two three")
  end)
end)

test("app run exits after the frame limit", function()
  H.with_app({
    width = 30,
    height = 8,
    frames = 1,
    body = ui.text("RUN"),
  }, function(app)
    app:run()
    A.eq(app.frames, 1)
    A.falsy(app.running)
  end)
end)

test("app background can be disabled", function()
  H.with_app({
    width = 20,
    height = 6,
    background = false,
    body = ui.text({ text = "X", height = 1 }),
  }, function(app)
    H.render_app(app)
    local _, _, bg = core.cell(19, 2)
    A.eq(bg, -1)
  end)
end)

return H.finish()
