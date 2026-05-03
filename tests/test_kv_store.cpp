#include <iostream>
#include <string>
#include "tiny_kv/kv_store.h"

static int passed = 0;
static int failed = 0;

#define TEST(name) std::cout << "[" << (++test_count) << "] " << name << " ... "
#define PASS()                              \
    do                                      \
    {                                       \
        std::cout << "PASSED" << std::endl; \
        passed++;                           \
    } while (0)
#define FAIL(msg)                                    \
    do                                               \
    {                                                \
        std::cout << "FAILED: " << msg << std::endl; \
        failed++;                                    \
    } while (0)

int main()
{
    int test_count = 0;

    //=== 1. 构造与初始状态 ===
    {
        TEST("default construct: IsEmpty=true, Size=0");
        tiny_kv::KVStore store;
        if (store.IsEmpty() && store.Size() == 0)
            PASS();
        else
            FAIL("empty store wrong state");
    }

    //=== 2. 基本写操作 ===
    {
        TEST("Put single: Size=1, IsEmpty=false, Get returns value");
        tiny_kv::KVStore store;
        bool ok = store.Put("dog", "1");
        tiny_kv::Value v;
        bool found = store.Get("dog", &v);
        if (ok && store.Size() == 1 && !store.IsEmpty() && found && v == "1")
            PASS();
        else
            FAIL("Put or Get failed");
    }

    {
        TEST("Put duplicate: returns false, Size unchanged, Get returns new value");
        tiny_kv::KVStore store;
        store.Put("cat", "old");
        bool ok = store.Put("cat", "new");
        tiny_kv::Value v;
        store.Get("cat", &v);
        if (!ok && store.Size() == 1 && v == "new")
            PASS();
        else
            FAIL("duplicate not updated correctly");
    }

    //=== 3. Get 操作 ===
    {
        TEST("Get non-existing: returns false");
        tiny_kv::KVStore store;
        store.Put("a", "1");
        tiny_kv::Value v = "untouched";
        bool ok = store.Get("nonexist", &v);
        if (!ok && v == "untouched")
            PASS();
        else
            FAIL("should return false for missing key");
    }

    {
        TEST("Get existing: returns true, value correct");
        tiny_kv::KVStore store;
        store.Put("key1", "val1");
        store.Put("key2", "val2");
        tiny_kv::Value v;
        bool ok = store.Get("key2", &v);
        if (ok && v == "val2")
            PASS();
        else
            FAIL("Get returned wrong value");
    }

    //=== 4. Exists 操作 ===
    {
        TEST("Exists: existing key returns true");
        tiny_kv::KVStore store;
        store.Put("x", "1");
        if (store.Exists("x"))
            PASS();
        else
            FAIL("Exists should return true");
    }

    {
        TEST("Exists: non-existing key returns false");
        tiny_kv::KVStore store;
        store.Put("x", "1");
        if (!store.Exists("y"))
            PASS();
        else
            FAIL("Exists should return false");
    }

    //=== 5. Delete 操作 ===
    {
        TEST("Delete existing: returns true, Size=0, Get=false");
        tiny_kv::KVStore store;
        store.Put("del", "me");
        bool ok = store.Delete("del");
        if (ok && store.Size() == 0 && store.IsEmpty() && !store.Exists("del"))
            PASS();
        else
            FAIL("Delete failed");
    }

    {
        TEST("Delete non-existing: returns false");
        tiny_kv::KVStore store;
        store.Put("k", "v");
        bool ok = store.Delete("no_such");
        if (!ok && store.Size() == 1)
            PASS();
        else
            FAIL("should return false for missing key");
    }

    //=== 6. Clear ===
    {
        TEST("Clear: IsEmpty=true, Size=0");
        tiny_kv::KVStore store;
        store.Put("a", "1");
        store.Put("b", "2");
        store.Clear();
        if (store.IsEmpty() && store.Size() == 0 && !store.Exists("a"))
            PASS();
        else
            FAIL("Clear did not empty store");
    }

    //=== 7. ForEach 遍历 ===
    {
        TEST("ForEach ordered: ascending 1..100");
        tiny_kv::KVStore store;
        char buf[16];
        // 零填充保证字典序等于数字序: "001" < "002" < ... < "100"
        for (int i = 1; i <= 100; ++i) {
            snprintf(buf, sizeof(buf), "%03d", i);
            store.Put(buf, std::to_string(i * 10));
        }
        int count = 0;
        int prev = 0;
        bool ordered = true;
        store.ForEach([&](const tiny_kv::Key& key, const tiny_kv::Value&) {
            ++count;
            int k = std::stoi(key);
            if (k <= prev) ordered = false;
            prev = k;
        });
        if (count == 100 && ordered)
            PASS();
        else
            FAIL("ForEach count or order wrong");
    }

    {
        TEST("ForEach empty: callback not invoked");
        tiny_kv::KVStore store;
        int call_count = 0;
        store.ForEach([&](const tiny_kv::Key&, const tiny_kv::Value&) {
            ++call_count;
        });
        if (call_count == 0)
            PASS();
        else
            FAIL("callback should not be called on empty store");
    }

    //=== 8. Delete + ForEach ===
    {
        TEST("Delete + ForEach: verify remaining keys");
        tiny_kv::KVStore store;
        for (int i = 1; i <= 10; ++i)
            store.Put(std::to_string(i), std::to_string(i));
        // 删除中间部分键
        store.Delete("3");
        store.Delete("5");
        store.Delete("7");
        store.Delete("9");
        int count = 0;
        bool has_deleted = false;
        store.ForEach([&](const tiny_kv::Key& key, const tiny_kv::Value&) {
            ++count;
            if (key == "3" || key == "5" || key == "7" || key == "9")
                has_deleted = true;
        });
        if (count == 6 && !has_deleted && store.Size() == 6)
            PASS();
        else
            FAIL("ForEach after Delete mismatch");
    }

    //=== 9. Size 一致性 ===
    {
        TEST("Size consistency: multiple Put/Delete/overwrite");
        tiny_kv::KVStore store;
        for (int i = 0; i < 50; ++i)
            store.Put(std::to_string(i), "v");
        for (int i = 0; i < 20; ++i)
            store.Delete(std::to_string(i));
        // Put 50, Delete 20 → 30 个; 覆盖一个未被删除的键, Size 不变
        store.Put("30", "new");
        if (store.Size() == 30)
            PASS();
        else
            FAIL("Size not consistent");
    }

    //=== 10. Clear + re-insert ===
    {
        TEST("Clear + re-insert: store reusable after Clear");
        tiny_kv::KVStore store;
        store.Put("a", "1");
        store.Clear();
        store.Put("b", "2");
        tiny_kv::Value v;
        bool found = store.Get("b", &v);
        if (store.Size() == 1 && found && v == "2" && !store.Exists("a"))
            PASS();
        else
            FAIL("store not reusable after Clear");
    }

    //=== 结果 ===
    std::cout << "\n=== Results ===" << std::endl;
    std::cout << "Passed: " << passed << "/" << (passed + failed) << std::endl;
    if (failed > 0)
        std::cout << "Failed: " << failed << std::endl;
    return failed == 0 ? 0 : 1;
}
