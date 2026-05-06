// 纯数据结构性能对比：跳表 vs std::map vs std::unordered_map
// 去掉 WAL/SSTable 开销，只测 memtable 层的写入、读取、有序遍历

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include <my_stl/skip_list.h>

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

//=== 数据生成 ================================================================

struct DataSet {
    std::vector<std::string> sorted_keys;   // 有序 key（用于 ForEach 验证）
    std::vector<std::string> rand_keys;     // 随机打乱 key
    std::vector<std::string> values;
    std::vector<std::string> miss_keys;
};

static DataSet MakeData(int n) {
    DataSet ds;
    char buf[32];

    for (int i = 0; i < n; ++i) {
        snprintf(buf, sizeof(buf), "key_%06d", i);
        ds.sorted_keys.emplace_back(buf);
    }

    ds.rand_keys = ds.sorted_keys;
    std::random_device rd;
    std::mt19937       g(rd());
    std::shuffle(ds.rand_keys.begin(), ds.rand_keys.end(), g);

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

//=== 计时辅助 ================================================================

struct Result {
    const char* name;
    double      qps;
    double      us_per_op;
    long        rss_kb;
    bool        has_rss;
};

// QPS量化和内存增量测量，支持对比不同实现的性能差异
template <typename F>
static Result Measure(const char* name, int n_ops, bool with_rss, F&& fn) {
    long rss_before = with_rss ? GetVmRSS() : 0;
    auto start = std::chrono::high_resolution_clock::now();
    fn();
    auto end = std::chrono::high_resolution_clock::now();
    long rss_after = with_rss ? GetVmRSS() : 0;

    double us =
        static_cast<double>(
            std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
    double qps       = static_cast<double>(n_ops) / (us / 1'000'000.0);
    double us_per_op = us / static_cast<double>(n_ops);

    return {name, qps, us_per_op, rss_after - rss_before, with_rss};
}

static void Print(const Result& r) {
    printf("  %-20s: %9.0f ops/s  (%6.1f us/op)", r.name, r.qps, r.us_per_op);
    if (r.has_rss) {
        printf("  RSS %+ld kB", r.rss_kb);
    }
    printf("\n");
}

static void PrintVs(const Result& baseline, const Result& other) {
    double ratio = other.qps / baseline.qps;
    double mem_ratio = baseline.rss_kb ? static_cast<double>(other.rss_kb) /
                                             static_cast<double>(baseline.rss_kb)
                                       : 1.0;
    printf("         vs %-14s: %5.1f%% QPS,  %5.1f%% MEM\n",
           baseline.name, ratio * 100.0, mem_ratio * 100.0);
}

//=== Bench: Put ==============================================================

template <typename Store, typename Init>
static Result BenchPut(const char* name, const DataSet& ds, Init&& init_fn) {
    int n = static_cast<int>(ds.rand_keys.size());
    return Measure(name, n, true, [&] {
        Store store = init_fn(static_cast<int>(ds.rand_keys.size()));
        for (int i = 0; i < n; ++i) {
            store.Put(ds.rand_keys[static_cast<size_t>(i)], ds.values[static_cast<size_t>(i)]);
        }
    });
}

//=== Bench: Get ==============================================================

template <typename Store>
static Result BenchGetHit(const DataSet& ds, Store& store) {
    int n = static_cast<int>(ds.rand_keys.size());
    return Measure("", n, false, [&] {
        for (int i = 0; i < n; ++i) {
            store.Get(ds.rand_keys[static_cast<size_t>(i)]);
        }
    });
}

template <typename Store>
static Result BenchGetMiss(const DataSet& ds, Store& store) {
    int n = static_cast<int>(ds.miss_keys.size());
    return Measure("", n, false, [&] {
        for (int i = 0; i < n; ++i) {
            store.Get(ds.miss_keys[static_cast<size_t>(i)]);
        }
    });
}

//=== Bench: ForEach (有序遍历，模拟 SSTable dump) =============================

template <typename Store>
static Result BenchForEach(const char* name, int n, Store& store) {
    return Measure(name, n, false, [&] {
        volatile int count = 0;
        store.ForEach([&count](const std::string& /*k*/, const std::string& /*v*/) {
            count++;
        });
        (void)count;
    });
}

//=== Wrapper: SkipList =======================================================

struct SkipListStore {
    MySTL::skip_list<std::string, std::string> table;

    void Put(const std::string& k, const std::string& v) { table.upsert(k, v); }
    bool Get(const std::string& k) const {
        auto* p = table.find(k);
        return p != nullptr;
    }
    template <typename F>
    void ForEach(F&& f) const {
        table.for_each(std::forward<F>(f));
    }
    size_t Size() const { return table.size(); }
};

//=== Wrapper: RBTree (std::map) ==============================================

struct RBTreeStore {
    std::map<std::string, std::string> table;
    std::mutex mtx_;  // 公平对比：跳表 insert 有锁

    void Put(const std::string& k, const std::string& v) {
        std::lock_guard<std::mutex> lock(mtx_);
        table.insert_or_assign(k, v);
    }
    bool Get(const std::string& k) const { return table.find(k) != table.end(); }
    template <typename F>
    void ForEach(F&& f) const {
        for (auto& [k, v] : table) f(k, v);
    }
    size_t Size() const { return table.size(); }
};

//=== Wrapper: HashMap (std::unordered_map) ====================================

struct HashMapStore {
    std::unordered_map<std::string, std::string> table;
    std::mutex mtx_;

    HashMapStore() = default;
    explicit HashMapStore(int n) { table.reserve(static_cast<size_t>(n)); }

    void Put(const std::string& k, const std::string& v) {
        std::lock_guard<std::mutex> lock(mtx_);
        table.insert_or_assign(k, v);
    }
    bool Get(const std::string& k) const { return table.find(k) != table.end(); }
    template <typename F>
    void ForEach(F&& f) const {
        // 必须排序——SSTable dump 要求有序输出
        std::vector<std::pair<const std::string*, const std::string*>> sorted;
        sorted.reserve(table.size());
        for (auto& [k, v] : table) {
            sorted.emplace_back(&k, &v);
        }
        std::sort(sorted.begin(), sorted.end(),
                  [](auto& a, auto& b) { return *a.first < *b.first; });
        for (auto& [k, v] : sorted) f(*k, *v);
    }
    size_t Size() const { return table.size(); }
};

//=== main ====================================================================

int main() {
    const int N = 100000;
    printf("Generating %d KV pairs (100B values)...\n", N);
    DataSet ds = MakeData(N);

    // --- Put ---
    printf("\n========== Put (%d inserts) ==========\n", N);

    auto MakeSkip  = [](int) -> SkipListStore { return {}; };
    auto MakeRB    = [](int) -> RBTreeStore   { return {}; };
    auto MakeHash  = [](int n) -> HashMapStore { return HashMapStore(n); };

    Result r_skip_put = BenchPut<SkipListStore>("SkipList", ds, MakeSkip);
    Result r_rb_put   = BenchPut<RBTreeStore>("RBTree", ds, MakeRB);
    Result r_hash_put = BenchPut<HashMapStore>("HashMap", ds, MakeHash);

    Print(r_hash_put);
    PrintVs(r_skip_put, r_hash_put);
    Print(r_rb_put);
    PrintVs(r_skip_put, r_rb_put);
    Print(r_skip_put);

    // --- Get (hit) ---
    printf("\n========== Get hit (%d lookups) ==========\n", N);

    SkipListStore  sl;
    RBTreeStore    rb;
    HashMapStore   hs(N);
    for (int i = 0; i < N; ++i) {
        sl.Put(ds.rand_keys[static_cast<size_t>(i)], ds.values[static_cast<size_t>(i)]);
        rb.Put(ds.rand_keys[static_cast<size_t>(i)], ds.values[static_cast<size_t>(i)]);
        hs.Put(ds.rand_keys[static_cast<size_t>(i)], ds.values[static_cast<size_t>(i)]);
    }

    Result r_hash_get = BenchGetHit<HashMapStore>(ds, hs);
    r_hash_get.name = "HashMap";
    Print(r_hash_get);
    Result r_skip_get = BenchGetHit<SkipListStore>(ds, sl);
    r_skip_get.name = "SkipList";
    Print(r_skip_get);
    PrintVs(r_skip_get, r_hash_get);
    Result r_rb_get = BenchGetHit<RBTreeStore>(ds, rb);
    r_rb_get.name = "RBTree";
    Print(r_rb_get);
    PrintVs(r_skip_get, r_rb_get);

    // --- Get (miss) ---
    printf("\n========== Get miss (%d lookups) ==========\n", N);

    Result r_hash_miss = BenchGetMiss<HashMapStore>(ds, hs);
    r_hash_miss.name = "HashMap";
    Print(r_hash_miss);
    Result r_skip_miss = BenchGetMiss<SkipListStore>(ds, sl);
    r_skip_miss.name = "SkipList";
    Print(r_skip_miss);
    PrintVs(r_skip_miss, r_hash_miss);
    Result r_rb_miss = BenchGetMiss<RBTreeStore>(ds, rb);
    r_rb_miss.name = "RBTree";
    Print(r_rb_miss);
    PrintVs(r_skip_miss, r_rb_miss);

    // --- ForEach (有序遍历 = SSTable dump 核心) ---
    printf("\n========== ForEach (%d entries, ordered dump) ==========\n", N);

    Result r_hash_fe = BenchForEach<HashMapStore>("HashMap", N, hs);
    Print(r_hash_fe);
    PrintVs(r_skip_put, r_hash_fe);

    Result r_rb_fe = BenchForEach<RBTreeStore>("RBTree", N, rb);
    Print(r_rb_fe);
    PrintVs(r_skip_put, r_rb_fe);

    Result r_skip_fe = BenchForEach<SkipListStore>("SkipList", N, sl);
    Print(r_skip_fe);

    printf("\nDone.\n");
    return 0;
}
