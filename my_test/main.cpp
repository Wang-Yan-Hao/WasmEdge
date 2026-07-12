#include <iostream>
#include <wasmedge/wasmedge.h>

int main() {
  // 1. 建立環境配置與 VM 容器
  WasmEdge_ConfigureContext *ConfCxt = WasmEdge_ConfigureCreate();
  WasmEdge_VMContext *VMCxt = WasmEdge_VMCreate(ConfCxt, NULL);

  std::cout << "[Step 1] Registering 'provider' module..." << std::endl;
  // 這裡會將 provider.wasm 載入、實例化並註冊到 Store 裡，名稱叫做 "provider"
  WasmEdge_String ProviderName = WasmEdge_StringCreateByCString("provider");
  WasmEdge_Result Res1 =
      WasmEdge_VMRegisterModuleFromFile(VMCxt, ProviderName, "provider.wasm");

  if (!WasmEdge_ResultOK(Res1)) {
    std::cerr << "Registration failed: " << WasmEdge_ResultGetMessage(Res1)
              << std::endl;
    return 1;
  }

  std::cout << "[Step 2] Running 'consumer.wasm'..." << std::endl;
  // 執行時，它會自動去 Store 找名為 "provider" 的模組
  WasmEdge_String FuncName = WasmEdge_StringCreateByCString("run");
  WasmEdge_Value Params[2] = {WasmEdge_ValueGenI32(10),
                              WasmEdge_ValueGenI32(20)};
  WasmEdge_Value Returns[1];

  WasmEdge_Result Res2 = WasmEdge_VMRunWasmFromFile(
      VMCxt, "consumer.wasm", FuncName, Params, 2, Returns, 1);

  if (WasmEdge_ResultOK(Res2)) {
    std::cout << ">>> Execution Result: " << WasmEdge_ValueGetI32(Returns[0])
              << " <<<" << std::endl;
  } else {
    std::cerr << "Execution failed: " << WasmEdge_ResultGetMessage(Res2)
              << std::endl;
  }

  // 清理資源
  WasmEdge_StringDelete(ProviderName);
  WasmEdge_StringDelete(FuncName);
  WasmEdge_VMDelete(VMCxt);
  WasmEdge_ConfigureDelete(ConfCxt);

  return 0;
}
