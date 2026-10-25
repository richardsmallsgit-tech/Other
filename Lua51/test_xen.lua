local function eq(a, b, label)
  assert(a == b, (label or 'value') .. ': expected ' .. tostring(b) .. ', got ' .. tostring(a))
end

assert(type(xen) == 'table')
assert(type(xen.version()) == 'string')
eq(xen.add(2, 3), 5, 'xen.add')
eq(xen.clamp(9, 0, 5), 5, 'xen.clamp')

-- Hooking a Lua function must affect aliases already pointing at it.
local function sum(a, b)
  return a + b, a * b
end
local alias = sum
local original
original = hookfunction(sum, function(a, b)
  local x, y = original(a, b)
  return x + 10, y + 20
end)
local a, b = sum(2, 3)
eq(a, 15, 'hooked first return')
eq(b, 26, 'hooked second return')
a, b = alias(3, 4)
eq(a, 17, 'alias first return')
eq(b, 32, 'alias second return')
a, b = original(2, 3)
eq(a, 5, 'original first return')
eq(b, 6, 'original second return')

-- Original errors must not leave the hook disabled.
local function mayfail(v)
  if v == 'bad' then error('expected test error') end
  return v .. '!'
end
local original_mayfail
original_mayfail = hookfunction(mayfail, function(v)
  if v == 'replacement-error' then error('replacement test error') end
  return original_mayfail(v) .. ' hooked'
end)
local ok = pcall(mayfail, 'bad')
assert(not ok, 'original error should propagate')
eq(mayfail('good'), 'good! hooked', 'hook restored after error')

-- C functions can be hooked too.
local old_tonumber
old_tonumber = hookfunction(tonumber, function(v)
  if v == 'xen-test' then return 123 end
  return old_tonumber(v)
end)
eq(tonumber('xen-test'), 123, 'hooked C function')
eq(tonumber('42'), 42, 'original C function')

local wrapped = newcclosure(function(x) return x * 2 end)
eq(wrapped(21), 42, 'newcclosure')
assert(iscclosure(wrapped))
assert(isfunction(wrapped))
assert(getgenv() == _G)
eq(xen.typeof(nil), 'nil', 'typeof')

print('XenLua function-hook tests passed')

-- Upvalue access and mutation.
local captured = 12
local function readCaptured()
  return captured
end
local ups = getupvalues(readCaptured)
assert(type(ups) == 'table')
eq(ups[1], 12, 'getupvalues numeric index')
eq(ups.captured, 12, 'getupvalues named entry')
local upname, upvalue = getupvalue(readCaptured, 1)
eq(upname, 'captured', 'getupvalue name')
eq(upvalue, 12, 'getupvalue value')
eq(setupvalue(readCaptured, 1, 99), 'captured', 'setupvalue name')
eq(readCaptured(), 99, 'setupvalue mutation')
assert(getupvalue(readCaptured, 99) == nil, 'missing upvalue should return nil')
assert(setupvalue(readCaptured, 99, 'unused') == nil, 'missing setupvalue should return nil')

-- Constants are read from the Lua function prototype.
local function constantProbe(x)
  local text = 'xen-constant-marker'
  return x + 17, text .. '!', true
end
local constants = getconstants(constantProbe)
assert(type(constants) == 'table', 'getconstants returns a table')
local foundMarker, foundNumber = false, false
for _, value in pairs(constants) do
  if value == 'xen-constant-marker' then foundMarker = true end
  if value == 17 then foundNumber = true end
end
assert(foundMarker, 'getconstants should expose string constants')
assert(foundNumber, 'getconstants should expose numeric constants')
assert(type(getconstants(print)) == 'table', 'C functions return an empty constants table')

print('XenLua upvalue/constants tests passed')


-- Environment, dynamic loading, registry, and raw metatable APIs.
assert(type(getfenv) == 'function' and type(setfenv) == 'function')
assert(type(loadstring) == 'function')
assert(getgenv() == _G, 'getgenv should return global environment')
assert(getrenv() == _G, 'getrenv currently aliases XenLua global environment')
assert(type(getreg()) == 'table', 'getreg should expose the registry')
local registry = getreg()
assert(registry == getreg(), 'registry table should be stable')
local loaded, loadErr = loadstring('return 20 + 22')
assert(type(loaded) == 'function', tostring(loadErr))
eq(loaded(), 42, 'loadstring')
local env = { answer = 42 }
local envfn = assert(loadstring('return answer'))
setfenv(envfn, env)
eq(envfn(), 42, 'setfenv/getfenv')
assert(getfenv(envfn) == env, 'getfenv should return assigned environment')
local protected = setmetatable({}, { __metatable = 'locked', marker = true })
eq(getmetatable(protected), 'locked', 'protected metatable')
local rawmt = getrawmetatable(protected)
assert(type(rawmt) == 'table' and rawmt.marker == true, 'getrawmetatable should bypass __metatable')
local target = {}
setrawmetatable(target, rawmt)
assert(getrawmetatable(target) == rawmt, 'setrawmetatable should install actual metatable')
assert(setrawmetatable(target, {}) == target, 'setrawmetatable should return target')
setrawmetatable(target, nil)
assert(getrawmetatable(target) == nil, 'setrawmetatable should accept nil')

print('XenLua environment/metatable tests passed')


-- GC object discovery: functions are included; tables are opt-in.
local gcMarker = { xen_gc_marker = true }
local function gcProbe() return gcMarker end
local objects = getgc()
assert(type(objects) == 'table', 'getgc returns a table')
local foundFunction = false
for _, object in ipairs(objects) do
  if object == gcProbe then foundFunction = true end
end
assert(foundFunction, 'getgc should find a live Lua function')
local withTables = getgc(true)
local foundTable = false
for _, object in ipairs(withTables) do
  if object == gcMarker then foundTable = true end
end
assert(foundTable, 'getgc(true) should find a live table')
print('XenLua GC discovery tests passed')


-- Function metadata helper and stock debug.getinfo compatibility.
local function metadataProbe(value)
  local marker = "metadata-constant"
  return value, marker
end
local info = getfunctioninfo(metadataProbe)
assert(type(info) == 'table', 'getfunctioninfo returns a table')
assert(info.is_lua == true and info.is_c == false, 'Lua function classification')
assert(info.what == 'Lua', 'Lua function source kind')
assert(type(info.source) == 'string' and #info.source > 0, 'function source metadata')
assert(type(info.short_src) == 'string', 'short source metadata')
assert(type(info.linedefined) == 'number', 'line metadata')
assert(type(info.lastlinedefined) == 'number', 'last line metadata')
assert(type(info.nups) == 'number', 'upvalue count metadata')
assert(info.constant_count >= 1, 'constant count metadata')
local cinfo = getfunctioninfo(print)
assert(cinfo.is_c == true and cinfo.is_lua == false, 'C function classification')
assert(cinfo.constant_count == 0, 'C function constant count')
local dbgInfo = debug.getinfo(metadataProbe, 'Snu')
assert(type(dbgInfo) == 'table' and dbgInfo.what == 'Lua', 'debug.getinfo function query')
assert(debug.getinfo(99999) == nil, 'debug.getinfo out-of-range stack level')
assert(type(xen.getfunctioninfo) == 'function', 'namespaced helper is registered')
assert(xen.getfunctioninfo(metadataProbe).what == 'Lua', 'namespaced helper works')
print('XenLua function metadata tests passed')

-- Native XenLua signal/connection subsystem.
assert(type(Signal) == 'function' and type(xen.Signal) == 'function')
local signal = Signal()
local received = {}
local connection = signal:Connect(function(a, b)
  received[#received + 1] = a + b
end)
assert(connection.Connected and connection.Enabled, 'new connection starts active')
signal:Fire(10, 5)
eq(received[1], 15, 'signal passes arguments')
connection:Disable()
assert(not connection.Enabled and connection.Connected, 'disable preserves connection')
signal:Fire(2, 3)
eq(#received, 1, 'disabled connection does not fire')
connection:Enable()
signal:Fire(2, 3)
eq(received[2], 5, 'enabled connection fires again')
local connections = getconnections(signal)
assert(type(connections) == 'table' and #connections == 1, 'getconnections returns handles')
assert(connections[1] == connection, 'getconnections returns original handle')
connection:Disconnect()
assert(not connection.Connected, 'disconnect changes connected state')
signal:Fire(100, 100)
eq(#received, 2, 'disconnected connection does not fire')
assert(connection:IsConnected() == false and connection:IsEnabled() == false)
local second = signal:Connect(function() error('signal callback error') end)
local okSignal = pcall(function() signal:Fire() end)
assert(not okSignal, 'signal callback errors propagate')
second:Disconnect()
print('XenLua signal/connection tests passed')


-- Cooperative task scheduler: real Lua coroutines, virtual-time waits, defer,
-- cancellation, argument passing, status, and error propagation.
assert(type(task) == 'table' and type(task.spawn) == 'function')
assert(type(xen.task) == 'table' and type(xen.task.step) == 'function')
local taskEvents = {}
local spawned = task.spawn(function(a, b)
  taskEvents[#taskEvents + 1] = a + b
  local elapsed = task.wait(0.5)
  taskEvents[#taskEvents + 1] = elapsed
end, 4, 7)
eq(taskEvents[1], 11, 'task.spawn starts immediately')
eq(task.status(spawned), 'waiting', 'spawned task waits after task.wait')
task.step(0.25)
eq(#taskEvents, 1, 'task does not resume before its delay')
task.step(0.25)
eq(taskEvents[2], 0.5, 'task.wait resumes with elapsed time')
eq(task.status(spawned), 'done', 'task finishes')
local deferredRan = false
local deferred = task.defer(function() deferredRan = true end)
eq(deferredRan, false, 'task.defer does not run immediately')
eq(task.status(deferred), 'deferred', 'deferred task status')
task.step(0)
eq(deferredRan, true, 'task.step runs deferred work')
eq(task.status(deferred), 'done', 'deferred task completes')
local cancelledRan = false
local cancelled = task.defer(function() cancelledRan = true end)
assert(task.cancel(cancelled), 'task.cancel should cancel pending work')
eq(task.status(cancelled), 'cancelled', 'cancelled task status')
task.step(0)
eq(cancelledRan, false, 'cancelled deferred task never runs')
local waitingRan = false
local waiting = task.spawn(function() task.wait(10); waitingRan = true end)
assert(task.cancel(waiting), 'waiting task can be cancelled')
eq(task.status(waiting), 'cancelled', 'waiting task cancellation status')
task.step(20)
eq(waitingRan, false, 'cancelled waiting task does not resume')
local bad = task.defer(function() error('expected scheduled failure') end)
local stepOk, stepErr = pcall(task.step, 0)
assert(not stepOk and tostring(stepErr):find('expected scheduled failure', 1, true), 'task.step propagates task errors')
eq(task.status(bad), 'failed', 'failed task status')
local waitOk = pcall(task.wait, 0)
assert(not waitOk, 'task.wait must reject calls from the main thread')
-- Multiple active tasks survive queue compaction, and nested spawn is drained
-- during the same scheduler step after the parent yields/returns.
local multi = {}
local first = task.spawn(function() multi[#multi + 1] = 'first' end)
local second = task.defer(function() multi[#multi + 1] = 'second' end)
local third = task.defer(function() multi[#multi + 1] = 'third' end)
task.step(0)
eq(multi[1], 'first', 'spawned task completes before the next step')
eq(multi[2], 'second', 'first deferred task executes')
eq(multi[3], 'third', 'second deferred task executes')
eq(task.status(first), 'done', 'completed task status survives compaction')
eq(task.status(second), 'done', 'deferred completion status survives compaction')
eq(task.status(third), 'done', 'second deferred completion status survives compaction')
local nestedRan = false
task.spawn(function()
  task.spawn(function() nestedRan = true end)
end)
assert(nestedRan, 'nested task.spawn is drained by the active scheduler pump')
print('XenLua task scheduler tests passed')
