#pragma once
#include "texture.h"
#include <string>
#include <unordered_map>
#include <memory>
#include <mutex>

/**
 * LRU + 引用计数资源缓存
 * 
 * 设计决策：
 * 1. 使用 std::shared_ptr 管理资源生命周期，自动释放无人引用的资源
 * 2. 使用互斥锁保护缓存表，适用于加载频率远低于渲染频率的场景
 * 3. 限制最大缓存数量，防止内存无限增长
 */
template<typename T>
class ResourceCache {
public:
    explicit ResourceCache(size_t max_entries = 64) : max_entries_(max_entries) {}

    std::shared_ptr<T> get(const std::string& key);
    void put(const std::string& key, std::shared_ptr<T> resource);
    void clear();
    size_t size() const;

private:
    size_t max_entries_;
    std::unordered_map<std::string, std::weak_ptr<T>> cache_;
    std::list<std::string> lru_list_;
    mutable std::mutex mutex_;
};

// 特化：纹理缓存
using TextureCache = ResourceCache<Texture>;
