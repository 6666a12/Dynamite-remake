/**
 * Dynamite 重构项目 —— 跨平台资源加载器
 *
 * 功能：
 * - 从本地 assets 目录读取文件到内存
 * - 路径安全校验：禁止目录遍历、扩展名白名单
 *
 * 安全策略：
 * 1. 禁止路径中包含 ".."，防止目录遍历攻击
 * 2. 扩展名白名单，仅允许加载游戏所需资源类型
 * 3. 文件大小上限，防止内存耗尽
 */

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <cstdint>
#include <algorithm>

#if defined(__ANDROID__)
    #include <SDL3/SDL.h>
#endif

// -----------------------------------------------------------------------------
// 安全常量定义
// -----------------------------------------------------------------------------

/** 允许加载的文件扩展名白名单 */
static const char* ALLOWED_EXTENSIONS[] = {
    ".png",   // 纹理/封面
    ".jpg",   // 纹理/封面（备用格式）
    ".jpeg",  // 纹理（备用格式）
    ".ogg",   // 音频（背景音乐、音效）
    ".mp3",   // 音频（背景音乐）
    ".chart", // 谱面数据
    ".json",  // 元数据、配置
    ".ttf",   // 字体
    ".ttc"    // 字体（集合）
};

/** 单文件大小上限：64 MB */
static constexpr size_t MAX_FILE_SIZE = 64 * 1024 * 1024;

// -----------------------------------------------------------------------------
// 路径安全校验
// -----------------------------------------------------------------------------

/**
 * 检查文件扩展名是否在白名单中
 *
 * @param filename 文件名（可含路径）
 * @return 白名单内返回 true
 */
static bool IsExtensionAllowed(const std::string& filename) {
    // 查找最后一个 '.' 的位置
    size_t dotPos = filename.rfind('.');
    if (dotPos == std::string::npos) {
        return false; // 无扩展名，拒绝
    }

    std::string ext = filename.substr(dotPos);
    // 统一转小写进行大小写不敏感比较
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c){ return static_cast<char>(std::tolower(c)); });

    for (const char* allowed : ALLOWED_EXTENSIONS) {
        if (ext == allowed) {
            return true;
        }
    }
    return false;
}

/**
 * 检查路径是否包含目录遍历攻击特征
 *
 * 禁止：
 * - ".." 相对路径跳转
 * - 绝对路径（以 '/' 或 'C:' 等开头）
 *
 * @param path 请求路径
 * @return 安全返回 true
 */
static bool IsPathSafe(const std::string& path) {
    // 1. 禁止空路径
    if (path.empty()) {
        return false;
    }

    // 2. 禁止包含 ".." 的相对路径跳转
    if (path.find("..") != std::string::npos) {
        return false;
    }

    // 3. 禁止绝对路径（Windows 盘符或 Unix 根目录）
    #ifdef _WIN32
    if (path.length() >= 2 && path[1] == ':') {
        return false; // Windows 绝对路径，如 C:\...
    }
    #endif
    if (path[0] == '/' || path[0] == '\\') {
        return false; // Unix 绝对路径或 Windows 根路径
    }

    return true;
}

// -----------------------------------------------------------------------------
// 文件加载
// -----------------------------------------------------------------------------

/**
 * 从 assets 目录加载文件到内存
 *
 * @param relativePath 相对于 assets 根目录的文件路径
 * @param outData 输出缓冲区，文件内容将追加至此
 * @return 成功返回 true，失败返回 false
 *
 * 示例：
 *   std::vector<uint8_t> data;
 *   bool ok = LoadFile("songs/song_001/cover.png", data);
 */
bool LoadFile(const std::string& relativePath, std::vector<uint8_t>& outData) {
    // 1. 路径安全校验
    if (!IsPathSafe(relativePath)) {
        std::fprintf(stderr, "[AssetLoader] 拒绝不安全路径: %s\n", relativePath.c_str());
        return false;
    }

    // 2. 扩展名白名单校验
    if (!IsExtensionAllowed(relativePath)) {
        std::fprintf(stderr, "[AssetLoader] 拒绝非白名单扩展名: %s\n", relativePath.c_str());
        return false;
    }

    // 3. 构造完整路径
    std::string assetRoot = "./assets/";
#if defined(__ANDROID__)
    // Android: assets 被复制到内部存储的 files/ 目录下
    assetRoot = std::string(SDL_GetAndroidInternalStoragePath()) + "/";
#endif
    std::string fullPath = assetRoot + relativePath;

    // 4. 以二进制模式打开文件
    std::FILE* fp = std::fopen(fullPath.c_str(), "rb");
    if (!fp) {
        std::fprintf(stderr, "[AssetLoader] 无法打开文件: %s\n", fullPath.c_str());
        return false;
    }

    // 5. 获取文件大小
    if (std::fseek(fp, 0, SEEK_END) != 0) {
        std::fclose(fp);
        return false;
    }
    long fileSize = std::ftell(fp);
    if (fileSize < 0) {
        std::fclose(fp);
        return false;
    }
    if (static_cast<size_t>(fileSize) > MAX_FILE_SIZE) {
        std::fprintf(stderr, "[AssetLoader] 文件过大: %s (%ld bytes, 上限 %zu)\n",
                     fullPath.c_str(), fileSize, MAX_FILE_SIZE);
        std::fclose(fp);
        return false;
    }
    std::rewind(fp);

    // 6. 读取文件内容
    size_t currentSize = outData.size();
    outData.resize(currentSize + static_cast<size_t>(fileSize));
    size_t readBytes = std::fread(outData.data() + currentSize, 1, static_cast<size_t>(fileSize), fp);
    std::fclose(fp);

    if (readBytes != static_cast<size_t>(fileSize)) {
        std::fprintf(stderr, "[AssetLoader] 读取不完整: %s\n", fullPath.c_str());
        outData.resize(currentSize); // 回滚
        return false;
    }

    std::printf("[AssetLoader] 加载成功: %s (%zu bytes)\n", fullPath.c_str(), readBytes);
    return true;
}

/**
 * 获取 assets 目录下某文件的完整路径
 *
 * @param relativePath 相对路径
 * @return 完整路径字符串；若路径不安全则返回空字符串
 */
std::string GetAssetFullPath(const std::string& relativePath) {
    if (!IsPathSafe(relativePath) || !IsExtensionAllowed(relativePath)) {
        return "";
    }
#if defined(__ANDROID__)
    return std::string(SDL_GetAndroidInternalStoragePath()) + "/" + relativePath;
#else
    return "./assets/" + relativePath;
#endif
}
