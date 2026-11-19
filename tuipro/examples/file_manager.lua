-- A small file browser: list the current directory and preview text files.
--
--   build/tuipro examples/file_manager.lua
--   build/tuipro --headless --frames 1 examples/file_manager.lua
local tuipro = require("tuipro")
local ui = tuipro.ui

local app = tuipro.App.new({
  title = "tuipro file manager",
  footer_hint = "↑/↓ select   r reload   q quit",
  background = "Normal",
})

local cwd = os.getenv("PWD") or "."
local entries = {}
local preview = ui.text({
  text = "",
  wrap = false,
  hl = "Normal",
})
local location = ui.text({ text = cwd, hl = "Muted", height = 1 })

local function read_preview(path)
  local f = io.open(path, "rb")
  if not f then
    return "(cannot read " .. path .. ")"
  end
  local data = f:read(4096) or ""
  f:close()
  data = data:gsub("\r\n", "\n"):gsub("[%z\1-\8\11\12\14-\31\127]", "·")
  local lines = {}
  for line in data:gmatch("([^\n]*)\n?") do
    lines[#lines + 1] = line
    if #lines >= 200 then
      break
    end
  end
  return table.concat(lines, "\n")
end

local function is_directory(path)
  local f = io.open(path, "rb")
  if not f then
    return false
  end
  local data, err = f:read(1)
  f:close()
  -- Directories open on Linux but reading them reports an error; empty files
  -- return nil without an error.
  return data == nil and err ~= nil
end

local function reload()
  entries = {}
  local pipe = io.popen("ls -1A 2>/dev/null")
  if pipe then
    for line in pipe:lines() do
      local path = cwd .. "/" .. line
      local dir = is_directory(path)
      entries[#entries + 1] = {
        label = (dir and "▸ " or "  ") .. line,
        detail = dir and "dir" or "file",
        path = path,
        directory = dir,
      }
    end
    pipe:close()
  end
  table.sort(entries, function(a, b)
    if a.directory ~= b.directory then
      return a.directory
    end
    return a.label < b.label
  end)
end

local function show_selected(item)
  if not item then
    preview.text = ""
  elseif item.directory then
    preview.text = "<directory>\n" .. item.path
  else
    preview.text = item.path .. "\n\n" .. read_preview(item.path)
  end
  preview:invalidate()
end

local list = ui.list({
  items = entries,
  flex = 1,
  show_scrollbar = true,
  width = 30,
  on_select = function(item)
    show_selected(item)
  end,
  on_activate = function(item)
    show_selected(item)
    app:info((item.directory and "Directory: " or "File: ") .. item.path)
  end,
})

local split = ui.split({ direction = "vertical" })
split:add(ui.box({
  title = "Directory",
  fill = "NormalFloat",
  padding = 0,
  content = list,
}), { size = 32 })
split:add(ui.box({
  title = "Preview",
  fill = "NormalFloat",
  padding = 1,
  content = preview,
}), { weight = 1 })

reload()
list:set_items(entries, 1, true)
show_selected(entries[1])

app:set_body(ui.vbox({
  gap = 0,
  children = {
    location,
    split,
  },
}))

app:bind("r", function()
  reload()
  list:set_items(entries, 1, true)
  show_selected(entries[1])
  app:success("Reloaded " .. cwd)
end, "Reload")
app:bind("q", function(a)
  a:quit()
end, "Quit")
app:bind("C-c", function(a)
  a:quit()
end, "Quit")

app:run()
