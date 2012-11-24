#ifndef LUAOBJECT_H
#define LUAOBJECT_H

#include <node.h>

extern "C"{
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

class LuaObject : public node::ObjectWrap {
 public:
  static void Init(v8::Handle<v8::Object> target);

 private:
  LuaObject();
  ~LuaObject();

  static v8::Handle<v8::Value> New(const v8::Arguments& args);
  static v8::Handle<v8::Value> DoFile(const v8::Arguments& args);
  static v8::Handle<v8::Value> GetGlobal(const v8::Arguments& args);
  static v8::Handle<v8::Value> SetGlobal(const v8::Arguments& args);
  static v8::Handle<v8::Value> Close(const v8::Arguments& args);
  lua_State *lua_;
};

#endif