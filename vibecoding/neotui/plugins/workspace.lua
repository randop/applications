-- workspace.lua - Lua FTXUI bridge for Panel 2 (Workspace)
-- This script builds UI elements using the 'workspace' global table

local function demo_workspace_ui()
    local title = workspace.text("Workspace Panel - Lua Built UI")
    local sep = workspace.separator()
    
    local label_name = workspace.text("Name: ")
    local input_name = workspace.input("Enter your name")
    
    local label_age = workspace.text("Age: ")
    local input_age = workspace.input("25")
    
    local check_enabled = workspace.checkbox("Enable feature", true)
    
    local btn_click = workspace.button("Click Me", function()
        print("Button clicked!")
    end)
    
    local name_row = workspace.hbox({ label_name, input_name })
    local age_row = workspace.hbox({ label_age, input_age })
    
    local form = workspace.vbox({
        name_row,
        age_row,
        sep,
        check_enabled,
        sep,
        btn_click,
    })
    
    local bordered = workspace.border(form)
    
    -- Wrap element in a component using renderer
    local root_comp = workspace.renderer(workspace.input(""), function()
        return bordered
    end)
    
    workspace.set_root(root_comp)
end

demo_workspace_ui()

return workspace