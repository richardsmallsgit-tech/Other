// Portable Xen API: uses only public Luau APIs; no Roblox offsets or Lua 5.1 internals.
#include "XenPortable.h"
#include "lua.h"
#include "lualib.h"
extern void* luaX_hookkey();

static int xen_add(lua_State* L) { double a=luaL_checknumber(L,1), b=luaL_checknumber(L,2); lua_pushnumber(L,a+b); return 1; }
static int xen_clamp(lua_State* L) { double v=luaL_checknumber(L,1), lo=luaL_checknumber(L,2), hi=luaL_checknumber(L,3); if(lo>hi) { luaL_error(L,"xen.clamp: minimum must be <= maximum"); return 0; } lua_pushnumber(L,v<lo?lo:v>hi?hi:v); return 1; }
static int xen_version(lua_State* L) { lua_pushstring(L,"XenLua Luau (portable runtime)"); return 1; }
static int xen_typeof(lua_State* L) { luaL_checkany(L,1); lua_pushstring(L,luaL_typename(L,1)); return 1; }
static int xen_iscclosure(lua_State* L) { lua_pushboolean(L,lua_iscfunction(L,1)); return 1; }
static int xen_isfunction(lua_State* L) { lua_pushboolean(L,lua_isfunction(L,1)); return 1; }
static int xen_getgenv(lua_State* L) { lua_pushvalue(L,LUA_GLOBALSINDEX); return 1; }
static int xen_getreg(lua_State* L) { lua_pushvalue(L,LUA_REGISTRYINDEX); return 1; }
static int xen_getrawmetatable(lua_State* L) { luaL_checkany(L,1); if(!lua_getmetatable(L,1)) lua_pushnil(L); return 1; }
static int xen_getupvalues(lua_State* L) {
    luaL_checktype(L,1,LUA_TFUNCTION); lua_newtable(L);
    for(int i=1; i<=200; ++i) {
        const char* name=lua_getupvalue(L,1,i); if(!name) break;
        lua_rawseti(L,-2,i); // consumes value
    }
    return 1;
}
static int xen_getupvalue(lua_State* L) {
    luaL_checktype(L,1,LUA_TFUNCTION); int i=(int)luaL_checkinteger(L,2);
    if(i<1) { luaL_error(L,"getupvalue: index must be positive"); return 0; }
    const char* name=lua_getupvalue(L,1,i); if(!name) {lua_pushnil(L); return 1;}
    lua_pushstring(L,name); lua_insert(L,-2); return 2;
}
static int xen_setupvalue(lua_State* L) {
    luaL_checktype(L,1,LUA_TFUNCTION); int i=(int)luaL_checkinteger(L,2); luaL_checkany(L,3);
    lua_pushvalue(L,3); const char* name=lua_setupvalue(L,1,i);
    if(name) lua_pushstring(L,name); else lua_pushnil(L); return 1;
}
// A real native C closure that forwards to a captured Luau callable.
// Preserve all arguments and multiple return values.
static int xen_newcclosure_dispatch(lua_State* L) {
    const int argc = lua_gettop(L);
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_insert(L, 1);
    lua_call(L, argc, LUA_MULTRET);
    return lua_gettop(L);
}
static int xen_newcclosure(lua_State* L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    lua_pushvalue(L, 1);
    lua_pushcclosure(L, xen_newcclosure_dispatch, "xen_newcclosure", 1);
    return 1;
}

// Hook registry lives in LUA_REGISTRYINDEX, so the GC retains all entries.
static int xen_hookfunction(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TFUNCTION);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    // lua_clonefunction preserves both Luau and native C closure state, including
    // C continuations and captured upvalues.
    // Reject self-hooking to prevent an immediate infinite recursion.
    if (lua_rawequal(L, 1, 2)) { luaL_error(L, "hookfunction: target and replacement must differ"); return 0; }
    lua_pushlightuserdata(L, luaX_hookkey());
    lua_rawget(L, LUA_REGISTRYINDEX);
    if (lua_isnil(L, -1))
    {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushlightuserdata(L, luaX_hookkey());
        lua_pushvalue(L, -2);
        lua_rawset(L, LUA_REGISTRYINDEX);
    }
    const int hooks = lua_gettop(L);
    lua_pushvalue(L, 1);
    lua_rawget(L, hooks);
    if (!lua_isnil(L, -1))
    {
        // The previous replacement is the original behavior of a re-hooked target.
        lua_pushvalue(L, -1);
    }
    else
    {
        lua_clonefunction(L, 1);
    }
    const int original = lua_gettop(L);
    lua_pushvalue(L, 1);
    lua_pushvalue(L, 2);
    lua_rawset(L, hooks);
    lua_pushvalue(L, original);
    return 1;
}

// sUNC portable subset. Roblox engine-dependent APIs are intentionally not stubbed.
static int sunc_islclosure(lua_State* L) { lua_pushboolean(L, lua_isfunction(L,1) && !lua_iscfunction(L,1)); return 1; }
static int sunc_clonefunction(lua_State* L) { luaL_checktype(L,1,LUA_TFUNCTION); lua_clonefunction(L,1); return 1; }
static int sunc_isreadonly(lua_State* L) { luaL_checktype(L,1,LUA_TTABLE); lua_pushboolean(L,lua_getreadonly(L,1)); return 1; }
static int sunc_setreadonly(lua_State* L) { luaL_checktype(L,1,LUA_TTABLE); lua_setreadonly(L,1,lua_toboolean(L,2)); return 0; }
static int sunc_setrawmetatable(lua_State* L) {
    luaL_checkany(L,1);
    if (!lua_isnil(L,2)) luaL_checktype(L,2,LUA_TTABLE);
    lua_settop(L,2);
    lua_setmetatable(L,1);
    lua_pushvalue(L,1);
    return 1;
}
static int sunc_identifyexecutor(lua_State* L) { lua_pushstring(L,"XenLuau"); lua_pushstring(L,"portable"); return 2; }
static int sunc_debug_getupvalue(lua_State* L) {
    luaL_checktype(L,1,LUA_TFUNCTION);
    int index=(int)luaL_checkinteger(L,2);
    if(index<1 || !lua_getupvalue(L,1,index)) { luaL_error(L,"invalid upvalue index"); return 0; }
    return 1;
}
static int sunc_debug_setupvalue(lua_State* L) {
    luaL_checktype(L,1,LUA_TFUNCTION);
    int index=(int)luaL_checkinteger(L,2);
    luaL_checkany(L,3);
    lua_pushvalue(L,3);
    if(index<1 || !lua_setupvalue(L,1,index)) { luaL_error(L,"invalid upvalue index"); return 0; }
    return 0;
}
static int sunc_debug_getupvalues(lua_State* L) { return xen_getupvalues(L); }
static const luaL_Reg sunc_globals[] = {
    {"iscclosure",xen_iscclosure},{"islclosure",sunc_islclosure},
    {"clonefunction",sunc_clonefunction},{"getgenv",xen_getgenv},
    {"getreg",xen_getreg},{"getrawmetatable",xen_getrawmetatable},
    {"setrawmetatable",sunc_setrawmetatable},{"isreadonly",sunc_isreadonly},
    {"setreadonly",sunc_setreadonly},{"identifyexecutor",sunc_identifyexecutor},
    {nullptr,nullptr}
};
static const luaL_Reg sunc_debug_funcs[] = {
    {"getupvalue",sunc_debug_getupvalue},{"getupvalues",sunc_debug_getupvalues},
    {"setupvalue",sunc_debug_setupvalue},{nullptr,nullptr}
};
static const luaL_Reg xen_funcs[] = {
    {"hookfunction",xen_hookfunction},{"newcclosure",xen_newcclosure},{"add",xen_add},{"clamp",xen_clamp},{"version",xen_version},
    {"typeof",xen_typeof},{"iscclosure",xen_iscclosure},{"isfunction",xen_isfunction},
    {"getgenv",xen_getgenv},{"getrenv",xen_getgenv},{"getreg",xen_getreg},
    {"getrawmetatable",xen_getrawmetatable},{"getupvalues",xen_getupvalues},
    {"getupvalue",xen_getupvalue},{"setupvalue",xen_setupvalue},
    {nullptr,nullptr}
};
void luaopen_xen_portable(lua_State* L) {
    luaL_register(L,"xen",xen_funcs); lua_pop(L,1);
    for(const luaL_Reg* f=sunc_globals; f->name; ++f) {
        lua_pushcfunction(L,f->func,f->name);
        lua_setglobal(L,f->name);
    }
    lua_getglobal(L,"debug");
    if(lua_istable(L,-1)) {
        for(const luaL_Reg* f=sunc_debug_funcs; f->name; ++f) {
            lua_pushcfunction(L,f->func,f->name);
            lua_setfield(L,-2,f->name);
        }
    }
    lua_pop(L,1);
    lua_pushcfunction(L, xen_hookfunction, "hookfunction");
    lua_setglobal(L, "hookfunction");
    lua_pushcfunction(L, xen_newcclosure, "newcclosure");
    lua_setglobal(L, "newcclosure");
}
