target triple = "x86_64-pc-windows-msvc"

define ptr @load_guest_pointer(i32 %address) {
entry:
  %wide = zext i32 %address to i64
  %slot = inttoptr i64 %wide to ptr
  %value = load ptr, ptr %slot, align 8
  ret ptr %value
}

define void @store_guest_pointer(i32 %address, ptr %value) {
entry:
  %wide = zext i32 %address to i64
  %slot = inttoptr i64 %wide to ptr
  store ptr %value, ptr %slot, align 8
  ret void
}
