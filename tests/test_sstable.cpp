#include <cstdio>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <unistd.h>

#include "tiny_kv/kv_store.h"
#include "tiny_kv/sstable_builder.h"
#include "tiny_kv/sstable_reader.h"

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

static std::string TmpPath(int n) {
    return "/tmp/test_sstable_" + std::to_string(n) + ".sst";
}

int main()
{
    int test_count = 0;

    //=== 1. 空 KVStore → SSTable → Get 返回 NotFound ===
    {
        TEST("Build empty store: Get returns NotFound");
        tiny_kv::KVStore store;

        tiny_kv::SSTableBuilder builder;
        bool ok = (builder.Build(store, TmpPath(1)) == tiny_kv::Status::OK);

        tiny_kv::SSTableReader reader;
        if (ok) ok = (reader.Open(TmpPath(1)) == tiny_kv::Status::OK);

        tiny_kv::Value v;
        if (ok) ok = (reader.Get("anything", &v) == tiny_kv::Status::NotFound);

        if (ok)
            PASS();
        else
            FAIL("empty SSTable test failed");
    }

    //=== 2. 单条 KV → SSTable → Get 返回正确值 ===
    {
        TEST("Build single KV: Get returns correct value");
        tiny_kv::KVStore store;
        store.Put("dog", "1");

        tiny_kv::SSTableBuilder builder;
        bool ok = (builder.Build(store, TmpPath(2)) == tiny_kv::Status::OK);

        tiny_kv::SSTableReader reader;
        if (ok) ok = (reader.Open(TmpPath(2)) == tiny_kv::Status::OK);

        tiny_kv::Value v;
        if (ok) ok = (reader.Get("dog", &v) == tiny_kv::Status::OK && v == "1");

        if (ok)
            PASS();
        else
            FAIL("Get returned wrong result");
    }

    //=== 3. 100 条 KV → SSTable → 验证全部可查 ===
    {
        TEST("Build 100 KV: verify all retrievable");
        tiny_kv::KVStore store;
        char key_buf[16];
        for (int i = 1; i <= 100; ++i) {
            snprintf(key_buf, sizeof(key_buf), "%03d", i);
            store.Put(key_buf, std::to_string(i * 10));
        }

        tiny_kv::SSTableBuilder builder;
        bool ok = (builder.Build(store, TmpPath(3)) == tiny_kv::Status::OK);

        tiny_kv::SSTableReader reader;
        if (ok) ok = (reader.Open(TmpPath(3)) == tiny_kv::Status::OK);

        if (ok) {
            for (int i = 1; i <= 100; ++i) {
                snprintf(key_buf, sizeof(key_buf), "%03d", i);
                tiny_kv::Value v;
                if (reader.Get(key_buf, &v) != tiny_kv::Status::OK ||
                    v != std::to_string(i * 10)) {
                    ok = false;
                    break;
                }
            }
        }

        if (ok)
            PASS();
        else
            FAIL("not all KV pairs found");
    }

    //=== 4. 100 条 → 查不存在的 key 返回 NotFound ===
    {
        TEST("Build 100 KV: non-existing returns NotFound");
        tiny_kv::KVStore store;
        char key_buf[16];
        for (int i = 1; i <= 100; ++i) {
            snprintf(key_buf, sizeof(key_buf), "%03d", i);
            store.Put(key_buf, std::to_string(i));
        }

        tiny_kv::SSTableBuilder builder;
        builder.Build(store, TmpPath(4));

        tiny_kv::SSTableReader reader;
        reader.Open(TmpPath(4));

        tiny_kv::Value v;
        if (reader.Get("999", &v) == tiny_kv::Status::NotFound)
            PASS();
        else
            FAIL("expected NotFound");
    }

    //=== 5. 多 Block：插入足够多大 value 强制跨 Block ===
    {
        TEST("Build multi-block: verify all KV retrievable");
        tiny_kv::KVStore store;
        std::string big_val(100, 'x');
        char key_buf[16];
        for (int i = 1; i <= 500; ++i) {
            snprintf(key_buf, sizeof(key_buf), "%04d", i);
            store.Put(key_buf, big_val + std::to_string(i));
        }

        tiny_kv::SSTableBuilder builder;
        bool ok = (builder.Build(store, TmpPath(5)) == tiny_kv::Status::OK);

        tiny_kv::SSTableReader reader;
        if (ok) ok = (reader.Open(TmpPath(5)) == tiny_kv::Status::OK);

        if (ok) {
            for (int i = 1; i <= 500; ++i) {
                snprintf(key_buf, sizeof(key_buf), "%04d", i);
                tiny_kv::Value v;
                if (reader.Get(key_buf, &v) != tiny_kv::Status::OK ||
                    v != big_val + std::to_string(i)) {
                    ok = false;
                    break;
                }
            }
        }

        if (ok)
            PASS();
        else
            FAIL("not all KV found in multi-block");
    }

    //=== 6. 二分边界：查 Block 的首尾 key ===
    {
        TEST("Binary search boundary: first/last key of each block");
        tiny_kv::KVStore store;
        char key_buf[16];
        for (int i = 1; i <= 64; ++i) {
            snprintf(key_buf, sizeof(key_buf), "k%02d", i);
            store.Put(key_buf, "v");
        }

        tiny_kv::SSTableBuilder builder;
        builder.Build(store, TmpPath(6));

        tiny_kv::SSTableReader reader;
        reader.Open(TmpPath(6));

        tiny_kv::Value v;
        if (reader.Get("k01", &v) == tiny_kv::Status::OK &&
            reader.Get("k64", &v) == tiny_kv::Status::OK)
            PASS();
        else
            FAIL("boundary keys not found");
    }

    //=== 7. Delete 语义：删除一半 key 后 Build，已删除的 NotFound ===
    {
        TEST("Delete semantics: deleted keys not in SSTable");
        tiny_kv::KVStore store;
        char key_buf[16];
        for (int i = 1; i <= 50; ++i) {
            snprintf(key_buf, sizeof(key_buf), "%03d", i);
            store.Put(key_buf, std::to_string(i));
        }
        for (int i = 2; i <= 50; i += 2) {
            snprintf(key_buf, sizeof(key_buf), "%03d", i);
            store.Delete(key_buf);
        }

        tiny_kv::SSTableBuilder builder;
        builder.Build(store, TmpPath(7));

        tiny_kv::SSTableReader reader;
        reader.Open(TmpPath(7));

        bool ok = true;
        tiny_kv::Value v;
        for (int i = 1; i <= 49; i += 2) {
            snprintf(key_buf, sizeof(key_buf), "%03d", i);
            if (reader.Get(key_buf, &v) != tiny_kv::Status::OK) ok = false;
        }
        for (int i = 2; i <= 50; i += 2) {
            snprintf(key_buf, sizeof(key_buf), "%03d", i);
            if (reader.Get(key_buf, &v) != tiny_kv::Status::NotFound) ok = false;
        }

        if (ok)
            PASS();
        else
            FAIL("delete semantics not preserved");
    }

    //=== 8. 损坏文件：magic 不对 → Corruption ===
    {
        TEST("Corrupted magic: Open returns Corruption");
        tiny_kv::KVStore store;
        store.Put("x", "y");

        tiny_kv::SSTableBuilder builder;
        builder.Build(store, TmpPath(8));

        // 破坏 magic（文件末尾倒数第 8 字节起）
        int fd = open(TmpPath(8).c_str(), O_WRONLY);
        if (fd < 0) {
            FAIL("cannot open file for corruption");
        } else {
            off_t sz = lseek(fd, 0, SEEK_END);
            uint32_t bad = 0xDEADBEEF;
            pwrite(fd, &bad, sizeof(bad), sz - 8);
            close(fd);

            tiny_kv::SSTableReader reader;
            if (reader.Open(TmpPath(8)) == tiny_kv::Status::Corruption)
                PASS();
            else
                FAIL("expected Corruption");
        }
    }

    //=== 9. Re-Open: 同一个 Reader 先 Close 再 Open 另一个文件 ===
    {
        TEST("Re-Open: close then open another file works");
        tiny_kv::KVStore store1, store2;
        store1.Put("a", "1");
        store2.Put("b", "2");

        tiny_kv::SSTableBuilder builder;
        builder.Build(store1, TmpPath(9));
        builder.Build(store2, TmpPath(10));

        tiny_kv::SSTableReader reader;
        reader.Open(TmpPath(9));

        tiny_kv::Value v;
        bool ok = (reader.Get("a", &v) == tiny_kv::Status::OK && v == "1");
        reader.Close();

        if (ok) ok = (reader.Open(TmpPath(10)) == tiny_kv::Status::OK);
        if (ok) ok = (reader.Get("b", &v) == tiny_kv::Status::OK && v == "2");
        if (ok) ok = (reader.Get("a", &v) == tiny_kv::Status::NotFound);

        if (ok)
            PASS();
        else
            FAIL("re-open results wrong");
    }

    //=== 10. 变长 key（1B ~ 256B）正常读写 ===
    {
        TEST("Variable-length keys: 1B to 256B");
        tiny_kv::KVStore store;
        for (int l = 1; l <= 256; ++l) {
            std::string key(l, 'k');
            key[l - 1] = static_cast<char>('a' + (l % 26));
            store.Put(key, std::to_string(l));
        }

        tiny_kv::SSTableBuilder builder;
        bool ok = (builder.Build(store, TmpPath(11)) == tiny_kv::Status::OK);

        tiny_kv::SSTableReader reader;
        if (ok) ok = (reader.Open(TmpPath(11)) == tiny_kv::Status::OK);

        if (ok) {
            for (int l = 1; l <= 256; ++l) {
                std::string key(l, 'k');
                key[l - 1] = static_cast<char>('a' + (l % 26));
                tiny_kv::Value v;
                if (reader.Get(key, &v) != tiny_kv::Status::OK ||
                    v != std::to_string(l)) {
                    ok = false;
                    break;
                }
            }
        }

        if (ok)
            PASS();
        else
            FAIL("variable-length key mismatch");
    }

    //=== 结果 ===
    std::cout << "\n=== Results ===" << std::endl;
    std::cout << "Passed: " << passed << "/" << (passed + failed) << std::endl;
    if (failed > 0)
        std::cout << "Failed: " << failed << std::endl;
    return failed == 0 ? 0 : 1;
}
