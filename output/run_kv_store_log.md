# 测试结果

生成时间: 2026-05-02 16:38:49

## 编译
```
CMake Warning (dev) at /opt/cmake-4.2.0-linux-x86_64/share/cmake-4.2/Modules/FetchContent.cmake:1963 (message):
  Calling FetchContent_Populate(my_stl) is deprecated, call
  FetchContent_MakeAvailable(my_stl) instead.  Policy CMP0169 can be set to
  OLD to allow FetchContent_Populate(my_stl) to be called directly for now,
  but the ability to call it with declared details will be removed completely
  in a future version.
Call Stack (most recent call first):
  CMakeLists.txt:19 (FetchContent_Populate)
This warning is for project developers.  Use -Wno-dev to suppress it.

-- Configuring done (0.0s)
-- Generating done (0.0s)
-- Build files have been written to: /home/xertion/Code/Tiny_KV/build
[ 60%] Built target tiny_kv
[100%] Built target test_kv_store
```
编译退出码: 0

## 测试运行
```
[1] default construct: IsEmpty=true, Size=0 ... PASSED
[2] Put single: Size=1, IsEmpty=false, Get returns value ... PASSED
[3] Put duplicate: returns false, Size unchanged, Get returns new value ... PASSED
[4] Get non-existing: returns false ... PASSED
[5] Get existing: returns true, value correct ... PASSED
[6] Exists: existing key returns true ... PASSED
[7] Exists: non-existing key returns false ... PASSED
[8] Delete existing: returns true, Size=0, Get=false ... PASSED
[9] Delete non-existing: returns false ... PASSED
[10] Clear: IsEmpty=true, Size=0 ... PASSED
[11] ForEach ordered: ascending 1..100 ... PASSED
[12] ForEach empty: callback not invoked ... PASSED
[13] Delete + ForEach: verify remaining keys ... PASSED
[14] Size consistency: multiple Put/Delete/overwrite ... PASSED
[15] Clear + re-insert: store reusable after Clear ... PASSED

=== Results ===
Passed: 15/15
```
测试退出码: 0
