-- OpenCode theme plugin.
--
-- Uses the dark variant of OpenCode's theme export. OpenCode's `interactive`
-- hue is tuipro's primary `accent` because it drives focused and selected
-- actions; OpenCode's `accent` hue is the secondary, purple accent here.
--
-- Run standalone with:  build/tuipro --plugin plugins/opencode
-- Or register it from a script:
--   local plugin = require("tuipro.plugins").load("opencode")
--   plugin.register(require("tuipro"))
--   require("tuipro.hl").set_theme("opencode")
local M = {}

-- Resolved dark-mode values from the OpenCode theme export.
local colors = {
  bg = "#0a0a0a",
  bg_raised = "#141414",
  bg_high = "#1e1e1e",
  bg_max = "#4c4c4c",
  border = "#484848",
  scrollbar = "#606060",
  text = "#eeeeee",
  text_strong = "#ffffff",
  muted = "#808080",
  faint = "#4c4c4c",
  interactive = "#fab283",
  accent_hue = "#9d7cd8",
  blue = "#5c9cf5",
  purple = "#9d7cd8",
  cyan = "#56b6c2",
  green = "#7fd88f",
  yellow = "#f5a742",
  orange = "#fab283",
  red = "#e06c75",
  type = "#e5c07b",
  diff_add = "#4fd6be",
  diff_delete = "#c53b53",
  diff_context = "#828bb8",
  diff_add_bg = "#20303b",
  diff_delete_bg = "#37222c",
  diff_add_highlight = "#b8db87",
  diff_delete_highlight = "#e26a75",
  diff_line_number = "#8f8f8f",
  diff_add_line_bg = "#1b2b34",
  diff_delete_line_bg = "#2d1f26",
}

-- Semantic roles used by tuipro's widgets and demos. Selected actions keep a
-- transparent background in OpenCode, represented by their existing surface.
colors.surface = colors.bg_raised
colors.surface2 = colors.bg_high
colors.panel = colors.bg_raised
colors.raised = colors.bg_max
colors.strong = colors.text_strong
colors.white = colors.text_strong
colors.black = colors.bg
colors.accent = colors.interactive
colors.accent2 = colors.accent_hue
colors.pink = colors.purple
colors.code = colors.green
colors.shadow = "#030303"
colors.selection = colors.surface
colors.selection_detail = colors.interactive
colors.add_bg = colors.diff_add_bg
colors.del_bg = colors.diff_delete_bg
colors.add_text = colors.diff_add
colors.del_text = colors.diff_delete
colors.add_num = colors.diff_line_number
colors.del_num = colors.diff_line_number
colors.todo_done = colors.muted
colors.pill_text = colors.bg

M.theme = {
  name = "opencode",
  base = "tuipro-dark",
  description = "OpenCode dark: near-black surfaces, orange actions and purple accents.",
  colors = colors,
  groups = {
    -- Core ---------------------------------------------------------------
    Normal = { fg = "text", bg = "bg" },
    NormalFloat = { fg = "text", bg = "panel" },
    Dim = { fg = "muted" },
    Muted = { fg = "muted" },
    Faint = { fg = "faint" },
    Border = { fg = "border", bg = "surface" },
    BorderFocus = { fg = "accent", bg = "surface", bold = false },
    Title = { fg = "accent2", bg = "surface", bold = true },
    Subtitle = { fg = "muted", bg = "surface", italic = true },
    Header = { fg = "accent2", bold = true },
    Accent = { fg = "accent", bold = true },
    Accent2 = { fg = "accent2", bold = true },
    Success = { fg = "green" },
    Warning = { fg = "yellow" },
    Error = { fg = "red", bold = false },
    Info = { fg = "cyan" },
    Link = { fg = "accent", underline = true },

    -- Application chrome --------------------------------------------------
    StatusLine = { fg = "text", bg = "surface2" },
    StatusLineMode = { fg = "pill_text", bg = "accent", bold = false },
    StatusLineAccent = { fg = "accent", bg = "surface2", bold = false },
    StatusLineInfo = { fg = "muted", bg = "surface2" },
    StatusLineError = { fg = "pill_text", bg = "red", bold = false },
    StatusLineWarning = { fg = "pill_text", bg = "yellow", bold = false },

    Tabline = { fg = "muted", bg = "surface" },
    TabActive = { fg = "accent", bg = "surface", bold = false },
    TabInactive = { fg = "muted", bg = "surface" },
    TabBadge = { fg = "pill_text", bg = "accent2", bold = false },

    -- Selected actions use orange text without changing their background.
    Menu = { fg = "text", bg = "panel" },
    MenuItem = { fg = "text", bg = "panel" },
    MenuSelected = { fg = "accent", bg = "panel", bold = false },
    MenuSelectedInactive = { fg = "muted", bg = "panel", bold = false },
    MenuHeader = { fg = "accent2", bg = "panel", bold = true },
    MenuKey = { fg = "yellow", bg = "panel" },
    MenuKeySelected = { fg = "accent", bg = "panel", bold = false },
    MenuDetail = { fg = "muted", bg = "panel" },
    MenuDisabled = { fg = "muted", bg = "panel" },

    List = { fg = "text", bg = "surface" },
    ListItem = { fg = "text", bg = "surface" },
    ListSelected = { fg = "accent", bg = "surface", bold = false },
    ListSelectedInactive = { fg = "muted", bg = "surface", bold = false },
    ListSelectedInfo = { fg = "accent", bg = "surface", bold = false },
    ListHeader = { fg = "accent2", bg = "surface", bold = true },
    ListDetail = { fg = "muted", bg = "surface" },
    ListMarker = { fg = "accent", bg = "surface", bold = false },
    ListMarkerSelected = { fg = "accent", bg = "surface", bold = false },
    ListIndex = { fg = "faint", bg = "surface" },
    ListIndexSelected = { fg = "accent", bg = "surface", bold = false },
    ListEmpty = { fg = "muted", bg = "surface", italic = true },
    ListDisabled = { fg = "muted", bg = "surface" },
    ListCheck = { fg = "green", bg = "surface" },
    ListCheckSelected = { fg = "green", bg = "surface", bold = false },

    TableHeader = { fg = "accent2", bg = "surface2", bold = true },
    TableBorder = { fg = "border", bg = "surface2" },
    TableRow = { fg = "text", bg = "surface" },
    TableRowAlt = { fg = "text", bg = "bg" },
    TableSelected = { fg = "accent", bg = "surface", bold = false },
    TableSelectedInactive = { fg = "muted", bg = "surface", bold = false },
    TableSelectedMuted = { fg = "accent", bg = "surface", bold = false },
    TableEmpty = { fg = "muted", bg = "surface", italic = true },

    -- Dialogs use OpenCode's raised background.
    Popup = { fg = "text", bg = "panel" },
    PopupBorder = { fg = "border", bg = "panel", bold = false },
    PopupTitle = { fg = "accent2", bg = "panel", bold = true },
    PopupFooter = { fg = "muted", bg = "panel" },
    Shadow = { fg = "shadow", bg = "shadow" },

    -- Form fields stay on the base background and turn orange when focused.
    Input = { fg = "text", bg = "bg" },
    InputFocus = { fg = "accent", bg = "bg" },
    InputDisabled = { fg = "muted", bg = "bg" },
    InputPlaceholder = { fg = "muted", bg = "bg", italic = true },
    InputPrompt = { fg = "accent2", bg = "bg", bold = true },
    InputPromptFocus = { fg = "accent", bg = "bg", bold = true },
    InputCursor = { fg = "bg", bg = "accent" },
    InputBorder = { fg = "border", bg = "bg" },
    InputBorderFocus = { fg = "accent", bg = "bg", bold = false },

    ProgressLabel = { fg = "muted" },
    ProgressFill = { fg = "green", bold = true },
    ProgressFillAlt = { fg = "accent", bold = true },
    ProgressTrack = { fg = "border" },
    ProgressPercent = { fg = "text", bold = true },

    Spinner = { fg = "accent", bold = true },
    SpinnerLabel = { fg = "muted" },
    SpinnerDone = { fg = "green", bold = true },
    SpinnerError = { fg = "red", bold = true },

    -- Buttons follow OpenCode's primary, secondary and destructive actions.
    Button = { fg = "muted", bg = "surface" },
    ButtonFocus = { fg = "pill_text", bg = "accent", bold = false },
    ButtonPrimary = { fg = "text", bg = "surface", bold = false },
    ButtonPrimaryFocus = {
      fg = "pill_text", bg = "accent", bold = false, underline = false,
    },
    ButtonDanger = { fg = "pill_text", bg = "red", bold = false },
    ButtonDangerFocus = {
      fg = "pill_text", bg = "red", bold = false, underline = false,
    },
    ButtonDisabled = { fg = "muted", bg = "surface", bold = false },
    ButtonHotkey = { fg = "yellow", bg = "surface", bold = true },
    ButtonHotkeyFocus = { fg = "pill_text", bg = "accent", bold = false },

    ScrollbarTrack = { fg = "border", bg = "surface" },
    ScrollbarThumb = { fg = "scrollbar", bg = "surface" },
    ScrollbarThumbFocus = { fg = "accent", bg = "surface" },

    CommandLine = { fg = "text", bg = "surface" },
    CommandLinePrompt = { fg = "accent", bg = "surface", bold = true },
    CommandLineCursor = { fg = "bg", bg = "accent" },
    CommandLineInfo = { fg = "muted", bg = "surface" },
    Message = { fg = "cyan", bg = "surface" },
    MessageInfo = { fg = "cyan", bg = "surface" },
    MessageWarn = { fg = "yellow", bg = "surface" },
    MessageError = { fg = "red", bg = "surface", bold = false },
    MessageSuccess = { fg = "green", bg = "surface" },

    Separator = { fg = "border" },
    SeparatorFocus = { fg = "accent" },
    Key = { fg = "yellow", bold = true },
    KeyDesc = { fg = "muted" },
    Badge = { fg = "pill_text", bg = "accent", bold = true },
    BadgeAlt = { fg = "pill_text", bg = "accent2", bold = true },
    BadgeGreen = { fg = "pill_text", bg = "green", bold = true },
    Pill = { fg = "pill_text", bg = "accent2", bold = true },
    Breadcrumb = { fg = "muted" },
    BreadcrumbActive = { fg = "text", bold = true },
    Toast = { fg = "text", bg = "panel" },
    ToastBorder = { fg = "border", bg = "panel" },
    ToastInfo = { fg = "cyan", bg = "panel", bold = true },
    ToastWarn = { fg = "yellow", bg = "panel", bold = true },
    ToastError = { fg = "red", bg = "panel", bold = true },
    ToastSuccess = { fg = "green", bg = "panel", bold = true },
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

-- Plugins installed into an existing app register the theme and add a command
-- for re-applying it later.
function M.setup(tuipro, _, app)
  local t = tuipro or require("tuipro")
  M.register(t)
  if app then
    app:set_theme(M.theme.name)
    app:command("opencode", function(_, a)
      a:set_theme(M.theme.name)
      a:success("OpenCode theme applied")
    end, "Apply the opencode theme")
  end
end

-- Standalone entry point used by `tuipro --plugin plugins/opencode`.
function M.run()
  local t = require("tuipro")
  M.register(t)
  return require("tuipro.demo_theme").run({ theme = M.theme.name })
end

return M
