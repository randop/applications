-- Tests for the theme registry, runtime switching and themed glyphs.
local here = (arg and arg[0] or "tests/test_theme.lua"):match("^(.*)/") or "."
local H = dofile(here .. "/helper.lua")
local t = H.t
local hl = H.hl
local theme = t.theme
local A = H.assert
local test = H.test

local function hex(value)
  return tonumber(tostring(value):gsub("^#", ""), 16)
end

local function clean(fn)
  hl.reset()
  local ok, err = pcall(fn)
  hl.reset()
  if not ok then
    error(err, 0)
  end
end

test("built-in themes are registered", function()
  clean(function()
    local names = theme.names()
    local dark = false
    local light = false
    for _, name in ipairs(names) do
      if name == "tuipro-dark" then
        dark = true
      elseif name == "tuipro-light" then
        light = true
      end
    end
    A.truthy(dark)
    A.truthy(light)
    A.eq(hl.theme, "tuipro-dark")
    A.truthy(theme.get("tuipro-dark"))
    A.truthy(theme.get("tuipro-light"))
    A.eq(theme.get("definitely-not-a-theme"), nil)
    A.eq(#theme.list() >= 2, true)
  end)
end)

test("themes without a base inherit the default baseline", function()
  clean(function()
    local def = theme.register({
      name = "test-minimal",
      colors = { accent = "#abcdef" },
      groups = { Normal = { fg = "accent", bg = "bg" } },
    })
    A.eq(def.colors.accent, "#abcdef")
    A.eq(def.colors.text, theme.get("tuipro-dark").colors.text,
        "unset no-base roles inherit the default palette")
    A.truthy(def.groups.Error, "unset no-base groups inherit the defaults")

    A.truthy(hl.set_theme("test-minimal"))
    A.eq(hl.fg("Normal"), hex("#abcdef"))
    A.eq(hl.fg("Error"), hex(theme.get("test-minimal").colors.red))
  end)
end)

test("custom themes inherit and override palette, groups and glyphs", function()
  clean(function()
    local def = theme.register({
      name = "test-solar",
      base = "tuipro-dark",
      description = "test theme",
      colors = {
        bg = "#101010",
        surface = "#202020",
        accent = "#ff8800",
      },
      groups = {
        Normal = { fg = "text", bg = "bg" },
        StatusLine = { fg = "accent", bg = "surface" },
      },
      glyphs = {
        list = { marker = "»" },
        button = { left = "<", right = ">" },
        spinner = { frames = { "1", "2", "3" } },
      },
    })
    A.eq(def.name, "test-solar")
    A.eq(def.colors.accent, "#ff8800")
    A.eq(def.colors.blue, theme.get("tuipro-dark").colors.blue,
        "unset roles inherit from the base theme")
    A.eq(#def.glyphs.spinner.frames, 3,
        "glyph arrays replace instead of merging with the base")

    A.truthy(hl.set_theme("test-solar"))
    A.eq(hl.theme, "test-solar")
    A.eq(hl.bg("Normal"), hex("#101010"))
    A.eq(hl.fg("StatusLine"), hex("#ff8800"))
    A.eq(hl.bg("StatusLine"), hex("#202020"))
    A.eq(hl.glyph("list", "marker"), "»")
    A.eq(hl.glyph("button", "left"), "<")
    A.eq(hl.glyph("button", "right"), ">")
    A.eq(hl.glyph("separator", "single"), "─",
        "unset glyphs inherit from the default theme")
  end)
end)

test("custom highlight overrides survive theme switches", function()
  clean(function()
    theme.register({
      name = "test-override",
      base = "tuipro-dark",
      colors = { accent2 = "#12ab34" },
    })
    A.truthy(hl.set_theme("test-override"))
    hl.define("TestCustom", { fg = "accent2", bg = "surface", bold = true })
    A.eq(hl.fg("TestCustom"), hex("#12ab34"))
    local _, bg, attrs = hl.get("TestCustom")
    A.ne(bg, hl.DEFAULT)
    A.eq(attrs, hl.attr.bold)

    A.truthy(hl.set_theme("tuipro-dark"))
    A.eq(hl.fg("TestCustom"), hex("#bb9af7"))

    hl.reset()
    A.eq(hl.get_group("TestCustom"), nil)
    A.eq(hl.theme, "tuipro-dark")
    A.eq(hl.glyph("list", "marker"), "›")
  end)
end)

test("unknown theme names return an error", function()
  clean(function()
    local def, err = hl.set_theme("definitely-not-a-theme")
    A.eq(def, nil)
    A.contains(tostring(err), "unknown theme")
    A.eq(hl.theme, "tuipro-dark")
  end)
end)

test("transparency follows custom theme neutral roles", function()
  clean(function()
    theme.register({
      name = "test-neutral",
      base = "tuipro-dark",
      colors = {
        bg = "#000020",
        surface = "#101030",
        surface2 = "#202040",
      },
    })
    A.truthy(hl.set_theme("test-neutral", { transparent = true }))
    A.truthy(hl.transparent)
    A.eq(hl.bg("Normal"), hl.DEFAULT)
    A.eq(hl.bg("StatusLine"), hl.DEFAULT)
    A.eq(hl.bg({ bg = "surface" }), hl.DEFAULT)
    A.ne(hl.bg("ListSelected"), hl.DEFAULT,
        "colored selection backgrounds stay opaque")

    A.truthy(hl.set_theme("test-neutral", { transparent = false }))
    A.falsy(hl.transparent)
    A.eq(hl.bg("Normal"), hex("#000020"))
    A.eq(hl.bg("StatusLine"), hex("#202040"))
    A.eq(hl.bg({ bg = "surface" }), hex("#101030"))
  end)
end)

test("widgets draw glyphs from the active theme", function()
  clean(function()
    theme.register({
      name = "test-widget-glyphs",
      base = "tuipro-dark",
      glyphs = {
        list = { marker = ">", marker_unselected = "." },
        button = { left = "<", right = ">" },
        progress = { fill = "=", track = "." },
        separator = { single = "-", vertical = "|" },
        canvas = { horizontal = "~", vertical = "!" },
      },
    })
    A.truthy(hl.set_theme("test-widget-glyphs"))

    local rows = H.render_widget(H.ui.list({ items = { "Alpha" } }), 20, 1)
    A.contains(rows[1], ">")
    A.contains(rows[1], "Alpha")

    local button = H.render_widget(H.ui.button({ label = "OK" }), 12, 1)
    A.contains(button[1], "<")
    A.contains(button[1], ">")

    local progress = H.render_widget(H.ui.progress({
      value = 50,
      show_label = false,
      show_percent = false,
      width = 12,
    }), 12, 1)
    A.contains(progress[1], "=")
    A.contains(progress[1], ".")

    local separator = H.render_widget(H.ui.separator({ style = "single" }), 10, 1)
    A.contains(separator[1], "-")
  end)
end)

test("application chrome styles can be overridden per app", function()
  clean(function()
    H.with_app({
      body = H.ui.text("body"),
      styles = {
        status = { mode = "Error", title = "Warning" },
        cmdline = { fill = "Normal", info = "Info" },
        messages = { info = "Warning" },
      },
    }, function(app)
      local sections = app:status_sections()
      A.eq(sections.left[1].hl, "Error")
      A.eq(sections.left[2].hl, "Warning")
      A.eq(app.styles.cmdline.fill, "Normal")
      app:info("hello")
      A.eq(app:message_style(), "Warning")
    end)
  end)
end)

test("light theme resolves the same semantic groups", function()
  clean(function()
    local dark_bg = hl.bg("Normal")
    A.truthy(hl.set_theme("tuipro-light"))
    A.eq(hl.theme, "tuipro-light")
    A.ne(hl.bg("Normal"), dark_bg)
    A.eq(hl.fg("Error"), hex(theme.get("tuipro-light").colors.red))
    A.eq(hl.fg("Success"), hex(theme.get("tuipro-light").colors.green))
  end)
end)

test("cli.load_theme applies a built-in theme", function()
  clean(function()
    local cli = t.cli
    A.eq(cli.theme, nil, "the test process does not pass --theme")
    local def = cli.load_theme("tuipro-light")
    A.truthy(def)
    A.eq(hl.theme, "tuipro-light")
    A.eq(t.hl.bg("Normal"), hex(theme.get("tuipro-light").colors.bg))
  end)
end)

test("cli.load_theme loads a theme shipped as a plugin", function()
  clean(function()
    A.eq(theme.get("aurora"), nil, "aurora is not registered before loading")
    local def = t.cli.load_theme("aurora")
    A.truthy(def)
    A.eq(hl.theme, "aurora")
    A.eq(hl.glyph("list", "marker"), theme.get("aurora").glyphs.list.marker)
  end)
end)

test("cli.load_theme reports unknown themes", function()
  clean(function()
    local def, err = t.cli.load_theme("definitely-not-a-theme")
    A.eq(def, nil)
    A.contains(tostring(err), "unknown theme")
    A.eq(hl.theme, theme.default_name, "an unknown theme leaves the current one")
  end)
end)

test("an app starts with the theme given on the command line", function()
  clean(function()
    H.with_app({
      theme = "tuipro-light",
      body = H.ui.text("body"),
    }, function(app)
      A.eq(hl.theme, "tuipro-light")
      A.eq(hl.bg("Normal"), hex(theme.get("tuipro-light").colors.bg))
      H.render_app(app)
      A.eq(hl.theme, "tuipro-light")
    end)

    -- An unknown name degrades gracefully: the app runs with a message.
    local before = hl.theme
    H.with_app({
      theme = "definitely-not-a-theme",
      body = H.ui.text("body"),
    }, function(app)
      A.truthy(app.message)
      A.contains(app.message.text, "unknown theme")
      A.eq(hl.theme, before, "an unknown theme keeps the current one")
    end)
  end)
end)

return H.finish()
