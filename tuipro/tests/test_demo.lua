-- Tests for the bundled demo dashboard, using its scripted/headless mode.
local here = (arg and arg[0] or "tests/test_demo.lua"):match("^(.*)/") or "."
local H = dofile(here .. "/helper.lua")
local A = H.assert
local test = H.test

test("demo renders the dashboard and can switch pages", function()
  local demo = require("tuipro.demo")
  local app
  local ok, err = pcall(function()
    app = demo.run({ once = true })
    A.truthy(app.demo)
    A.truthy(app.demo.menu)
    A.truthy(app.demo.tabs)

    local rows = H.snapshot()
    A.row(rows, "Dashboard")
    A.row(rows, "Navigate")
    A.row(rows, "CPU")
    A.row(rows, "Memory")
    A.row(rows, "Network")
    A.row(rows, "NORMAL")
    A.row(rows, "1-5 pages")

    app.demo.switch_page(nil, 2, true)
    app:layout()
    app:render()
    rows = H.snapshot()
    A.row(rows, "Tasks")
    A.row(rows, "Design the cell grid")
    A.row(rows, "Add")

    app.demo.switch_page(nil, 3, true)
    app:layout()
    app:render()
    rows = H.snapshot()
    A.row(rows, "Progress")
    A.row(rows, "Buttons")

    app:execute_command("tasks")
    local _, active = app.demo.stack:active()
    A.eq(active, 2)
    app:execute_command("widgets")
    _, active = app.demo.stack:active()
    A.eq(active, 3)
  end)
  if app then
    app:shutdown()
  end
  if not ok then
    error(err, 0)
  end
end)

test("demo about popup opens, renders and closes with escape", function()
  local demo = require("tuipro.demo")
  local app
  local ok, err = pcall(function()
    app = demo.run({ once = true })
    app.demo.show_about()
    A.eq(#app.overlays, 1)
    app:layout()
    app:render()
    local rows = H.snapshot()
    A.row(rows, "About tuipro")
    A.row(rows, "C grid + LuaJIT widgets")
    A.row(rows, "Close")
    A.eq(app.mode, "normal")

    app:handle_event(H.key("esc"))
    A.eq(#app.overlays, 0)
    app:layout()
    app:render()
    rows = H.snapshot()
    A.no_row(rows, "About tuipro")
  end)
  if app then
    app:shutdown()
  end
  if not ok then
    error(err, 0)
  end
end)

test("dialogs page opens basic, scrollable and picture dialogs", function()
  local demo = require("tuipro.demo")
  local app
  local ok, err = pcall(function()
    app = demo.run({ once = true })
    app.demo.switch_page(nil, 5, true)
    app:layout()
    app:render()
    local rows = H.snapshot()
    A.row(rows, "Dialogs")
    A.row(rows, "Basic")
    A.row(rows, "Scrollable")
    A.row(rows, "Picture JPG")

    -- Basic dialog: title header, text body and two buttons.
    app.demo.show_dialog("basic")
    A.eq(#app.overlays, 1)
    app:layout()
    app:render()
    rows = H.snapshot()
    A.row(rows, "Confirm action")
    A.row(rows, "Apply the pending changes")
    A.row(rows, "Cancel")
    A.row(rows, "OK")
    app:handle_event(H.key("esc"))
    A.eq(#app.overlays, 0)

    -- Scrollable dialog: a form taller than the viewport, with a scrollbar.
    local popup, viewport = app.demo.dialogs.scroll()
    A.eq(#app.overlays, 1)
    A.truthy(popup)
    app:layout()
    app:render()
    rows = H.snapshot()
    A.row(rows, "Account settings")
    A.row(rows, "Profile")
    A.truthy(viewport.total > viewport.visible_h)
    A.eq(viewport.bar_w, 1)

    -- Tab descends into the form and the viewport follows focus.
    A.eq(app.focus, viewport)
    local scrolled = false
    for _ = 1, 12 do
      app:handle_event(H.key("tab"))
      app:layout()
      app:render()
      if viewport.offset > 0 then
        scrolled = true
        break
      end
    end
    A.truthy(scrolled)
    A.ne(app.focus, viewport)

    -- Page keys and the wheel also scroll the form.
    local before = viewport.offset
    A.truthy(viewport:on_key(H.key("pagedown")))
    A.truthy(viewport.offset > before)
    app:handle_event(H.key("esc"))
    A.eq(#app.overlays, 0)

    -- Picture dialog: metadata and the block-art preview.
    app.demo.show_dialog("picture")
    A.eq(#app.overlays, 1)
    app:layout()
    app:render()
    rows = H.snapshot()
    A.row(rows, "example.jpg")
    A.row(rows, "JPEG")
    A.row(rows, "Close")
    A.row(rows, "▀")
    app:handle_event(H.key("esc"))
    A.eq(#app.overlays, 0)
  end)
  if app then
    app:shutdown()
  end
  if not ok then
    error(err, 0)
  end
end)

return H.finish()
