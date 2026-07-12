#include <iostream>
#include <wasmedge/wasmedge.h>

void run_registered_func(WasmEdge_VMContext *VM, const char *mod_name,
                         const char *func_name) {
  WasmEdge_String ModName = WasmEdge_StringCreateByCString(mod_name);
  WasmEdge_String FuncName = WasmEdge_StringCreateByCString(func_name);

  WasmEdge_Value Params[2] = {WasmEdge_ValueGenI32(10),
                              WasmEdge_ValueGenI32(20)};
  WasmEdge_Value Returns[1];

  std::cout << "  -> [Execute] Calling " << mod_name << "::" << func_name
            << "... ";
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
  WasmEdge_ConfigureContext *Conf = WasmEdge_ConfigureCreate();
  WasmEdge_VMContext *VM1 = WasmEdge_VMCreate(Conf, nullptr);

  WasmEdge_String Mod2Name = WasmEdge_StringCreateByCString("module_2");
  WasmEdge_VMRegisterModuleFromFile(VM1, Mod2Name, "./wasm_src/provider.wasm");

  WasmEdge_String Mod1Name = WasmEdge_StringCreateByCString("module_1");
  WasmEdge_VMRegisterModuleFromFile(VM1, Mod1Name, "./wasm_src/consumer.wasm");

  std::cout << "\n--- Operation 1: Delete VM1 ---" << std::endl;
  WasmEdge_VMDelete(VM1);

  WasmEdge_StringDelete(Mod2Name);
  WasmEdge_StringDelete(Mod1Name);
  WasmEdge_ConfigureDelete(Conf);
}
