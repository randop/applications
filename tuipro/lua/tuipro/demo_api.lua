-- Built-in HTTP client demo.  Run with: tuipro --demo-api
--
-- The panel on the left renders a response; the list on the right keeps a
-- short request history.  Nothing touches the network until Send is pressed,
-- so `--headless --frames 1 --demo-api` is a safe smoke test.
local t = require("tuipro")
local ui = t.ui
local util = t.util
local hl = t.hl
local json = require("tuipro.json")

local M = {}

local DEFAULT_URL = "https://httpbin.org/json"
local MAX_HISTORY = 8

local DEMO_BODY = [[{
  "demo": "tuipro fetch",
  "native": "libuv + OpenSSL",
  "features": [
    "HTTP and HTTPS",
    "redirects",
    "gzip/deflate",
    "AbortController",
    "Promise then/catch/await"
  ],
  "hint": "Press Enter or click Send to make a real request."
}]]

local function define_highlights()
  hl.define("ApiTitle", { fg = "#c0caf5", bold = true })
  hl.define("ApiLabel", { fg = "#7aa2f7", bold = true })
  hl.define("ApiMuted", { fg = "#565f89" })
  hl.define("ApiUrl", { fg = "#9ece6a" })
  hl.define("ApiOk", { fg = "#9ece6a", bold = true })
  hl.define("ApiErr", { fg = "#f7768e", bold = true })
  hl.define("ApiStatus", { fg = "#e0af68" })
  hl.define("ApiBody", { fg = "#c0caf5" })
  hl.define("ApiBorder", { fg = "#3b4261" })
  hl.define("ApiHistory", { fg = "#a9b1d6" })
end

local function trim(s)
  return (tostring(s or ""):gsub("^%s+", ""):gsub("%s+$", ""))
end

local function short_url(url, width)
  return util.truncate(tostring(url or ""), width or 34)
end

local function header_lines(headers)
  if not headers then
    return ""
  end
  local lines = {}
  for name, value in pairs(headers) do
    lines[#lines + 1] = name:lower() .. ": " .. tostring(value)
  end
  table.sort(lines)
  if #lines > 8 then
    local extra = #lines - 8
    lines = { unpack(lines, 1, 8) }
    lines[#lines + 1] = "… " .. extra .. " more"
  end
  return table.concat(lines, "\n")
end

function M.run(opts)
  if opts == M then
    opts = nil
  end
  opts = opts or {}
  hl.set_transparent(opts.transparent ~= false)
  define_highlights()

  local app = t.App.new({
    title = "tuipro fetch",
    footer_hint = "enter send   esc quit   tab focus   ctrl+q quit",
    background = "Normal",
    width = opts.width,
    height = opts.height,
  })

  local state = {
    url = opts.url or DEFAULT_URL,
    loading = false,
    promise = nil,
    history = {},
    response = {
      status = 200,
      status_text = "OK",
      url = "demo://tuipro/fetch",
      headers = {
        ["content-type"] = "application/json",
        ["x-demo"] = "true",
      },
      body = DEMO_BODY,
      elapsed_ms = 0,
    },
  }
  state.history[1] = {
    method = "GET",
    url = "demo://tuipro/fetch",
    status = 200,
    ms = 0,
    note = "built-in",
  }

  local input
  local send_button
  local abort_button
  local response_summary
  local response_body
  local response_headers
  local history_text
  local status_text

  local function refresh()
    if state.loading then
      status_text.text = "  fetching " .. short_url(state.url, 60) .. " …"
      status_text.hl = "ApiStatus"
    elseif state.response.error then
      status_text.text = "  " .. state.response.error
      status_text.hl = "ApiErr"
    else
      status_text.text = string.format(
          "  %s %s · %d bytes · %.0f ms",
          state.response.status or 0,
          state.response.status_text or "",
          #(state.response.body or ""),
          state.response.elapsed_ms or 0)
      status_text.hl = state.response.status >= 200 and
          state.response.status < 400 and "ApiOk" or "ApiErr"
    end
    response_summary.text = state.response.error and
        ("request failed") or
        string.format("%s %s   %s",
            state.response.status or 0,
            state.response.status_text or "",
            short_url(state.response.url, 48))
    response_summary.hl = state.response.error and "ApiErr" or "ApiOk"
    response_headers.text = header_lines(state.response.headers)
    response_body.text = state.response.body or ""
    response_body.hl = state.response.error and "ApiErr" or "ApiBody"
    if state.loading then
      abort_button.disabled = false
      send_button.disabled = true
    else
      abort_button.disabled = true
      send_button.disabled = false
    end
    app:invalidate()
  end

  local function add_history(entry)
    table.insert(state.history, 1, entry)
    while #state.history > MAX_HISTORY do
      table.remove(state.history)
    end
  end

  history_text = ui.text({
    text = function()
      local lines = {}
      for i = 1, #state.history do
        local h = state.history[i]
        local mark = h.error and "!" or (h.status and h.status >= 200 and
            h.status < 400 and "✓" or "•")
        lines[#lines + 1] = string.format("%s %s %s", mark, h.method or "GET",
            h.status and tostring(h.status) or "…")
        lines[#lines + 1] = "  " .. short_url(h.url, 32)
        if h.ms then
          lines[#lines + 1] = string.format("  %.0f ms", h.ms)
        end
      end
      if #lines == 0 then
        return "No requests yet"
      end
      return table.concat(lines, "\n")
    end,
    wrap = false,
    hl = "ApiHistory",
  })

  local function send()
    if state.loading then
      return
    end
    local url = trim(input:get())
    if url == "" then
      return
    end
    state.url = url
    state.loading = true
    state.response.error = nil
    local started = util.now()
    refresh()

    local promise = fetch(url, { timeout_ms = 20000 })
    state.promise = promise
    promise:then_(function(res)
      res:text():then_(function(body)
        state.loading = false
        state.promise = nil
        state.response = {
          status = res.status,
          status_text = res.statusText,
          url = res.url,
          headers = res.headers:to_table(),
          body = body,
          elapsed_ms = util.now() - started,
        }
        add_history({
          method = "GET",
          url = res.url,
          status = res.status,
          ms = util.now() - started,
        })
        refresh()
      end)
    end):catch(function(err)
      state.loading = false
      state.promise = nil
      local elapsed = util.now() - started
      state.response = {
        status = 0,
        status_text = "ERR",
        url = url,
        headers = {},
        body = tostring(err),
        elapsed_ms = elapsed,
        error = tostring(err),
      }
      add_history({
        method = "GET",
        url = url,
        error = true,
        ms = elapsed,
      })
      refresh()
    end)
  end

  local function abort()
    if state.promise then
      state.promise:abort()
      state.promise = nil
    end
  end

  input = ui.input({
    value = state.url,
    placeholder = "https://example.com/",
    prompt = " ",
    flex = 1,
    hl = "ApiUrl",
    focus_hl = "ApiUrl",
    on_submit = function()
      send()
    end,
  })

  send_button = ui.button({
    label = "Send",
    primary = true,
    on_press = function()
      send()
    end,
  })
  abort_button = ui.button({
    label = "Abort",
    danger = true,
    disabled = true,
    on_press = function()
      abort()
    end,
  })

  response_summary = ui.text({ text = "", hl = "ApiOk", height = 1 })
  response_headers = ui.text({
    text = "",
    hl = "ApiMuted",
    max_lines = 4,
    height = 4,
  })
  response_body = ui.text({
    text = state.response.body,
    wrap = true,
    hl = "ApiBody",
  })
  status_text = ui.text({ text = "", hl = "ApiStatus", height = 1 })

  local response_box = ui.box({
    title = "Response",
    flex = 3,
    border_style = "rounded",
    border_hl = "ApiBorder",
    padding = { left = 1, right = 1, top = 0, bottom = 0 },
    children = {
      ui.vbox({
        gap = 0,
        children = {
          response_summary,
          response_headers,
          ui.separator({ hl = "ApiBorder" }),
          ui.viewport({
            flex = 1,
            children = { response_body },
          }),
        },
      }),
    },
  })

  local history_box = ui.box({
    title = "History",
    width = 40,
    border_style = "rounded",
    border_hl = "ApiBorder",
    padding = { left = 1, right = 1, top = 0, bottom = 0 },
    children = {
      ui.viewport({
        flex = 1,
        children = { history_text },
      }),
    },
  })

  local root = ui.vbox({
    fill = "Normal",
    gap = 0,
    children = {
      ui.hbox({
        height = 2,
        children = {
          ui.text({ text = "  tuipro fetch", hl = "ApiTitle" }),
          ui.spacer(),
          ui.text({ text = "libuv · OpenSSL · HTTP/1.1  ", hl = "ApiMuted" }),
        },
      }),
      ui.hbox({
        height = 1,
        gap = 1,
        children = {
          ui.text({ text = "URL", width = 3, hl = "ApiLabel" }),
          input,
          send_button,
          abort_button,
        },
      }),
      ui.text({ text = "", height = 1 }),
      ui.hbox({
        flex = 1,
        gap = 1,
        children = {
          response_box,
          history_box,
        },
      }),
      status_text,
    },
  })

  app:set_body(root)
  app:bind("C-q", function(a) a:quit() end, "Quit")
  app:bind("C-c", function(a) a:quit() end, "Quit")
  app:bind("escape", function(a) a:quit() end, "Quit")
  app:bind("C-l", function()
    input:set("")
    app:set_focus(input)
  end, "Clear URL")

  refresh()
  app:set_focus(input)

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

M.DEFAULT_URL = DEFAULT_URL
M.json = json

return M
