local socket = require("socket")
local ssl = require("ssl")
local https = require("ssl.https")

local body, code, headers, status = https.request("https://blog.quizbin.com/about")
print(code, status)
print(body)
