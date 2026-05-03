#include <cstdio>
#include <iostream>
#include <string>
#include <thread>

#include "tiny_kv/db.h"

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

static std::string TmpDbPath(int n) {
    std::string path = "/tmp/test_db_" + std::to_string(n);
    // 清理旧数据
    std::string cmd = "rm -rf " + path + " && mkdir -p " + path;
    system(cmd.c_str());
    return path;
}

int main()
{
    int test_count = 0;

    //=== 1. Open empty DB → Put → Get 返回正确值 ===
    {
        TEST("Open empty DB: Put and Get");
        std::string path = TmpDbPath(1);

        tiny_kv::DB db;
        bool ok = (db.Open(path) == tiny_kv::Status::OK);
        if (ok) ok = (db.Put("hello", "world") == tiny_kv::Status::OK);

        tiny_kv::Value v;
        if (ok) ok = (db.Get("hello", &v) == tiny_kv::Status::OK && v == "world");

        db.Close();
        if (ok)
            PASS();
        else
            FAIL("expected world");
    }

    //=== 2. Put 覆盖写 ===
    {
        TEST("Put overwrite: Get returns latest value");
        std::string path = TmpDbPath(2);

        tiny_kv::DB db;
        db.Open(path);
        db.Put("k", "v1");
        db.Put("k", "v2");

        tiny_kv::Value v;
        bool ok = (db.Get("k", &v) == tiny_kv::Status::OK && v == "v2");

        db.Close();
        if (ok)
            PASS();
        else
            FAIL("expected v2, got " + v);
    }

    //=== 3. Delete → Get NotFound ===
    {
        TEST("Delete: Get returns NotFound");
        std::string path = TmpDbPath(3);

        tiny_kv::DB db;
        db.Open(path);
        db.Put("del", "val");
        db.Delete("del");

        tiny_kv::Value v;
        bool ok = (db.Get("del", &v) == tiny_kv::Status::NotFound);

        db.Close();
        if (ok)
            PASS();
        else
            FAIL("expected NotFound after Delete");
    }

    //=== 4. 空 key → InvalidArgument ===
    {
        TEST("Empty key returns InvalidArgument");
        std::string path = TmpDbPath(4);

        tiny_kv::DB db;
        db.Open(path);
        bool ok = (db.Put("", "x") == tiny_kv::Status::InvalidArgument);
        if (ok) ok = (db.Delete("") == tiny_kv::Status::InvalidArgument);
        tiny_kv::Value v;
        if (ok) ok = (db.Get("", &v) == tiny_kv::Status::InvalidArgument);

        db.Close();
        if (ok)
            PASS();
        else
            FAIL("expected InvalidArgument for empty key");
    }

    //=== 5. 重启持久化 ===
    {
        TEST("Reopen: data persists after Close");
        std::string path = TmpDbPath(5);

        {
            tiny_kv::DB db;
            db.Open(path);
            db.Put("persist", "keep");
            db.Put("key2", "val2");
            db.Close();
        }

        {
            tiny_kv::DB db;
            db.Open(path);

            tiny_kv::Value v;
            bool ok = (db.Get("persist", &v) == tiny_kv::Status::OK && v == "keep");
            if (ok) ok = (db.Get("key2", &v) == tiny_kv::Status::OK && v == "val2");

            db.Close();
            if (ok)
                PASS();
            else
                FAIL("data not recovered after reopen");
        }
    }

    //=== 6. 重启后 Delete 语义保持 ===
    {
        TEST("Reopen: Delete semantics preserved");
        std::string path = TmpDbPath(6);

        {
            tiny_kv::DB db;
            db.Open(path);
            db.Put("a", "1");
            db.Delete("a");
            db.Close();
        }

        {
            tiny_kv::DB db;
            db.Open(path);
            tiny_kv::Value v;
            bool ok = (db.Get("a", &v) == tiny_kv::Status::NotFound);
            db.Close();
            if (ok)
                PASS();
            else
                FAIL("Delete not preserved after reopen");
        }
    }

    //=== 7. 多条数据随机验证 ===
    {
        TEST("Multiple keys: all retrievable");
        std::string path = TmpDbPath(7);

        tiny_kv::DB db;
        db.Open(path);

        char key_buf[16];
        bool ok = true;
        for (int i = 1; i <= 100; ++i) {
            snprintf(key_buf, sizeof(key_buf), "key%03d", i);
            if (db.Put(key_buf, std::to_string(i * 10)) != tiny_kv::Status::OK) {
                ok = false;
                break;
            }
        }

        if (ok) {
            for (int i = 1; i <= 100; ++i) {
                snprintf(key_buf, sizeof(key_buf), "key%03d", i);
                tiny_kv::Value v;
                if (db.Get(key_buf, &v) != tiny_kv::Status::OK ||
                    v != std::to_string(i * 10)) {
                    ok = false;
                    break;
                }
            }
        }

        db.Close();
        if (ok)
            PASS();
        else
            FAIL("not all keys found");
    }

    //=== 8. memtable dump 触发（写入大 value 超 4MB） ===
    {
        TEST("Memtable dump: data readable after exceeding 4MB");
        std::string path = TmpDbPath(8);

        tiny_kv::DB db;
        db.Open(path);

        std::string big_val(2048, 'x');  // 每条约 2KB
        int count = 2500;                 // 总计约 5MB

        bool ok = true;
        for (int i = 0; i < count; ++i) {
            std::string key = "big" + std::to_string(i);
            if (db.Put(key, big_val) != tiny_kv::Status::OK) {
                ok = false;
                break;
            }
        }

        if (ok) {
            // 验证抽样
            for (int i = 0; i < count; i += 250) {
                std::string key = "big" + std::to_string(i);
                tiny_kv::Value v;
                if (db.Get(key, &v) != tiny_kv::Status::OK || v != big_val) {
                    ok = false;
                    break;
                }
            }
        }

        db.Close();
        if (ok)
            PASS();
        else
            FAIL("data not found after memtable dump");
    }

    //=== 9. 并发读写基础验证 ===
    {
        TEST("Concurrent read/write: no crash");
        std::string path = TmpDbPath(9);

        tiny_kv::DB db;
        db.Open(path);

        for (int i = 0; i < 200; ++i) {
            db.Put("k" + std::to_string(i), "v" + std::to_string(i));
        }

        std::atomic<bool> running{true};
        std::atomic<int> read_ok{0};

        std::thread reader([&] {
            while (running) {
                tiny_kv::Value v;
                if (db.Get("k50", &v) == tiny_kv::Status::OK && v == "v50") {
                    read_ok++;
                }
            }
        });

        std::thread writer([&] {
            for (int i = 200; i < 400; ++i) {
                db.Put("k" + std::to_string(i), "v" + std::to_string(i));
            }
            running = false;
        });

        writer.join();
        reader.join();

        bool ok = (read_ok > 0);

        db.Close();
        if (ok)
            PASS();
        else
            FAIL("concurrent reads failed");
    }

    //=== 10. Get 不存在的 key ===
    {
        TEST("Get non-existing key returns NotFound");
        std::string path = TmpDbPath(10);

        tiny_kv::DB db;
        db.Open(path);
        db.Put("exists", "yes");

        tiny_kv::Value v;
        bool ok = (db.Get("nonexist", &v) == tiny_kv::Status::NotFound);

        db.Close();
        if (ok)
            PASS();
        else
            FAIL("expected NotFound");
    }

    //=== 结果 ===
    std::cout << "\n=== Results ===" << std::endl;
    std::cout << "Passed: " << passed << "/" << (passed + failed) << std::endl;
    if (failed > 0)
        std::cout << "Failed: " << failed << std::endl;
    return failed == 0 ? 0 : 1;
}
