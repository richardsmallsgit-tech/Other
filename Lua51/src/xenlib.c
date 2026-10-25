/*
** XenLua custom built-in library.
** Naming convention:
**   C entry point: luaopen_xen
**   C implementation functions: xen_<name>
**   Lua-facing API: xen.<name>
*/

#include <stddef.h>
#include <string.h>
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

/* Internal closure/prototype layouts are needed for bytecode constants. */
#define LUA_CORE
#include "lobject.h"
#include "lstate.h"
#include "lgc.h"

#define XENLUA_VERSION "XenLua 1.1.0 (Lua 5.1.4 compatible runtime)"

static int xen_add(lua_State *L) {
  lua_Number a = luaL_checknumber(L, 1);
  lua_Number b = luaL_checknumber(L, 2);
  lua_pushnumber(L, a + b);
  return 1;
}

static int xen_clamp(lua_State *L) {
  lua_Number value = luaL_checknumber(L, 1);
  lua_Number minimum = luaL_checknumber(L, 2);
  lua_Number maximum = luaL_checknumber(L, 3);
  if (minimum > maximum)
    return luaL_error(L, "xen.clamp: minimum must be <= maximum");
  if (value < minimum) value = minimum;
  if (value > maximum) value = maximum;
  lua_pushnumber(L, value);
  return 1;
}

static int xen_version(lua_State *L) {
  lua_pushliteral(L, XENLUA_VERSION);
  return 1;
}

static int xen_typeof(lua_State *L) {
  luaL_checkany(L, 1);
  lua_pushstring(L, luaL_typename(L, 1));
  return 1;
}

/*
** VM-wide function hooks are stored in the registry table "XenLua.Hooks".
** ldo.c consults this table in luaD_precall, so aliases to the same function
** are hooked too. The returned original-callable temporarily removes its own
** mapping while the original runs, then restores the hook even on errors.
*/
#define XEN_HOOKS_REGISTRY "XenLua.Hooks"

static void xen_pushhooks(lua_State *L) {
  lua_getfield(L, LUA_REGISTRYINDEX, XEN_HOOKS_REGISTRY);
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_setfield(L, LUA_REGISTRYINDEX, XEN_HOOKS_REGISTRY);
  }
}

static int xen_original_dispatch(lua_State *L) {
  int nargs = lua_gettop(L);
  int status;
  int nresults;
  /* Remove the hook for this exact function while calling the original. */
  xen_pushhooks(L);
  lua_pushvalue(L, lua_upvalueindex(1));
  lua_pushnil(L);
  lua_rawset(L, -3);
  lua_pop(L, 1);

  lua_pushvalue(L, lua_upvalueindex(1));
  {
    int i;
    for (i = 1; i <= nargs; ++i)
      lua_pushvalue(L, i);
  }
  status = lua_pcall(L, nargs, LUA_MULTRET, 0);
  nresults = lua_gettop(L) - nargs;

  /* Restore the replacement regardless of whether the original succeeded. */
  xen_pushhooks(L);
  lua_pushvalue(L, lua_upvalueindex(1));
  lua_pushvalue(L, lua_upvalueindex(2));
  lua_rawset(L, -3);
  lua_pop(L, 1);

  if (status != 0)
    return lua_error(L);
  return nresults;
}

static int xen_hookfunction(lua_State *L) {
  luaL_checktype(L, 1, LUA_TFUNCTION);
  luaL_checktype(L, 2, LUA_TFUNCTION);
  if (lua_rawequal(L, 1, 2))
    return luaL_error(L, "hookfunction: replacement must differ from original");

  xen_pushhooks(L);
  lua_pushvalue(L, 1);
  lua_pushvalue(L, 2);
  lua_rawset(L, -3);
  lua_pop(L, 1);

  /* Return a callable original that bypasses its own hook during invocation. */
  lua_pushvalue(L, 1);
  lua_pushvalue(L, 2);
  lua_pushcclosure(L, xen_original_dispatch, 2);
  return 1;
}

static int xen_newcclosure_dispatch(lua_State *L) {
  int nargs = lua_gettop(L);
  int i;
  lua_pushvalue(L, lua_upvalueindex(1));
  for (i = 1; i <= nargs; ++i)
    lua_pushvalue(L, i);
  lua_call(L, nargs, LUA_MULTRET);
  return lua_gettop(L) - nargs;
}

static int xen_newcclosure(lua_State *L) {
  luaL_checktype(L, 1, LUA_TFUNCTION);
  lua_pushvalue(L, 1);
  lua_pushcclosure(L, xen_newcclosure_dispatch, 1);
  return 1;
}

static int xen_iscclosure(lua_State *L) {
  lua_pushboolean(L, lua_iscfunction(L, 1));
  return 1;
}

/* Return a table of a function's upvalues. Values are stored at numeric
** indices; debug names (when present) are also available as string keys. */
static int xen_getupvalues(lua_State *L) {
  int i = 1;
  const char *name;
  luaL_checktype(L, 1, LUA_TFUNCTION);
  lua_newtable(L);
  while ((name = lua_getupvalue(L, 1, i)) != NULL) {
    lua_pushvalue(L, -1);
    lua_rawseti(L, -3, i);
    if (name[0] != '\0') {
      lua_pushstring(L, name);
      lua_pushvalue(L, -2);
      lua_rawset(L, -4);
    }
    lua_pop(L, 1);
    ++i;
  }
  return 1;
}

static int xen_getupvalue(lua_State *L) {
  const char *name;
  int index;
  luaL_checktype(L, 1, LUA_TFUNCTION);
  index = (int)luaL_checkinteger(L, 2);
  if (index < 1)
    return luaL_error(L, "getupvalue: index must be positive");
  name = lua_getupvalue(L, 1, index);
  if (name == NULL) {
    lua_pushnil(L);
    return 1;
  }
  lua_pushstring(L, name);
  lua_insert(L, -2);
  return 2;
}

static int xen_setupvalue(lua_State *L) {
  const char *name;
  luaL_checktype(L, 1, LUA_TFUNCTION);
  (void)luaL_checkinteger(L, 2);
  luaL_checkany(L, 3);
  name = lua_setupvalue(L, 1, (int)lua_tointeger(L, 2));
  if (name == NULL) lua_pushnil(L);
  else lua_pushstring(L, name);
  return 1;
}

/* Lua 5.1 bytecode constants live in Proto.k. This API deliberately exposes
** only primitive constants, matching what the Lua compiler emits. */
static int xen_getconstants(lua_State *L) {
  const Closure *cl;
  const Proto *p;
  int i;
  luaL_checktype(L, 1, LUA_TFUNCTION);
  if (lua_iscfunction(L, 1)) {
    lua_newtable(L);
    return 1;
  }
  cl = (const Closure *)lua_topointer(L, 1);
  if (cl == NULL || cl->l.p == NULL)
    return luaL_error(L, "getconstants: unable to inspect function");
  p = cl->l.p;
  lua_newtable(L);
  for (i = 0; i < p->sizek; ++i) {
    const TValue *v = &p->k[i];
    lua_pushnumber(L, (lua_Number)i + 1);
    switch (ttype(v)) {
      case LUA_TNIL: lua_pushnil(L); break;
      case LUA_TBOOLEAN: lua_pushboolean(L, bvalue(v)); break;
      case LUA_TNUMBER: lua_pushnumber(L, nvalue(v)); break;
      case LUA_TSTRING: lua_pushstring(L, svalue(v)); break;
      default: lua_pushnil(L); break;
    }
    /* nil constants cannot be represented as table entries; use a numeric
    ** index with nil naturally omitted, consistent with Lua table semantics. */
    lua_rawset(L, -3);
  }
  return 1;
}

/* Return a stable, function-focused metadata record. Lua 5.1's debug.getinfo
** remains available for stack-level and option-string queries; this helper is
** intentionally simple for scripts that just want to inspect a function. */
static int xen_getfunctioninfo(lua_State *L) {
  lua_Debug ar;
  int isC;
  luaL_checktype(L, 1, LUA_TFUNCTION);
  isC = lua_iscfunction(L, 1);
  lua_pushvalue(L, 1);
  if (!lua_getinfo(L, ">Snu", &ar))
    return luaL_error(L, "getfunctioninfo: unable to inspect function");

  lua_createtable(L, 0, 10);
  lua_pushstring(L, ar.source != NULL ? ar.source : "=?");
  lua_setfield(L, -2, "source");
  lua_pushstring(L, ar.short_src);
  lua_setfield(L, -2, "short_src");
  lua_pushstring(L, ar.what != NULL ? ar.what : (isC ? "C" : "Lua"));
  lua_setfield(L, -2, "what");
  lua_pushinteger(L, ar.linedefined);
  lua_setfield(L, -2, "linedefined");
  lua_pushinteger(L, ar.lastlinedefined);
  lua_setfield(L, -2, "lastlinedefined");
  lua_pushinteger(L, ar.nups);
  lua_setfield(L, -2, "nups");
  lua_pushboolean(L, isC);
  lua_setfield(L, -2, "is_c");
  lua_pushboolean(L, !isC);
  lua_setfield(L, -2, "is_lua");
  if (!isC) {
    const Closure *cl = (const Closure *)lua_topointer(L, 1);
    int count = (cl != NULL && cl->l.p != NULL) ? cl->l.p->sizek : 0;
    lua_pushinteger(L, count);
    lua_setfield(L, -2, "constant_count");
  } else {
    lua_pushinteger(L, 0);
    lua_setfield(L, -2, "constant_count");
  }
  return 1;
}

static int xen_getgenv(lua_State *L) {
  lua_pushvalue(L, LUA_GLOBALSINDEX);
  return 1;
}

/* XenLua has no separate host/runtime environment, so getrenv currently
** aliases the interpreter's global environment. */
static int xen_getrenv(lua_State *L) {
  lua_pushvalue(L, LUA_GLOBALSINDEX);
  return 1;
}

/* Expose the registry table for introspection and custom runtime libraries. */
static int xen_getreg(lua_State *L) {
  lua_pushvalue(L, LUA_REGISTRYINDEX);
  return 1;
}

/*
** Return live GC-managed values from this Lua state. A full collection first
** removes unreachable objects; GC is then paused while the result is built so
** the list cannot be swept midway through enumeration. By default tables are
** omitted (getgc(true) includes them). Internal Proto/UpVal objects are not
** Lua values and are intentionally not exposed.
*/
static void xen_pushgcvalue(lua_State *L, GCObject *o) {
  switch (o->gch.tt) {
    case LUA_TSTRING: setsvalue(L, L->top, gco2ts(o)); break;
    case LUA_TTABLE: sethvalue(L, L->top, gco2h(o)); break;
    case LUA_TFUNCTION: setclvalue(L, L->top, gco2cl(o)); break;
    case LUA_TUSERDATA: setuvalue(L, L->top, gco2u(o)); break;
    case LUA_TTHREAD: setthvalue(L, L->top, gco2th(o)); break;
    default: setnilvalue(L->top); break;
  }
  L->top++;
}

static int xen_getgc(lua_State *L) {
  int includeTables = lua_toboolean(L, 1);
  GCObject *o;
  int index = 0;
  /* Collect before pausing so unreachable objects are not returned. */
  lua_gc(L, LUA_GCCOLLECT, 0);
  lua_gc(L, LUA_GCSTOP, 0);
  lua_newtable(L);
  for (o = G(L)->rootgc; o != NULL; o = o->gch.next) {
    int tt = o->gch.tt;
    if (isdead(G(L), o)) continue;
    if (tt == LUA_TFUNCTION || tt == LUA_TUSERDATA || tt == LUA_TTHREAD ||
        tt == LUA_TSTRING || (includeTables && tt == LUA_TTABLE)) {
      xen_pushgcvalue(L, o);
      if (!lua_isnil(L, -1)) {
        lua_rawseti(L, -2, ++index);
      } else {
        lua_pop(L, 1);
      }
    }
  }
  lua_gc(L, LUA_GCRESTART, 0);
  return 1;
}

/* Raw metatable access ignores the __metatable protection field. */
static int xen_getrawmetatable(lua_State *L) {
  luaL_checkany(L, 1);
  if (!lua_getmetatable(L, 1))
    lua_pushnil(L);
  return 1;
}

static int xen_setrawmetatable(lua_State *L) {
  int t = lua_type(L, 1);
  luaL_checkany(L, 2);
  if (t != LUA_TTABLE && t != LUA_TUSERDATA)
    return luaL_error(L, "setrawmetatable: expected table or userdata as argument 1");
  if (!lua_isnil(L, 2) && !lua_istable(L, 2))
    return luaL_error(L, "setrawmetatable: metatable must be a table or nil");
  lua_pushvalue(L, 2);
  lua_setmetatable(L, 1);
  lua_pushvalue(L, 1);
  return 1;
}

static int xen_isfunction(lua_State *L) {
  lua_pushboolean(L, lua_isfunction(L, 1));
  return 1;
}


/* A small native signal/connection system owned by XenLua. Connections are
** retained by the signal's environment table; each connection's environment
** retains its signal, so the raw signal pointer remains valid while connected
** handles are reachable. Callback references are released by __gc. */
#define XEN_SIGNAL_MT "XenLua.Signal"
#define XEN_CONNECTION_MT "XenLua.Connection"

typedef struct XenSignal { unsigned int next_id; } XenSignal;
typedef struct XenConnection {
  XenSignal *signal;
  int callback_ref;
  unsigned int id;
  int connected;
  int enabled;
} XenConnection;

static XenSignal *xen_checksignal(lua_State *L, int idx) {
  return (XenSignal *)luaL_checkudata(L, idx, XEN_SIGNAL_MT);
}
static XenConnection *xen_checkconnection(lua_State *L, int idx) {
  return (XenConnection *)luaL_checkudata(L, idx, XEN_CONNECTION_MT);
}

static int xen_connection_gc(lua_State *L) {
  XenConnection *c = (XenConnection *)luaL_checkudata(L, 1, XEN_CONNECTION_MT);
  if (c->callback_ref != LUA_NOREF && c->callback_ref != LUA_REFNIL) {
    luaL_unref(L, LUA_REGISTRYINDEX, c->callback_ref);
    c->callback_ref = LUA_NOREF;
  }
  return 0;
}
static int xen_connection_disconnect(lua_State *L) {
  XenConnection *c = xen_checkconnection(L, 1);
  c->connected = 0;
  if (c->callback_ref != LUA_NOREF && c->callback_ref != LUA_REFNIL) {
    luaL_unref(L, LUA_REGISTRYINDEX, c->callback_ref);
    c->callback_ref = LUA_NOREF;
  }
  lua_pushboolean(L, 1);
  return 1;
}
static int xen_connection_enable(lua_State *L) {
  XenConnection *c = xen_checkconnection(L, 1); c->enabled = 1;
  lua_pushboolean(L, 1); return 1;
}
static int xen_connection_disable(lua_State *L) {
  XenConnection *c = xen_checkconnection(L, 1); c->enabled = 0;
  lua_pushboolean(L, 1); return 1;
}
static int xen_connection_isconnected(lua_State *L) {
  lua_pushboolean(L, xen_checkconnection(L, 1)->connected); return 1;
}
static int xen_connection_isenabled(lua_State *L) {
  XenConnection *c = xen_checkconnection(L, 1);
  lua_pushboolean(L, c->connected && c->enabled); return 1;
}
static int xen_connection_index(lua_State *L) {
  XenConnection *c = xen_checkconnection(L, 1);
  const char *key = luaL_checkstring(L, 2);
  if (strcmp(key, "Connected") == 0) lua_pushboolean(L, c->connected);
  else if (strcmp(key, "Enabled") == 0) lua_pushboolean(L, c->enabled);
  else if (strcmp(key, "Id") == 0) lua_pushnumber(L, (lua_Number)c->id);
  else {
    luaL_getmetatable(L, XEN_CONNECTION_MT);
    lua_getfield(L, -1, key);
    lua_remove(L, -2);
  }
  return 1;
}
static int xen_connection_newindex(lua_State *L) {
  XenConnection *c = xen_checkconnection(L, 1);
  const char *key = luaL_checkstring(L, 2);
  if (strcmp(key, "Enabled") == 0) c->enabled = lua_toboolean(L, 3) && c->connected;
  else if (strcmp(key, "Connected") == 0 || strcmp(key, "Id") == 0)
    return luaL_error(L, "connection.%s is read-only", key);
  else return luaL_error(L, "unknown connection property '%s'", key);
  return 0;
}

static int xen_signal_connect(lua_State *L) {
  XenSignal *sig = xen_checksignal(L, 1);
  XenConnection *c;
  int env, connections;
  luaL_checktype(L, 2, LUA_TFUNCTION);
  c = (XenConnection *)lua_newuserdata(L, sizeof(*c));
  c->signal = sig; c->id = sig->next_id++; c->connected = 1; c->enabled = 1;
  lua_pushvalue(L, 2); c->callback_ref = luaL_ref(L, LUA_REGISTRYINDEX);
  luaL_getmetatable(L, XEN_CONNECTION_MT); lua_setmetatable(L, -2);
  /* Connection environment keeps the signal alive as long as its handle is. */
  lua_newtable(L); env = lua_gettop(L);
  lua_pushvalue(L, 1); lua_setfield(L, env, "signal");
  lua_setfenv(L, -2);
  /* Signal environment owns all handles so getconnections is stable. */
  lua_getfenv(L, 1); env = lua_gettop(L);
  lua_getfield(L, env, "connections"); connections = lua_gettop(L);
  lua_pushvalue(L, -3); /* newly created connection userdata */
  lua_rawseti(L, connections, (int)c->id);
  lua_pop(L, 2); /* connections and signal environment */
  return 1;
}

static int xen_signal_fire(lua_State *L) {
  int nargs = lua_gettop(L) - 1;
  int i, n, status;
  xen_checksignal(L, 1);
  lua_getfenv(L, 1); lua_getfield(L, -1, "connections");
  n = (int)lua_objlen(L, -1);
  for (i = 1; i <= n; ++i) {
    XenConnection *c;
    int j;
    lua_rawgeti(L, -1, i);
    if (lua_isnil(L, -1)) { lua_pop(L, 1); continue; }
    c = (XenConnection *)luaL_checkudata(L, -1, XEN_CONNECTION_MT);
    if (!c->connected || !c->enabled || c->callback_ref == LUA_NOREF || c->callback_ref == LUA_REFNIL) {
      lua_pop(L, 1); continue;
    }
    lua_rawgeti(L, LUA_REGISTRYINDEX, c->callback_ref);
    for (j = 0; j < nargs; ++j) lua_pushvalue(L, 2 + j);
    status = lua_pcall(L, nargs, 0, 0);
    if (status != 0) {
      lua_remove(L, -2); /* remove connection, preserve error object */
      return lua_error(L);
    }
    lua_pop(L, 1); /* connection userdata */
  }
  lua_pop(L, 2); /* connections table and environment */
  return 0;
}
static int xen_signal_getconnections(lua_State *L) {
  int i, n;
  xen_checksignal(L, 1);
  lua_getfenv(L, 1); lua_getfield(L, -1, "connections");
  n = (int)lua_objlen(L, -1);
  lua_createtable(L, n, 0);
  for (i = 1; i <= n; ++i) {
    lua_rawgeti(L, -2, i);
    lua_rawseti(L, -2, i);
  }
  lua_remove(L, -2); /* internal connections table */
  lua_remove(L, -2); /* signal environment */
  return 1;
}
static int xen_signal_new(lua_State *L) {
  XenSignal *sig = (XenSignal *)lua_newuserdata(L, sizeof(*sig));
  sig->next_id = 1;
  luaL_getmetatable(L, XEN_SIGNAL_MT); lua_setmetatable(L, -2);
  lua_newtable(L);
  lua_newtable(L); lua_setfield(L, -2, "connections");
  lua_setfenv(L, -2);
  return 1;
}
static int xen_getconnections(lua_State *L) {
  (void)xen_checksignal(L, 1);
  return xen_signal_getconnections(L);
}
static void xen_register_signal(lua_State *L) {
  static const luaL_Reg signal_methods[] = {
    {"Connect", xen_signal_connect}, {"Fire", xen_signal_fire},
    {"GetConnections", xen_signal_getconnections}, {NULL, NULL}
  };
  static const luaL_Reg connection_methods[] = {
    {"Disconnect", xen_connection_disconnect}, {"Enable", xen_connection_enable},
    {"Disable", xen_connection_disable}, {"IsConnected", xen_connection_isconnected},
    {"IsEnabled", xen_connection_isenabled}, {"__gc", xen_connection_gc},
    {"__index", xen_connection_index}, {"__newindex", xen_connection_newindex},
    {NULL, NULL}
  };
  luaL_newmetatable(L, XEN_SIGNAL_MT);
  luaL_register(L, NULL, signal_methods);
  lua_pushvalue(L, -1); lua_setfield(L, -2, "__index");
  lua_pushliteral(L, "XenLua Signal"); lua_setfield(L, -2, "__metatable");
  lua_pop(L, 1);
  luaL_newmetatable(L, XEN_CONNECTION_MT);
  luaL_register(L, NULL, connection_methods);
  lua_pushliteral(L, "XenLua Connection"); lua_setfield(L, -2, "__metatable");
  lua_pop(L, 1);
}

/* Cooperative task scheduler built on Lua 5.1 coroutines. Time advances only
** when task.step(dt) is called; there is no hidden wall-clock thread. */
#define XEN_TASKS_REGISTRY "XenLua.Tasks"

static void xen_pushscheduler(lua_State *L) {
  lua_getfield(L, LUA_REGISTRYINDEX, XEN_TASKS_REGISTRY);
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_newtable(L); lua_setfield(L, -2, "tasks");
    lua_pushnumber(L, 0); lua_setfield(L, -2, "now");
    lua_pushboolean(L, 0); lua_setfield(L, -2, "running");
    lua_pushvalue(L, -1);
    lua_setfield(L, LUA_REGISTRYINDEX, XEN_TASKS_REGISTRY);
  }
}

static void xen_push_task_states(lua_State *L) {
  lua_getfield(L, LUA_REGISTRYINDEX, "XenLua.TaskStates");
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_newtable(L); lua_pushliteral(L, "k"); lua_setfield(L, -2, "__mode");
    lua_setmetatable(L, -2);
    lua_pushvalue(L, -1); lua_setfield(L, LUA_REGISTRYINDEX, "XenLua.TaskStates");
  }
}

static void xen_record_state(lua_State *L, int record, const char *state) {
  int absrecord = record > 0 ? record : lua_gettop(L) + record + 1;
  lua_pushstring(L, state);
  lua_setfield(L, absrecord, "state");
  lua_getfield(L, absrecord, "thread");
  if (lua_isthread(L, -1)) {
    xen_push_task_states(L);
    lua_pushvalue(L, -2);
    lua_pushstring(L, state);
    lua_rawset(L, -3);
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
}

static void xen_scheduler_compact(lua_State *L, int tasks) {
  int i, n = (int)lua_objlen(L, tasks), write = 1;
  for (i = 1; i <= n; ++i) {
    int record;
    const char *state;
    lua_rawgeti(L, tasks, i); record = lua_gettop(L);
    if (lua_isnil(L, record)) { lua_pop(L, 1); continue; }
    lua_getfield(L, record, "state"); state = lua_tostring(L, -1);
    if (state == NULL || (strcmp(state, "done") != 0 && strcmp(state, "failed") != 0 && strcmp(state, "cancelled") != 0)) {
      lua_pop(L, 1);
      if (write != i) { lua_pushvalue(L, record); lua_rawseti(L, tasks, write); }
      ++write;
    } else lua_pop(L, 1);
    lua_pop(L, 1);
  }
  for (i = write; i <= n; ++i) { lua_pushnil(L); lua_rawseti(L, tasks, i); }
}

/* Run each currently queued task at most once per pump. A yielded task stores
** its requested delay; task.step advances virtual time and resumes due tasks. */
static int xen_scheduler_pump(lua_State *L, int allow_deferred) {
  int base = lua_gettop(L), count = 0, i, n, tasks;
  lua_Number now;
  xen_pushscheduler(L);
  {
    int scheduler = lua_gettop(L);
    lua_getfield(L, scheduler, "running");
    if (lua_toboolean(L, -1)) { lua_settop(L, base); return 0; }
    lua_pop(L, 1);
    lua_pushboolean(L, 1); lua_setfield(L, scheduler, "running");
    lua_getfield(L, scheduler, "now"); now = lua_tonumber(L, -1); lua_pop(L, 1);
    lua_getfield(L, scheduler, "tasks");
    {
      tasks = lua_gettop(L);
      n = (int)lua_objlen(L, tasks);
      for (i = 1; i <= n; ++i) {
        int record, thread_index, resume_args = 0, should_run = 0;
        lua_State *co;
        const char *state;
        lua_rawgeti(L, tasks, i);
        record = lua_gettop(L);
        lua_getfield(L, record, "state"); state = lua_tostring(L, -1);
        if (state != NULL && strcmp(state, "ready") == 0) {
          lua_getfield(L, record, "started");
          if (!lua_toboolean(L, -1)) {
            lua_getfield(L, record, "nargs"); resume_args = (int)lua_tointeger(L, -1); lua_pop(L, 1);
            lua_pushboolean(L, 1); lua_setfield(L, record, "started");
          }
          lua_pop(L, 1);
          should_run = 1;
        }
        else if (state != NULL && strcmp(state, "deferred") == 0 && allow_deferred) {
          should_run = 1; xen_record_state(L, record, "ready");
        } else if (state != NULL && strcmp(state, "waiting") == 0) {
          lua_getfield(L, record, "wake");
          if (lua_tonumber(L, -1) <= now) {
            lua_Number started;
            lua_pop(L, 1);
            lua_getfield(L, record, "wait_started"); started = lua_tonumber(L, -1); lua_pop(L, 1);
            lua_getfield(L, record, "thread"); co = lua_tothread(L, -1); lua_pop(L, 1);
            if (co != NULL) {
              lua_settop(co, 0);
              lua_pushnumber(co, now - started);
              resume_args = 1;
              should_run = 1;
            }
            xen_record_state(L, record, "ready");
          } else lua_pop(L, 1);
        }
        lua_pop(L, 1); /* state */
        if (!should_run) { lua_pop(L, 1); continue; }
        lua_getfield(L, record, "thread");
        thread_index = lua_gettop(L); co = lua_tothread(L, thread_index);
        if (co == NULL) { lua_pop(L, 2); continue; }
        xen_record_state(L, record, "running");
        {
          int status = lua_resume(co, resume_args);
          ++count;
          if (status == LUA_YIELD) {
            lua_Number delay = 0;
            if (lua_gettop(co) > 0 && lua_isnumber(co, -1)) delay = lua_tonumber(co, -1);
            if (delay < 0) delay = 0;
            lua_pushnumber(L, now + delay); lua_setfield(L, record, "wake");
            lua_pushnumber(L, now); lua_setfield(L, record, "wait_started");
            xen_record_state(L, record, "waiting");
          } else if (status == 0) {
            xen_record_state(L, record, "done");
          } else {
            const char *msg = lua_tostring(co, -1);
            char error_copy[512];
            if (msg == NULL) msg = "scheduled task failed with a non-string error";
            strncpy(error_copy, msg, sizeof(error_copy) - 1);
            error_copy[sizeof(error_copy) - 1] = '\0';
            xen_record_state(L, record, "failed");
            lua_pop(L, 2); /* thread and record */
            xen_scheduler_compact(L, tasks);
            lua_pushboolean(L, 0); lua_setfield(L, scheduler, "running");
            lua_settop(L, base);
            return luaL_error(L, "task: %s", error_copy);
          }
        }
        lua_pop(L, 2); /* thread and record */
        n = (int)lua_objlen(L, tasks);
      }
    }
    xen_scheduler_compact(L, tasks);
    lua_pop(L, 1); /* tasks */
    lua_pushboolean(L, 0); lua_setfield(L, scheduler, "running");
  }
  lua_settop(L, base);
  return count;
}

static int xen_task_wait(lua_State *L) {
  int base, i, n, found = 0;
  lua_Number delay = luaL_optnumber(L, 1, 0);
  if (delay < 0) delay = 0;
  if (lua_pushthread(L)) return luaL_error(L, "task.wait cannot yield from the main thread");
  base = lua_gettop(L);
  xen_pushscheduler(L);
  lua_getfield(L, -1, "tasks"); n = (int)lua_objlen(L, -1);
  for (i = 1; i <= n; ++i) {
    lua_rawgeti(L, -1, i);
    lua_getfield(L, -1, "thread");
    if (lua_rawequal(L, -1, base)) found = 1;
    lua_pop(L, 2);
    if (found) break;
  }
  lua_settop(L, base);
  if (!found) return luaL_error(L, "task.wait must be called from a task.spawn/defer coroutine");
  lua_pushnumber(L, delay);
  return lua_yield(L, 1);
}

static int xen_task_create(lua_State *L, int deferred) {
  int nargs = lua_gettop(L), i, tasks, thread_index, record, index;
  lua_State *co;
  luaL_checktype(L, 1, LUA_TFUNCTION);
  xen_pushscheduler(L);
  lua_getfield(L, -1, "tasks"); tasks = lua_gettop(L);
  index = (int)lua_objlen(L, tasks) + 1;
  co = lua_newthread(L); thread_index = lua_gettop(L);
  lua_newtable(L); record = lua_gettop(L);
  lua_pushvalue(L, thread_index); lua_setfield(L, record, "thread");
  xen_record_state(L, record, deferred ? "deferred" : "ready");
  lua_pushnumber(L, 0); lua_setfield(L, record, "wake");
  lua_pushnumber(L, 0); lua_setfield(L, record, "wait_started");
  lua_pushboolean(L, 0); lua_setfield(L, record, "started");
  lua_pushnumber(L, (lua_Number)(nargs - 1)); lua_setfield(L, record, "nargs");
  lua_pushvalue(L, 1); lua_xmove(L, co, 1);
  for (i = 2; i <= nargs; ++i) { lua_pushvalue(L, i); lua_xmove(L, co, 1); }
  lua_pushvalue(L, record); lua_rawseti(L, tasks, index);
  if (!deferred) (void)xen_scheduler_pump(L, 0);
  lua_pushvalue(L, thread_index);
  return 1;
}
static int xen_task_spawn(lua_State *L) { return xen_task_create(L, 0); }
static int xen_task_defer(lua_State *L) { return xen_task_create(L, 1); }
static int xen_task_step(lua_State *L) {
  lua_Number dt = luaL_optnumber(L, 1, 0);
  if (dt < 0) return luaL_error(L, "task.step: delta must be non-negative");
  xen_pushscheduler(L);
  lua_getfield(L, -1, "now"); dt += lua_tonumber(L, -1); lua_pop(L, 1);
  lua_pushnumber(L, dt); lua_setfield(L, -2, "now");
  lua_pop(L, 1);
  lua_pushnumber(L, (lua_Number)xen_scheduler_pump(L, 1));
  return 1;
}
static int xen_task_cancel(lua_State *L) {
  int i, n, found = 0;
  luaL_checktype(L, 1, LUA_TTHREAD);
  xen_pushscheduler(L); lua_getfield(L, -1, "tasks"); n = (int)lua_objlen(L, -1);
  {
    int tasks = lua_gettop(L);
    for (i = 1; i <= n; ++i) {
      int record;
      const char *state;
      lua_rawgeti(L, tasks, i); record = lua_gettop(L);
      if (lua_isnil(L, record)) { lua_pop(L, 1); continue; }
      lua_getfield(L, record, "thread");
      if (lua_rawequal(L, -1, 1)) {
        lua_pop(L, 1);
        lua_getfield(L, record, "state"); state = lua_tostring(L, -1);
        if (state != NULL && strcmp(state, "done") != 0 && strcmp(state, "failed") != 0 && strcmp(state, "cancelled") != 0) {
          lua_pop(L, 1);
          xen_record_state(L, record, "cancelled"); found = 1;
        } else lua_pop(L, 1);
        lua_pop(L, 1);
        break;
      }
      lua_pop(L, 2);
    }
  }
  lua_pushboolean(L, found); return 1;
}
static int xen_task_status(lua_State *L) {
  int i, n;
  luaL_checktype(L, 1, LUA_TTHREAD);
  xen_pushscheduler(L); lua_getfield(L, -1, "tasks"); n = (int)lua_objlen(L, -1);
  {
    int tasks = lua_gettop(L);
    for (i = 1; i <= n; ++i) {
      lua_rawgeti(L, tasks, i);
      if (lua_isnil(L, -1)) { lua_pop(L, 1); continue; }
      lua_getfield(L, -1, "thread");
      if (lua_rawequal(L, -1, 1)) {
        lua_pop(L, 1); lua_getfield(L, -1, "state"); return 1;
      }
      lua_pop(L, 2);
    }
  }
  xen_push_task_states(L);
  lua_pushvalue(L, 1); lua_rawget(L, -2);
  if (!lua_isnil(L, -1)) return 1;
  lua_pop(L, 1);
  lua_pushstring(L, lua_status(lua_tothread(L, 1)) == LUA_YIELD ? "suspended" : "unknown");
  return 1;
}
static void xen_register_task(lua_State *L) {
  static const luaL_Reg task_functions[] = {
    {"spawn", xen_task_spawn}, {"defer", xen_task_defer}, {"wait", xen_task_wait},
    {"step", xen_task_step}, {"cancel", xen_task_cancel}, {"status", xen_task_status},
    {NULL, NULL}
  };
  lua_newtable(L);
  luaL_register(L, NULL, task_functions);
  lua_setglobal(L, "task");
  lua_getglobal(L, "xen");
  lua_newtable(L); luaL_register(L, NULL, task_functions); lua_setfield(L, -2, "task");
  lua_pop(L, 1);
}

static const luaL_Reg xen_functions[] = {
  {"add", xen_add},
  {"clamp", xen_clamp},
  {"version", xen_version},
  {"typeof", xen_typeof},
  {"hookfunction", xen_hookfunction},
  {"newcclosure", xen_newcclosure},
  {"iscclosure", xen_iscclosure},
  {"isfunction", xen_isfunction},
  {"getgenv", xen_getgenv},
  {"getfunctioninfo", xen_getfunctioninfo},
  {"getrenv", xen_getrenv},
  {"getreg", xen_getreg},
  {"getgc", xen_getgc},
  {"getrawmetatable", xen_getrawmetatable},
  {"setrawmetatable", xen_setrawmetatable},
  {"getupvalues", xen_getupvalues},
  {"getupvalue", xen_getupvalue},
  {"setupvalue", xen_setupvalue},
  {"getconstants", xen_getconstants},
  {"Signal", xen_signal_new},
  {"getconnections", xen_getconnections},
  {NULL, NULL}
};

LUALIB_API int luaopen_xen(lua_State *L) {
  static const luaL_Reg globals[] = {
    {"hookfunction", xen_hookfunction},
    {"newcclosure", xen_newcclosure},
    {"iscclosure", xen_iscclosure},
    {"isfunction", xen_isfunction},
    {"getgenv", xen_getgenv},
    {"getfunctioninfo", xen_getfunctioninfo},
    {"getrenv", xen_getrenv},
    {"getreg", xen_getreg},
  {"getgc", xen_getgc},
    {"getrawmetatable", xen_getrawmetatable},
    {"setrawmetatable", xen_setrawmetatable},
    {"getupvalues", xen_getupvalues},
    {"getupvalue", xen_getupvalue},
    {"setupvalue", xen_setupvalue},
    {"getconstants", xen_getconstants},
    {"getconnections", xen_getconnections},
    {NULL, NULL}
  };
  const luaL_Reg *lib;
  xen_register_signal(L);
  luaL_register(L, "xen", xen_functions);
  xen_register_task(L);
  lua_pushcfunction(L, xen_signal_new); lua_setglobal(L, "Signal");
  for (lib = globals; lib->name != NULL; ++lib) {
    lua_pushcfunction(L, lib->func);
    lua_setglobal(L, lib->name);
  }
  return 1;
}
