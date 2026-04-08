-- workspace.lua - Lua FTXUI bridge for Panel 2 (Workspace)
-- This script builds UI elements using the 'workspace' global table

log("Workspace loaded!")

local function demo_workspace_ui()
    -- Create input components
    local input_name = workspace.input("Enter your name")
    local input_age = workspace.input("25")
    
    -- Create checkbox and button components
    local check_enabled = workspace.checkbox("Enable feature", true)
    
    local btn_click = workspace.button("Click Me", function()
        log("Button clicked!")
    end)
    
    -- Create a container with all interactive components - this handles sizing properly
    local form_container = workspace.container_vert({
        input_name,
        input_age,
        check_enabled,
        btn_click,
    })
    
    -- Set the container as root - this ensures proper component handling
    workspace.set_root(form_container)
end

demo_workspace_ui()

return workspace