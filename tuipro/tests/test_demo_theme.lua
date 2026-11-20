-- Tests for the theme showcase demo (--demo-theme).
local here = (arg and arg[0] or "tests/test_demo_theme.lua"):match("^(.*)/") or "."
local H = dofile(here .. "/helper.lua")
local A = H.assert
local test = H.test

local function with_theme(opts, fn)
  local demo = require("tuipro.demo_theme")
  local app
  local ok, err = pcall(function()
    opts = opts or {}
    opts.once = true
    opts.width = opts.width or 110
    opts.height = opts.height or 34
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

local function theme_index(app, name)
  for i, candidate in ipairs(app.theme_demo.themes) do
    if candidate == name then
      return i
    end
  end
  return nil
end

local function hex(value)
  return tonumber(value:gsub("^#", ""), 16)
end

test("tokyo-night renders as a flat, borderless editor layout", function()
  with_theme({ theme = "tokyo-night" }, function(app)
    local demo = app.theme_demo
    A.eq(demo.state.theme, "tokyo-night")
    A.falsy(demo.sidebar.border)
    A.falsy(demo.preview.border)
    A.falsy(demo.palette_box.border)
    A.eq(H.hl.bg("Normal"), hex("#1a1b26"))
    A.eq(H.hl.bg("ListItem"), hex("#1a1b26"))

    local rows = H.snapshot()
    A.row(rows, "Themes")
    A.row(rows, "Preview")
    A.row(rows, "Palette roles")

    -- Other themes keep the framed showcase.
    A.truthy(demo.apply("tuipro-dark", true))
    A.truthy(demo.sidebar.border)
    A.truthy(demo.preview.border)
    A.truthy(demo.palette_box.border)
  end)
end)

test("theme demo renders the theme list, preview and palette", function()
  with_theme(nil, function(app)
    local demo = app.theme_demo
    A.truthy(demo)
    A.truthy(demo.list)
    A.truthy(demo.preview)
    A.truthy(demo.palette)
    A.ge(#demo.themes, 5)
    A.truthy(theme_index(app, "aurora"), "the aurora plugin theme is registered")
    A.truthy(theme_index(app, "opencode"),
        "the opencode plugin theme is registered")
    A.truthy(theme_index(app, "tokyo-night"),
        "the tokyo-night plugin theme is registered")
    A.truthy(theme_index(app, "tuipro-dark"))
    A.truthy(theme_index(app, "tuipro-light"))
    A.eq(demo.state.theme, "aurora")
    A.eq(H.hl.theme, "aurora")

    local rows = H.snapshot()
    A.row(rows, "tuipro themes")
    A.row(rows, "Themes")
    A.row(rows, "Preview")
    A.row(rows, "Palette roles")
    A.row(rows, "Primary")
    A.row(rows, "NORMAL")
  end)
end)

test("themes can be applied by name, list selection and command", function()
  with_theme({ theme = "tuipro-dark" }, function(app)
    local demo = app.theme_demo
    local dark_bg = H.hl.bg("Normal")

    A.truthy(demo.apply("tuipro-light", true))
    A.eq(demo.state.theme, "tuipro-light")
    A.eq(H.hl.theme, "tuipro-light")
    A.ne(H.hl.bg("Normal"), dark_bg)
    A.eq(demo.list.index, theme_index(app, "tuipro-light"))

    app:execute_command("theme tuipro-dark")
    A.eq(demo.state.theme, "tuipro-dark")
    A.eq(H.hl.theme, "tuipro-dark")

    local aurora = theme_index(app, "aurora")
    demo.list:set_index(aurora)
    A.eq(demo.state.theme, "aurora")
    A.eq(H.hl.theme, "aurora")

    app:execute_command("theme-next")
    A.eq(demo.state.theme, demo.themes[(aurora % #demo.themes) + 1])
  end)
end)

test("transparency and border glyphs are adjustable at runtime", function()
  with_theme({ theme = "tuipro-dark" }, function(app)
    local demo = app.theme_demo
    A.eq(demo.state.border_style, "rounded")
    A.eq(demo.preview.border_style, "rounded")
    A.eq(H.hl.glyph("border", "rounded").tl, "╭")

    demo.cycle_glyphs()
    A.eq(demo.state.border_style, "single")
    A.eq(demo.preview.border_style, "single")
    A.eq(H.hl.glyph("border", "single").tl, "┌")

    A.truthy(demo.set_glyphs("double"))
    A.eq(demo.preview.border_style, "double")
    A.falsy(demo.set_glyphs("definitely-not-a-style"))

    H.hl.set_transparent(false)
    demo.toggle_transparency()
    A.truthy(H.hl.transparent)
    A.eq(H.hl.bg("Normal"), H.hl.DEFAULT)

    demo.toggle_transparency()
    A.falsy(H.hl.transparent)
    A.ne(H.hl.bg("Normal"), H.hl.DEFAULT)
  end)
end)

test("keyboard shortcuts switch themes and themed plugin glyphs apply", function()
  with_theme({ theme = "tuipro-dark" }, function(app)
    local demo = app.theme_demo
    app:set_focus(demo.list)

    local before = demo.state.theme
    app:handle_event(H.text_key("t"))
    A.ne(demo.state.theme, before)
    A.eq(H.hl.theme, demo.state.theme)

    app:handle_event(H.text_key("1"))
    A.eq(demo.state.theme, demo.themes[1])
    A.eq(H.hl.theme, demo.themes[1])

    app:handle_event(H.text_key("x"))
    A.truthy(H.hl.transparent)
    app:handle_event(H.text_key("x"))
    A.falsy(H.hl.transparent)

    -- The aurora plugin changes list/button glyphs as well as colors.
    demo.apply("aurora", true)
    A.eq(H.hl.glyph("list", "marker"), "❯")
    A.eq(H.hl.glyph("button", "left"), "«")
    A.eq(H.hl.glyph("button", "right"), "»")
  end)
end)

return H.finish()
