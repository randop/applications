-- Tests for plugin resolution, loading and installation.
local here = (arg and arg[0] or "tests/test_plugin.lua"):match("^(.*)/") or "."
local H = dofile(here .. "/helper.lua")
local t = H.t
local ui = H.ui
local A = H.assert
local test = H.test
local plugins = t.plugins

local function hex(value)
  return tonumber(tostring(value):gsub("^#", ""), 16)
end

test("plugin resolver and discovery", function()
  local path = plugins.resolve("plugins/hello")
  A.truthy(path)
  A.contains(path, "plugins/hello/init.lua")
  A.eq(plugins.resolve("definitely-not-a-real-plugin-xyz"), nil)

  local found = plugins.discover("plugins")
  local hit = false
  for _, p in ipairs(found) do
    if p:find("plugins/hello/init.lua", 1, true) then
      hit = true
    end
  end
  A.truthy(hit, "discover should find the hello plugin")
end)

test("aurora theme plugin registers a theme and command", function()
  local entry, path = plugins.load("plugins/aurora", t)
  A.eq(type(entry), "table")
  A.truthy(path)
  A.eq(entry.theme.name, "aurora")
  entry.register(t)
  A.truthy(t.theme.get("aurora"))
  A.eq(#t.theme.get("aurora").glyphs.spinner.frames, 8,
      "the plugin's spinner frame array replaces the inherited one")

  H.with_app({ body = ui.text("host") }, function(app)
    plugins.install(entry, app, t)
    A.eq(t.hl.theme, "aurora")
    A.truthy(app.commands.aurora)
    app:execute_command("aurora")
    A.eq(t.hl.theme, "aurora")
  end)
  t.hl.reset()
end)

test("tokyo-night theme plugin registers the TokyoNight Night palette", function()
  local entry, path = plugins.load("plugins/tokyo-night", t)
  A.eq(type(entry), "table")
  A.truthy(path)
  A.eq(entry.theme.name, "tokyo-night")
  A.eq(entry.theme.base, "tuipro-dark")
  entry.register(t)

  local def = t.theme.get("tokyo-night")
  A.truthy(def)

  -- Exact TokyoNight Night palette and resolved UI colors.
  local palette = {
    bg = "#1a1b26",
    bg_dark = "#16161e",
    bg_dark1 = "#0c0e14",
    bg_highlight = "#292e42",
    bg_float = "#16161e",
    bg_popup = "#16161e",
    bg_search = "#3d59a1",
    bg_sidebar = "#16161e",
    bg_statusline = "#16161e",
    bg_visual = "#283457",
    black = "#15161e",
    border = "#15161e",
    border_highlight = "#27a1b9",
    blue = "#7aa2f7",
    blue0 = "#3d59a1",
    blue1 = "#2ac3de",
    blue2 = "#0db9d7",
    blue5 = "#89ddff",
    blue6 = "#b4f9f8",
    blue7 = "#394b70",
    comment = "#565f89",
    cyan = "#7dcfff",
    dark3 = "#545c7e",
    dark5 = "#737aa2",
    diff_add = "#243e4a",
    diff_change = "#1f2231",
    diff_delete = "#4a272f",
    diff_text = "#394b70",
    error = "#db4b4b",
    fg = "#c0caf5",
    fg_dark = "#a9b1d6",
    fg_float = "#c0caf5",
    fg_gutter = "#3b4261",
    fg_sidebar = "#a9b1d6",
    git_add = "#449dab",
    git_change = "#6183bb",
    git_delete = "#914c54",
    git_ignore = "#545c7e",
    green = "#9ece6a",
    green1 = "#73daca",
    green2 = "#41a6b5",
    hint = "#1abc9c",
    info = "#0db9d7",
    magenta = "#bb9af7",
    magenta2 = "#ff007c",
    orange = "#ff9e64",
    purple = "#9d7cd8",
    red = "#f7768e",
    red1 = "#db4b4b",
    teal = "#1abc9c",
    terminal_black = "#414868",
    todo = "#7aa2f7",
    warning = "#e0af68",
    yellow = "#e0af68",
    pmenu_sel = "#343a55",
    pmenu_sbar = "#1f1f29",
    pmenu_thumb = "#3b4261",
    -- lualine normal.a/b/c for the night style.
    statusline_a_bg = "#7aa2f7",
    statusline_a_fg = "#15161e",
    statusline_b_bg = "#3b4261",
    statusline_b_fg = "#7aa2f7",
    statusline_c_bg = "#16161e",
    statusline_c_fg = "#a9b1d6",
  }
  for role, value in pairs(palette) do
    A.eq(def.colors[role], value, "palette role " .. role)
  end

  A.eq(def.colors.text, "#c0caf5")
  A.eq(def.colors.surface, "#1a1b26")
  A.eq(def.colors.surface2, "#292e42")
  A.eq(def.colors.panel, "#16161e")
  A.eq(def.colors.muted, "#a9b1d6")
  A.eq(def.colors.faint, "#565f89")
  A.eq(def.colors.purple, "#9d7cd8")
  A.eq(def.colors.pink, "#ff007c")
  A.eq(def.colors.selection, "#283457")
  A.eq(def.colors.add_bg, "#243e4a")
  A.eq(def.colors.del_bg, "#4a272f")
  A.eq(def.colors.add_text, "#449dab")
  A.eq(def.colors.del_text, "#914c54")
  A.eq(def.colors.pill_text, "#15161e")

  A.eq(def.groups.Normal.fg, "text")
  A.eq(def.groups.NormalFloat.bg, "bg_float")
  A.eq(def.groups.List.bg, "bg")
  A.eq(def.groups.Menu.bg, "bg_popup")
  A.eq(def.styling.flat, true,
      "the showcase renders it as a borderless editor")
  A.eq(def.groups.StatusLine.fg, "statusline_c_fg")
  A.eq(def.groups.StatusLine.bg, "statusline_c_bg")
  A.eq(def.groups.StatusLineMode.fg, "statusline_a_fg")
  A.eq(def.groups.StatusLineMode.bg, "statusline_a_bg")
  A.eq(def.groups.StatusLineMode.bold, false)
  A.eq(def.groups.StatusLineAccent.fg, "statusline_b_fg")
  A.eq(def.groups.StatusLineAccent.bg, "statusline_b_bg")
  A.eq(def.groups.StatusLineAccent.bold, false)
  A.eq(def.groups.Tabline.fg, "fg_gutter")
  A.eq(def.groups.TabActive.fg, "black")
  A.eq(def.groups.TabActive.bg, "blue")
  A.eq(def.groups.TabActive.bold, false)
  A.eq(def.groups.TabInactive.bg, "bg_statusline")
  A.eq(def.groups.ListSelected.bg, "pmenu_sel")
  A.eq(def.groups.ListSelected.bold, false)
  A.eq(def.groups.TableSelected.bg, "selection")
  A.eq(def.groups.TableSelected.bold, false)
  A.eq(def.groups.PopupBorder.fg, "border_highlight")
  A.eq(def.groups.PopupBorder.bold, false)
  A.eq(def.groups.Error.fg, "error")
  A.eq(def.groups.Error.bold, false)
  A.eq(def.groups.InputCursor.fg, "bg")
  A.eq(def.groups.InputCursor.bg, "text")
  A.eq(def.groups.Key.fg, "cyan")
  A.eq(def.glyphs.list.marker, "›",
      "glyphs not overridden by the theme inherit the defaults")

  -- Resolve the Night values through the live highlight machinery.
  H.hl.set_theme("tokyo-night")
  A.eq(H.hl.fg("Normal"), hex("#c0caf5"))
  A.eq(H.hl.bg("Normal"), hex("#1a1b26"))
  A.eq(H.hl.bg("NormalFloat"), hex("#16161e"))
  A.eq(H.hl.bg("ListItem"), hex("#1a1b26"))
  A.eq(H.hl.bg("Menu"), hex("#16161e"))
  A.eq(H.hl.bg("StatusLine"), hex("#16161e"))
  A.eq(H.hl.fg("StatusLine"), hex("#a9b1d6"))
  A.eq(H.hl.bg("StatusLineMode"), hex("#7aa2f7"))
  A.eq(H.hl.fg("StatusLineMode"), hex("#15161e"))
  A.eq(H.hl.attrs("StatusLineMode"), 0, "lualine_bold defaults to false")
  A.eq(H.hl.bg("TabActive"), hex("#7aa2f7"))
  A.eq(H.hl.fg("TabActive"), hex("#15161e"))
  A.eq(H.hl.bg("ListSelected"), hex("#343a55"))
  A.eq(H.hl.fg("ListSelected"), hex("#c0caf5"))
  A.eq(H.hl.fg("PopupBorder"), hex("#27a1b9"))
  A.eq(H.hl.bg("PopupBorder"), hex("#16161e"))
  A.eq(H.hl.bg("TableSelected"), hex("#283457"))
  A.eq(H.hl.fg("Error"), hex("#db4b4b"))
  A.eq(H.hl.attrs("Error"), 0)
  A.eq(H.hl.fg("Key"), hex("#7dcfff"))
  H.hl.reset()

  H.with_app({ body = ui.text("host") }, function(app)
    plugins.install(entry, app, t)
    A.eq(t.hl.theme, "tokyo-night")
    A.truthy(app.commands["tokyo-night"])
    t.hl.set_theme("tuipro-dark")
    app:execute_command("tokyo-night")
    A.eq(t.hl.theme, "tokyo-night")
  end)
  t.hl.reset()
end)

test("opencode theme plugin registers the exported dark palette", function()
  local entry, path = plugins.load("plugins/opencode", t)
  A.eq(type(entry), "table")
  A.truthy(path)
  A.eq(entry.theme.name, "opencode")
  A.eq(entry.theme.base, "tuipro-dark")
  entry.register(t)

  local def = t.theme.get("opencode")
  A.truthy(def)

  local palette = {
    bg = "#0a0a0a",
    surface = "#141414",
    surface2 = "#1e1e1e",
    panel = "#141414",
    raised = "#4c4c4c",
    border = "#484848",
    scrollbar = "#606060",
    text = "#eeeeee",
    strong = "#ffffff",
    white = "#ffffff",
    black = "#0a0a0a",
    muted = "#808080",
    faint = "#4c4c4c",
    accent = "#fab283",
    accent2 = "#9d7cd8",
    blue = "#5c9cf5",
    purple = "#9d7cd8",
    cyan = "#56b6c2",
    green = "#7fd88f",
    yellow = "#f5a742",
    orange = "#fab283",
    red = "#e06c75",
    pink = "#9d7cd8",
    code = "#7fd88f",
    shadow = "#030303",
    selection = "#141414",
    selection_detail = "#fab283",
    add_bg = "#20303b",
    del_bg = "#37222c",
    add_text = "#4fd6be",
    add_num = "#8f8f8f",
    del_text = "#c53b53",
    del_num = "#8f8f8f",
    todo_done = "#808080",
    pill_text = "#0a0a0a",
  }
  for role, value in pairs(palette) do
    A.eq(def.colors[role], value, "palette role " .. role)
  end

  A.eq(def.groups.Normal.fg, "text")
  A.eq(def.groups.Normal.bg, "bg")
  A.eq(def.groups.NormalFloat.bg, "panel")
  A.eq(def.groups.Title.fg, "accent2")
  A.eq(def.groups.ListSelected.fg, "accent")
  A.eq(def.groups.ListSelected.bg, "surface")
  A.eq(def.groups.ListSelected.bold, false)
  A.eq(def.groups.MenuSelected.bg, "panel")
  A.eq(def.groups.TableSelected.bg, "surface")
  A.eq(def.groups.ButtonFocus.fg, "pill_text")
  A.eq(def.groups.ButtonFocus.bg, "accent")
  A.eq(def.groups.ButtonPrimaryFocus.underline, false)
  A.eq(def.groups.InputFocus.fg, "accent")
  A.eq(def.groups.PopupBorder.fg, "border")
  A.eq(def.glyphs.list.marker, "›",
      "glyphs not overridden by the theme inherit the defaults")

  -- Resolve exported values through the live highlight machinery.
  H.hl.set_theme("opencode")
  A.eq(H.hl.fg("Normal"), hex("#eeeeee"))
  A.eq(H.hl.bg("Normal"), hex("#0a0a0a"))
  A.eq(H.hl.bg("NormalFloat"), hex("#141414"))
  A.eq(H.hl.fg("Title"), hex("#9d7cd8"))
  A.eq(H.hl.fg("ListSelected"), hex("#fab283"))
  A.eq(H.hl.bg("ListSelected"), hex("#141414"))
  A.eq(H.hl.attrs("ListSelected"), 0)
  A.eq(H.hl.bg("MenuSelected"), hex("#141414"))
  A.eq(H.hl.fg("TableSelected"), hex("#fab283"))
  A.eq(H.hl.fg("ButtonFocus"), hex("#0a0a0a"))
  A.eq(H.hl.bg("ButtonFocus"), hex("#fab283"))
  A.eq(H.hl.attrs("ButtonPrimaryFocus"), 0)
  A.eq(H.hl.fg("InputFocus"), hex("#fab283"))
  A.eq(H.hl.bg("InputFocus"), hex("#0a0a0a"))
  A.eq(H.hl.fg("PopupBorder"), hex("#484848"))
  A.eq(H.hl.fg("ScrollbarThumb"), hex("#606060"))
  A.eq(H.hl.fg("Error"), hex("#e06c75"))
  A.eq(H.hl.attrs("Error"), 0)
  H.hl.reset()

  H.with_app({ body = ui.text("host") }, function(app)
    plugins.install(entry, app, t)
    A.eq(t.hl.theme, "opencode")
    A.truthy(app.commands.opencode)
    t.hl.set_theme("tuipro-dark")
    app:execute_command("opencode")
    A.eq(t.hl.theme, "opencode")
  end)
  t.hl.reset()
end)

test("plugin load errors are returned", function()
  local entry, err = plugins.load("definitely-not-a-real-plugin-xyz", t)
  A.eq(entry, nil)
  A.truthy(err)
end)

test("hello plugin loads and installs a command", function()
  local entry, path = plugins.load("plugins/hello", t)
  A.eq(type(entry), "table")
  A.truthy(path)
  A.eq(type(entry.run), "function")
  A.eq(type(entry.setup), "function")

  H.with_app({ body = ui.text("host") }, function(app)
    local installed = plugins.install(entry, app, t)
    A.eq(installed, entry)
    A.truthy(app.commands.hello)
    app:execute_command("hello")
    A.contains(app.message.text, "Hello from the hello plugin")
  end)
end)

test("plugin tables can mount widgets, commands and bindings", function()
  local mounted, bound = false, 0
  local entry = {
    mount = function(app)
      mounted = true
      app:set_body(ui.text("mounted"))
    end,
    commands = {
      ping = function(arg, app)
        app:info("pong " .. arg)
      end,
    },
    bindings = {
      z = function(app)
        bound = bound + 1
      end,
    },
  }

  H.with_app({ body = ui.text("before") }, function(app)
    plugins.install(entry, app, t)
    A.truthy(mounted)
    A.truthy(app.commands.ping)
    app:execute_command("ping 1")
    A.eq(app.message.text, "pong 1")

    app:layout()
    local rows = H.render_app(app)
    A.row(rows, "mounted")
    app:handle_event(H.text_key("z"))
    A.eq(bound, 1)
  end)
end)

test("function plugin entries receive tuipro, cli and app", function()
  local seen = nil
  local entry = function(tuipro, cli, app)
    seen = { tuipro = tuipro, cli = cli, app = app }
    return "installed"
  end

  H.with_app({ body = ui.text("host") }, function(app)
    local result = plugins.install(entry, app, t)
    A.eq(result, "installed")
    A.eq(seen.tuipro, t)
    A.truthy(seen.cli)
    A.eq(seen.app, app)
  end)
end)

return H.finish()
