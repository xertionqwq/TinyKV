#ifndef TINY_KV_KV_STORE_H_
#define TINY_KV_KV_STORE_H_

#include <my_stl/containers/skip_list.h>

#include "types.h"

namespace tiny_kv {
    class KVStore {
        public:
            KVStore() = default;
            ~KVStore() = default;

            KVStore(const KVStore&) = delete;
            KVStore& operator=(const KVStore&) = delete;

            // 写操作
            bool Put(const Key& key, const Value& value);   // 插入或覆盖，返回 true=新增 false=更新
            bool Delete(const Key& key);                     // 返回 true=已删除 false=不存在

            // 读操作
            bool Get(const Key& key, Value* value) const;    // 返回 true=找到
            bool Exists(const Key& key) const;

            // 状态
            size_t Size() const;
            bool IsEmpty() const;
            void Clear();

            // 遍历：按键升序遍历所有 KV 对, 并使用 F 函数回调处理 (只读)
            template <typename F>
            void ForEach(F&& callback) const {
                table_.for_each(std::forward<F>(callback));
            }

        private:
            MySTL::skip_list<Key, Value> table_;
        };

}  // namespace tiny_kv

#endif
