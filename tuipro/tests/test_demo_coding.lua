-- Tests for the opencode-style coding demo (--demo-coding).
local here = (arg and arg[0] or "tests/test_demo_coding.lua"):match("^(.*)/") or "."
local H = dofile(here .. "/helper.lua")
local A = H.assert
local test = H.test

local function with_coding(opts, fn)
  local demo = require("tuipro.demo_coding")
  local app
  local ok, err = pcall(function()
    opts = opts or {}
    opts.once = true
    opts.width = opts.width or 120
    opts.height = opts.height or 40
    app = demo.run(opts)
    fn(app)
  end)
  if app then
    app:shutdown()
  end
  if not ok then
    error(err, 0)
  end
end

-- Find a block containing a phrase in the active conversation.
local function has_block(session, phrase)
  for _, block in ipairs(session.blocks or {}) do
    if type(block.text) == "string" and block.text:find(phrase, 1, true) then
      return true
    end
  end
  return false
end

test("coding demo renders the sidebar, transcript and prompt", function()
  with_coding(nil, function(app)
    A.truthy(app.coding)
    A.truthy(app.coding.session_list)
    A.truthy(app.coding.transcript)
    A.truthy(app.coding.viewport)
    A.truthy(app.coding.prompt)
    A.eq(app.focus, app.coding.prompt)
    A.eq(app.coding.state.active, 1)

    local rows = H.snapshot()
    A.row(rows, "SESSIONS")
    A.row(rows, "auth middleware")
    A.row(rows, "flaky retry tests")
    A.row(rows, "Refactor the auth middleware")
    A.row(rows, "● Read")
    A.row(rows, "src/auth/middleware.ts")
    A.row(rows, "claude-sonnet-4")
    A.row(rows, "Ask anything")
    A.row(rows, "BUILD")
    A.row(rows, "tuipro coding")
    A.row(rows, "ctrl+p commands")

    A.truthy(app.coding.viewport.total > app.coding.viewport.visible_h)
    A.eq(app.coding.viewport.bar_w, 1)
    A.eq(app.coding.viewport.offset, 0)

    -- Neutral surfaces fall through, diff colors do not.
    A.truthy(H.hl.transparent)
    local _, main_bg = H.hl.get("CodingMain")
    A.eq(main_bg, H.hl.DEFAULT)
    local _, diff_bg = H.hl.get("CodingDiffAdd")
    A.ne(diff_bg, H.hl.DEFAULT)
  end)
end)

test("coding demo switches sessions from the sidebar", function()
  with_coding(nil, function(app)
    local rows = H.snapshot()
    A.row(rows, "flaky retry tests")

    app.coding.session_list:set_index(2)
    A.eq(app.coding.state.active, 2)
    app:layout()
    app:render()
    rows = H.snapshot()
    A.row(rows, "The flaky retry tests fail intermittently")
    A.row(rows, "npm test -- retry")
    A.no_row(rows, "Refactor the auth middleware")

    -- Enter on a session moves focus to the prompt.
    app:set_focus(app.coding.session_list)
    app.coding.session_list:set_index(3)
    app.coding.session_list:activate()
    A.eq(app.coding.state.active, 3)
    A.eq(app.focus, app.coding.prompt)
    app:layout()
    app:render()
    rows = H.snapshot()
    A.row(rows, "command palette")
    A.row(rows, "Add a command palette bound to ctrl+p")
  end)
end)

test("coding demo accepts a prompt and completes the simulated reply", function()
  with_coding(nil, function(app)
    local prompt = app.coding.prompt
    app:set_focus(prompt)
    H.type_text(prompt, "Add a health check endpoint")
    A.eq(prompt:get(), "Add a health check endpoint")

    app:handle_event(H.key("enter"))
    A.eq(prompt:get(), "")
    A.truthy(app.coding.state.pending)
    app:layout()
    app:render()
    local rows = H.snapshot()
    A.row(rows, "Add a health check endpoint")
    A.row(rows, "Thinking")
    A.truthy(app.coding.viewport.offset > 0)

    A.truthy(app.coding.finish_reply())
    A.falsy(app.coding.state.pending)
    app:layout()
    app:render()
    rows = H.snapshot()
    A.row(rows, "src/app/handler.ts")
    A.row(rows, "withRetry")
    A.row(rows, "The change is in place")
    A.row(rows, "1 file changed")
    A.no_row(rows, "Thinking")

    local session = app.coding.active_session()
    A.truthy(has_block(session, "Add a health check endpoint"))
    A.truthy(has_block(session, "On it. I'll inspect the relevant code"))
  end)
end)

test("coding demo palette, sidebar toggle and new session", function()
  with_coding(nil, function(app)
    app:set_focus(app.coding.prompt)
    app:handle_event(H.key("C-p"))
    A.eq(#app.overlays, 1)
    app:layout()
    app:render()
    local rows = H.snapshot()
    A.row(rows, "Commands")
    A.row(rows, "New session")
    A.row(rows, "Quit")
    app:handle_event(H.key("esc"))
    A.eq(#app.overlays, 0)

    app:handle_event(H.key("C-s"))
    A.falsy(app.coding.state.sidebar_visible)
    app:layout()
    app:render()
    rows = H.snapshot()
    A.no_row(rows, "SESSIONS")
    A.row(rows, "Ask anything")
    app:handle_event(H.key("C-s"))
    A.truthy(app.coding.state.sidebar_visible)

    local before = #app.coding.sessions
    app:handle_event(H.key("C-o"))
    A.eq(#app.coding.sessions, before + 1)
    A.eq(app.coding.state.active, 1)
    A.eq(app.focus, app.coding.prompt)
    app:layout()
    app:render()
    rows = H.snapshot()
    A.row(rows, "untitled session")
    A.row(rows, "What should we build?")

    -- The first prompt becomes the session title.
    H.type_text(app.coding.prompt, "Fix the health check")
    app:handle_event(H.key("enter"))
    A.eq(app.coding.active_session().title, "Fix the health check")
    app:layout()
    app:render()
    rows = H.snapshot()
    A.row(rows, "Fix the health check")

    app:execute_command("sidebar")
    A.falsy(app.coding.state.sidebar_visible)
  end)
end)

return H.finish()
