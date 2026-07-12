#include <iostream>
#include <wasmedge/wasmedge.h>

int main() {
  // --- Step 0: 初始化環境 ---
  WasmEdge_ConfigureContext *ConfCxt = WasmEdge_ConfigureCreate();
  WasmEdge_VMContext *VMCxt = WasmEdge_VMCreate(ConfCxt, nullptr);
  WasmEdge_String ProviderName = WasmEdge_StringCreateByCString("provider");

  WasmEdge_Result Res =
      WasmEdge_VMRegisterModuleFromFile(VMCxt, ProviderName, "provider.wasm");
  if (!WasmEdge_ResultOK(Res)) {
    std::cerr << "Failed to register provider.wasm: "
              << WasmEdge_ResultGetMessage(Res) << std::endl;
    return -1;
  }

  WasmEdge_ASTModuleContext *ASTCxt = nullptr;
  WasmEdge_LoaderContext *LoadCxt = WasmEdge_VMGetLoaderContext(VMCxt);

  Res = WasmEdge_LoaderParseFromFile(LoadCxt, &ASTCxt, "consumer.wasm");
  if (!WasmEdge_ResultOK(Res)) {
    std::cerr << "Failed to parse consumer.wasm" << std::endl;
    return -1;
  }
  // 2. 將 AST 載入 VM
  WasmEdge_VMLoadWasmFromASTModule(VMCxt, ASTCxt);
  // 執行驗證 (Validate)
  Res = WasmEdge_VMValidate(VMCxt);
  if (!WasmEdge_ResultOK(Res)) {
    std::cerr << "Failed to validate consumer.wasm: "
              << WasmEdge_ResultGetMessage(Res) << std::endl;
    return -1;
  }
  // 3. 執行實體化
  Res = WasmEdge_VMInstantiate(VMCxt);
  if (!WasmEdge_ResultOK(Res)) {
    std::cerr << "Failed to instantiate consumer.wasm: "
              << WasmEdge_ResultGetMessage(Res) << std::endl;
    return -1;
  }
  // 取得剛剛產生的匿名實例 (Active Module)
  const WasmEdge_ModuleInstanceContext *ConsumerInst =
      WasmEdge_VMGetActiveModule(VMCxt);
  std::cout << "Consumer Instance (Anonymous) created at: " << ConsumerInst
            << std::endl;

  std::cout << "\n--- Step 3: Try to Delete [provider] while Consumer is "
               "alive ---"
            << std::endl;

  WasmEdge_VMDeleteRegisteredModule(VMCxt, ProviderName);
  // WasmEdge_VMForceDeleteRegisteredModule(VMCxt, ProviderName);

  std::cout << "Note: Check spdlog output for deferred deletion." << std::endl;
  std::cout << "\n--- Step 3.5: Execute Consumer code after ForceDelete ---"
            << std::endl;
  WasmEdge_String FuncName = WasmEdge_StringCreateByCString("run");
  // 2. 準備兩個 i32 參數 (例如 10 + 20)
  WasmEdge_Value Params[2] = {WasmEdge_ValueGenI32(10),
                              WasmEdge_ValueGenI32(20)};
  WasmEdge_Value Returns[1];
  // 3. 執行
  Res = WasmEdge_VMExecute(VMCxt, FuncName, Params, 2, Returns, 1);

  if (WasmEdge_ResultOK(Res)) {
    std::cout << "Execution Success! Result: "
              << WasmEdge_ValueGetI32(Returns[0]) << std::endl;
    std::cout << "This proves that even if 'provider' is logically deleted, "
                 "'consumer' can still use its memory."
              << std::endl;
  } else {
    std::cerr << "Execution Failed: " << WasmEdge_ResultGetMessage(Res)
              << std::endl;
  }
  WasmEdge_StringDelete(FuncName);

  std::cout << "\n--- Step 4: Kill Anonymous Module by Overwriting ---"
            << std::endl;
  // 1. 準備最迷你的 WASM Buffer
  const uint8_t DummyWasm[] = {0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
  // 2. 執行 VM 三部曲 (注意：這裡不要再寫 WasmEdge_Result，直接用 Res)
  Res = WasmEdge_VMLoadWasmFromBuffer(VMCxt, DummyWasm, sizeof(DummyWasm));
  if (!WasmEdge_ResultOK(Res)) {
    std::cout << "Load failed!" << std::endl;
    return -1;
  }
  Res = WasmEdge_VMValidate(VMCxt);
  if (!WasmEdge_ResultOK(Res)) {
    std::cout << "Validate failed!" << std::endl;
    return -1;
  }

  Res = WasmEdge_VMInstantiate(VMCxt);
  if (WasmEdge_ResultOK(Res)) {
    std::cout << "Successfully overwritten the anonymous module." << std::endl;
  } else {
    std::cout << "Instantiate failed!" << std::endl;
  }

  // --- Step 5: 清理環境 ---
  std::cout << "\n--- Step 5: Final Cleanup ---" << std::endl;
  WasmEdge_VMDelete(VMCxt);
  WasmEdge_ConfigureDelete(ConfCxt);
  WasmEdge_StringDelete(ProviderName);
  WasmEdge_ASTModuleDelete(ASTCxt); // 注意：Load 完後 AST 通常可以提早刪除

  std::cout << "Test finished successfully." << std::endl;
  return 0;
}
