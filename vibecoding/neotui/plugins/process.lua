-- return "Hello!"

-- return string.format("%q", 'a string with "quotes" and \n new line')

local x = string.gsub("hello world", "(%w+)", "%1 %1")

local lines = {}
for i = 1, 100 do
	lines[i] = "Hello world " .. i
end

local result = table.concat(lines, "\n") .. "\n"

return string.format("%s\n%s\n%s", os.tmpname(), x, result)
