#!/usr/bin/env python3
"""
Dynamite 桌面开发构建脚本（备用方案）

用途：
1. 绕过 MSYS2 UCRT64 g++ 15.2.0 collect2 链接崩溃问题
2. 直接控制编译和链接过程

已知限制（Windows桌面）：
- Windows 的 opengl32.dll 仅提供 OpenGL 1.1 函数
- 代码中使用的 OpenGL ES 3.0 函数（glBindVertexArray, glUseProgram 等）
  在 Windows 桌面需要通过 GLAD/GL3W 运行时加载，或安装 ANGLE
- 真机 Android/iOS 无此问题（系统原生支持 OpenGL ES 3.0）

解决方案：
- 方案A：在 Android Studio / Xcode 中直接构建真机版本（推荐）
- 方案B：安装 ANGLE 或 Mesa3D 的 OpenGL ES 模拟层
- 方案C：集成 GLAD 单头文件加载器（需修改 render_batch.cpp/gl_context.cpp）

使用方法:
    cd dynamite-rebuild/core
    python ../tools/build_desktop.py
    ./build/dynamite_desktop.exe
"""

import os
import sys
import subprocess
import glob
import shutil

# =============================================================================
# 配置
# =============================================================================
CORE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD_DIR = os.path.join(CORE_DIR, "build")
SRC_DIR = os.path.join(CORE_DIR, "src")
THIRD_PARTY = os.path.join(CORE_DIR, "third_party")

# 编译器
CXX = "g++"
CXXFLAGS = [
    "-std=c++17",
    "-O2",
    "-DNDEBUG",
    "-I" + SRC_DIR,
    "-I" + os.path.join(THIRD_PARTY, "miniaudio"),
    "-I" + os.path.join(THIRD_PARTY, "glm"),
    "-I" + os.path.join(THIRD_PARTY, "json", "single_include"),
    "-I" + os.path.join(THIRD_PARTY, "stb"),
    "-I" + os.path.join(THIRD_PARTY, "SDL3", "include"),
    "-Wall",
    "-Wextra",
    "-Wno-unused-parameter",
]

# 链接库（Windows MSYS2 UCRT64）
LD = "ld"
LDFLAGS = [
    "-m", "i386pep",  # 64-bit PE
    "-Bdynamic",
    "--subsystem", "console",  # 使用控制台子系统（需要 main() 而非 WinMain）
]

# 库路径
LIB_DIRS = [
    "-LE:/msys2/ucrt64/lib",
    "-LE:/msys2/ucrt64/bin/../lib/gcc/x86_64-w64-mingw32/15.2.0",
]

# 链接的库（顺序很重要：被依赖的在前）
LIBS = [
    "-lSDL3",
    "-lopengl32",
    "-lwinmm",
    "-limm32",
    "-lversion",
    "-lsetupapi",
    "-lgdi32",
    "-luser32",
    "-lshell32",
    "-lole32",
    "-loleaut32",
    "-lws2_32",
    "-lkernel32",
    "-ladvapi32",
    "-lstdc++",
    "-lmingw32",
    "-lgcc_eh",
    "-lgcc",
    "-lmingwex",
    "-lmsvcrt",
    "-lkernel32",
    "-lpthread",
    "-ladvapi32",
    "-lshell32",
    "-luser32",
    "-lkernel32",
    "-lmingw32",
    "-lgcc_eh",
    "-lgcc",
    "-lmingwex",
    "-lmsvcrt",
    "-lkernel32",
]

# 启动对象和默认manifest
CRT_DIR = "E:/msys2/ucrt64/bin/../lib/gcc/x86_64-w64-mingw32/15.2.0"
LIB_DIR = "E:/msys2/ucrt64/bin/../lib/gcc/x86_64-w64-mingw32/15.2.0/../../../../lib"
START_OBJS = [
    os.path.join(LIB_DIR, "crt2.o"),
    os.path.join(CRT_DIR, "crtbegin.o"),
]
END_OBJS = [
    os.path.join(LIB_DIR, "default-manifest.o"),
    os.path.join(CRT_DIR, "crtend.o"),
]

# 源文件（排除移动端桥接）
SOURCE_PATTERNS = [
    "src/engine/*.cpp",
    "src/utils/*.cpp",
    "src/platform/*.cpp",
    "src/scenes/*.cpp",
    "src/bridge/go_bridge_desktop.cpp",
    "src/main.cpp",
]


def find_sources():
    """收集所有需要编译的源文件"""
    sources = []
    for pattern in SOURCE_PATTERNS:
        full_pattern = os.path.join(CORE_DIR, pattern)
        for f in glob.glob(full_pattern):
            sources.append(os.path.abspath(f))
    return sorted(set(sources))


def compile_file(src_path, obj_path):
    """编译单个源文件为对象文件"""
    cmd = [CXX] + CXXFLAGS + ["-c", src_path, "-o", obj_path]
    print(f"[CC] {os.path.relpath(src_path, CORE_DIR)}")
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"编译失败: {src_path}")
        print(result.stderr)
        return False
    return True


def link_executable(obj_files, exe_path):
    """使用 ld 链接可执行文件"""
    cmd = [LD] + LDFLAGS + ["-o", exe_path] + START_OBJS + obj_files + LIB_DIRS + LIBS + END_OBJS
    print(f"[LD] {os.path.relpath(exe_path, CORE_DIR)}")
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"链接失败:")
        print(result.stderr)
        return False
    return True


def copy_assets():
    """复制资源到构建目录"""
    src_assets = os.path.join(CORE_DIR, "..", "assets")
    dst_assets = os.path.join(BUILD_DIR, "assets")
    if os.path.exists(src_assets):
        if os.path.exists(dst_assets):
            shutil.rmtree(dst_assets)
        shutil.copytree(src_assets, dst_assets)
        print(f"[ASSETS] 已复制到 {dst_assets}")


def copy_sdl3_dll():
    """复制 SDL3.dll 到构建目录"""
    sdl3_paths = [
        "E:/msys2/ucrt64/bin/SDL3.dll",
        "C:/msys64/ucrt64/bin/SDL3.dll",
        "C:/mingw64/bin/SDL3.dll",
    ]
    for dll in sdl3_paths:
        if os.path.exists(dll):
            dst = os.path.join(BUILD_DIR, "SDL3.dll")
            shutil.copy2(dll, dst)
            print(f"[DLL] 已复制 SDL3.dll")
            return True
    print("[WARN] 未找到 SDL3.dll，运行时需要手动配置 PATH")
    return False


def main():
    os.makedirs(BUILD_DIR, exist_ok=True)

    sources = find_sources()
    print(f"发现 {len(sources)} 个源文件")

    # 编译阶段
    obj_files = []
    for src in sources:
        rel = os.path.relpath(src, CORE_DIR)
        obj_name = rel.replace(os.sep, "_").replace(".", "_") + ".obj"
        obj_path = os.path.join(BUILD_DIR, obj_name)

        # 检查是否需要重新编译
        if os.path.exists(obj_path) and os.path.getmtime(obj_path) >= os.path.getmtime(src):
            pass  # 跳过
        else:
            if not compile_file(src, obj_path):
                sys.exit(1)
        obj_files.append(obj_path)

    # 链接阶段
    exe_path = os.path.join(BUILD_DIR, "dynamite_desktop.exe")
    if not link_executable(obj_files, exe_path):
        sys.exit(1)

    # 复制资源
    copy_assets()
    copy_sdl3_dll()

    print(f"\n[OK] 构建成功: {exe_path}")
    print(f"运行方式: cd {BUILD_DIR} && ./dynamite_desktop.exe")


if __name__ == "__main__":
    main()
