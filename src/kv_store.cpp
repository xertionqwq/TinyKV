#include "tiny_kv/kv_store.h"

namespace tiny_kv {

    // Put: 插入键值对，键不存在则插入返回true，键已存在则覆盖返回false
    bool KVStore::Put(const Key& key, const Value& value) {
        return table_.upsert(key, value);
    }

    // Delete: 删除键值对，键存在并删除成功返回true，键不存在返回false
    bool KVStore::Delete(const Key& key) {
        return table_.erase(key);
    }

    // Get: 按键查找，找到则将值写入*value并返回true，未找到返回false
    bool KVStore::Get(const Key& key, Value* value) const {
        const Value* v = table_.find(key);
        if (v) {
            *value = *v;
            return true;
        }
        return false;
    }

    // Exists: 判断键是否存在，存在返回true，不存在返回false
    bool KVStore::Exists(const Key& key) const {
        return table_.find(key) != nullptr;
    }

    // Size: 返回当前存储的键值对数量
    size_t KVStore::Size() const {
        return table_.size();
    }

    // IsEmpty: 判断存储是否为空，无键值对返回true
    bool KVStore::IsEmpty() const {
        return table_.isempty();
    }

    // Clear: 清空所有键值对
    void KVStore::Clear() {
        table_.clear();
    }

}  // namespace tiny_kv
