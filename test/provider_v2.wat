(module
  (func $add (param i32 i32) (result i32)
    local.get 0
    local.get 1
    i32.add
    i32.const 100  ;; 這是我們要加的 100
    i32.add)
  (export "add_func" (func $add))
)
