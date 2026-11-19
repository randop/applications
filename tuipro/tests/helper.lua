-- Tiny self-contained test harness for tuipro.
--
-- Test files are plain Lua scripts executed by the tuipro binary in headless
-- mode.  A test file ends with `return H.finish()`; the process exit status is
-- nonzero when any test failed.
local t = require("tuipro")
local core = t.core
local keys = t.keys

local H = {
  t = t,
  core = core,
  ui = t.ui,
  util = t.util,
  hl = t.hl,
  keys = keys,
  passed = 0,
  failed = 0,
  failures = {},
}
H.assert = {}

local function describe(v)
  if type(v) == "string" then
    return string.format("%q", v)
  end
  return tostring(v)
end

local function fail(msg, level)
  error(msg or "assertion failed", (level or 2) + 1)
end

-- ---------------------------------------------------------------------------
-- Assertions
-- ---------------------------------------------------------------------------

function H.assert.truthy(v, msg)
  if not v then
    fail(msg or ("expected a truthy value, got " .. describe(v)), 3)
  end
end

function H.assert.falsy(v, msg)
  if v then
    fail(msg or ("expected a falsy value, got " .. describe(v)), 3)
  end
end

function H.assert.eq(actual, expected, msg)
  if actual ~= expected then
    fail((msg and (msg .. ": ") or "") ..
        "expected " .. describe(expected) .. ", got " .. describe(actual), 3)
  end
end

function H.assert.ne(actual, unexpected, msg)
  if actual == unexpected then
    fail((msg and (msg .. ": ") or "") ..
        "expected a value different from " .. describe(unexpected), 3)
  end
end

function H.assert.near(actual, expected, epsilon, msg)
  epsilon = epsilon or 0.000001
  if math.abs((actual or 0) - expected) > epsilon then
    fail((msg and (msg .. ": ") or "") ..
        "expected " .. describe(expected) .. " (+/- " .. epsilon ..
        "), got " .. describe(actual), 3)
  end
end

function H.assert.contains(haystack, needle, msg)
  if type(haystack) ~= "string" or not haystack:find(needle, 1, true) then
    fail((msg and (msg .. ": ") or "") ..
        "expected " .. describe(haystack) .. " to contain " .. describe(needle), 3)
  end
end

function H.assert.not_contains(haystack, needle, msg)
  if type(haystack) == "string" and haystack:find(needle, 1, true) then
    fail((msg and (msg .. ": ") or "") ..
        "expected " .. describe(haystack) .. " not to contain " ..
        describe(needle), 3)
  end
end

function H.assert.match(s, pattern, msg)
  if type(s) ~= "string" or not s:match(pattern) then
    fail((msg and (msg .. ": ") or "") ..
        "expected " .. describe(s) .. " to match " .. describe(pattern), 3)
  end
end

function H.assert.gt(a, b, msg)
  if not (a > b) then
    fail((msg and (msg .. ": ") or "") .. "expected " .. describe(a) ..
        " > " .. describe(b), 3)
  end
end

function H.assert.ge(a, b, msg)
  if not (a >= b) then
    fail((msg and (msg .. ": ") or "") .. "expected " .. describe(a) ..
        " >= " .. describe(b), 3)
  end
end

function H.assert.lt(a, b, msg)
  if not (a < b) then
    fail((msg and (msg .. ": ") or "") .. "expected " .. describe(a) ..
        " < " .. describe(b), 3)
  end
end

function H.assert.le(a, b, msg)
  if not (a <= b) then
    fail((msg and (msg .. ": ") or "") .. "expected " .. describe(a) ..
        " <= " .. describe(b), 3)
  end
end

function H.assert.nil_value(v, msg)
  if v ~= nil then
    fail(msg or ("expected nil, got " .. describe(v)), 3)
  end
end

function H.assert.error_matches(fn, pattern, msg)
  local ok, err = pcall(fn)
  if ok then
    fail((msg and (msg .. ": ") or "") .. "expected an error", 3)
  end
  if pattern and not tostring(err):match(pattern) then
    fail((msg and (msg .. ": ") or "") .. "error " .. describe(tostring(err)) ..
        " did not match " .. describe(pattern), 3)
  end
end

function H.assert.no_error(fn, msg)
  local ok, err = pcall(fn)
  if not ok then
    fail((msg and (msg .. ": ") or "") .. "unexpected error: " .. tostring(err), 3)
  end
end

-- ---------------------------------------------------------------------------
-- Snapshot / rendering helpers
-- ---------------------------------------------------------------------------

function H.snapshot()
  return core.snapshot()
end

function H.join(rows)
  return table.concat(rows or {}, "\n")
end

function H.row_has(rows, needle)
  for _, row in ipairs(rows or {}) do
    if row:find(needle, 1, true) then
      return true
    end
  end
  return false
end

function H.row_index(rows, needle)
  for i, row in ipairs(rows or {}) do
    if row:find(needle, 1, true) then
      return i
    end
  end
  return nil
end

function H.assert.row(rows, needle, msg)
  if not H.row_has(rows, needle) then
    fail((msg and (msg .. ": ") or "") ..
        "expected a row containing " .. describe(needle) ..
        " in:\n" .. H.join(rows), 3)
  end
end

function H.assert.no_row(rows, needle, msg)
  if H.row_has(rows, needle) then
    fail((msg and (msg .. ": ") or "") ..
        "expected no row containing " .. describe(needle) ..
        " in:\n" .. H.join(rows), 3)
  end
end

-- Run fn with an initialized headless core, always shutting it down again.
function H.with_core(w, h, fn)
  core.init({
    headless = true,
    width = w or 40,
    height = h or 10,
    truecolor = true,
  })
  local ok, err = pcall(fn)
  core.shutdown()
  if not ok then
    error(err, 0)
  end
end

-- Run fn(app) with a headless application, always shutting it down again.
function H.with_app(opts, fn)
  opts = opts or {}
  opts.headless = true
  opts.width = opts.width or 60
  opts.height = opts.height or 18
  local app = t.App.new(opts)
  local ok, err = pcall(fn, app)
  app:shutdown()
  if not ok then
    error(err, 0)
  end
  return app
end

-- Render a widget into a fresh headless grid and return snapshot rows.
function H.render_widget(widget, w, h)
  local rows
  H.with_core(w or 40, h or 10, function()
    core.clear()
    widget:resize(0, 0, w or 40, h or 10)
    widget:render(t.canvas.new(0, 0, w or 40, h or 10))
    core.flush()
    rows = core.snapshot()
  end)
  return rows
end

function H.render_app(app)
  app:layout()
  app:render()
  return core.snapshot()
end

-- ---------------------------------------------------------------------------
-- Event helpers
-- ---------------------------------------------------------------------------

local function mod_table(parsed)
  return {
    ctrl = parsed.ctrl or false,
    alt = parsed.alt or false,
    shift = parsed.shift or false,
    super = parsed.super or false,
  }
end

function H.key(name)
  local parsed = keys.parse(name)
  return {
    type = "key",
    key = parsed.key,
    mods = mod_table(parsed),
  }
end

function H.text_key(text)
  return {
    type = "key",
    key = text,
    text = text,
    mods = { ctrl = false, alt = false, shift = false, super = false },
  }
end

function H.mouse(action, button, x, y)
  return {
    type = "mouse",
    action = action,
    button = button or "left",
    x = x,
    y = y,
    mods = { ctrl = false, alt = false, shift = false, super = false },
  }
end

function H.paste(text)
  return { type = "paste", text = text }
end

function H.resize(w, h)
  return { type = "resize", width = w, height = h }
end

-- Type a string one character at a time into the focused widget or command
-- line (ASCII only; tests cover UTF-8 explicitly through Input:insert).
function H.type_text(target, text)
  for i = 1, #text do
    local ch = text:sub(i, i)
    if target.handle_key then
      target:handle_key(H.text_key(ch))
    else
      target:handle(H.text_key(ch))
    end
  end
end

-- ---------------------------------------------------------------------------
-- Runner
-- ---------------------------------------------------------------------------

function H.test(name, fn)
  local ok, err = pcall(fn)
  if ok then
    H.passed = H.passed + 1
    io.write(".")
  else
    H.failed = H.failed + 1
    H.failures[#H.failures + 1] = { name = name, err = tostring(err) }
    io.write("F")
    io.write("\nFAIL " .. name .. "\n  " .. tostring(err) .. "\n")
  end
  io.flush()
end

function H.finish()
  io.write("\n")
  if H.failed == 0 then
    print(string.format("%d passed, 0 failed", H.passed))
    return 0
  end
  print(string.format("%d passed, %d failed", H.passed, H.failed))
  for _, f in ipairs(H.failures) do
    print("  failed: " .. f.name)
  end
  return 1
end

return H
