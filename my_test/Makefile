# 基礎路徑設定
WASM_ROOT = ..
BUILD_DIR = $(WASM_ROOT)/build
LIB_PATH  = $(BUILD_DIR)/lib/api

# 編譯器與參數
CXX      = g++
CXXFLAGS = -I$(BUILD_DIR)/include/api
# 使用 RPATH 讓編譯出來的執行檔自動找到 .so，不需要手動 export LD_LIBRARY_PATH
LDFLAGS  = $(LIB_PATH)/libwasmedge.so -Wl,-rpath,$(LIB_PATH)

# 自動搜尋所有的 .cpp 檔案
SRCS = $(wildcard *.cpp)
# 將 .cpp 檔名轉為不帶副檔名的目標檔名
BINS = $(SRCS:.cpp=)

.PHONY: all clean run

# 預設目標：編譯所有程式
all: $(BINS)
	@echo "Build finished for: $(BINS)"

# 通用編譯規則
%: %.cpp
	$(CXX) $(CXXFLAGS) $< $(LDFLAGS) -o $@

# 清理編譯產物
clean:
	rm -f $(BINS)

# 快速執行 main (test_dep) 的捷徑
run: all
	export WASMEDGE_LOG_LEVEL=info && ./main