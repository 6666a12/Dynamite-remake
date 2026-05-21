package com.TunerGames.Dynamit

import android.content.res.AssetManager
import android.os.Bundle
import org.libsdl.app.SDLActivity
import java.io.File
import java.io.IOException

/**
 * Dynamite 主 Activity
 *
 * 继承 SDLActivity，由 SDL3 管理 Native 层的窗口、输入和主循环。
 * C++ 入口位于 core/src/main.cpp，在 Android 上被重命名为 SDL_main()，
 * 编译为 libmain.so，由 SDLActivity 自动加载并调用。
 */
class MainActivity : SDLActivity() {

    override fun getLibraries(): Array<String> {
        // gamecore 包含 SDL3 + 引擎代码，main 是入口 so
        return arrayOf("gamecore", "main")
    }

    override fun getMainFunction(): String {
        return "SDL_main"
    }

    // 强制锁定横屏，覆盖 SDL 的动态方向计算
    override fun setOrientationBis(w: Int, h: Int, resizable: Boolean, hint: String) {
        requestedOrientation = android.content.pm.ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        // 在 SDL 启动前将 APK assets 复制到内部存储，供 C++ 标准文件 IO 访问
        copyAssetsToFiles("songs")
        copyAssetsToFiles("skins")
        copyAssetsToFiles("ui")
        copyAssetsToFiles("config")
        copyAssetsToFiles("fonts")
        super.onCreate(savedInstanceState)
    }

    /**
     * 递归复制 APK assets 到 /data/data/<package>/files/
     */
    private fun copyAssetsToFiles(srcPath: String) {
        val assetManager = assets
        val dstDir = File(filesDir, srcPath)
        try {
            val list = assetManager.list(srcPath)
            if (list.isNullOrEmpty()) {
                // srcPath 是文件
                copyAssetFile(assetManager, srcPath, dstDir)
            } else {
                // srcPath 是目录
                dstDir.mkdirs()
                for (name in list) {
                    copyAssetsToFiles("$srcPath/$name")
                }
            }
        } catch (e: IOException) {
            // 可能是文件（某些 Android 版本 list() 对文件返回 null 而非空数组）
            copyAssetFile(assetManager, srcPath, dstDir)
        }
    }

    private fun copyAssetFile(assetManager: AssetManager, srcPath: String, dstFile: File) {
        if (dstFile.exists()) return // 已存在则跳过，避免重复复制
        dstFile.parentFile?.mkdirs()
        try {
            assetManager.open(srcPath).use { input ->
                dstFile.outputStream().use { output ->
                    input.copyTo(output)
                }
            }
        } catch (e: IOException) {
            android.util.Log.e("Dynamite", "Failed to copy asset: $srcPath", e)
        }
    }
}
