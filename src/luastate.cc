#define BUILDING_NODELUA
#include "luastate.h"

using namespace v8;

uv_async_t async;
std::map<char*, Persistent<Function> > functions;

struct async_baton{
  bool has_cb;
  Persistent<Function> callback;
  char* data;
  bool error;
  char msg[1000];
  LuaState* state;
};

void do_file(uv_work_t *req){
  async_baton* baton = static_cast<async_baton*>(req->data);

  if(luaL_dofile(baton->state->lua_, baton->data)){
    baton->error = true;
    sprintf(baton->msg, "Exception In File %s Has Failed:\n%s\n", baton->data, lua_tostring(baton->state->lua_, -1));
  }
}


void do_string(uv_work_t *req){
  async_baton* baton = static_cast<async_baton*>(req->data);

  if(luaL_dostring(baton->state->lua_, baton->data)){
    baton->error = true;
    sprintf(baton->msg, "Exception Of Lua Code Has Failed:\n%s\n", baton->data, lua_tostring(baton->state->lua_, -1));
  }
}


void async_after(uv_work_t *req){
  HandleScope scope;

  async_baton* baton = (async_baton *)req->data;

  Local<Value> argv[2];
  const int argc = 2;

  if(baton->error){
    argv[0] = String::NewSymbol(baton->msg);
    argv[1] = Local<Value>::New(Undefined());
  } else{
    argv[0] = Local<Value>::New(Undefined());
    if(lua_gettop(baton->state->lua_)){
      argv[1] = lua_to_value(baton->state->lua_, -1);
    } else{
      argv[1] = Local<Value>::New(Undefined());
    }
  }

  TryCatch try_catch;

  if(baton->has_cb){
    baton->callback->Call(Context::GetCurrent()->Global(), argc, argv);
  }

  baton->callback.Dispose();
  delete baton;
  delete req;

  if (try_catch.HasCaught()) {
    node::FatalException(try_catch);
  }
}


LuaState::LuaState(){};
LuaState::~LuaState(){};


void LuaState::Init(Handle<Object> target){
  Local<FunctionTemplate> tpl = FunctionTemplate::New(New);
  tpl->SetClassName(String::NewSymbol("LuaState"));
  tpl->InstanceTemplate()->SetInternalFieldCount(2);

  tpl->PrototypeTemplate()->Set(String::NewSymbol("doFileSync"),
				FunctionTemplate::New(DoFileSync)->GetFunction());
  tpl->PrototypeTemplate()->Set(String::NewSymbol("doFile"),
				FunctionTemplate::New(DoFile)->GetFunction());

  tpl->PrototypeTemplate()->Set(String::NewSymbol("doStringSync"),
				FunctionTemplate::New(DoStringSync)->GetFunction());
  tpl->PrototypeTemplate()->Set(String::NewSymbol("doString"),
				FunctionTemplate::New(DoString)->GetFunction());

  tpl->PrototypeTemplate()->Set(String::NewSymbol("setGlobal"),
				FunctionTemplate::New(SetGlobal)->GetFunction());
  tpl->PrototypeTemplate()->Set(String::NewSymbol("getGlobal"),
				FunctionTemplate::New(GetGlobal)->GetFunction());

  tpl->PrototypeTemplate()->Set(String::NewSymbol("status"),
				FunctionTemplate::New(Status)->GetFunction());
  tpl->PrototypeTemplate()->Set(String::NewSymbol("close"),
				FunctionTemplate::New(Close)->GetFunction());
  tpl->PrototypeTemplate()->Set(String::NewSymbol("collectGarbage"),
				FunctionTemplate::New(CollectGarbage)->GetFunction());

  tpl->PrototypeTemplate()->Set(String::NewSymbol("registerFunction"),
				FunctionTemplate::New(RegisterFunction)->GetFunction());

  Persistent<Function> constructor = Persistent<Function>::New(tpl->GetFunction());
  target->Set(String::NewSymbol("LuaState"), constructor);
}


int LuaState::CallFunction(lua_State* L){
  int n = lua_gettop(L);

  char * func_name = (char *)lua_tostring(L, lua_upvalueindex(1));

  const unsigned argc = n;
  Local<Value>* argv = new Local<Value>[argc];
  int i;
  for(i = 1; i <= n; ++i){
    argv[i - 1] = lua_to_value(L, i);
  }

  Handle<Value> ret_val = Undefined();

  std::map<char *,Persistent<Function> >::iterator iter;
  for(iter = functions.begin(); iter != functions.end(); iter++){
    if(strcmp(iter->first, func_name) == 0){
      Persistent<Function> func = iter->second;
      ret_val = func->Call(Context::GetCurrent()->Global(), argc, argv);
      break;
    }
  }

  push_value_to_lua(L, ret_val);
  return 1;
}


Handle<Value> LuaState::New(const Arguments& args){
  HandleScope scope;

  if(!args.IsConstructCall()) {
    return ThrowException(Exception::TypeError(String::New("LuaState Requires The 'new' Operator To Create An Instance")));
  }

  LuaState* obj = new LuaState();
  obj->lua_ = lua_open();
  luaL_openlibs(obj->lua_);
  obj->Wrap(args.This());

  return args.This();
}


Handle<Value> LuaState::DoFileSync(const Arguments& args){
  HandleScope scope;

  if(args.Length() < 1){
    ThrowException(Exception::TypeError(String::New("LuaState.doFileSync Takes Only 1 Argument")));
    return scope.Close(Undefined());
  }

  if(!args[0]->IsString()){
    ThrowException(Exception::TypeError(String::New("LuaState.doFileSync Argument 1 Must Be A String")));
    return scope.Close(Undefined());
  }

  char* file_name = get_str(args[0]);

  LuaState* obj = ObjectWrap::Unwrap<LuaState>(args.This());
  if(luaL_dofile(obj->lua_, file_name)){
    char buf[1000];
    sprintf(buf, "Exception Of File %s Has Failed:\n%s\n", file_name, lua_tostring(obj->lua_, -1));
    ThrowException(Exception::Error(String::New(buf)));
    return scope.Close(Undefined());
  }

  if(lua_gettop(obj->lua_)){
    return scope.Close(lua_to_value(obj->lua_, -1));
  } else{
    return scope.Close(Undefined());
  }
}


Handle<Value> LuaState::DoFile(const Arguments& args){
  HandleScope scope;

  if(args.Length() < 1){
    ThrowException(Exception::TypeError(String::New("LuaState.doFile Requires At Least 1 Argument")));
    return scope.Close(Undefined());
  }

  if(!args[0]->IsString()){
    ThrowException(Exception::TypeError(String::New("LuaState.doFile First Argument Must Be A String")));
    return scope.Close(Undefined());
  }

  if(args.Length() > 1 && !args[1]->IsFunction()){
    ThrowException(Exception::TypeError(String::New("LuaState.doFile Second Argument Must Be A Function")));
    return scope.Close(Undefined());
  }

  LuaState* obj = ObjectWrap::Unwrap<LuaState>(args.This());
  async_baton* baton = new async_baton();
  baton->data = get_str(args[0]);
  baton->state = obj;
  obj->Ref();

  if(args.Length() > 1){
    baton->has_cb = true;
    baton->callback = Persistent<Function>::New(Local<Function>::Cast(args[1]));
  }

  uv_work_t *req = new uv_work_t;
  req->data = baton;
  uv_queue_work(uv_default_loop(), req, do_file, async_after);

  return scope.Close(Undefined());
}


Handle<Value> LuaState::DoStringSync(const Arguments& args) {
  HandleScope scope;

  if(args.Length() < 1){
    ThrowException(Exception::TypeError(String::New("LuaState.doStringSync Requires 1 Argument")));
    return scope.Close(Undefined());
  }

  char *lua_code = get_str(args[0]);

  LuaState* obj = ObjectWrap::Unwrap<LuaState>(args.This());
  if(luaL_dostring(obj->lua_, lua_code)){
    char buf[1000];
    sprintf(buf, "Execution Of Lua Code Has Failed:\n%s\n", lua_tostring(obj->lua_, -1));
    ThrowException(Exception::TypeError(String::New(buf)));
    return scope.Close(Undefined());
  }

  if(lua_gettop(obj->lua_)){
    return scope.Close(lua_to_value(obj->lua_, -1));
  } else{
    return scope.Close(Undefined());
  }
}


Handle<Value> LuaState::DoString(const Arguments& args){
  HandleScope scope;

  if(args.Length() < 1){
    ThrowException(Exception::TypeError(String::New("LuaState.doString Requires At Least 1 Argument")));
    return scope.Close(Undefined());
  }

  if(!args[0]->IsString()){
    ThrowException(Exception::TypeError(String::New("LuaState.doString: First Argument Must Be A String")));
    return scope.Close(Undefined());
  }

  LuaState* obj = ObjectWrap::Unwrap<LuaState>(args.This());
  async_baton* baton = new async_baton();
  baton->data = get_str(args[0]);
  baton->state = obj;
  obj->Ref();

  if(args.Length() > 1 && !args[1]->IsFunction()){
    ThrowException(Exception::TypeError(String::New("LuaState.doString Second Argument Must Be A Function")));
    return scope.Close(Undefined());
  }

  if(args.Length() > 1){
    baton->has_cb = true;
    baton->callback = Persistent<Function>::New(Local<Function>::Cast(args[1]));
  }

  uv_work_t *req = new uv_work_t;
  req->data = baton;
  uv_queue_work(uv_default_loop(), req, do_string, async_after);

  return scope.Close(Undefined());
}


Handle<Value> LuaState::SetGlobal(const Arguments& args) {
  HandleScope scope;

  if(args.Length() < 2){
    ThrowException(Exception::TypeError(String::New("LuaState.setGlobal Requires 2 Arguments")));
    return scope.Close(Undefined());
  }

  if(!args[0]->IsString()){
    ThrowException(Exception::TypeError(String::New("LuaState.setGlobal Argument 1 Must Be A String")));
    return scope.Close(Undefined());
  }

  char *global_name = get_str(args[0]);

  LuaState* obj = ObjectWrap::Unwrap<LuaState>(args.This());

  push_value_to_lua(obj->lua_, args[1]);
  lua_setglobal(obj->lua_, global_name);

  return scope.Close(Undefined());
}


Handle<Value> LuaState::GetGlobal(const Arguments& args) {
  HandleScope scope;

  if(args.Length() < 1){
    ThrowException(Exception::TypeError(String::New("LuaState.getGlobal Requires 1 Argument")));
    return scope.Close(Undefined());
  }

  if(!args[0]->IsString()){
    ThrowException(Exception::TypeError(String::New("LuaState.getGlobal Argument 1 Must Be A String")));
    return scope.Close(Undefined());
  }

  char *global_name = get_str(args[0]);

  LuaState* obj = ObjectWrap::Unwrap<LuaState>(args.This());
  lua_getglobal(obj->lua_, global_name);

  Local<Value> val = lua_to_value(obj->lua_, -1);

  return scope.Close(val);
}

Handle<Value> LuaState::Close(const Arguments& args){
  HandleScope scope;
  LuaState* obj = ObjectWrap::Unwrap<LuaState>(args.This());
  lua_close(obj->lua_);
  return scope.Close(Undefined());
}


Handle<Value> LuaState::Status(const Arguments& args){
  HandleScope scope;
  LuaState* obj = ObjectWrap::Unwrap<LuaState>(args.This());
  int status = lua_status(obj->lua_);

  return scope.Close(Number::New(status));
}


Handle<Value> LuaState::CollectGarbage(const Arguments& args){
  HandleScope scope;

  if(args.Length() < 1){
    ThrowException(Exception::TypeError(String::New("LuaState.collectGarbage Requires 1 Argument")));
    return scope.Close(Undefined());
  }

  if(!args[0]->IsNumber()){
    ThrowException(Exception::TypeError(String::New("LuaSatte.collectGarbage Argument 1 Must Be A Number, try nodelua.GC.[TYPE]")));
    return scope.Close(Undefined());
  }

  LuaState* obj = ObjectWrap::Unwrap<LuaState>(args.This());
  int type = (int)args[0]->ToNumber()->Value();
  int gc = lua_gc(obj->lua_, type, 0);

  return scope.Close(Number::New(gc));
}


Handle<Value> LuaState::RegisterFunction(const Arguments& args){
  HandleScope scope;

  if(args.Length() < 1){
    ThrowException(Exception::TypeError(String::New("nodelua.registerFunction Must Have 2 Arguments")));
    return scope.Close(Undefined());
  }

  if(!args[0]->IsString()){
    ThrowException(Exception::TypeError(String::New("nodelua.registerFunction Argument 1 Must Be A String")));
    return scope.Close(Undefined());
  }

  if(!args[1]->IsFunction()){
    ThrowException(Exception::TypeError(String::New("nodelua.registerFunction Argument 2 Must Be A Function")));
    return scope.Close(Undefined());
  }

  LuaState* obj = ObjectWrap::Unwrap<LuaState>(args.This());

  Persistent<Function> func = Persistent<Function>::New(Local<Function>::Cast(args[1]));
  char* func_name = get_str(args[0]);
  functions[func_name] = func;

  lua_pushstring(obj->lua_, func_name);
  lua_pushcclosure(obj->lua_, CallFunction, 1);
  lua_setglobal(obj->lua_, func_name);

  return scope.Close(Undefined());
}
