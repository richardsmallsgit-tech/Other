assert(type(clonefunction) == 'function')
assert(iscclosure(print) and not islclosure(print))
local f = function(x) return x + 1 end
assert(islclosure(f) and not iscclosure(f))
local g = clonefunction(f)
assert(g ~= f and g(4) == 5)
local t = {}
assert(not isreadonly(t))
setreadonly(t, true)
assert(isreadonly(t))
setreadonly(t, false)
t.x = 1
local mt = {__metatable = 'locked'}
setrawmetatable(t, mt)
assert(getrawmetatable(t) == mt)
local function make() local x=17; return function() return x end end
local closure=make()
assert(debug.getupvalue(closure,1)==17)
debug.setupvalue(closure,1,23)
assert(closure()==23)
print('sUNC portable smoke tests passed')
