-- return "Hello!"

-- return string.format("%q", 'a string with "quotes" and \n new line')

local x = string.gsub("hello world", "(%w+)", "%1 %1")
local str = "Hello World!\n"

-- Repeat 100 times
local repeated = string.rep(str, 100)

return string.format("%s \n %s \n %s", os.tmpname(), x, repeated)
