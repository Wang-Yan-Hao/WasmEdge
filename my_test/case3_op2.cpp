#include <iostream>
#include <stdlib.h>
#include <wasmedge/wasmedge.h>

WasmEdge_Result DummyAdd(void *Data,
                         const WasmEdge_CallingFrameContext *CallFrameCxt,
                         const WasmEdge_Value *In, WasmEdge_Value *Out) {
  Out[0] = WasmEdge_ValueGenI32(WasmEdge_ValueGetI32(In[0]) +
                                WasmEdge_ValueGetI32(In[1]));
  return WasmEdge_Result_Success;
}

void run_registered_func(WasmEdge_VMContext *VM, const char *mod_name,
                         const char *func_name) {
  WasmEdge_String ModName = WasmEdge_StringCreateByCString(mod_name);
  WasmEdge_String FuncName = WasmEdge_StringCreateByCString(func_name);
  WasmEdge_Value Params[2] = {WasmEdge_ValueGenI32(10),
                              WasmEdge_ValueGenI32(20)};
  WasmEdge_Value Returns[1];

  std::cout << "  -> [Execute] " << mod_name << "::" << func_name << "... ";
  WasmEdge_Result Res = WasmEdge_VMExecuteRegistered(VM, ModName, FuncName,
                                                     Params, 2, Returns, 1);

  if (WasmEdge_ResultOK(Res)) {
    std::cout << "Success! Result = " << WasmEdge_ValueGetI32(Returns[0])
              << std::endl;
  } else {
    std::cout << "Failed! Error = " << WasmEdge_ResultGetMessage(Res)
              << std::endl;
  }
  WasmEdge_StringDelete(ModName);
  WasmEdge_StringDelete(FuncName);
}

int main() {
  /* Named module_2 */
  WasmEdge_ConfigureContext *Conf = WasmEdge_ConfigureCreate();
  WasmEdge_String Mod2Name = WasmEdge_StringCreateByCString("module_2");
  WasmEdge_ModuleInstanceContext *HostMod2 =
      WasmEdge_ModuleInstanceCreate(Mod2Name);
  WasmEdge_ValType P[2] = {WasmEdge_ValTypeGenI32(), WasmEdge_ValTypeGenI32()};
  WasmEdge_ValType R[1] = {WasmEdge_ValTypeGenI32()};
  WasmEdge_FunctionTypeContext *FType = WasmEdge_FunctionTypeCreate(P, 2, R, 1);
  WasmEdge_FunctionInstanceContext *HostFunc =
      WasmEdge_FunctionInstanceCreate(FType, DummyAdd, NULL, 0);
  WasmEdge_String FuncName = WasmEdge_StringCreateByCString("add");
  WasmEdge_ModuleInstanceAddFunction(HostMod2, FuncName, HostFunc);

  /* VM */
  WasmEdge_VMContext *VM1 = WasmEdge_VMCreate(Conf, NULL);
  WasmEdge_VMContext *VM2 = WasmEdge_VMCreate(Conf, NULL);
  WasmEdge_VMRegisterModuleFromImport(VM1, HostMod2);
  WasmEdge_VMRegisterModuleFromImport(VM2, HostMod2);

  WasmEdge_String Mod1Name = WasmEdge_StringCreateByCString("module_1");
  WasmEdge_String Mod4Name = WasmEdge_StringCreateByCString("module_4");
  WasmEdge_VMRegisterModuleFromFile(VM1, Mod1Name, "./wasm_src/consumer.wasm");
  WasmEdge_VMRegisterModuleFromFile(VM2, Mod4Name, "./wasm_src/consumer.wasm");

  run_registered_func(VM1, "module_1", "run");
  run_registered_func(VM2, "module_4", "run");

  // Try to remove two VM
  WasmEdge_VMDelete(VM1);
  WasmEdge_VMDelete(VM2);
  WasmEdge_ModuleInstanceDelete(HostMod2);
}
