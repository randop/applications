-- Simple timer function that callbacks to C++ every second
-- Runs 10 times to allow clean shutdown without infinite loop

function timer()
	for i = 1, 10 do
		local timestamp = os.date("%Y-%m-%d %H:%M:%S")
		cpp_callback(timestamp) -- Call registered C++ function
		sleep(1) -- Sleep 1 second using C++-exposed function
	end
	print("Lua timer completed")
end

timer()
