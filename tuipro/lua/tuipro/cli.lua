-- Access to command-line options passed by the C launcher.
local M = {}

local raw = _G.tuipro_cli
if type(raw) == "table" then
  for k, v in pairs(raw) do
    M[k] = v
  end
end

local function env_bool(name)
  local v = os.getenv(name)
  if not v or v == "" then
    return nil
  end
  if v == "0" or v:lower() == "false" or v:lower() == "no" then
    return false
  end
  return true
end

if M.headless == nil then
  M.headless = env_bool("TUIPRO_HEADLESS") or false
end
M.width = tonumber(M.width) or tonumber(os.getenv("TUIPRO_WIDTH")) or 80
M.height = tonumber(M.height) or tonumber(os.getenv("TUIPRO_HEIGHT")) or 24
M.fps = tonumber(M.fps) or 60
M.frames = tonumber(M.frames) or 0
M.args = M.args or {}

if M.truecolor == nil then
  local v = os.getenv("TUIPRO_TRUECOLOR")
  if v == "0" then
    M.truecolor = false
  elseif v and v ~= "" then
    M.truecolor = true
  end
end

-- An empty --theme= value means "no theme requested".
if M.theme == "" then
  M.theme = nil
end

function M.is_headless()
  return M.headless == true
end

-- Load and apply the theme requested with --theme NAME.
--
-- Returns the resolved theme definition, or nil plus an error message when no
-- theme by that name exists.  Themes shipped as plugins register themselves
-- when the plugin is loaded, so an unknown name is retried after loading a
-- plugin with the same name (for example --theme aurora).
function M.load_theme(name)
  if type(name) ~= "string" or name == "" then
    return nil
  end
  local hl = require("tuipro.hl")
  local theme = require("tuipro.theme")
  local def, err = hl.set_theme(name)
  if def then
    return def
  end
  local ok, entry = pcall(require("tuipro.plugins").load, name)
  if ok and type(entry) == "table" then
    if type(entry.register) == "function" then
      pcall(entry.register, require("tuipro"))
    elseif entry.theme then
      pcall(theme.register, entry.theme)
    end
    def, err = hl.set_theme(name)
    if def then
      return def
    end
  end
  return nil, err or ("unknown theme: " .. tostring(name))
end

return M
