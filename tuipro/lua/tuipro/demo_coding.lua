-- Built-in coding demo.  Run with: tuipro --demo-coding
--
-- The screen is styled after opencode's terminal UI: a raised session sidebar,
-- a transcript with assistant messages, tool calls and diff panels, a prompt
-- box and a status footer.  Everything is ordinary Lua on top of the widget
-- toolkit; the transcript is a small custom widget in this file.
local t = require("tuipro")
local ui = t.ui
local util = t.util
local hl = t.hl
local Widget = t.Widget

local M = {}

local MODEL = "claude-sonnet-4"
local SIDEBAR_WIDTH = 34
local MAX_SESSIONS = 8

M.SIDEBAR_WIDTH = SIDEBAR_WIDTH
M.MODEL = MODEL
M.VERSION = t.version or "0.1.0"

-- ---------------------------------------------------------------------------
-- Palette and highlight groups
--
-- Neutral surfaces use the theme's neutral roles (bg, surface, surface2,
-- panel) so hl.set_transparent() lets them fall through to the terminal
-- background.  Accents and diff colors keep their own backgrounds.
-- ---------------------------------------------------------------------------

-- Roles are resolved from the active theme palette, so the coding demo
-- follows hl.set_theme() just like the rest of the toolkit.
local C = {
  bg = "bg",
  panel = "panel",
  raised = "raised",
  border = "border",
  text = "text",
  strong = "strong",
  muted = "muted",
  faint = "faint",
  accent = "accent",
  blue = "blue",
  purple = "purple",
  green = "green",
  red = "red",
  orange = "orange",
  cyan = "cyan",
  yellow = "yellow",
  code = "code",
  selection = "selection",
  selection_detail = "selection_detail",
  add_bg = "add_bg",
  del_bg = "del_bg",
  add_text = "add_text",
  add_num = "add_num",
  del_text = "del_text",
  del_num = "del_num",
  todo_done = "todo_done",
  pill_text = "pill_text",
}

local function define_highlights()
  local groups = {
    -- Base text.  These carry no background and inherit Normal.
    CodingMain = { fg = C.text },
    CodingText = { fg = C.text },
    CodingStrong = { fg = C.strong, bold = true },
    CodingMuted = { fg = C.muted },
    CodingFaint = { fg = C.faint },
    CodingItalic = { fg = C.muted, italic = true },
    CodingAccent = { fg = C.accent, bold = true },
    CodingBlue = { fg = C.blue },
    CodingPurple = { fg = C.purple },
    CodingGreen = { fg = C.green },
    CodingRed = { fg = C.red },
    CodingOrange = { fg = C.orange },
    CodingCyan = { fg = C.cyan },
    CodingYellow = { fg = C.yellow },
    CodingCode = { fg = C.code },
    CodingTodoDone = { fg = C.todo_done, strike = true },
    CodingBorder = { fg = C.border },

    -- Sidebar, including the session list and its scrollbar.
    CodingSidebar = { fg = C.text, bg = "surface" },
    CodingSidebarTitle = { fg = C.strong, bg = "surface", bold = true },
    CodingSidebarAccent = { fg = C.accent, bg = "surface", bold = true },
    CodingSidebarMuted = { fg = C.muted, bg = "surface" },
    CodingSidebarFaint = { fg = C.faint, bg = "surface" },
    CodingSidebarBlue = { fg = C.blue, bg = "surface" },
    CodingSessionItem = { fg = C.text, bg = "surface" },
    CodingSessionSelected = { fg = C.accent, bg = C.selection, bold = true },
    CodingSessionSelectedInactive = { fg = C.accent, bg = "surface" },
    CodingSessionDetail = { fg = C.muted, bg = "surface" },
    CodingSessionDetailSelected = { fg = C.selection_detail, bg = C.selection },
    CodingSessionMarker = { fg = C.accent, bg = "surface", bold = true },
    CodingSessionMarkerSelected = { fg = C.accent, bg = C.selection, bold = true },
    CodingSessionIndex = { fg = C.faint, bg = "surface" },
    CodingSessionIndexSelected = { fg = C.selection_detail, bg = C.selection },
    CodingSessionHeader = { fg = C.muted, bg = "surface", bold = true },
    CodingSessionEmpty = { fg = C.muted, bg = "surface", italic = true },
    CodingSessionDisabled = { fg = C.faint, bg = "surface" },
    CodingSessionCheck = { fg = C.green, bg = "surface" },
    CodingSessionCheckSelected = { fg = C.accent, bg = C.selection, bold = true },
    CodingSessionTrack = { fg = C.border, bg = "surface" },
    CodingSessionThumb = { fg = C.muted, bg = "surface" },
    CodingSessionThumbFocus = { fg = C.accent, bg = "surface" },
    CodingProgressFill = { fg = C.green, bg = "surface" },
    CodingProgressTrack = { fg = C.border, bg = "surface" },
    CodingProgressLabel = { fg = C.muted, bg = "surface" },
    CodingProgressPercent = { fg = C.text, bg = "surface" },

    -- Header strip.
    CodingHeader = { fg = C.text, bg = "surface2" },
    CodingHeaderTitle = { fg = C.strong, bg = "surface2", bold = true },
    CodingHeaderMuted = { fg = C.muted, bg = "surface2" },
    CodingHeaderBlue = { fg = C.blue, bg = "surface2" },
    CodingHeaderGreen = { fg = C.green, bg = "surface2" },
    CodingHeaderAccent = { fg = C.accent, bg = "surface2", bold = true },

    -- Bottom status bar.
    CodingStatus = { fg = C.text, bg = "surface2" },
    CodingStatusMode = { fg = C.pill_text, bg = C.accent, bold = true },
    CodingStatusTitle = { fg = C.strong, bg = "surface2", bold = true },
    CodingStatusInfo = { fg = C.muted, bg = "surface2" },
    CodingStatusBlue = { fg = C.blue, bg = "surface2" },
    CodingStatusFaint = { fg = C.faint, bg = "surface2" },

    -- Prompt panel and input.
    CodingPanel = { fg = C.text, bg = "panel" },
    CodingPanelText = { fg = C.text, bg = "panel" },
    CodingPanelMuted = { fg = C.muted, bg = "panel" },
    CodingPanelFaint = { fg = C.faint, bg = "panel" },
    CodingPanelBlue = { fg = C.blue, bg = "panel" },
    CodingPill = { fg = C.pill_text, bg = C.accent, bold = true },
    CodingBox = { fg = C.text, bg = "panel" },
    CodingInput = { fg = C.text, bg = "panel" },
    CodingInputFocus = { fg = C.text, bg = "panel" },
    CodingInputPlaceholder = { fg = C.muted, bg = "panel", italic = true },
    CodingInputPrompt = { fg = C.accent, bg = "panel", bold = true },
    CodingInputPromptFocus = { fg = C.accent, bg = "panel", bold = true },
    CodingInputBorder = { fg = C.border, bg = "panel" },
    CodingInputBorderFocus = { fg = C.accent, bg = "panel", bold = true },

    -- Transcript scrollbar (the viewport sits on the main background).
    CodingScrollTrack = { fg = C.border, bg = "bg" },
    CodingScrollThumb = { fg = C.faint, bg = "bg" },
    CodingScrollThumbFocus = { fg = C.accent, bg = "bg" },

    -- Tool cards and code lines.
    CodingCardBg = { fg = C.text, bg = "panel" },
    CodingCard = { fg = C.text, bg = "panel" },
    CodingCardStrong = { fg = C.strong, bg = "panel", bold = true },
    CodingCardMuted = { fg = C.muted, bg = "panel" },
    CodingCardFaint = { fg = C.faint, bg = "panel" },
    CodingCardBlue = { fg = C.blue, bg = "panel" },
    CodingCardGreen = { fg = C.green, bg = "panel" },
    CodingCardRed = { fg = C.red, bg = "panel" },
    CodingCardPurple = { fg = C.purple, bg = "panel" },
    CodingCardCode = { fg = C.code, bg = "panel" },

    -- Diff rows.  Colored backgrounds intentionally survive transparency.
    CodingDiffAddBg = { fg = C.green, bg = C.add_bg },
    CodingDiffAdd = { fg = C.add_text, bg = C.add_bg },
    CodingDiffAddNum = { fg = C.add_num, bg = C.add_bg },
    CodingDiffAddMark = { fg = C.green, bg = C.add_bg, bold = true },
    CodingDiffDelBg = { fg = C.red, bg = C.del_bg },
    CodingDiffDel = { fg = C.del_text, bg = C.del_bg },
    CodingDiffDelNum = { fg = C.del_num, bg = C.del_bg },
    CodingDiffDelMark = { fg = C.red, bg = C.del_bg, bold = true },
    CodingDiffCtx = { fg = C.muted, bg = "panel" },
    CodingDiffCtxNum = { fg = C.faint, bg = "panel" },

    -- Command palette popup.
    CodingPopup = { fg = C.text, bg = "panel" },
    CodingPopupBorder = { fg = C.accent, bg = "panel", bold = true },
    CodingPopupTitle = { fg = C.accent, bg = "panel", bold = true },
    CodingPopupFooter = { fg = C.muted, bg = "panel" },
    CodingPaletteItem = { fg = C.text, bg = "panel" },
    CodingPaletteSelected = { fg = C.pill_text, bg = C.accent, bold = true },
    CodingPaletteSelectedInactive = { fg = C.accent, bg = "panel" },
    CodingPaletteDetail = { fg = C.muted, bg = "panel" },
    CodingPaletteDetailSelected = { fg = C.pill_text, bg = C.accent },
    CodingPaletteMarker = { fg = C.accent, bg = "panel", bold = true },
    CodingPaletteMarkerSelected = { fg = C.pill_text, bg = C.accent },
    CodingPaletteTrack = { fg = C.border, bg = "panel" },
    CodingPaletteThumb = { fg = C.muted, bg = "panel" },
    CodingPaletteThumbFocus = { fg = C.accent, bg = "panel" },
  }
  for name, spec in pairs(groups) do
    hl.define(name, spec)
  end
end

-- ---------------------------------------------------------------------------
-- Transcript widget
--
-- Blocks are composed into styled rows whenever the width or content changes.
-- Each row is a list of { text, hl } segments; an optional row background is
-- filled first so diff and card rows look continuous.
-- ---------------------------------------------------------------------------

local Transcript = Widget:extend("Transcript")

local fallback_spinner_frames = { "⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏" }

function Transcript.new(opts)
  opts = opts or {}
  local self = setmetatable({}, Transcript)
  Widget.init(self, opts)
  self.blocks = opts.blocks or {}
  self.phase = opts.phase or 0
  self.focusable = false
  self.name = opts.name or "Transcript"
  self._cache = nil
  self._cache_width = nil
  self._cache_phase = nil
  self._cache_blocks = nil
  return self
end

function Transcript:set_blocks(blocks)
  self.blocks = blocks or {}
  self._cache = nil
  self:invalidate()
  self:invalidate_layout()
  return self
end

function Transcript:set_phase(phase)
  if self.phase == phase then
    return
  end
  self.phase = phase
  self._cache = nil
  self:invalidate()
end

local function line(out, parts)
  out[#out + 1] = { parts = parts }
end

local function blank(out)
  out[#out + 1] = { parts = {} }
end

local function wrapped(out, text, width, style, prefix, prefix_style)
  for _, part in ipairs(util.wrap(tostring(text or ""), math.max(1, width), {})) do
    local parts = {}
    if prefix and prefix ~= "" then
      parts[#parts + 1] = { text = prefix, hl = prefix_style or style }
    end
    parts[#parts + 1] = { text = part, hl = style }
    line(out, parts)
  end
end

local function add_user(self, b, out, width)
  blank(out)
  line(out, {
    { text = "▌ ", hl = "CodingAccent" },
    { text = "You", hl = "CodingStrong" },
    { text = b.time and ("  " .. b.time) or "", hl = "CodingFaint" },
  })
  wrapped(out, b.text, width, "CodingText", "▌ ", "CodingAccent")
end

local function add_assistant(self, b, out, width)
  blank(out)
  local pending = b.pending == true
  local frames = hl.glyph("spinner", "frames", fallback_spinner_frames)
  local frame = pending and frames[(self.phase % #frames) + 1] or
      hl.glyph("spinner", "idle", "◍")
  local parts = {
    { text = "  ", hl = "CodingText" },
    { text = frame .. " ", hl = pending and "CodingYellow" or "CodingAccent" },
    { text = "tuipro", hl = "CodingStrong" },
    { text = "  " .. (b.model or MODEL), hl = "CodingFaint" },
  }
  if pending then
    parts[#parts + 1] = { text = "  ·  thinking", hl = "CodingYellow" }
  end
  line(out, parts)
  local text = b.text
  if pending and (not text or text == "") then
    text = "Thinking…"
  end
  if text and text ~= "" then
    wrapped(out, text, width, pending and "CodingItalic" or "CodingText", "  ", "CodingText")
  end
end

local function add_card_padding(out)
  out[#out + 1] = { parts = {}, bg = "CodingCardBg" }
end

local function add_tool(self, b, out, width)
  blank(out)
  local parts = {
    { text = "  ", hl = "CodingText" },
    { text = "● ", hl = "CodingGreen" },
    { text = b.name or "Tool", hl = "CodingStrong" },
  }
  if b.target and b.target ~= "" then
    parts[#parts + 1] = { text = "  " .. b.target, hl = "CodingBlue" }
  end
  if b.detail and b.detail ~= "" then
    parts[#parts + 1] = { text = "  ·  " .. b.detail, hl = "CodingFaint" }
  end
  line(out, parts)

  if b.lines and #b.lines > 0 then
    add_card_padding(out)
    local start = b.start or 1
    local last = start + #b.lines - 1
    local num_w = math.max(2, #tostring(last))
    for i, text in ipairs(b.lines) do
      local n = start + i - 1
      out[#out + 1] = {
        bg = "CodingCardBg",
        parts = {
          { text = "    ", hl = "CodingCardFaint" },
          { text = string.format("%" .. num_w .. "d  ", n), hl = "CodingCardFaint" },
          { text = text, hl = "CodingCardCode" },
        },
      }
    end
    add_card_padding(out)
  elseif b.output and #b.output > 0 then
    add_card_padding(out)
    for _, text in ipairs(b.output) do
      local style = "CodingCardMuted"
      if text:match("^PASS") or text:match("passed") then
        style = "CodingCardGreen"
      elseif text:match("^FAIL") or text:match("Error") or text:match("failed") then
        style = "CodingCardRed"
      end
      out[#out + 1] = {
        bg = "CodingCardBg",
        parts = {
          { text = "    ", hl = "CodingCardMuted" },
          { text = text, hl = style },
        },
      }
    end
    add_card_padding(out)
  end

  if b.note and b.note ~= "" then
    wrapped(out, b.note, width, "CodingFaint", "  ", "CodingText")
  end
end

local function add_edit(self, b, out, width)
  blank(out)
  local parts = {
    { text = "  ", hl = "CodingText" },
    { text = "● ", hl = "CodingPurple" },
    { text = b.name or "Edit", hl = "CodingStrong" },
    { text = "  " .. (b.target or ""), hl = "CodingBlue" },
  }
  if b.adds and b.adds > 0 then
    parts[#parts + 1] = { text = "  +" .. b.adds, hl = "CodingGreen" }
  end
  if b.dels and b.dels > 0 then
    parts[#parts + 1] = { text = "  −" .. b.dels, hl = "CodingRed" }
  end
  line(out, parts)

  if b.diff and #b.diff > 0 then
    add_card_padding(out)
    local num_w = 3
    for _, d in ipairs(b.diff) do
      local n = tostring(d.n or "")
      num_w = math.max(num_w, #n)
    end
    for _, d in ipairs(b.diff) do
      local n = string.format("%" .. num_w .. "s  ", tostring(d.n or ""))
      if d.t == "add" then
        out[#out + 1] = {
          bg = "CodingDiffAddBg",
          parts = {
            { text = "    ", hl = "CodingDiffAddNum" },
            { text = n, hl = "CodingDiffAddNum" },
            { text = "+ ", hl = "CodingDiffAddMark" },
            { text = d.text, hl = "CodingDiffAdd" },
          },
        }
      elseif d.t == "del" then
        out[#out + 1] = {
          bg = "CodingDiffDelBg",
          parts = {
            { text = "    ", hl = "CodingDiffDelNum" },
            { text = n, hl = "CodingDiffDelNum" },
            { text = "- ", hl = "CodingDiffDelMark" },
            { text = d.text, hl = "CodingDiffDel" },
          },
        }
      else
        out[#out + 1] = {
          bg = "CodingCardBg",
          parts = {
            { text = "    ", hl = "CodingDiffCtxNum" },
            { text = n, hl = "CodingDiffCtxNum" },
            { text = "  ", hl = "CodingDiffCtx" },
            { text = d.text, hl = "CodingDiffCtx" },
          },
        }
      end
    end
    add_card_padding(out)
  end
end

local function add_todo(self, b, out, width)
  blank(out)
  local done = 0
  for _, item in ipairs(b.items or {}) do
    if item.done then
      done = done + 1
    end
  end
  line(out, {
    { text = "  ", hl = "CodingText" },
    { text = "☰ ", hl = "CodingYellow" },
    { text = b.title or "Todos", hl = "CodingStrong" },
    { text = "  " .. done .. "/" .. #(b.items or {}), hl = "CodingFaint" },
  })
  for _, item in ipairs(b.items or {}) do
    line(out, {
      { text = "  ", hl = "CodingText" },
      { text = item.done and "✓ " or "○ ", hl = item.done and "CodingGreen" or "CodingFaint" },
      { text = item.text, hl = item.done and "CodingTodoDone" or "CodingText" },
    })
  end
end

local function add_note(self, b, out, width)
  blank(out)
  wrapped(out, b.text, width, b.hl or "CodingItalic", "  ", "CodingText")
end

local function add_summary(self, b, out, width)
  blank(out)
  local parts = {
    { text = "  ✓ ", hl = "CodingGreen" },
    { text = b.text or "done", hl = "CodingStrong" },
  }
  if b.adds and b.adds > 0 then
    parts[#parts + 1] = { text = "  +" .. b.adds, hl = "CodingGreen" }
  end
  if b.dels and b.dels > 0 then
    parts[#parts + 1] = { text = "  −" .. b.dels, hl = "CodingRed" }
  end
  if b.detail then
    parts[#parts + 1] = { text = "  ·  " .. b.detail, hl = "CodingFaint" }
  end
  line(out, parts)
end

local function add_divider(self, b, out, width)
  blank(out)
  line(out, {
    { text = "  ", hl = "CodingText" },
    { text = string.rep("─", math.max(1, width - 4)), hl = "CodingBorder" },
  })
end

function Transcript:_compose(width)
  width = math.max(1, util.round(width or self.w or 1))
  if self._cache and self._cache_width == width and self._cache_phase == self.phase and
      self._cache_blocks == self.blocks then
    return self._cache
  end
  local out = {}
  local text_w = math.max(8, width - 4)
  for _, b in ipairs(self.blocks) do
    local kind = b.kind or "note"
    if kind == "user" then
      add_user(self, b, out, text_w)
    elseif kind == "assistant" then
      add_assistant(self, b, out, text_w)
    elseif kind == "tool" then
      add_tool(self, b, out, text_w)
    elseif kind == "edit" then
      add_edit(self, b, out, text_w)
    elseif kind == "todo" then
      add_todo(self, b, out, text_w)
    elseif kind == "summary" then
      add_summary(self, b, out, text_w)
    elseif kind == "divider" then
      add_divider(self, b, out, text_w)
    else
      add_note(self, b, out, text_w)
    end
  end
  if #out == 0 then
    line(out, { { text = "  No messages yet.", hl = "CodingMuted" } })
  end
  self._cache = out
  self._cache_width = width
  self._cache_phase = self.phase
  self._cache_blocks = self.blocks
  return out
end

function Transcript:preferred_height(avail)
  return math.max(1, #self:_compose(avail or self.w))
end

function Transcript:render(canvas)
  if self.w <= 0 or self.h <= 0 then
    return
  end
  local rows = self:_compose(self.w)
  local limit = math.min(#rows, self.h)
  for i = 1, limit do
    local row = rows[i]
    local y = i - 1
    if row.bg then
      canvas:fill(0, y, self.w, 1, " ", row.bg)
    end
    local x = 0
    for _, part in ipairs(row.parts) do
      if part.text ~= "" then
        canvas:put(x, y, part.text, part.hl)
        x = x + util.width(part.text)
      end
    end
  end
end

-- ---------------------------------------------------------------------------
-- Sample conversations
-- ---------------------------------------------------------------------------

local function make_sessions()
  return {
    {
      title = "auth middleware",
      detail = "12m",
      branch = "feat/auth-retry",
      context = 48,
      tokens = 18.4,
      blocks = {
        {
          kind = "user",
          time = "12:42",
          text = "Refactor the auth middleware to use the shared retry helper and remove the duplicated backoff loop.",
        },
        {
          kind = "assistant",
          text = "I'll inspect the middleware, the shared helper and the tests that cover the retry path.",
        },
        {
          kind = "tool",
          tool = "read",
          name = "Read",
          target = "src/auth/middleware.ts",
          detail = "64 lines",
          start = 28,
          lines = {
            "export async function authenticate(req: Request) {",
            "  let attempt = 0",
            "  while (attempt < MAX_ATTEMPTS) {",
            "    try {",
            "      const token = await verify(req.headers.authorization)",
            "      return { ok: true, token }",
            "    } catch (err) {",
            "      attempt += 1",
            "      if (attempt >= MAX_ATTEMPTS) throw err",
            "      await sleep(2 ** attempt * 100)",
            "    }",
            "  }",
            "}",
          },
        },
        {
          kind = "tool",
          tool = "bash",
          name = "Bash",
          target = "rg \"withRetry|backoff\" src/auth",
          detail = "exit 0",
          output = {
            "src/auth/middleware.ts:28:  while (attempt < MAX_ATTEMPTS) {",
            "src/auth/middleware.ts:36:      await sleep(2 ** attempt * 100)",
            "src/net/retry.ts:6:export async function withRetry<T>(fn: () => Promise<T>, opts: RetryOptions) {",
          },
        },
        {
          kind = "tool",
          tool = "read",
          name = "Read",
          target = "src/net/retry.ts",
          detail = "18 lines",
          start = 1,
          lines = {
            "export interface RetryOptions {",
            "  attempts: number",
            "  onRetry?: (err: unknown, attempt: number) => void",
            "}",
            "",
            "export async function withRetry<T>(fn: () => Promise<T>, opts: RetryOptions) {",
            "  for (let attempt = 1; ; attempt++) {",
            "    try { return await fn() }",
            "    catch (err) {",
            "      if (attempt >= opts.attempts) throw err",
            "      opts.onRetry?.(err, attempt)",
            "    }",
            "    await sleep(Math.min(1000, 100 * 2 ** attempt))",
            "  }",
            "}",
          },
        },
        {
          kind = "edit",
          name = "Edit",
          target = "src/auth/middleware.ts",
          adds = 7,
          dels = 11,
          diff = {
            { t = "ctx", n = 30, text = "export async function authenticate(req: Request) {" },
            { t = "del", n = 31, text = "  let attempt = 0" },
            { t = "del", n = 32, text = "  while (attempt < MAX_ATTEMPTS) {" },
            { t = "del", n = 33, text = "    try {" },
            { t = "del", n = 34, text = "      const token = await verify(req.headers.authorization)" },
            { t = "del", n = 35, text = "      return { ok: true, token }" },
            { t = "del", n = 36, text = "    } catch (err) {" },
            { t = "del", n = 37, text = "      attempt += 1" },
            { t = "del", n = 38, text = "      if (attempt >= MAX_ATTEMPTS) throw err" },
            { t = "del", n = 39, text = "      await sleep(2 ** attempt * 100)" },
            { t = "del", n = 40, text = "    }" },
            { t = "del", n = 41, text = "  }" },
            { t = "add", n = 31, text = "  const token = await withRetry(" },
            { t = "add", n = 32, text = "    () => verify(req.headers.authorization)," },
            { t = "add", n = 33, text = "    {" },
            { t = "add", n = 34, text = "      attempts: MAX_ATTEMPTS," },
            { t = "add", n = 35, text = "      onRetry: (err, attempt) => log.warn({ err, attempt })," },
            { t = "add", n = 36, text = "    }," },
            { t = "add", n = 37, text = "  )" },
            { t = "add", n = 38, text = "  return { ok: true, token }" },
            { t = "ctx", n = 39, text = "}" },
          },
        },
        {
          kind = "tool",
          tool = "bash",
          name = "Bash",
          target = "npm test -- auth",
          detail = "exit 0",
          output = {
            "PASS  src/auth/middleware.test.ts (14 tests)",
            "PASS  src/net/retry.test.ts (19 tests)",
            "Tests: 33 passed, 33 total",
          },
        },
        {
          kind = "assistant",
          text = "Done. The middleware now calls `withRetry` with the same three-attempt budget and logs each retry at warn level. The duplicated loop and sleep call are gone, and the focused auth suite is green.",
        },
        { kind = "summary", text = "1 file changed", adds = 7, dels = 11, detail = "33 tests passed" },
      },
    },
    {
      title = "flaky retry tests",
      detail = "1h",
      branch = "fix/retry-race",
      context = 31,
      tokens = 8.2,
      blocks = {
        {
          kind = "user",
          time = "11:58",
          text = "The flaky retry tests fail intermittently on CI. Find the race and stabilize them.",
        },
        {
          kind = "assistant",
          text = "I'll run the retry suite a few times and inspect the fake timer setup.",
        },
        {
          kind = "tool",
          tool = "bash",
          name = "Bash",
          target = "npm test -- retry -- --repeat 5",
          detail = "exit 1",
          output = {
            "FAIL  src/net/retry.test.ts",
            "  ● retries with backoff › uses fake timers",
            "    expected 100ms, received 96ms",
            "Tests: 1 failed, 18 passed",
          },
        },
        {
          kind = "tool",
          tool = "read",
          name = "Read",
          target = "src/net/retry.test.ts",
          detail = "64 lines",
          start = 22,
          lines = {
            "beforeEach(() => {",
            "  vi.useFakeTimers()",
            "})",
            "",
            "it(\"retries with backoff\", async () => {",
            "  await vi.runAllTimersAsync()",
            "  expect(sleep).toHaveBeenCalledWith(100)",
            "})",
          },
        },
        {
          kind = "edit",
          name = "Edit",
          target = "src/net/retry.test.ts",
          adds = 2,
          dels = 2,
          diff = {
            { t = "ctx", n = 28, text = "it(\"retries with backoff\", async () => {" },
            { t = "del", n = 29, text = "  await vi.runAllTimersAsync()" },
            { t = "add", n = 29, text = "  await vi.advanceTimersByTimeAsync(100)" },
            { t = "del", n = 30, text = "  expect(sleep).toHaveBeenCalledWith(100)" },
            { t = "add", n = 30, text = "  expect(sleep).toHaveBeenLastCalledWith(100)" },
            { t = "ctx", n = 31, text = "})" },
          },
        },
        {
          kind = "tool",
          tool = "bash",
          name = "Bash",
          target = "npm test -- retry -- --repeat 5",
          detail = "exit 0",
          output = {
            "PASS  src/net/retry.test.ts (19 tests, 5 runs)",
            "Tests: 19 passed, 19 total",
          },
        },
        {
          kind = "assistant",
          text = "The race came from draining timers before the retry helper scheduled its backoff. Advancing by the backoff window makes the assertion deterministic across runs.",
        },
        { kind = "summary", text = "1 file changed", adds = 2, dels = 2, detail = "19 tests passed x5" },
      },
    },
    {
      title = "command palette",
      detail = "3h",
      branch = "feat/palette",
      context = 52,
      tokens = 21.7,
      blocks = {
        {
          kind = "user",
          time = "10:26",
          text = "Add a command palette bound to ctrl+p. It should list the registered commands and run the selected one.",
        },
        {
          kind = "assistant",
          text = "I'll reuse the existing command registry and put the palette in a modal popup.",
        },
        {
          kind = "tool",
          tool = "read",
          name = "Read",
          target = "src/app/commands.ts",
          detail = "88 lines",
          start = 1,
          lines = {
            "export interface Command {",
            "  name: string",
            "  desc?: string",
            "  run: (arg: string, app: App) => void",
            "}",
            "",
            "export function register(app: App) {",
            "  app.command(\"help\", help, \"Show help\")",
            "  app.command(\"quit\", quit, \"Quit\")",
            "}",
          },
        },
        {
          kind = "edit",
          name = "Edit",
          target = "src/app/commands.ts",
          adds = 24,
          dels = 1,
          diff = {
            { t = "ctx", n = 76, text = "  app.command(\"quit\", quit, \"Quit\")" },
            { t = "add", n = 77, text = "  app.bind(\"C-p\", (app) => {" },
            { t = "add", n = 78, text = "    const items = Object.values(app.commands).map((cmd) => ({" },
            { t = "add", n = 79, text = "      label: cmd.name," },
            { t = "add", n = 80, text = "      detail: cmd.desc," },
            { t = "add", n = 81, text = "      command: cmd.name," },
            { t = "add", n = 82, text = "    }))" },
            { t = "add", n = 83, text = "    const list = ui.list({ items, height: 10 })" },
            { t = "add", n = 84, text = "    const popup = ui.popup({ title: \"Commands\", content: list })" },
            { t = "add", n = 85, text = "    list.on_activate = (item) => {" },
            { t = "add", n = 86, text = "      popup:close()" },
            { t = "add", n = 87, text = "      app:execute_command(item.command)" },
            { t = "add", n = 88, text = "    }" },
            { t = "add", n = 89, text = "    app:open_popup(popup, { close_on_esc = true })" },
            { t = "add", n = 90, text = "  }, \"Command palette\")" },
            { t = "ctx", n = 91, text = "}" },
          },
        },
        {
          kind = "tool",
          tool = "bash",
          name = "Bash",
          target = "npm test -- commands",
          detail = "exit 0",
          output = {
            "PASS  src/app/commands.test.ts (9 tests)",
            "Tests: 9 passed, 9 total",
          },
        },
        {
          kind = "assistant",
          text = "The palette opens with ctrl+p, lists every registered command with its description and runs the selection after closing the popup.",
        },
        { kind = "summary", text = "1 file changed", adds = 24, dels = 1, detail = "9 tests passed" },
      },
    },
    {
      title = "plugin docs",
      detail = "1d",
      branch = "docs/plugins",
      context = 27,
      tokens = 6.1,
      blocks = {
        {
          kind = "user",
          time = "09:14",
          text = "Document the plugin hooks with a minimal example and link the page from the README.",
        },
        {
          kind = "assistant",
          text = "I'll read the current plugin guide, then add the hook lifecycle and a complete example.",
        },
        {
          kind = "tool",
          tool = "read",
          name = "Read",
          target = "docs/plugins.md",
          detail = "142 lines",
          start = 1,
          lines = {
            "# Plugins",
            "",
            "A plugin is a Lua chunk returning a function or a table.",
            "",
            "## Hooks",
            "",
            "| Hook | When it runs |",
            "| --- | --- |",
            "| `setup` | before the app starts |",
            "| `mount` | after the widget tree is built |",
          },
        },
        {
          kind = "edit",
          name = "Edit",
          target = "docs/plugins.md",
          adds = 26,
          dels = 0,
          diff = {
            { t = "ctx", n = 16, text = "| `mount` | after the widget tree is built |" },
            { t = "add", n = 17, text = "" },
            { t = "add", n = 18, text = "## Minimal plugin" },
            { t = "add", n = 19, text = "" },
            { t = "add", n = 20, text = "```lua" },
            { t = "add", n = 21, text = "local M = {}" },
            { t = "add", n = 22, text = "" },
            { t = "add", n = 23, text = "function M.setup(tuipro, cli, app)" },
            { t = "add", n = 24, text = "  app:command(\"hello\", function(_, a)" },
            { t = "add", n = 25, text = "    a:success(\"Hello from the plugin\")" },
            { t = "add", n = 26, text = "  end, \"Say hello\")" },
            { t = "add", n = 27, text = "end" },
            { t = "add", n = 28, text = "" },
            { t = "add", n = 29, text = "return M" },
            { t = "add", n = 30, text = "```" },
            { t = "ctx", n = 31, text = "" },
            { t = "ctx", n = 32, text = "Load it with `--plugin path/to/plugin`." },
          },
        },
        {
          kind = "tool",
          tool = "read",
          name = "Read",
          target = "README.md",
          detail = "250 lines",
          start = 202,
          lines = {
            "## Plugins",
            "",
            "A plugin is a Lua chunk returning a function or a table.",
            "",
            "Run it with `tuipro --plugin path/to/plugin`.",
          },
        },
        {
          kind = "edit",
          name = "Edit",
          target = "README.md",
          adds = 1,
          dels = 0,
          diff = {
            { t = "ctx", n = 225, text = "Run it with `tuipro --plugin path/to/plugin`, or load it from Lua with" },
            { t = "ctx", n = 226, text = "`tuipro.plugins.load()` and `tuipro.plugins.install()`." },
            { t = "add", n = 227, text = "" },
            { t = "add", n = 228, text = "See [docs/plugins.md](docs/plugins.md) for the hook lifecycle and a minimal example." },
          },
        },
        {
          kind = "assistant",
          text = "Added the hook lifecycle table, a complete minimal plugin and a README link to the guide. The example uses only the public API.",
        },
        { kind = "summary", text = "2 files changed", adds = 27, dels = 0 },
      },
    },
    {
      title = "sidebar overflow",
      detail = "2d",
      branch = "fix/sidebar",
      context = 39,
      tokens = 12.3,
      blocks = {
        {
          kind = "user",
          time = "08:03",
          text = "The sidebar overflows when the terminal is resized below 80 columns. Reproduce it and make the layout responsive.",
        },
        {
          kind = "assistant",
          text = "I'll reproduce the narrow layout, then clamp the sidebar width against the content minimum.",
        },
        {
          kind = "tool",
          tool = "read",
          name = "Read",
          target = "src/ui/sidebar.ts",
          detail = "96 lines",
          start = 1,
          lines = {
            "export const SIDEBAR_WIDTH = 42",
            "const CONTENT_MIN_WIDTH = 44",
            "",
            "export function sidebarWidth(total: number) {",
            "  return SIDEBAR_WIDTH",
            "}",
          },
        },
        {
          kind = "note",
          text = "Reproduced at 72x24: the fixed 42-column sidebar leaves 30 columns for the chat, below the 44-column content minimum.",
          hl = "CodingYellow",
        },
        {
          kind = "edit",
          name = "Edit",
          target = "src/ui/sidebar.ts",
          adds = 4,
          dels = 2,
          diff = {
            { t = "ctx", n = 1, text = "export const SIDEBAR_WIDTH = 42" },
            { t = "ctx", n = 2, text = "const CONTENT_MIN_WIDTH = 44" },
            { t = "ctx", n = 3, text = "" },
            { t = "del", n = 4, text = "export function sidebarWidth(total: number) {" },
            { t = "del", n = 5, text = "  return SIDEBAR_WIDTH" },
            { t = "add", n = 4, text = "export function sidebarWidth(total: number) {" },
            { t = "add", n = 5, text = "  const available = Math.max(0, total - CONTENT_MIN_WIDTH)" },
            { t = "add", n = 6, text = "  return Math.max(0, Math.min(SIDEBAR_WIDTH, available))" },
            { t = "ctx", n = 7, text = "}" },
          },
        },
        {
          kind = "tool",
          tool = "bash",
          name = "Bash",
          target = "npm test -- sidebar layout",
          detail = "exit 0",
          output = {
            "PASS  src/ui/sidebar.test.ts (11 tests)",
            "PASS  src/app/layout.test.ts (8 tests)",
            "Tests: 19 passed, 19 total",
          },
        },
        {
          kind = "assistant",
          text = "The sidebar now yields width to the content minimum instead of clipping the chat. The 72-column regression test passes and wider layouts still use the full 42 columns.",
        },
        { kind = "summary", text = "1 file changed", adds = 4, dels = 2, detail = "19 tests passed" },
      },
    },
  }
end

-- A short simulated reply, chosen from keywords in the prompt.
local function make_reply(prompt)
  local lower = tostring(prompt or ""):lower()
  if lower:find("doc", 1, true) or lower:find("readme", 1, true) then
    return {
      {
        kind = "assistant",
        text = "On it. I'll outline the hook lifecycle and add a minimal example to the plugin guide.",
      },
      {
        kind = "tool",
        tool = "read",
        name = "Read",
        target = "docs/plugins.md",
        detail = "142 lines",
        start = 12,
        lines = {
          "## Hooks",
          "",
          "| Hook | When it runs |",
          "| --- | --- |",
          "| `setup` | before the app starts |",
          "| `mount` | after the widget tree is built |",
        },
      },
      {
        kind = "edit",
        name = "Edit",
        target = "docs/plugins.md",
        adds = 14,
        dels = 0,
        diff = {
          { t = "ctx", n = 16, text = "| `mount` | after the widget tree is built |" },
          { t = "add", n = 17, text = "" },
          { t = "add", n = 18, text = "## Minimal plugin" },
          { t = "add", n = 19, text = "" },
          { t = "add", n = 20, text = "```lua" },
          { t = "add", n = 21, text = "local M = {}" },
          { t = "add", n = 22, text = "function M.setup(_, _, app)" },
          { t = "add", n = 23, text = "  app:command(\"hello\", function(_, a)" },
          { t = "add", n = 24, text = "    a:success(\"Hello\")" },
          { t = "add", n = 25, text = "  end)" },
          { t = "add", n = 26, text = "end" },
          { t = "add", n = 27, text = "return M" },
          { t = "add", n = 28, text = "```" },
        },
      },
      {
        kind = "assistant",
        text = "The guide now has a complete example that only uses public APIs. I linked the section from the README.",
      },
      { kind = "summary", text = "1 file changed", adds = 14, dels = 0 },
    }
  elseif lower:find("test", 1, true) then
    return {
      {
        kind = "assistant",
        text = "On it. I'll reproduce the failure, tighten the test setup and run the suite repeatedly.",
      },
      {
        kind = "tool",
        tool = "bash",
        name = "Bash",
        target = "npm test -- retry -- --repeat 3",
        detail = "exit 1",
        output = {
          "FAIL  src/net/retry.test.ts",
          "  expected 100ms, received 96ms",
          "Tests: 1 failed, 18 passed",
        },
      },
      {
        kind = "edit",
        name = "Edit",
        target = "src/net/retry.test.ts",
        adds = 2,
        dels = 2,
        diff = {
          { t = "del", n = 29, text = "  await vi.runAllTimersAsync()" },
          { t = "add", n = 29, text = "  await vi.advanceTimersByTimeAsync(100)" },
          { t = "del", n = 30, text = "  expect(sleep).toHaveBeenCalledWith(100)" },
          { t = "add", n = 30, text = "  expect(sleep).toHaveBeenLastCalledWith(100)" },
        },
      },
      {
        kind = "tool",
        tool = "bash",
        name = "Bash",
        target = "npm test -- retry -- --repeat 3",
        detail = "exit 0",
        output = {
          "PASS  src/net/retry.test.ts (19 tests, 3 runs)",
          "Tests: 19 passed, 19 total",
        },
      },
      {
        kind = "assistant",
        text = "The assertion is stable across repeated runs now. The fix advances fake timers by the backoff window instead of draining them immediately.",
      },
      { kind = "summary", text = "1 file changed", adds = 2, dels = 2, detail = "19 tests passed" },
    }
  end

  local target = "src/app/handler.ts"
  if lower:find("sidebar", 1, true) then
    target = "src/ui/sidebar.ts"
  elseif lower:find("palette", 1, true) or lower:find("command", 1, true) then
    target = "src/app/commands.ts"
  elseif lower:find("auth", 1, true) then
    target = "src/auth/middleware.ts"
  end

  return {
    {
      kind = "assistant",
      text = "On it. I'll inspect the relevant code, keep the change small and run the focused tests.",
    },
    {
      kind = "tool",
      tool = "read",
      name = "Read",
      target = target,
      detail = "48 lines",
      start = 1,
      lines = {
        "export async function handle(input: Input) {",
        "  const result = await run(input)",
        "  if (!result.ok) throw new Error(result.message)",
        "  return result.value",
        "}",
      },
    },
    {
      kind = "edit",
      name = "Edit",
      target = target,
      adds = 4,
      dels = 2,
      diff = {
        { t = "ctx", n = 1, text = "export async function handle(input: Input) {" },
        { t = "del", n = 2, text = "  const result = await run(input)" },
        { t = "add", n = 2, text = "  const result = await withRetry(() => run(input), {" },
        { t = "add", n = 3, text = "    attempts: 3," },
        { t = "add", n = 4, text = "  })" },
        { t = "ctx", n = 5, text = "  if (!result.ok) throw new Error(result.message)" },
      },
    },
    {
      kind = "tool",
      tool = "bash",
      name = "Bash",
      target = "npm test -- --runInBand",
      detail = "exit 0",
      output = {
        "PASS  " .. target:gsub("%.ts$", ".test.ts"),
        "Tests: 24 passed, 24 total",
      },
    },
    {
      kind = "assistant",
      text = "The change is in place and the focused suite is green. I kept the diff small so it is easy to review.",
    },
    { kind = "summary", text = "1 file changed", adds = 4, dels = 2, detail = "24 tests passed" },
  }
end

local function find_block(blocks, target)
  for i, b in ipairs(blocks) do
    if b == target then
      return i
    end
  end
  return nil
end

-- ---------------------------------------------------------------------------
-- Entry point
-- ---------------------------------------------------------------------------

function M.run(opts)
  if opts == M then
    opts = nil
  end
  opts = opts or {}
  hl.set_transparent(opts.transparent ~= false)
  define_highlights()

  local app = t.App.new({
    title = "tuipro coding",
    footer_hint = "? help   : command   ctrl+q quit",
    background = "Normal",
    width = opts.width,
    height = opts.height,
  })

  local state = {
    sessions = make_sessions(),
    active = 1,
    phase = 0,
    pending = nil,
    reply_timer = nil,
    sidebar_visible = true,
  }

  local function current()
    return state.sessions[state.active] or state.sessions[1]
  end

  local transcript = Transcript.new({ blocks = current().blocks })

  local session_list

  local function refresh_list()
    local items = {}
    for i, s in ipairs(state.sessions) do
      items[i] = {
        label = s.title,
        detail = s.detail or "",
        session = i,
      }
    end
    session_list:set_items(items, state.active, true)
  end

  session_list = ui.list({
    items = {},
    flex = 1,
    show_marker = true,
    show_scrollbar = true,
    detail_align = "right",
    wheel_step = 3,
    name = "Sessions",
    styles = {
      item = "CodingSessionItem",
      selected = "CodingSessionSelected",
      selected_inactive = "CodingSessionSelectedInactive",
      detail = "CodingSessionDetail",
      detail_selected = "CodingSessionDetailSelected",
      disabled = "CodingSessionDisabled",
      marker = "CodingSessionMarker",
      marker_selected = "CodingSessionMarkerSelected",
      index = "CodingSessionIndex",
      index_selected = "CodingSessionIndexSelected",
      empty = "CodingSessionEmpty",
      check = "CodingSessionCheck",
      check_selected = "CodingSessionCheckSelected",
      header = "CodingSessionHeader",
      scrollbar_track = "CodingSessionTrack",
      scrollbar_thumb = "CodingSessionThumb",
      scrollbar_thumb_focus = "CodingSessionThumbFocus",
    },
  })

  local viewport = ui.viewport({
    content = transcript,
    flex = 1,
    name = "Transcript",
    fill = "CodingMain",
    show_scrollbar = true,
    wheel_step = 3,
    track_hl = "CodingScrollTrack",
    thumb_hl = "CodingScrollThumb",
    thumb_focus_hl = "CodingScrollThumbFocus",
  })

  local prompt_input
  local finish_reply
  local function submit_prompt(text)
    text = util.trim(text)
    if text == "" then
      app:info("Type a prompt first")
      return false
    end
    if state.pending then
      finish_reply()
    end
    local session = current()
    if session.untitled then
      session.untitled = false
      session.title = util.truncate(text, 24)
      refresh_list()
    end
    session.blocks[#session.blocks + 1] = {
      kind = "user",
      text = text,
      time = os.date("%H:%M"),
    }
    local pending = { kind = "assistant", pending = true, text = "Thinking…" }
    session.blocks[#session.blocks + 1] = pending
    state.pending = { session = session, block = pending, prompt = text }
    state.phase = 0
    transcript:set_phase(state.phase)
    transcript:set_blocks(session.blocks)
    prompt_input:set("", true)
    viewport.offset = math.huge
    app:layout_invalidate()
    app:invalidate()
    app:info("Thinking…")
    state.reply_timer = app:after(800, function()
      if state.pending and state.pending.block == pending then
        finish_reply()
      end
    end)
    return true
  end

  function finish_reply()
    local p = state.pending
    if not p then
      return false
    end
    state.pending = nil
    app:clear_message()
    if state.reply_timer then
      app:cancel(state.reply_timer)
      state.reply_timer = nil
    end
    local blocks = p.session.blocks
    local index = find_block(blocks, p.block)
    if index then
      table.remove(blocks, index)
    else
      index = #blocks + 1
    end
    for offset, b in ipairs(make_reply(p.prompt)) do
      table.insert(blocks, index + offset - 1, b)
    end
    p.session.context = math.min(96, (p.session.context or 0) + 4)
    p.session.tokens = (p.session.tokens or 0) + 1.3
    if current() == p.session then
      transcript:set_blocks(blocks)
      viewport.offset = math.huge
      app:layout_invalidate()
    end
    app:invalidate()
    return true
  end

  local function select_session(index)
    index = util.clamp(index or 1, 1, #state.sessions)
    state.active = index
    local session = state.sessions[index]
    transcript:set_blocks(session.blocks)
    transcript:set_phase(state.phase)
    viewport:set_offset(0)
    session_list:set_index(index, true)
    app:set_title("tuipro · " .. session.title)
    app:layout_invalidate()
    app:invalidate()
    return session
  end

  session_list.on_select = function(_, index)
    select_session(index)
  end
  session_list.on_activate = function(_, index)
    select_session(index)
    app:set_focus(prompt_input)
  end

  refresh_list()

  local function new_session()
    local session = {
      title = "untitled session",
      detail = "now",
      branch = "main",
      context = 0,
      tokens = 0,
      untitled = true,
      blocks = {
        {
          kind = "assistant",
          text = "What should we build? Type a prompt below and press enter.",
        },
        {
          kind = "note",
          text = "This screen is a demo: replies, tool calls and diffs are simulated locally.",
        },
      },
    }
    table.insert(state.sessions, 1, session)
    if #state.sessions > MAX_SESSIONS then
      table.remove(state.sessions)
    end
    state.active = 1
    state.phase = 0
    refresh_list()
    transcript:set_blocks(session.blocks)
    viewport:set_offset(0)
    app:set_focus(prompt_input)
    app:set_title("tuipro · " .. session.title)
    app:layout_invalidate()
    app:info("New session")
    return session
  end

  local sidebar_inner = ui.vbox({
    fill = "CodingSidebar",
    gap = 0,
    children = {
      ui.hbox({
        height = 1,
        children = {
          ui.text({ text = "▌", width = 2, hl = "CodingSidebarAccent" }),
          ui.text({ text = "tuipro", hl = "CodingSidebarTitle" }),
          ui.spacer(),
          ui.text({ text = "v" .. M.VERSION, hl = "CodingSidebarFaint" }),
        },
      }),
      ui.text({ text = "terminal coding agent", hl = "CodingSidebarMuted", height = 1 }),
      ui.spacer(1),
      ui.statusline({
        height = 1,
        padding = 0,
        background = "CodingSidebar",
        left = { text = "SESSIONS", hl = "CodingSidebarFaint" },
        right = { text = "+ new", hl = "CodingSidebarAccent" },
      }),
      session_list,
      ui.spacer(1),
      ui.statusline({
        height = 1,
        padding = 0,
        background = "CodingSidebar",
        left = { text = "CONTEXT", hl = "CodingSidebarFaint" },
        right = function()
          return { text = tostring(current().context or 0) .. "%", hl = "CodingSidebarMuted" }
        end,
      }),
      ui.progress({
        value = function()
          return current().context or 0
        end,
        max = 100,
        show_label = false,
        show_percent = false,
        smooth = true,
        height = 1,
        fill_hl = "CodingProgressFill",
        track_hl = "CodingProgressTrack",
      }),
      ui.statusline({
        height = 1,
        padding = 0,
        background = "CodingSidebar",
        left = { text = MODEL, hl = "CodingSidebarBlue" },
        right = function()
          return {
            text = string.format("%.1fk tokens", current().tokens or 0),
            hl = "CodingSidebarFaint",
          }
        end,
      }),
      ui.spacer(1),
      ui.text({ text = "ctrl+p commands  ctrl+o new", hl = "CodingSidebarFaint", height = 1 }),
    },
  })

  local sidebar = ui.padding({
    width = SIDEBAR_WIDTH,
    fill = "CodingSidebar",
    padding = { top = 0, bottom = 1, left = 2, right = 2 },
    content = sidebar_inner,
  })

  local header = ui.statusline({
    height = 1,
    padding = 1,
    background = "CodingHeader",
    left = function()
      local s = current()
      return {
        { text = s.title, hl = "CodingHeaderTitle" },
        { text = "  " .. s.branch, hl = "CodingHeaderMuted" },
      }
    end,
    right = function()
      return {
        { text = MODEL, hl = "CodingHeaderBlue" },
        { text = "  ·  ", hl = "CodingHeaderMuted" },
        { text = "● ready", hl = "CodingHeaderGreen" },
      }
    end,
  })

  local prompt_status = ui.statusline({
    height = 1,
    padding = 2,
    background = "CodingPanel",
    left = function()
      local s = current()
      return {
        { text = " BUILD ", hl = "CodingPill" },
        { text = " " .. MODEL, hl = "CodingPanelText" },
        { text = "  " .. s.branch, hl = "CodingPanelFaint" },
      }
    end,
    right = function()
      local s = current()
      return {
        { text = "context " .. tostring(s.context or 0) .. "%", hl = "CodingPanelMuted" },
        { text = "   " .. string.format("%.1fk tokens", s.tokens or 0), hl = "CodingPanelFaint" },
      }
    end,
  })

  prompt_input = ui.input({
    prompt = "❯ ",
    placeholder = "Ask anything…  @ files  / commands",
    name = "Prompt",
    hl = "CodingInput",
    focus_hl = "CodingInputFocus",
    placeholder_hl = "CodingInputPlaceholder",
    prompt_hl = "CodingInputPrompt",
    prompt_focus_hl = "CodingInputPromptFocus",
    on_submit = function(text)
      submit_prompt(text)
    end,
  })

  local prompt_box = ui.box({
    height = 3,
    border = true,
    border_style = "rounded",
    border_hl = "CodingInputBorder",
    border_focus_hl = "CodingInputBorderFocus",
    fill = "CodingBox",
    padding = { top = 0, bottom = 0, left = 1, right = 1 },
    content = prompt_input,
  })

  local prompt_hints = ui.text({
    text = "enter send   tab focus   ctrl+s sidebar   ctrl+p commands   ctrl+o new",
    hl = "CodingPanelFaint",
    height = 1,
    padding = 2,
    max_lines = 1,
  })

  local prompt_section = ui.vbox({
    height = 6,
    fill = "CodingPanel",
    gap = 0,
    children = {
      prompt_status,
      ui.spacer(1),
      prompt_box,
      prompt_hints,
    },
  })

  local main = ui.vbox({
    flex = 1,
    fill = "CodingMain",
    gap = 0,
    children = {
      header,
      ui.separator({ height = 1, hl = "CodingBorder" }),
      viewport,
      prompt_section,
    },
  })

  local body = ui.hbox({
    gap = 0,
    children = { sidebar, main },
  })
  app:set_body(body)

  app:set_statusline(ui.statusline({
    height = 1,
    padding = 1,
    background = "CodingStatus",
    left = function()
      local s = current()
      return {
        { text = " BUILD ", hl = "CodingStatusMode" },
        { text = " tuipro coding ", hl = "CodingStatusTitle" },
        { text = " " .. s.branch, hl = "CodingStatusInfo" },
      }
    end,
    right = function()
      local s = current()
      return {
        { text = tostring(#state.sessions) .. " sessions", hl = "CodingStatusFaint" },
        { text = "   " .. MODEL, hl = "CodingStatusBlue" },
        { text = "   " .. string.format("%.1fk tokens", s.tokens or 0), hl = "CodingStatusInfo" },
      }
    end,
  }))

  local function toggle_sidebar()
    state.sidebar_visible = not state.sidebar_visible
    sidebar.visible = state.sidebar_visible
    if not state.sidebar_visible then
      local focus = app.focus
      while focus do
        if focus == session_list then
          app:set_focus(prompt_input)
          break
        end
        focus = focus.parent
      end
    end
    app:layout_invalidate()
    app:info(state.sidebar_visible and "Sidebar shown" or "Sidebar hidden")
  end

  local function open_palette()
    local commands = {
      { label = "New session", detail = "ctrl+o", run = function()
        new_session()
      end },
      { label = "Toggle sidebar", detail = "ctrl+s", run = function()
        toggle_sidebar()
      end },
      { label = "Scroll transcript to top", detail = "home", run = function()
        viewport:set_offset(0)
      end },
      { label = "Scroll transcript to bottom", detail = "end", run = function()
        viewport.offset = math.huge
        app:layout_invalidate()
      end },
      { label = "Focus session list", detail = "sessions", run = function()
        app:set_focus(session_list)
      end },
      { label = "Help", detail = "?", run = function()
        app:show_help()
      end },
      { label = "Quit", detail = "ctrl+q", run = function()
        app:quit()
      end },
    }
    local items = {}
    for i, cmd in ipairs(commands) do
      items[i] = { label = cmd.label, detail = cmd.detail, command = cmd }
    end
    local list = ui.list({
      items = items,
      height = #items,
      show_scrollbar = false,
      detail_align = "right",
      styles = {
        item = "CodingPaletteItem",
        selected = "CodingPaletteSelected",
        selected_inactive = "CodingPaletteSelectedInactive",
        detail = "CodingPaletteDetail",
        detail_selected = "CodingPaletteDetailSelected",
        marker = "CodingPaletteMarker",
        marker_selected = "CodingPaletteMarkerSelected",
        index = "CodingPaletteItem",
        index_selected = "CodingPaletteSelected",
        empty = "CodingPaletteItem",
        check = "CodingPaletteItem",
        check_selected = "CodingPaletteSelected",
        header = "CodingPaletteItem",
        scrollbar_track = "CodingPaletteTrack",
        scrollbar_thumb = "CodingPaletteThumb",
        scrollbar_thumb_focus = "CodingPaletteThumbFocus",
      },
    })
    local popup
    popup = ui.popup({
      title = "Commands",
      width = math.min(58, app.width - 4),
      height = #items + 5,
      footer = "Enter runs · Esc closes",
      fill = "CodingPopup",
      border_hl = "CodingPopupBorder",
      title_hl = "CodingPopupTitle",
      footer_hl = "CodingPopupFooter",
      content = list,
    })
    list.on_activate = function(item)
      popup:close()
      if item.command and item.command.run then
        item.command.run()
      end
    end
    app:open_popup(popup, { close_on_esc = true })
    return popup
  end

  app.coding = {
    state = state,
    sessions = state.sessions,
    session_list = session_list,
    transcript = transcript,
    viewport = viewport,
    prompt = prompt_input,
    prompt_input = prompt_input,
    select_session = select_session,
    submit_prompt = submit_prompt,
    finish_reply = finish_reply,
    new_session = new_session,
    toggle_sidebar = toggle_sidebar,
    open_palette = open_palette,
    active_session = current,
  }

  app:command("new", function()
    new_session()
  end, "Start a new coding session")
  app:command("sessions", function()
    app:set_focus(session_list)
  end, "Focus the session list")
  app:command("sidebar", function()
    toggle_sidebar()
  end, "Toggle the session sidebar")
  app:command("palette", function()
    open_palette()
  end, "Open the command palette")
  app:command("clear", function()
    local s = current()
    s.blocks = {}
    transcript:set_blocks(s.blocks)
    viewport:set_offset(0)
    app:layout_invalidate()
  end, "Clear the current transcript")

  app:bind("C-s", function()
    toggle_sidebar()
  end, "Toggle sidebar")
  app:bind("C-p", function()
    open_palette()
  end, "Command palette")
  app:bind("C-o", function()
    new_session()
  end, "New session")

  app:every(90, function()
    if state.pending then
      state.phase = state.phase + 1
      transcript:set_phase(state.phase)
    end
  end)

  select_session(1)
  app:set_focus(prompt_input)

  if opts.setup then
    opts.setup(app)
  end
  if opts.once then
    app:layout()
    app:render()
    return app
  end
  app:run()
  return app
end

return M
