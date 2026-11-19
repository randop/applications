-- Small plugin loader.  A plugin is a Lua chunk (usually init.lua) that
-- returns a function or a table.  Functions are called as f(tuipro, cli);
-- tables may expose setup(tuipro, cli), mount(app), commands and bindings.
local M = {}

local function file_exists(path)
  local f = io.open(path, "rb")
  if not f then
    return false
  end
  -- io.open() also succeeds for directories on Linux; reading distinguishes
  -- a readable (possibly empty) file from a directory.
  local data, err = f:read(1)
  f:close()
  return data ~= nil or err == nil
end

local function join(a, b)
  if a:sub(-1) == "/" then
    return a .. b
  end
  return a .. "/" .. b
end

-- Return a Lua file path for a plugin name or path, or nil.
function M.resolve(name)
  if not name or name == "" then
    return nil
  end
  local init = join(name, "init.lua")
  if file_exists(init) then
    return init
  end
  if file_exists(name) then
    return name
  end
  if name:match("%.lua$") then
    return file_exists(name) and name or nil
  end

  local roots = {}
  local cwd = os.getenv("PWD") or "."
  roots[#roots + 1] = join(cwd, "plugins")
  local runtime = os.getenv("TUIPRO_RUNTIME")
  if runtime and runtime ~= "" then
    roots[#roots + 1] = join(runtime, "plugins")
    roots[#roots + 1] = runtime
  end
  local extra = os.getenv("TUIPRO_PLUGIN_PATH")
  if extra then
    for dir in extra:gmatch("[^:]+") do
      roots[#roots + 1] = dir
    end
  end
  for _, root in ipairs(roots) do
    local p = join(root, join(name, "init.lua"))
    if file_exists(p) then
      return p
    end
    p = join(root, name .. ".lua")
    if file_exists(p) then
      return p
    end
    p = join(root, name)
    if file_exists(p) then
      return p
    end
  end
  return nil
end

function M.discover(dir)
  dir = dir or "plugins"
  local out = {}
  local pipe = io.popen('find "' .. dir:gsub('"', '\\"') ..
      '" -mindepth 2 -maxdepth 2 -name init.lua -type f 2>/dev/null')
  if not pipe then
    return out
  end
  for line in pipe:lines() do
    out[#out + 1] = line
  end
  pipe:close()
  table.sort(out)
  return out
end

-- Load a plugin chunk and call it with (tuipro, cli) if it returns a function.
-- Tables are returned as-is; use install() to apply them to an app.
function M.load(name, tuipro)
  local path = M.resolve(name)
  local result
  if path then
    local chunk, err = loadfile(path)
    if not chunk then
      return nil, err or ("cannot load plugin: " .. tostring(name))
    end
    local ok, value = pcall(chunk)
    if not ok then
      return nil, value
    end
    result = value
  else
    local ok, value = pcall(require, name)
    if not ok then
      return nil, tostring(value)
    end
    result = value
    path = name
  end
  if type(result) == "function" then
    local t = tuipro or require("tuipro")
    local cli = require("tuipro.cli")
    local ok, value = pcall(result, t, cli)
    if not ok then
      return nil, value
    end
    return value, path
  end
  return result, path
end

-- Apply a loaded plugin's optional tables to an application.
function M.install(entry, app, tuipro)
  if type(entry) == "function" then
    return entry(tuipro or require("tuipro"), require("tuipro.cli"), app)
  end
  if type(entry) ~= "table" then
    return entry
  end
  local t = tuipro or require("tuipro")
  local cli = require("tuipro.cli")
  if entry.setup then
    entry.setup(t, cli, app)
  end
  if entry.mount then
    entry.mount(app)
  end
  if app then
    if type(entry.commands) == "table" then
      for name, def in pairs(entry.commands) do
        if type(def) == "table" then
          app:command(name, def.fn or def[1], def.desc or def[2])
        else
          app:command(name, def)
        end
      end
    end
    if type(entry.bindings) == "table" then
      for lhs, def in pairs(entry.bindings) do
        if type(def) == "table" then
          app:bind(lhs, def.fn or def[1], def.desc or def[2])
        else
          app:bind(lhs, def)
        end
      end
    end
  end
  return entry
end

function M.load_all(dir, app, tuipro)
  local loaded = {}
  for _, path in ipairs(M.discover(dir)) do
    local entry, err = M.load(path, tuipro)
    if entry then
      M.install(entry, app, tuipro)
      loaded[#loaded + 1] = entry
    else
      io.stderr:write("[tuipro] plugin " .. path .. ": " .. tostring(err) .. "\n")
    end
  end
  return loaded
end

return M
