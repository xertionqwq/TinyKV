#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>

#include "tiny_kv/thread_pool.h"

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

    //=== 1. 构造/析构：不提交任务，验证不崩溃 ===
    {
        TEST("construct and destruct with no tasks");
        {
            tiny_kv::ThreadPool pool(2);
        }
        PASS();
    }

    //=== 2. 提交单个任务 ===
    {
        TEST("submit single task");
        std::atomic<int> counter{0};
        {
            tiny_kv::ThreadPool pool(2);
            pool.Submit([&] { counter.fetch_add(1); });
        }  // 析构自动 Shutdown，排空队列
        if (counter.load() == 1)
            PASS();
        else
            FAIL("expected counter=1, got " + std::to_string(counter.load()));
    }

    //=== 3. 提交多个任务，验证全部执行 ===
    {
        TEST("submit multiple tasks");
        const int N = 1000;
        std::atomic<int> counter{0};
        {
            tiny_kv::ThreadPool pool(4);
            for (int i = 0; i < N; ++i) {
                pool.Submit([&] { counter.fetch_add(1); });
            }
        }
        if (counter.load() == N)
            PASS();
        else
            FAIL("expected counter=" + std::to_string(N) + ", got " + std::to_string(counter.load()));
    }

    //=== 4. 排空语义：Submit 后立即 Shutdown，任务被执行 ===
    {
        TEST("drain queue on shutdown");
        std::atomic<int> counter{0};
        tiny_kv::ThreadPool pool(2);
        pool.Submit([&] { counter.fetch_add(1); });
        pool.Submit([&] { counter.fetch_add(1); });
        pool.Shutdown();
        if (counter.load() == 2)
            PASS();
        else
            FAIL("expected counter=2, got " + std::to_string(counter.load()));
    }

    //=== 5. 异常不崩溃：提交抛异常的任务后继续正常执行 ===
    {
        TEST("exception does not crash worker thread");
        std::atomic<int> counter{0};
        tiny_kv::ThreadPool pool(2);
        pool.Submit([] { throw std::runtime_error("oops"); });
        pool.Submit([&] { counter.fetch_add(1); });
        pool.Shutdown();
        if (counter.load() == 1)
            PASS();
        else
            FAIL("expected counter=1, got " + std::to_string(counter.load()));
    }

    //=== 结果 ===
    std::cout << "\n=== Results ===" << std::endl;
    std::cout << "Passed: " << passed << "/" << (passed + failed) << std::endl;
    if (failed > 0)
        std::cout << "Failed: " << failed << std::endl;
    return failed == 0 ? 0 : 1;
}
