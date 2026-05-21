/**
 * ResourceCache 模板类显式实例化实现 —— LRU 资源缓存
 * 
 * 核心机制：
 * 1. 缓存表使用 unordered_map<string, weak_ptr<T>>，不阻止资源被外部释放
 * 2. LRU 链表记录访问顺序，当缓存满时淘汰最久未使用的 key
 * 3. 互斥锁保护所有缓存操作，保证多线程安全（渲染线程与加载线程并发）
 */

#include <list>
#include <algorithm>
#include "utils/resource_cache.h"
#include "utils/logger.h"

/**
 * 获取缓存中的资源
 * 
 * 流程：
 * 1. 查找 key 是否存在
 * 2. 若存在，尝试 lock() weak_ptr 提升为 shared_ptr
 * 3. 若资源仍存活，将其移至 LRU 链表头部（最近使用），返回 shared_ptr
 * 4. 若资源已过期（weak_ptr.lock 失败），清理该条目，返回 nullptr
 */
template<typename T>
std::shared_ptr<T> ResourceCache<T>::get(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = cache_.find(key);
    if (it == cache_.end()) {
        return nullptr;  // 缓存未命中
    }

    std::shared_ptr<T> sp = it->second.lock();
    if (!sp) {
        // 资源已被外部释放，weak_ptr 无法提升，清理残留条目
        cache_.erase(it);
        lru_list_.remove(key);
        return nullptr;
    }

    // LRU 更新：从当前位置移除并压入链表头部（最近使用）
    lru_list_.remove(key);
    lru_list_.push_front(key);
    return sp;
}

/**
 * 将资源放入缓存
 * 
 * 流程：
 * 1. 若 key 已存在，先移除旧 LRU 节点
 * 2. 插入新的 weak_ptr 映射，并将 key 压入 LRU 头部
 * 3. 若缓存数量超过上限，从链表尾部淘汰最旧条目
 */
template<typename T>
void ResourceCache<T>::put(const std::string& key, std::shared_ptr<T> resource) {
    if (!resource) {
        return;  // 拒绝空指针入缓存
    }

    std::lock_guard<std::mutex> lock(mutex_);

    auto it = cache_.find(key);
    if (it != cache_.end()) {
        // 覆盖旧条目：先从 LRU 链表移除旧位置
        lru_list_.remove(key);
    }

    cache_[key] = resource;       // weak_ptr 自动由 shared_ptr 构造
    lru_list_.push_front(key);    // 新条目为最近使用

    // LRU 淘汰策略：当缓存条目数超过上限时，移除最久未使用的尾部条目
    while (cache_.size() > max_entries_) {
        const std::string& oldest = lru_list_.back();
        cache_.erase(oldest);
        lru_list_.pop_back();
    }
}

/**
 * 清空所有缓存条目
 */
template<typename T>
void ResourceCache<T>::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_.clear();
    lru_list_.clear();
}

/**
 * 返回当前缓存中的条目数量
 */
template<typename T>
size_t ResourceCache<T>::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cache_.size();
}

/**
 * 显式模板实例化：TextureCache
 * 
 * 将模板类的所有成员函数在此编译单元实例化为 Texture 版本，
 * 避免其他 cpp 文件重复实例化导致的代码膨胀或链接错误。
 */
template class ResourceCache<Texture>;
