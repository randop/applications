-- Tests for the pure-Lua JSON codec, fetch/Promise layer and native client.
--
-- Network tests use the fixture server started by tests/run.sh.  When the
-- TUIPRO_HTTP_PORT / TUIPRO_HTTPS_PORT variables are absent (for example when
-- running this file directly) those tests are skipped.
local here = (arg and arg[0] or "tests/test_http.lua"):match("^(.*)/") or "."
local H = dofile(here .. "/helper.lua")
local A = H.assert
local test = H.test
local t = H.t
local http = t.http
local json = require("tuipro.json")
local Promise = http.Promise
local Headers = http.Headers

local HTTP_PORT = tonumber(os.getenv("TUIPRO_HTTP_PORT") or "")
local HTTPS_PORT = tonumber(os.getenv("TUIPRO_HTTPS_PORT") or "")

local function base()
  return "http://127.0.0.1:" .. HTTP_PORT
end

local function secure_base()
  return "https://127.0.0.1:" .. HTTPS_PORT
end

local function expect_error(fn, fragment)
  local ok, err = pcall(fn)
  A.falsy(ok, "expected an error")
  if fragment then
    A.contains(tostring(err), fragment)
  end
  return tostring(err)
end

-- ---------------------------------------------------------------------------
-- JSON
-- ---------------------------------------------------------------------------

test("json encodes scalars, arrays and sorted objects", function()
  A.eq(json.encode(nil), "null")
  A.eq(json.encode(true), "true")
  A.eq(json.encode(false), "false")
  A.eq(json.encode(42), "42")
  A.eq(json.encode(-3.5), "-3.5")
  A.eq(json.encode("a\"b\\c\n"), '"a\\"b\\\\c\\n"')
  A.eq(json.encode({ 1, 2, 3 }), "[1,2,3]")
  A.eq(json.encode({ b = 2, a = 1 }), '{"a":1,"b":2}')
  A.eq(json.encode({}), "[]")
end)

test("json rejects cycles and non-string object keys", function()
  local cycle = {}
  cycle.self = cycle
  expect_error(function() json.encode(cycle) end, "circular")
  expect_error(function() json.encode({ [2] = "x" }) end, "keys must be strings")
  expect_error(function() json.encode(math.huge) end, "non-finite")
end)

test("json decodes values, null and unicode escapes", function()
  A.eq(json.decode("42"), 42)
  A.eq(json.decode('"hi"'), "hi")
  A.eq(json.decode("[1,2,3]")[2], 2)
  local obj = json.decode('{"a":1,"b":[true,false,null],"c":{"d":"e"}}')
  A.eq(obj.a, 1)
  A.eq(obj.b[1], true)
  A.eq(obj.b[2], false)
  A.eq(obj.b[3], json.null)
  A.eq(obj.c.d, "e")
  A.eq(json.decode('"\\u0041\\u00e9\\ud83d\\ude00"'), "A\195\169\240\159\152\128")
  A.eq(json.decode('\239\187\191{"bom":true}').bom, true)
  A.eq(json.decode('"a\\/b"'), "a/b")
end)

test("json rejects malformed input", function()
  expect_error(function() json.decode("") end, "unexpected end")
  expect_error(function() json.decode("[1,]") end, "unexpected character")
  expect_error(function() json.decode('{"a":1} trailing') end, "trailing")
  expect_error(function() json.decode('"\\q"') end, "invalid escape")
  expect_error(function() json.decode('"\\ud800"') end, "low surrogate")
  expect_error(function() json.decode("01") end, "leading zeros")
  expect_error(function() json.decode('"\1"') end, "control character")
end)

-- ---------------------------------------------------------------------------
-- Headers
-- ---------------------------------------------------------------------------

test("headers support case-insensitive get, append, set and delete", function()
  local h = Headers.new({ ["Content-Type"] = "text/plain", ["X-A"] = "1" })
  A.eq(h:get("content-type"), "text/plain")
  A.eq(h:get("missing"), nil)
  A.truthy(h:has("X-A"))
  h:append("Set-Cookie", "a=1")
  h:append("Set-Cookie", "b=2")
  A.eq(h:get("set-cookie"), "a=1, b=2")
  A.eq(#h:getAll("SET-COOKIE"), 2)
  h:set("X-A", "3")
  A.eq(h:get("x-a"), "3")
  A.eq(#h:getAll("x-a"), 1)
  h:delete("x-a")
  A.falsy(h:has("X-A"))

  local names, values = {}, {}
  for name, value in h:entries() do
    names[#names + 1] = name
    values[#values + 1] = value
  end
  A.eq(#names, 3)
  A.eq(#values, 3)

  local table_form = h:to_table()
  A.eq(table_form["Set-Cookie"], "a=1, b=2")
  local copy = Headers.new(h)
  A.eq(copy:get("content-type"), "text/plain")
end)

test("headers reject invalid names and values", function()
  expect_error(function() Headers.new({ ["bad name"] = "x" }) end,
               "invalid header name")
  expect_error(function() Headers.new({ ["X"] = "a\r\nb" }) end,
               "invalid header value")
end)

-- ---------------------------------------------------------------------------
-- Promise
-- ---------------------------------------------------------------------------

test("promises chain, catch, finally and await", function()
  local order = {}
  local value = Promise.resolve(2):then_(function(v)
    order[#order + 1] = "then"
    return v * 3
  end):then_(function(v)
    order[#order + 1] = "then2"
    return v + 1
  end)
  A.eq(value:await(100), 7)
  A.eq(table.concat(order, ","), "then,then2")

  local caught = Promise.reject("boom"):catch(function(err)
    return "caught:" .. tostring(err)
  end)
  A.eq(caught:await(100), "caught:boom")

  local ran = false
  local after = Promise.resolve("x"):finally(function()
    ran = true
  end)
  A.eq(after:await(100), "x")
  A.truthy(ran)

  local rejected = Promise.reject("no"):finally(function() ran = false end)
  local ok, err = pcall(function() rejected:await(100) end)
  A.falsy(ok)
  A.eq(tostring(err), "no")
  A.falsy(ran)

  local pending = Promise.new()
  A.eq(select(1, pending:next(0)), "pending")
  pending:then_(function() end)
  A.eq(select(1, pending:next(10)), "pending")
end)

test("promise.all preserves order and rejects on first failure", function()
  local all = Promise.all({
    Promise.resolve(1),
    Promise.resolve(2),
    Promise.resolve(3),
  })
  local values = all:await(100)
  A.eq(values[1], 1)
  A.eq(values[2], 2)
  A.eq(values[3], 3)

  local empty = Promise.all({}):await(100)
  A.eq(#empty, 0)

  local failed = Promise.all({
    Promise.resolve("ok"),
    Promise.reject("bad"),
  })
  local ok, err = pcall(function() failed:await(100) end)
  A.falsy(ok)
  A.eq(tostring(err), "bad")
end)

-- ---------------------------------------------------------------------------
-- Local validation that does not touch the network
-- ---------------------------------------------------------------------------

test("fetch validates arguments before starting a request", function()
  local err = expect_error(function() fetch(42):await(100) end,
                           "URL must be a string")
  A.contains(err, "TypeError")
  expect_error(function() fetch(""):await(100) end, "invalid URL")
  expect_error(function() fetch("ftp://example.com"):await(100) end,
               "unsupported URL scheme")
  expect_error(function()
    fetch("http://127.0.0.1:1/", { method = "GET", body = "x" }):await(100)
  end, "cannot have a body")
  expect_error(function()
    fetch("http://127.0.0.1:1/", { headers = { ["bad name"] = "x" } }):await(100)
  end, "invalid header name")
  expect_error(function()
    fetch("http://127.0.0.1:1/", { redirect = "weird" }):await(100)
  end, "unsupported redirect mode")
end)

test("an already-aborted signal rejects immediately", function()
  local controller = http.AbortController.new()
  controller:abort("stop now")
  local signal = controller.signal
  A.truthy(signal.aborted)
  local ok, err = pcall(function()
    fetch("http://127.0.0.1:1/", { signal = signal }):await(100)
  end)
  A.falsy(ok)
  A.eq(tostring(err), "stop now")

  local seen = nil
  signal:addEventListener(function(reason) seen = reason end)
  A.eq(seen, "stop now")
end)

-- ---------------------------------------------------------------------------
-- Network tests
-- ---------------------------------------------------------------------------

if HTTP_PORT then
  test("fetch performs GET, exposes status and body", function()
    local res = fetch(base() .. "/hello"):await(5000)
    A.eq(res.status, 200)
    A.eq(res.statusText, "OK")
    A.truthy(res.ok)
    A.falsy(res.redirected)
    A.eq(res.url, base() .. "/hello")
    A.eq(res:header("content-type"), "text/plain")
    A.eq(res:text():await(1000), "hello from the tuipro test server\n")
    A.eq(res:arrayBuffer():await(1000), res:text():await(1000))
    A.eq(t.http.native.pending(), 0)
  end)

  test("fetch decodes JSON and exposes response headers", function()
    local res = fetch(base() .. "/json"):await(5000)
    local data = res:json():await(1000)
    A.eq(data.ok, true)
    A.eq(data.message, "hello")
    A.eq(data.n, 42)
    A.eq(data.nested.a[2], 2)
    A.eq(res.headers:get("content-type"), "application/json")

    local multi = fetch(base() .. "/headers"):await(5000)
    A.eq(#multi.headers:getAll("set-cookie"), 2)
    A.eq(multi.headers:get("x-test"), "yes")
  end)

  test("fetch follows redirects and reports the final URL", function()
    local res = fetch(base() .. "/redirect"):await(5000)
    A.eq(res.status, 200)
    A.truthy(res.redirected)
    A.eq(res.url, base() .. "/json")
    A.eq(res:json():await(1000).ok, true)

    local chain = fetch(base() .. "/redirect-chain/3"):await(5000)
    A.eq(chain.status, 200)
    A.truthy(chain.redirected)
    A.eq(chain:json():await(1000).redirected, true)

    local absolute = fetch(base() .. "/redirect-absolute"):await(5000)
    A.eq(absolute.status, 200)
    A.eq(absolute.url, base() .. "/json")
  end)

  test("fetch honours manual and error redirect modes", function()
    local manual = fetch(base() .. "/redirect", { redirect = "manual" }):await(5000)
    A.eq(manual.status, 302)
    A.falsy(manual.redirected)
    A.eq(manual.headers:get("location"), "/json")
    A.eq(manual:text():await(1000), "")

    expect_error(function()
      fetch(base() .. "/redirect", { redirect = "error" }):await(5000)
    end, "redirect mode is error")

    expect_error(function()
      fetch(base() .. "/redirect", { max_redirects = 0 }):await(5000)
    end, "redirect count exceeded")
  end)

  test("redirects preserve or drop credentials based on origin", function()
    local direct = fetch(base() .. "/echo", {
      headers = { Authorization = "Bearer secret", Cookie = "a=1" },
    }):await(5000)
    local direct_headers = direct:json():await(1000).headers
    A.eq(direct_headers.authorization, "Bearer secret")
    A.eq(direct_headers.cookie, "a=1")

    local same = fetch(base() .. "/redirect-same", {
      headers = { Authorization = "Bearer secret" },
    }):await(5000)
    A.eq(same:json():await(1000).headers.authorization, "Bearer secret")

    local cross = fetch(base() .. "/redirect-cross", {
      headers = { Authorization = "Bearer secret", Cookie = "a=1" },
    }):await(5000)
    local cross_headers = cross:json():await(1000).headers
    A.eq(cross_headers.authorization, nil)
    A.eq(cross_headers.cookie, nil)
  end)

  test("URL userinfo becomes basic authorization", function()
    local url = "http://user:pass@127.0.0.1:" .. HTTP_PORT .. "/echo"
    local res = fetch(url):await(5000)
    A.eq(res:json():await(1000).headers.authorization, "Basic dXNlcjpwYXNz")
  end)

  test("fetch decodes chunked, EOF and gzip bodies", function()
    local chunked = fetch(base() .. "/chunked"):await(5000)
    A.eq(chunked:text():await(1000), "hello chunked world")

    local ext = fetch(base() .. "/chunked-ext"):await(5000)
    A.eq(ext:text():await(1000), "ext works")

    local eof = fetch(base() .. "/eof"):await(5000)
    A.eq(eof:text():await(1000), "body until eof")

    local gz = fetch(base() .. "/gzip"):await(5000)
    A.eq(gz:json():await(1000).compressed, "gzip")

    local deflate = fetch(base() .. "/deflate"):await(5000)
    A.eq(deflate:json():await(1000).compressed, "deflate")

    local raw = fetch(base() .. "/gzip", { decompress = false }):await(5000)
    local body = raw:text():await(1000)
    A.eq(body:sub(1, 2), "\31\139")
  end)

  test("fetch handles empty, error and large responses", function()
    local nocontent = fetch(base() .. "/nocontent"):await(5000)
    A.eq(nocontent.status, 204)
    A.truthy(nocontent.ok)
    A.eq(nocontent:text():await(1000), "")

    local missing = fetch(base() .. "/status/404"):await(5000)
    A.eq(missing.status, 404)
    A.falsy(missing.ok)
    A.eq(missing:text():await(1000), "status 404")

    local large = fetch(base() .. "/large"):await(5000)
    local large_body = large:text():await(1000)
    A.eq(#large_body, 256 * 1024)
  end)

  test("fetch sends POST bodies and headers", function()
    local res = fetch(base() .. "/echo", {
      method = "POST",
      body = "hello=1&x=2",
      headers = {
        ["Content-Type"] = "application/x-www-form-urlencoded",
        ["X-Test"] = "abc",
      },
    }):await(5000)
    local data = res:json():await(1000)
    A.eq(data.method, "POST")
    A.eq(data.body, "hello=1&x=2")
    A.eq(data.headers["content-type"], "application/x-www-form-urlencoded")
    A.eq(data.headers["x-test"], "abc")
    A.eq(data.headers["content-length"], "11")
  end)

  test("fetch converts 303 POST redirects to GET", function()
    local res = fetch(base() .. "/redirect-post", {
      method = "POST",
      body = "payload",
    }):await(5000)
    A.eq(res.status, 200)
    A.eq(res:text():await(1000), "GET")
  end)

  test("HEAD responses do not wait for a body", function()
    local res = fetch(base() .. "/hello", { method = "HEAD" }):await(5000)
    A.eq(res.status, 200)
    A.eq(res:text():await(1000), "")
  end)

  test("fetch detaches abort listeners after settling", function()
    local controller = http.AbortController.new()
    local before = #controller.signal._listeners
    local res = fetch(base() .. "/json", { signal = controller.signal }):await(5000)
    A.eq(res.status, 200)
    A.eq(#controller.signal._listeners, before)
    A.eq(controller.signal.aborted, false)
  end)

  test("fetch reports timeouts, aborts and refused connections", function()
    expect_error(function()
      fetch(base() .. "/slow", { timeout_ms = 50 }):await(5000)
    end, "timed out")

    local controller = http.AbortController.new()
    local pending = fetch(base() .. "/abort", { signal = controller.signal })
    controller:abort()
    local ok, err = pcall(function() pending:await(5000) end)
    A.falsy(ok)
    A.contains(tostring(err), "AbortError")

    expect_error(function()
      fetch("http://127.0.0.1:1/"):await(5000)
    end, "connect")
    A.eq(t.http.native.pending(), 0)
  end)

  test("fetch rejects malformed server responses", function()
    expect_error(function()
      fetch(base() .. "/bad-status"):await(5000)
    end, "invalid HTTP status")
    expect_error(function()
      fetch(base() .. "/bad-content-length"):await(5000)
    end, "invalid Content-Length")
    expect_error(function()
      fetch(base() .. "/conflicting-length"):await(5000)
    end, "invalid Content-Length")
    expect_error(function()
      fetch(base() .. "/truncated-length"):await(5000)
    end, "before response completed")
    expect_error(function()
      fetch(base() .. "/bad-chunk-size"):await(5000)
    end, "invalid chunk size")
    expect_error(function()
      fetch(base() .. "/bad-chunk-garbage"):await(5000)
    end, "invalid chunk size")
    expect_error(function()
      fetch(base() .. "/bad-chunk-lf"):await(5000)
    end, "invalid chunk terminator")
    expect_error(function()
      fetch(base() .. "/bad-gzip"):await(5000)
    end, "compressed response")
    expect_error(function()
      fetch(base() .. "/oversize-header"):await(5000)
    end, "headers too large")
    A.eq(t.http.native.pending(), 0)
  end)

  test("many sequential and concurrent requests settle cleanly", function()
    for i = 1, 20 do
      local res = fetch(base() .. "/json?i=" .. i):await(5000)
      A.eq(res.status, 200)
      A.eq(res:json():await(1000).n, 42)
    end
    A.eq(t.http.native.pending(), 0)

    local promises = {}
    for i = 1, 8 do
      promises[i] = fetch(base() .. "/status/200?c=" .. i)
    end
    local results = Promise.all(promises):await(5000)
    A.eq(#results, 8)
    for i = 1, #results do
      A.eq(results[i].status, 200)
    end
    A.eq(t.http.native.pending(), 0)
  end)

  test("fetch settles from the app event loop without blocking", function()
    local app = t.App.new({ headless = true, width = 70, height = 16 })
    local settled = false
    local status = nil
    local error_text = nil
    fetch(base() .. "/json"):then_(function(res)
      status = res.status
      settled = true
      app:quit()
    end):catch(function(err)
      error_text = tostring(err)
      settled = true
      app:quit()
    end)
    app:run()
    A.truthy(settled)
    A.eq(error_text, nil)
    A.eq(status, 200)
    A.eq(t.http.native.pending(), 0)
  end)
else
  io.write("(network tests skipped: TUIPRO_HTTP_PORT not set)")
end

if HTTPS_PORT then
  test("fetch verifies TLS certificates and supports opt-out", function()
    expect_error(function()
      fetch(secure_base() .. "/json"):await(5000)
    end, "certificate verify failed")

    local res = fetch(secure_base() .. "/json",
                      { reject_unauthorized = false }):await(5000)
    A.eq(res.status, 200)
    A.eq(res:json():await(1000).ok, true)

    local camel = fetch(secure_base() .. "/json",
                        { rejectUnauthorized = false }):await(5000)
    A.eq(camel.status, 200)
  end)
else
  io.write("(https tests skipped: TUIPRO_HTTPS_PORT not set)")
end

-- ---------------------------------------------------------------------------
-- Demo
-- ---------------------------------------------------------------------------

test("--demo-api renders without touching the network", function()
  local demo = require("tuipro.demo_api")
  local app
  local ok, err = pcall(function()
    app = demo.run({ once = true })
    local rows = H.snapshot()
    A.row(rows, "tuipro fetch")
    A.row(rows, "Response")
    A.row(rows, "History")
    A.row(rows, "libuv")
    A.row(rows, "demo://tuipro/fetch")
    A.row(rows, "Press Enter or click Send")
    A.row(rows, "Send")
    A.row(rows, "Abort")
  end)
  if app then
    app:shutdown()
  end
  if not ok then
    error(err, 0)
  end
end)

return H.finish()
