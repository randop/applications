-- Comprehensive LUA script demonstrating all standard libraries

local results = {}

-- Helper to add a result line
local function add(s)
	table.insert(results, tostring(s))
end

add("=== Lua Standard Libraries Demo ===")
add("Lua version: " .. _VERSION)

-- 1. Basic library (_G, assert, error, ipairs, pairs, pcall, select, tonumber, tostring, type, etc.)
add("\n--- Basic Library ---")
add("Type of 42: " .. type(42))
add("Type of 'hello': " .. type("hello"))
add("Tonumber('123') = " .. tonumber("123"))
add("Tostring(3.14) = " .. tostring(3.14))

local success, err = pcall(function()
	error("Test error")
end)
add("pcall caught error: " .. tostring(success) .. " | " .. tostring(err))

-- ipairs and pairs
local t = { 10, 20, nil, 40 }
local ip = {}
for i, v in ipairs(t) do
	table.insert(ip, i .. "=" .. v)
end
add("ipairs on {10,20,nil,40}: " .. table.concat(ip, ", "))

local p = {}
for k, v in pairs(t) do
	table.insert(p, k .. "=" .. tostring(v))
end
add("pairs on same table: " .. table.concat(p, ", "))

-- select
add("select('#', 1,2,3,4) = " .. select("#", 1, 2, 3, 4))
add("select(2, 'a','b','c') = " .. select(2, "a", "b", "c"))

-- 2. Coroutine library
add("\n--- Coroutine Library ---")
local co = coroutine.create(function()
	add("  Coroutine yielded with: " .. coroutine.yield("hello from coro"))
	return "done"
end)

local status, val = coroutine.resume(co, "initial")
add("Coroutine status after first resume: " .. tostring(status) .. " | value: " .. tostring(val))

local status2, val2 = coroutine.resume(co)
add("Coroutine final resume: " .. tostring(status2) .. " | value: " .. tostring(val2))

add("Coroutine status: " .. coroutine.status(co))

-- 3. Package library
add("\n--- Package Library ---")
add("package.path: " .. tostring(package.path))
add("package.cpath: " .. tostring(package.cpath))
add("package.loaded._G exists: " .. tostring(package.loaded._G ~= nil))
add("package.preload count (approx): " .. tostring(#(package.preload or {})))

-- 4. String library
add("\n--- String Library ---")
local s = "  Hello, Lua World!  "
add("Original: '" .. s .. "'")
add("len: " .. #s)
add("upper: " .. string.upper(s))
add("lower: " .. string.lower(s))
add("sub(8,11): " .. string.sub(s, 8, 11))
add("find 'Lua': " .. tostring(string.find(s, "Lua")))
add("gsub 'l'->'L': " .. string.gsub(s, "l", "L"))
add("format: %d %s %.2f = " .. string.format("%d %s %.2f", 42, "test", 3.14159))
add("match '(%w+)': " .. (string.match(s, "(%w+)") or ""))

-- Byte and char
add("byte(1): " .. string.byte(s, 1))
add("char(72,101,108): " .. string.char(72, 101, 108))

-- 5. Table library
add("\n--- Table Library ---")
local tbl = { 1, 2, 3, 4, 5 }
table.insert(tbl, 6)
add("After insert 6: " .. table.concat(tbl, ", "))

table.remove(tbl, 3)
add("After remove index 3: " .. table.concat(tbl, ", "))

table.sort(tbl, function(a, b)
	return a > b
end)
add("Sorted descending: " .. table.concat(tbl, ", "))

local concat_test = table.concat({ "a", "b", "c" }, "-")
add("table.concat demo: " .. concat_test)

-- 6. Math library
add("\n--- Math Library ---")
add("math.pi ≈ " .. math.pi)
add("math.sqrt(16) = " .. math.sqrt(16))
add("math.sin(math.pi/2) = " .. math.sin(math.pi / 2))
add("math.floor(3.7) = " .. math.floor(3.7))
add("math.ceil(3.2) = " .. math.ceil(3.2))
add("math.max(10, 20, 5) = " .. math.max(10, 20, 5))
add("math.min(10, 20, 5) = " .. math.min(10, 20, 5))
add("math.randomseed(42); math.random() ≈ " .. math.random())
add("math.fmod(10, 3) = " .. math.fmod(10, 3))
add("math.exp(1) ≈ " .. math.exp(1))
add("math.log( math.exp(1) ) = " .. math.log(math.exp(1)))

-- 7. OS library (non-file operations: time, date, clock, getenv, etc.)
add("\n--- OS Library (non-IO) ---")
add("os.clock() (CPU time) ≈ " .. os.clock())
add("os.time() (timestamp) = " .. os.time())
add("os.date('%Y-%m-%d %H:%M:%S') = " .. os.date("%Y-%m-%d %H:%M:%S"))
add("os.difftime(os.time(), os.time()-3600) = " .. os.difftime(os.time(), os.time() - 3600))

local env_path = os.getenv("PATH") or "PATH not set"
add("os.getenv('PATH') length: " .. #env_path)

add("os.tmpname(): " .. os.tmpname())
-- os.execute, os.remove, os.rename are avoided to stay safe / minimal

-- 8. Debug library
add("\n--- Debug Library ---")
add("debug.getinfo(0, 'n').namewhat (approx): " .. tostring((debug.getinfo(0, "n") or {}).namewhat))
add("debug.getlocal(1, 1) exists: " .. tostring(debug.getlocal(1, 1) ~= nil))

-- Simple traceback example
local tb = debug.traceback("Demo traceback", 1)
add("debug.traceback (first 100 chars): " .. string.sub(tb, 1, 100) .. "...")

-- 9. Final summary
add("\n=== End of Demo ===")
add("Total results collected: " .. #results)

-- Output everything concatenated by newline
local output = table.concat(results, "\n")
return output
