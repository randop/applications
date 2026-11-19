-- Vertical menu built on List.  Items may carry a shortcut/key shown on the
-- right-hand side.
local List = require("tuipro.ui.list")

local Menu = List:extend("Menu")

local menu_styles = {
  item = "MenuItem",
  selected = "MenuSelected",
  selected_inactive = "MenuSelectedInactive",
  detail = "MenuDetail",
  detail_selected = "MenuKeySelected",
  disabled = "MenuDisabled",
  marker = "MenuKey",
  marker_selected = "MenuKeySelected",
  index = "ListIndex",
  index_selected = "ListIndexSelected",
  empty = "ListEmpty",
  check = "ListCheck",
  check_selected = "ListCheckSelected",
  header = "MenuHeader",
}

function Menu.new(opts)
  opts = opts or {}
  local self = List.new(opts)
  setmetatable(self, Menu)
  for k, v in pairs(menu_styles) do
    self.styles[k] = v
  end
  for k, v in pairs(opts.styles or {}) do
    self.styles[k] = v
  end
  self.marker = opts.marker or "›"
  self.marker_unselected = opts.marker_unselected or " "
  self.show_marker = opts.show_marker ~= false
  self.show_index = false
  self.detail_align = "right"
  self.name = opts.name or "Menu"
  return self
end

function Menu:_detail(item)
  return tostring(item.shortcut or item.key or item.detail or "")
end

return Menu
