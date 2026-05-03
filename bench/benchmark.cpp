#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "tiny_kv/db.h"

//=== 内存测量 ================================================================

static long GetVmRSS() {
    FILE* fp = fopen("/proc/self/status", "r");
    if (!fp) return -1;
    char line[256];
    long rss = -1;
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "VmRSS:", 6) == 0) {
            rss = atol(line + 6);
            break;
        }
    }
    fclose(fp);
    return rss;  // kB
}

//=== 计时辅助 ================================================================

struct BenchResult {
    const char* name;
    double      qps;
    double      us_per_op;
    long        rss_delta;  // Put 用
    bool        has_rss;
};

static BenchResult Measure(const char* name, int n_ops, bool with_rss,
                           const std::function<void()>& fn) {
    long rss_before = with_rss ? GetVmRSS() : 0;

    auto start = std::chrono::high_resolution_clock::now();
    fn();
    auto end = std::chrono::high_resolution_clock::now();

    long rss_after = with_rss ? GetVmRSS() : 0;

    double elapsed_us =
        static_cast<double>(
            std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
    double qps       = static_cast<double>(n_ops) / (elapsed_us / 1'000'000.0);
    double us_per_op = elapsed_us / static_cast<double>(n_ops);

    return {name, qps, us_per_op, rss_after - rss_before, with_rss};
}

static void PrintResult(const BenchResult& r) {
    printf("  %-20s: %8.0f ops/s  (%7.1f us/op)", r.name, r.qps, r.us_per_op);
    if (r.has_rss) {
        printf("  RSS %+ld kB", r.rss_delta);
    }
    printf("\n");
}

//=== 数据生成 ================================================================

struct DataSet {
    std::vector<std::string> keys;
    std::vector<std::string> values;
    std::vector<std::string> miss_keys;
};

static DataSet MakeData(int n) {
    DataSet ds;

    char buf[32];
    for (int i = 0; i < n; ++i) {
        snprintf(buf, sizeof(buf), "key_%06d", i);
        ds.keys.emplace_back(buf);
    }
    std::random_device rd;
    std::mt19937       g(rd());
    std::shuffle(ds.keys.begin(), ds.keys.end(), g);

    std::mt19937                        vg(rd());
    std::uniform_int_distribution<char> vdist('a', 'z');
    ds.values.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        std::string v(100, ' ');
        for (int j = 0; j < 100; ++j) v[static_cast<size_t>(j)] = vdist(vg);
        ds.values.push_back(v);
    }

    for (int i = 0; i < n; ++i) {
        snprintf(buf, sizeof(buf), "miss_%06d", i);
        ds.miss_keys.emplace_back(buf);
    }
    std::shuffle(ds.miss_keys.begin(), ds.miss_keys.end(), g);

    return ds;
}

//=== Bench: Put ==============================================================

static void BenchPutDB(const DataSet& ds, std::vector<BenchResult>* results) {
    int n = static_cast<int>(ds.keys.size());
    results->push_back(Measure("tiny_kv::DB", n, true, [&] {
        system("rm -rf /tmp/bench_db && mkdir -p /tmp/bench_db");
        tiny_kv::DB db;
        db.Open("/tmp/bench_db");
        for (int i = 0; i < n; ++i) {
            db.Put(ds.keys[static_cast<size_t>(i)], ds.values[static_cast<size_t>(i)]);
        }
        db.Close();
        system("rm -rf /tmp/bench_db");
    }));
}

static void BenchPutStdMap(const DataSet& ds, std::vector<BenchResult>* results) {
    int n = static_cast<int>(ds.keys.size());
    results->push_back(Measure("std::map", n, true, [&] {
        std::map<std::string, std::string> m;
        for (int i = 0; i < n; ++i) {
            m[ds.keys[static_cast<size_t>(i)]] = ds.values[static_cast<size_t>(i)];
        }
    }));
}

static void BenchPutStdUnorderedMap(const DataSet& ds, std::vector<BenchResult>* results) {
    int n = static_cast<int>(ds.keys.size());
    results->push_back(Measure("std::unordered_map", n, true, [&] {
        std::unordered_map<std::string, std::string> m;
        m.reserve(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) {
            m[ds.keys[static_cast<size_t>(i)]] = ds.values[static_cast<size_t>(i)];
        }
    }));
}

//=== Bench: Get (hit) ========================================================

static void BenchGetHitDB(const DataSet& ds, std::vector<BenchResult>* results) {
    int n = static_cast<int>(ds.keys.size());
    system("rm -rf /tmp/bench_db && mkdir -p /tmp/bench_db");

    // 先写入（不计时）
    {
        tiny_kv::DB db;
        db.Open("/tmp/bench_db");
        for (int i = 0; i < n; ++i) {
            db.Put(ds.keys[static_cast<size_t>(i)], ds.values[static_cast<size_t>(i)]);
        }
        db.Close();
    }

    // 计时 Get
    results->push_back(Measure("tiny_kv::DB", n, false, [&] {
        tiny_kv::DB db;
        db.Open("/tmp/bench_db");
        for (int i = 0; i < n; ++i) {
            tiny_kv::Value v;
            db.Get(ds.keys[static_cast<size_t>(i)], &v);
        }
        db.Close();
    }));

    system("rm -rf /tmp/bench_db");
}

static void BenchGetHitStdMap(const DataSet& ds, std::vector<BenchResult>* results) {
    int n = static_cast<int>(ds.keys.size());
    std::map<std::string, std::string> m;
    for (int i = 0; i < n; ++i) {
        m[ds.keys[static_cast<size_t>(i)]] = ds.values[static_cast<size_t>(i)];
    }

    results->push_back(Measure("std::map", n, false, [&] {
        for (int i = 0; i < n; ++i) {
            auto it = m.find(ds.keys[static_cast<size_t>(i)]);
            volatile bool found = (it != m.end());
            (void)found;
        }
    }));
}

static void BenchGetHitStdUnorderedMap(const DataSet& ds, std::vector<BenchResult>* results) {
    int n = static_cast<int>(ds.keys.size());
    std::unordered_map<std::string, std::string> m;
    m.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        m[ds.keys[static_cast<size_t>(i)]] = ds.values[static_cast<size_t>(i)];
    }

    results->push_back(Measure("std::unordered_map", n, false, [&] {
        for (int i = 0; i < n; ++i) {
            auto it = m.find(ds.keys[static_cast<size_t>(i)]);
            volatile bool found = (it != m.end());
            (void)found;
        }
    }));
}

//=== Bench: Get (miss) =======================================================

static void BenchGetMissDB(const DataSet& ds, std::vector<BenchResult>* results) {
    int n = static_cast<int>(ds.keys.size());
    system("rm -rf /tmp/bench_db && mkdir -p /tmp/bench_db");

    {
        tiny_kv::DB db;
        db.Open("/tmp/bench_db");
        for (int i = 0; i < n; ++i) {
            db.Put(ds.keys[static_cast<size_t>(i)], ds.values[static_cast<size_t>(i)]);
        }
        db.Close();
    }

    results->push_back(Measure("tiny_kv::DB", n, false, [&] {
        tiny_kv::DB db;
        db.Open("/tmp/bench_db");
        for (int i = 0; i < n; ++i) {
            tiny_kv::Value v;
            db.Get(ds.miss_keys[static_cast<size_t>(i)], &v);
        }
        db.Close();
    }));

    system("rm -rf /tmp/bench_db");
}

static void BenchGetMissStdMap(const DataSet& ds, std::vector<BenchResult>* results) {
    int n = static_cast<int>(ds.keys.size());
    std::map<std::string, std::string> m;
    for (int i = 0; i < n; ++i) {
        m[ds.keys[static_cast<size_t>(i)]] = ds.values[static_cast<size_t>(i)];
    }

    results->push_back(Measure("std::map", n, false, [&] {
        for (int i = 0; i < n; ++i) {
            auto it = m.find(ds.miss_keys[static_cast<size_t>(i)]);
            volatile bool found = (it != m.end());
            (void)found;
        }
    }));
}

static void BenchGetMissStdUnorderedMap(const DataSet& ds, std::vector<BenchResult>* results) {
    int n = static_cast<int>(ds.keys.size());
    std::unordered_map<std::string, std::string> m;
    m.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        m[ds.keys[static_cast<size_t>(i)]] = ds.values[static_cast<size_t>(i)];
    }

    results->push_back(Measure("std::unordered_map", n, false, [&] {
        for (int i = 0; i < n; ++i) {
            auto it = m.find(ds.miss_keys[static_cast<size_t>(i)]);
            volatile bool found = (it != m.end());
            (void)found;
        }
    }));
}

//=== main ====================================================================

int main() {
    const int N = 100000;

    printf("Generating %d random KV pairs (100B values)...\n", N);
    DataSet ds = MakeData(N);

    {
        printf("\n=== Put (%d inserts) ===\n", N);
        std::vector<BenchResult> results;
        BenchPutStdUnorderedMap(ds, &results);
        BenchPutStdMap(ds, &results);
        BenchPutDB(ds, &results);
        for (auto& r : results) PrintResult(r);
    }

    {
        printf("\n=== Get hit (%d lookups) ===\n", N);
        std::vector<BenchResult> results;
        BenchGetHitStdUnorderedMap(ds, &results);
        BenchGetHitStdMap(ds, &results);
        BenchGetHitDB(ds, &results);
        for (auto& r : results) PrintResult(r);
    }

    {
        printf("\n=== Get miss (%d lookups) ===\n", N);
        std::vector<BenchResult> results;
        BenchGetMissStdUnorderedMap(ds, &results);
        BenchGetMissStdMap(ds, &results);
        BenchGetMissDB(ds, &results);
        for (auto& r : results) PrintResult(r);
    }

    printf("\nDone.\n");
    return 0;
}
