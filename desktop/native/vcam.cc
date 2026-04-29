#include <napi.h>

// Declared in platform-specific files
Napi::Value Start(const Napi::CallbackInfo& info);
void Stop(const Napi::CallbackInfo& info);
Napi::Value PushFrame(const Napi::CallbackInfo& info);

Napi::Object Init(Napi::Env env, Napi::Object exports) {
  exports.Set("start",     Napi::Function::New(env, Start));
  exports.Set("stop",      Napi::Function::New(env, Stop));
  exports.Set("pushFrame", Napi::Function::New(env, PushFrame));
  return exports;
}

NODE_API_MODULE(vcam, Init)
