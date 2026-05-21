#pragma once
#include <string>

/**
 * 跨平台资源加载器 —— 带路径安全校验
 */
class AssetLoader {
public:
    explicit AssetLoader(const std::string& root);

    // 解析相对路径为绝对路径，同时进行安全检查
    // 若路径非法（遍历攻击、非法扩展名），返回空字符串
    std::string resolvePath(const std::string& relativePath) const;

    // 读取文件到内存
    static bool loadFile(const std::string& path, std::vector<uint8_t>& outData);

private:
    std::string root_;
};
