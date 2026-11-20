-- TokyoNight theme plugin.
--
-- Implements the "night" style, the default `tokyonight-night` colorscheme.
-- The palette comes from lua/tokyonight/colors/night.lua (the night overrides
-- applied to storm) and the UI groups use the resolved values from
-- extras/lua/tokyonight_night.lua.  Statusline sections follow
-- lua/lualine/themes/_tokyonight.lua with lualine_bold = false.
--
-- Colors are mapped onto tuipro's semantic groups so widget chrome matches the
-- corresponding Neovim groups: Normal/NormalFloat, Pmenu/PmenuSel,
-- FloatBorder/FloatTitle, TabLine/TabLineSel, Visual, Diff* and the lualine
-- normal/insert/command/visual/replace/terminal sections.  Style attributes
-- (bold, italic, underline) are set explicitly where upstream sets them.
--
-- Run standalone with:  build/tuipro --plugin plugins/tokyo-night
-- Or register it from a script:
--   local plugin = require("tuipro.plugins").load("tokyo-night")
--   plugin.register(require("tuipro"))
--   require("tuipro.hl").set_theme("tokyo-night")
local M = {}

-- Exact Night palette.  bg/bg_dark/bg_dark1 are the night overrides; the
-- remaining values are inherited from storm.  Derived entries below (visual
-- selection, popup menu, diff and border colors) are the values TokyoNight
-- resolves for the night style rather than approximations.
local colors = {
  bg = "#1a1b26",
  bg_dark = "#16161e",
  bg_dark1 = "#0c0e14",
  bg_float = "#16161e",
  bg_highlight = "#292e42",
  bg_popup = "#16161e",
  bg_search = "#3d59a1",
  bg_sidebar = "#16161e",
  bg_statusline = "#16161e",
  bg_visual = "#283457",
  black = "#15161e",
  blue = "#7aa2f7",
  blue0 = "#3d59a1",
  blue1 = "#2ac3de",
  blue2 = "#0db9d7",
  blue5 = "#89ddff",
  blue6 = "#b4f9f8",
  blue7 = "#394b70",
  border = "#15161e", -- WinSeparator / VertSplit / ColorColumn
  border_highlight = "#27a1b9", -- FloatBorder / FloatTitle
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
  -- Resolved UI colors from the upstream night highlight map.
  pmenu_sel = "#343a55", -- PmenuSel / PmenuMatchSel background
  pmenu_sbar = "#1f1f29", -- PmenuSbar background
  pmenu_thumb = "#3b4261", -- PmenuThumb background
  -- Statusline sections from lualine's TokyoNight theme (lualine_bold=false).
  statusline_a_bg = "#7aa2f7",
  statusline_a_fg = "#15161e",
  statusline_b_bg = "#3b4261",
  statusline_b_fg = "#7aa2f7",
  statusline_c_bg = "#16161e",
  statusline_c_fg = "#a9b1d6",
}

-- Semantic roles used by the rest of tuipro.  Normal windows share the main
-- Night background; only floats, popup menus, the statusline and the tabline
-- get the darker bg_dark, exactly like upstream resolves those groups.
colors.surface = colors.bg
colors.surface2 = colors.bg_highlight
colors.panel = colors.bg_float
colors.raised = colors.bg_highlight
colors.text = colors.fg
colors.strong = colors.fg
colors.white = colors.fg
colors.muted = colors.fg_dark
colors.faint = colors.comment
colors.accent = colors.blue
colors.accent2 = colors.magenta
colors.pink = colors.magenta2
colors.code = colors.blue5
colors.shadow = colors.bg_dark1
colors.selection = colors.bg_visual
colors.selection_detail = colors.blue1
colors.add_bg = colors.diff_add
colors.del_bg = colors.diff_delete
colors.add_text = colors.git_add
colors.add_num = colors.git_add
colors.del_text = colors.git_delete
colors.del_num = colors.git_delete
colors.todo_done = colors.comment
colors.pill_text = colors.black

M.theme = {
  name = "tokyo-night",
  base = "tuipro-dark",
  description = "TokyoNight Night: deep navy surfaces and neon accents.",
  -- Showcase metadata: render like a borderless Neovim window (Normal
  -- background, WinSeparator-colored splits, no framed pane borders).
  styling = { flat = true },
  colors = colors,
  groups = {
    -- Core ---------------------------------------------------------------
    Normal = { fg = "text", bg = "bg" },
    NormalFloat = { fg = "text", bg = "bg_float" },
    Dim = { fg = "comment" },
    Muted = { fg = "fg_dark" },
    Faint = { fg = "comment" },
    Border = { fg = "border", bg = "surface" },
    BorderFocus = { fg = "border_highlight", bg = "surface", bold = false },
    Title = { fg = "blue", bg = "surface", bold = true },
    Subtitle = { fg = "fg_dark", bg = "surface", italic = true },
    Header = { fg = "blue", bold = true },
    Accent = { fg = "blue", bold = true },
    Accent2 = { fg = "magenta", bold = true },
    Success = { fg = "green" },
    Warning = { fg = "yellow" },
    Error = { fg = "error", bold = false },
    Info = { fg = "info" },
    Link = { fg = "cyan", underline = true },

    -- Statusline: lualine normal.a/b/c, with no bold by default ---------
    StatusLine = { fg = "statusline_c_fg", bg = "statusline_c_bg" },
    StatusLineMode = { fg = "statusline_a_fg", bg = "statusline_a_bg",
      bold = false },
    StatusLineAccent = { fg = "statusline_b_fg", bg = "statusline_b_bg",
      bold = false },
    StatusLineInfo = { fg = "statusline_c_fg", bg = "statusline_c_bg" },
    StatusLineError = { fg = "statusline_a_fg", bg = "red", bold = false },
    StatusLineWarning = { fg = "statusline_a_fg", bg = "yellow",
      bold = false },

    -- Tabs: TabLine / TabLineSel ----------------------------------------
    Tabline = { fg = "fg_gutter", bg = "bg_statusline" },
    TabActive = { fg = "black", bg = "blue", bold = false },
    TabInactive = { fg = "fg_gutter", bg = "bg_statusline" },
    TabBadge = { fg = "black", bg = "magenta", bold = false },

    -- Menus: Pmenu / PmenuSel / PmenuMatchSel ----------------------------
    Menu = { fg = "text", bg = "bg_popup" },
    MenuItem = { fg = "text", bg = "bg_popup" },
    MenuSelected = { fg = "text", bg = "pmenu_sel", bold = false },
    MenuSelectedInactive = { fg = "fg_gutter", bg = "bg_popup",
      bold = false },
    MenuHeader = { fg = "blue", bg = "bg_popup", bold = true },
    MenuKey = { fg = "orange", bg = "bg_popup" },
    MenuKeySelected = { fg = "blue1", bg = "pmenu_sel", bold = false },
    MenuDetail = { fg = "fg_dark", bg = "bg_popup" },
    MenuDisabled = { fg = "dark3", bg = "bg_popup" },

    -- Lists: Pmenu / PmenuSel -------------------------------------------
    List = { fg = "text", bg = "bg" },
    ListItem = { fg = "text", bg = "bg" },
    ListSelected = { fg = "text", bg = "pmenu_sel", bold = false },
    ListSelectedInactive = { fg = "fg_gutter", bg = "bg_popup",
      bold = false },
    ListSelectedInfo = { fg = "blue1", bg = "pmenu_sel" },
    ListHeader = { fg = "blue", bg = "bg", bold = true },
    ListDetail = { fg = "fg_dark", bg = "bg" },
    ListMarker = { fg = "blue", bg = "bg", bold = false },
    ListMarkerSelected = { fg = "blue1", bg = "pmenu_sel", bold = false },
    ListIndex = { fg = "comment", bg = "bg_popup" },
    ListIndexSelected = { fg = "blue1", bg = "pmenu_sel" },
    ListEmpty = { fg = "fg_dark", bg = "bg", italic = true },
    ListDisabled = { fg = "dark3", bg = "bg" },
    ListCheck = { fg = "green", bg = "bg" },
    ListCheckSelected = { fg = "green", bg = "pmenu_sel", bold = false },

    -- Tables: Title / CursorLine / Visual -------------------------------
    TableHeader = { fg = "blue", bg = "surface2", bold = true },
    TableBorder = { fg = "fg_gutter", bg = "surface2" },
    TableRow = { fg = "text", bg = "surface" },
    TableRowAlt = { fg = "text", bg = "bg" },
    TableSelected = { fg = "text", bg = "selection", bold = false },
    TableSelectedInactive = { fg = "fg_gutter", bg = "surface",
      bold = false },
    TableSelectedMuted = { fg = "blue1", bg = "selection" },
    TableEmpty = { fg = "fg_dark", bg = "surface", italic = true },

    -- Popups: NormalFloat / FloatBorder / FloatTitle --------------------
    Popup = { fg = "text", bg = "bg_float" },
    PopupBorder = { fg = "border_highlight", bg = "bg_float",
      bold = false },
    PopupTitle = { fg = "border_highlight", bg = "bg_float",
      bold = false },
    PopupFooter = { fg = "fg_dark", bg = "bg_float" },
    Shadow = { fg = "shadow", bg = "shadow" },

    -- Inputs ------------------------------------------------------------
    Input = { fg = "text", bg = "bg_float" },
    InputFocus = { fg = "text", bg = "bg_float" },
    InputDisabled = { fg = "dark3", bg = "bg_float" },
    InputPlaceholder = { fg = "comment", bg = "bg_float", italic = true },
    InputPrompt = { fg = "blue", bg = "bg_float", bold = true },
    InputPromptFocus = { fg = "blue", bg = "bg_float", bold = true },
    InputCursor = { fg = "bg", bg = "text" },
    InputBorder = { fg = "fg_gutter", bg = "bg_float" },
    InputBorderFocus = { fg = "border_highlight", bg = "bg_float" },

    -- Progress and spinners ---------------------------------------------
    ProgressLabel = { fg = "fg_dark" },
    ProgressFill = { fg = "green", bold = true },
    ProgressFillAlt = { fg = "blue", bold = true },
    ProgressTrack = { fg = "fg_gutter" },
    ProgressPercent = { fg = "text", bold = true },

    Spinner = { fg = "blue", bold = true },
    SpinnerLabel = { fg = "fg_dark" },
    SpinnerDone = { fg = "green", bold = true },
    SpinnerError = { fg = "error", bold = true },

    -- Buttons: lualine normal/insert/replace section colors ------------
    Button = { fg = "text", bg = "surface2" },
    ButtonFocus = { fg = "statusline_a_fg", bg = "blue", bold = false },
    ButtonPrimary = { fg = "statusline_a_fg", bg = "green", bold = false },
    ButtonPrimaryFocus = {
      fg = "statusline_a_fg", bg = "green", bold = false, underline = true,
    },
    ButtonDanger = { fg = "statusline_a_fg", bg = "red", bold = false },
    ButtonDangerFocus = {
      fg = "statusline_a_fg", bg = "red", bold = false, underline = true,
    },
    ButtonDisabled = { fg = "dark3", bg = "bg_float" },
    ButtonHotkey = { fg = "orange", bg = "surface2", bold = true },
    ButtonHotkeyFocus = { fg = "statusline_a_fg", bg = "blue",
      bold = false },

    -- Scrollbars: PmenuSbar / PmenuThumb --------------------------------
    ScrollbarTrack = { fg = "pmenu_sbar", bg = "bg_float" },
    ScrollbarThumb = { fg = "pmenu_thumb", bg = "bg_float" },
    ScrollbarThumbFocus = { fg = "blue", bg = "bg_float" },

    -- Command line and messages: MsgArea / MoreMsg ----------------------
    CommandLine = { fg = "fg_dark", bg = "bg" },
    CommandLinePrompt = { fg = "blue", bg = "bg", bold = true },
    CommandLineCursor = { fg = "bg", bg = "text" },
    CommandLineInfo = { fg = "fg_gutter", bg = "bg" },
    Message = { fg = "fg_dark", bg = "bg" },
    MessageInfo = { fg = "info", bg = "bg" },
    MessageWarn = { fg = "yellow", bg = "bg" },
    MessageError = { fg = "error", bg = "bg", bold = false },
    MessageSuccess = { fg = "green", bg = "bg" },

    -- Small chrome pieces ------------------------------------------------
    Separator = { fg = "fg_gutter" },
    SeparatorFocus = { fg = "blue" },
    Key = { fg = "cyan", bold = false },
    KeyDesc = { fg = "magenta" },
    Badge = { fg = "black", bg = "blue", bold = true },
    BadgeAlt = { fg = "black", bg = "magenta", bold = true },
    BadgeGreen = { fg = "black", bg = "green", bold = true },
    Pill = { fg = "black", bg = "magenta", bold = true },
    Breadcrumb = { fg = "fg_dark" },
    BreadcrumbActive = { fg = "text", bold = true },
    Toast = { fg = "text", bg = "bg_float" },
    ToastBorder = { fg = "border_highlight", bg = "bg_float" },
    ToastInfo = { fg = "info", bg = "bg_float", bold = true },
    ToastWarn = { fg = "yellow", bg = "bg_float", bold = true },
    ToastError = { fg = "error", bg = "bg_float", bold = true },
    ToastSuccess = { fg = "green", bg = "bg_float", bold = true },
  },
}

-- Register (or refresh) the theme in the global registry.
function M.register(tuipro)
  local t = tuipro or require("tuipro")
  return t.theme.register(M.theme)
end

-- Register and activate the theme.
function M.apply(tuipro, opts)
  local t = tuipro or require("tuipro")
  M.register(t)
  return t.hl.set_theme(M.theme.name, opts)
end

-- Plugins installed into an existing app register the theme and add a
-- command for re-applying it later.
function M.setup(tuipro, _, app)
  local t = tuipro or require("tuipro")
  M.register(t)
  if app then
    app:set_theme(M.theme.name)
    app:command("tokyo-night", function(_, a)
      a:set_theme(M.theme.name)
      a:success("TokyoNight theme applied")
    end, "Apply the tokyo-night theme")
  end
end

-- Standalone entry point used by `tuipro --plugin plugins/tokyo-night`.
function M.run()
  local t = require("tuipro")
  M.register(t)
  return require("tuipro.demo_theme").run({ theme = M.theme.name })
end

return M
