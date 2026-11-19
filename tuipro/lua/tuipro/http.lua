-- Node.js-style fetch built on the native libuv/OpenSSL backend.
--
--   local res = fetch("https://example.com"):await()
--   print(res.status, res:header("content-type"))
--   print(res:text():await())
--
-- In an App the returned promises settle from the normal event loop, so no
-- explicit waiting is needed:
--
--   fetch(url):then_(function(res) ... app:invalidate() end)
--              :catch(function(err) app:error(tostring(err)) end)
local native = require("tuipro.http_native")
local json = require("tuipro.json")

local M = {}

M.json = json

-- ---------------------------------------------------------------------------
-- Errors
-- ---------------------------------------------------------------------------

local FetchError = {}
FetchError.__index = FetchError
FetchError.__tostring = function(self)
  return (self.name or "Error") .. ": " .. (self.message or "")
end

function FetchError.new(message, code, name)
  return setmetatable({
    name = name or (code == "AbortError" and "AbortError")
        or (code == "TimeoutError" and "TimeoutError")
        or "TypeError",
    message = tostring(message or "fetch failed"),
    code = code,
  }, FetchError)
end

M.FetchError = FetchError

-- ---------------------------------------------------------------------------
-- Promise
-- ---------------------------------------------------------------------------

local Promise = {}
Promise.__index = Promise

local function is_promise(v)
  return type(v) == "table" and getmetatable(v) == Promise
end

local function settle(promise, state, value)
  if promise.state ~= "pending" then
    return
  end
  promise.state = state
  promise.value = value
  local handlers = promise.handlers
  promise.handlers = nil
  for i = 1, #handlers do
    local h = handlers[i]
    handlers[i] = nil
    if h then
      h.run()
    end
  end
end

local function run_handler(parent, h)
  local cb
  if parent.state == "fulfilled" then
    cb = h.on_fulfilled
  else
    cb = h.on_rejected
  end
  local child = h.child
  if type(cb) ~= "function" then
    settle(child, parent.state, parent.value)
    return
  end
  local ok, result = pcall(cb, parent.value)
  if not ok then
    settle(child, "rejected", result)
  elseif is_promise(result) then
    result:then_(function(v)
      settle(child, "fulfilled", v)
    end, function(e)
      settle(child, "rejected", e)
    end)
  else
    settle(child, "fulfilled", result)
  end
end

function Promise.new(executor)
  local self = setmetatable({
    state = "pending",
    value = nil,
    handlers = {},
  }, Promise)
  if executor then
    local ok, err = pcall(executor, function(value)
      if is_promise(value) then
        value:then_(function(v) settle(self, "fulfilled", v) end,
                    function(e) settle(self, "rejected", e) end)
      else
        settle(self, "fulfilled", value)
      end
    end, function(reason)
      settle(self, "rejected", reason)
    end)
    if not ok then
      settle(self, "rejected", err)
    end
  end
  return self
end

function Promise.resolve(value)
  if is_promise(value) then
    return value
  end
  local p = Promise.new()
  settle(p, "fulfilled", value)
  return p
end

function Promise.reject(reason)
  local p = Promise.new()
  settle(p, "rejected", reason)
  return p
end

function Promise:then_(on_fulfilled, on_rejected)
  local child = Promise.new()
  local h = {
    child = child,
    on_fulfilled = on_fulfilled,
    on_rejected = on_rejected,
  }
  h.run = function()
    run_handler(self, h)
  end
  if self.state == "pending" then
    self.handlers[#self.handlers + 1] = h
  else
    h.run()
  end
  return child
end

function Promise:catch(on_rejected)
  return self:then_(nil, on_rejected)
end

function Promise:finally(fn)
  return self:then_(function(value)
    local ok, err = pcall(fn)
    if not ok then
      error(err, 0)
    end
    return value
  end, function(reason)
    pcall(fn)
    error(reason, 0)
  end)
end

-- Drive the native loop until this promise settles or timeout_ms expires.
-- Returns "fulfilled", value | "rejected", reason | "pending", nil.
function Promise:next(timeout_ms)
  if self.state ~= "pending" then
    return self.state, self.value
  end
  timeout_ms = timeout_ms or 30000
  local deadline = native.ticks() + timeout_ms
  while self.state == "pending" do
    local remain
    if timeout_ms < 0 then
      remain = -1
    else
      remain = deadline - native.ticks()
      if remain <= 0 then
        break
      end
    end
    local rc = native.run(math.floor(remain))
    if rc < 0 then
      error("cannot wait for a promise from inside a fetch callback", 2)
    end
    if rc == 1 then
      break
    end
  end
  return self.state, self.value
end

Promise.poll = Promise.next

function Promise:await(timeout_ms)
  local state, value = self:next(timeout_ms)
  if state == "fulfilled" then
    return value
  elseif state == "rejected" then
    error(value, 0)
  end
  error(FetchError.new("promise timed out", "TimeoutError"), 0)
end

function Promise:abort()
  if self._request and self.state == "pending" then
    native.abort(self._request)
  end
  return self
end

function Promise.all(promises)
  return Promise.new(function(resolve, reject)
    local results = {}
    local remaining = #promises
    if remaining == 0 then
      resolve(results)
      return
    end
    for i = 1, #promises do
      Promise.resolve(promises[i]):then_(function(value)
        results[i] = value
        remaining = remaining - 1
        if remaining == 0 then
          resolve(results)
        end
      end, reject)
    end
  end)
end

M.Promise = Promise

-- ---------------------------------------------------------------------------
-- Headers
-- ---------------------------------------------------------------------------

local Headers = {}
Headers.__index = Headers

local function header_name(name)
  name = tostring(name or "")
  if name == "" or name:find("[%c%s]") then
    error("invalid header name", 3)
  end
  return name
end

local function header_value(value)
  value = tostring(value == nil and "" or value)
  if value:find("[\r\n%z]") then
    error("invalid header value", 3)
  end
  return value
end

local function header_equals(a, b)
  return a:lower() == b:lower()
end

function Headers.new(init)
  local self = setmetatable({ _items = {} }, Headers)
  if init ~= nil then
    self:_init(init)
  end
  return self
end

function Headers.from_flat(flat)
  local self = Headers.new()
  if type(flat) == "table" then
    for i = 1, #flat, 2 do
      local name = flat[i]
      local value = flat[i + 1]
      if name and value ~= nil then
        self:append(name, value)
      end
    end
  end
  return self
end

function Headers:_init(init)
  if getmetatable(init) == Headers then
    for i = 1, #init._items do
      local item = init._items[i]
      self:append(item.name, item.value)
    end
    return
  end
  if type(init) ~= "table" then
    error("Headers expects a table", 3)
  end
  if #init > 0 and type(init[1]) == "table" then
    for i = 1, #init do
      local pair = init[i]
      if type(pair) == "table" and pair[1] ~= nil then
        self:append(pair[1], pair[2])
      end
    end
    return
  end
  if #init >= 2 and type(init[1]) == "string" and type(init[2]) == "string" then
    -- Flat [name, value, name, value, ...] list.
    for i = 1, #init, 2 do
      self:append(init[i], init[i + 1])
    end
    return
  end
  for name, value in pairs(init) do
    if type(name) == "string" then
      if type(value) == "table" then
        for i = 1, #value do
          self:append(name, value[i])
        end
      else
        self:append(name, value)
      end
    end
  end
end

function Headers:append(name, value)
  name = header_name(name)
  value = header_value(value)
  self._items[#self._items + 1] = { name = name, value = value }
  return self
end

function Headers:set(name, value)
  name = header_name(name)
  value = header_value(value)
  self:delete(name)
  self._items[#self._items + 1] = { name = name, value = value }
  return self
end

function Headers:delete(name)
  local out = {}
  for i = 1, #self._items do
    local item = self._items[i]
    if not header_equals(item.name, name) then
      out[#out + 1] = item
    end
  end
  self._items = out
  return self
end

function Headers:get(name)
  local values = {}
  for i = 1, #self._items do
    local item = self._items[i]
    if header_equals(item.name, name) then
      values[#values + 1] = item.value
    end
  end
  if #values == 0 then
    return nil
  end
  return table.concat(values, ", ")
end

function Headers:getAll(name)
  local values = {}
  for i = 1, #self._items do
    local item = self._items[i]
    if header_equals(item.name, name) then
      values[#values + 1] = item.value
    end
  end
  return values
end

function Headers:has(name)
  return self:get(name) ~= nil
end

function Headers:entries()
  local i = 0
  return function()
    i = i + 1
    local item = self._items[i]
    if item then
      return item.name, item.value
    end
  end
end

Headers.each = Headers.entries

function Headers:keys()
  local i = 0
  return function()
    i = i + 1
    local item = self._items[i]
    if item then
      return item.name
    end
  end
end

function Headers:values()
  local i = 0
  return function()
    i = i + 1
    local item = self._items[i]
    if item then
      return item.value
    end
  end
end

function Headers:forEach(fn)
  for i = 1, #self._items do
    local item = self._items[i]
    fn(item.value, item.name, self)
  end
end

function Headers:to_table()
  local out = {}
  for i = 1, #self._items do
    local item = self._items[i]
    local existing = out[item.name]
    if existing then
      out[item.name] = existing .. ", " .. item.value
    else
      out[item.name] = item.value
    end
  end
  return out
end

-- Native request headers are an array of { name, value } pairs.
function Headers:_pairs()
  local out = {}
  for i = 1, #self._items do
    local item = self._items[i]
    out[#out + 1] = { item.name, item.value }
  end
  return out
end

function Headers:_flat()
  local flat = {}
  for i = 1, #self._items do
    local item = self._items[i]
    flat[#flat + 1] = item.name
    flat[#flat + 1] = item.value
  end
  return flat
end

M.Headers = Headers

-- ---------------------------------------------------------------------------
-- AbortController / AbortSignal
-- ---------------------------------------------------------------------------

local AbortSignal = {}
AbortSignal.__index = AbortSignal

function AbortSignal.new()
  return setmetatable({
    aborted = false,
    reason = nil,
    _listeners = {},
    onabort = nil,
  }, AbortSignal)
end

function AbortSignal:addEventListener(fn)
  if type(fn) ~= "function" then
    error("AbortSignal listener must be a function", 2)
  end
  if self.aborted then
    fn(self.reason)
  else
    self._listeners[#self._listeners + 1] = fn
  end
  return self
end

function AbortSignal:removeEventListener(fn)
  for i = #self._listeners, 1, -1 do
    if self._listeners[i] == fn then
      table.remove(self._listeners, i)
    end
  end
  return self
end

function AbortSignal:_abort(reason)
  if self.aborted then
    return
  end
  self.aborted = true
  self.reason = reason or FetchError.new("The operation was aborted",
                                         "AbortError")
  local listeners = self._listeners
  self._listeners = {}
  if type(self.onabort) == "function" then
    pcall(self.onabort, self.reason)
  end
  for i = 1, #listeners do
    pcall(listeners[i], self.reason)
  end
end

local AbortController = {}
AbortController.__index = AbortController

function AbortController.new()
  return setmetatable({ signal = AbortSignal.new() }, AbortController)
end

function AbortController:abort(reason)
  self.signal:_abort(reason)
  return self
end

M.AbortSignal = AbortSignal
M.AbortController = AbortController

-- ---------------------------------------------------------------------------
-- Response
-- ---------------------------------------------------------------------------

local Response = {}
Response.__index = Response

function Response.new(fields)
  fields = fields or {}
  local self = setmetatable(fields, Response)
  self.status = tonumber(self.status) or 0
  self.statusText = self.statusText or ""
  self.ok = self.status >= 200 and self.status <= 299
  self.redirected = self.redirected == true
  self.type = self.type or "basic"
  self.url = self.url or ""
  self.headers = self.headers or Headers.new()
  self._body = self._body or ""
  return self
end

function Response:header(name)
  return self.headers:get(name)
end

function Response:text()
  return Promise.resolve(self._body)
end

function Response:json()
  return self:text():then_(function(body)
    return json.decode(body)
  end)
end

-- Lua has no ArrayBuffer type; binary-safe strings are the closest thing and
-- match what the native layer already buffers.
function Response:arrayBuffer()
  return Promise.resolve(self._body)
end

function Response:bytes()
  return self:arrayBuffer()
end

M.Response = Response

-- ---------------------------------------------------------------------------
-- fetch
-- ---------------------------------------------------------------------------

local function normalize_options(opts)
  opts = opts or {}
  local out = {
    method = opts.method,
    body = opts.body,
    redirect = opts.redirect,
    max_redirects = opts.max_redirects,
    reject_unauthorized = opts.reject_unauthorized,
    decompress = opts.decompress,
    timeout_ms = opts.timeout_ms or opts.timeout,
  }
  if out.reject_unauthorized == nil then
    out.reject_unauthorized = opts.rejectUnauthorized
  end
  local headers = opts.headers
  if headers ~= nil then
    if getmetatable(headers) ~= Headers then
      headers = Headers.new(headers)
    end
    out.headers = headers:_pairs()
  end
  return out
end

function M.fetch(url, opts)
  opts = opts or {}
  if type(url) ~= "string" then
    return Promise.reject(FetchError.new("fetch URL must be a string"))
  end
  local signal = opts.signal
  if signal and signal.aborted then
    return Promise.reject(signal.reason or
        FetchError.new("The operation was aborted", "AbortError"))
  end

  local native_opts = normalize_options(opts)
  if native_opts.body ~= nil and type(native_opts.body) ~= "string" then
    return Promise.reject(FetchError.new("fetch body must be a string"))
  end

  local promise = Promise.new()
  local request = nil
  local chunks = {}
  local response = nil
  local on_abort = nil

  local function detach_signal()
    if signal and on_abort and type(signal.removeEventListener) == "function" then
      signal:removeEventListener(on_abort)
    end
    on_abort = nil
  end

  local callbacks = {
    on_headers = function(status, status_text, flat_headers, final_url,
                          redirected)
      response = Response.new({
        status = status,
        statusText = status_text,
        headers = Headers.from_flat(flat_headers),
        url = final_url,
        redirected = redirected,
      })
    end,
    on_data = function(chunk)
      chunks[#chunks + 1] = chunk
    end,
    on_end = function()
      if response then
        response._body = table.concat(chunks)
      else
        response = Response.new({ status = 0, _body = table.concat(chunks) })
      end
      detach_signal()
      if promise._request == request then
        promise._request = nil
      end
      settle(promise, "fulfilled", response)
    end,
    on_error = function(message, code)
      detach_signal()
      if promise._request == request then
        promise._request = nil
      end
      settle(promise, "rejected", FetchError.new(message, code))
    end,
  }

  local ok, result = pcall(native.request, url, native_opts, callbacks)
  if not ok then
    detach_signal()
    settle(promise, "rejected", result)
    return promise
  end
  request = result
  promise._request = request

  if signal and promise.state == "pending" then
    on_abort = function()
      if request and promise.state == "pending" then
        native.abort(request)
      end
    end
    if type(signal.addEventListener) == "function" then
      signal:addEventListener(on_abort)
    else
      local previous = signal.onabort
      signal.onabort = function(reason)
        if type(previous) == "function" then
          previous(reason)
        end
        if on_abort then
          on_abort(reason)
        end
      end
    end
    if signal.aborted then
      on_abort()
    end
  end

  return promise
end

M.request = M.fetch

-- Direct access for advanced users and tests.
M.native = native
M.version = native.version

return M
