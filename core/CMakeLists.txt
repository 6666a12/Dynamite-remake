cmake_minimum_required(VERSION 3.25)
project(GameCore CXX C)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# =============================================================================
# 平台检测
# =============================================================================
if(ANDROID)
    set(PLATFORM_ANDROID TRUE)
elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(PLATFORM_IOS TRUE)
else()
    set(PLATFORM_DESKTOP TRUE)
endif()

# =============================================================================
# 构建选项
# =============================================================================
if(ANDROID)
    # Android 必须构建 SHARED 库供 JNI 加载
    set(BUILD_STATIC OFF CACHE BOOL "" FORCE)
    set(BUILD_DESKTOP_EXE OFF CACHE BOOL "" FORCE)
    # Android 上 SDL3 必须是 SHARED（SDLActivity 加载 libSDL3.so）
    set(SDL_SHARED ON CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
else()
    option(BUILD_STATIC "构建静态链接库/可执行文件" ON)
    option(BUILD_DESKTOP_EXE "构建桌面可执行文件（开发调试用）" ON)
endif()

# =============================================================================
# SDL3 配置（优先静态链接）
# =============================================================================
set(SDL_SHARED OFF CACHE BOOL "Build SDL as shared library" FORCE)
set(SDL_STATIC ON CACHE BOOL "Build SDL as static library" FORCE)
set(SDL_TESTS OFF CACHE BOOL "Build SDL tests" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "Build SDL test library" FORCE)

# 禁用不需要的 SDL 子系统，减少体积
set(SDL_DUMMYAUDIO OFF CACHE BOOL "" FORCE)
set(SDL_DUMMYVIDEO OFF CACHE BOOL "" FORCE)
set(SDL_DISKAUDIO OFF CACHE BOOL "" FORCE)
set(SDL_DIRECTX OFF CACHE BOOL "" FORCE)

find_package(SDL3 QUIET)
if(NOT SDL3_FOUND)
    message(STATUS "SDL3 not found via find_package, using submodule...")
    add_subdirectory(third_party/SDL3 EXCLUDE_FROM_ALL)
    set(SDL3_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL3/include")
    if(ANDROID)
        set(SDL3_LIBRARIES SDL3::SDL3)
    else()
        set(SDL3_LIBRARIES SDL3::SDL3-static)
    endif()
else()
    # 如果系统有SDL3，也优先使用静态版本
    if(TARGET SDL3::SDL3-static)
        set(SDL3_LIBRARIES SDL3::SDL3-static)
    else()
        set(SDL3_LIBRARIES SDL3::SDL3)
    endif()
endif()

# =============================================================================
# 源文件收集
# =============================================================================
file(GLOB_RECURSE ENGINE_SOURCES 
    src/engine/*.cpp 
    src/engine/*.c
    src/utils/*.cpp 
    src/utils/*.c
    src/platform/*.cpp 
    src/platform/*.c
    src/bridge/*.cpp 
    src/bridge/*.c
    src/scenes/*.cpp 
    src/scenes/*.c
)

# 新 gameplay 渲染架构（v1.1 规范）
file(GLOB GAMEPLAY_SOURCES
    src/gameplay/*.cpp
) 
list(APPEND ENGINE_SOURCES ${GAMEPLAY_SOURCES})

# 排除旧版 scene_gameplay.cpp（ODR 冲突避免）
list(FILTER ENGINE_SOURCES EXCLUDE REGEX "scene_gameplay\\.cpp$")

# 平台桥接文件选择：只保留当前平台的实现
if(ANDROID)
    list(FILTER ENGINE_SOURCES EXCLUDE REGEX "go_bridge_desktop\\.cpp$")
    list(FILTER ENGINE_SOURCES EXCLUDE REGEX "go_bridge_ios\\.mm$")
elseif(PLATFORM_IOS)
    list(FILTER ENGINE_SOURCES EXCLUDE REGEX "go_bridge_desktop\\.cpp$")
    list(FILTER ENGINE_SOURCES EXCLUDE REGEX "go_bridge_android\\.cpp$")
else()
    # Desktop: 使用 mock 桌面实现
    list(FILTER ENGINE_SOURCES EXCLUDE REGEX "go_bridge_android\\.cpp$")
    list(FILTER ENGINE_SOURCES EXCLUDE REGEX "go_bridge_ios\\.mm$")
endif()

# =============================================================================
# 核心库目标
# =============================================================================
if(BUILD_STATIC)
    add_library(gamecore STATIC ${ENGINE_SOURCES})
    message(STATUS "Building gamecore as STATIC library")
else()
    add_library(gamecore SHARED ${ENGINE_SOURCES})
    message(STATUS "Building gamecore as SHARED library")
endif()

target_include_directories(gamecore PUBLIC
    src
    third_party/miniaudio
    third_party/glm
    third_party/json/single_include
    third_party/stb
    third_party
    ${SDL3_INCLUDE_DIRS}
)

# =============================================================================
# 平台链接配置
# =============================================================================
if(PLATFORM_ANDROID)
    target_link_libraries(gamecore PUBLIC
        ${SDL3_LIBRARIES}
        GLESv3
        android
        log
        OpenSLES
        # aaudio 通过 miniaudio 在运行时动态加载 (dlopen)，
        # 无需显式链接。保持 minSdk=24 兼容 Android 7.0。
    )
elseif(PLATFORM_IOS)
    target_link_libraries(gamecore PUBLIC
        ${SDL3_LIBRARIES}
        "-framework OpenGLES"
        "-framework UIKit"
        "-framework Foundation"
        "-framework AudioToolbox"
        "-framework CoreGraphics"
        "-framework QuartzCore"
    )
else()
    # 桌面开发配置
    find_package(OpenGL REQUIRED)
    
    # 桌面环境：优先使用系统已安装的动态链接 SDL3
    # MSYS2的静态libSDL3.a在某些版本下会导致链接器崩溃，使用动态链接更稳定
    if(WIN32)
        message(STATUS "Using dynamic SDL3 for Windows desktop development")
        set(SDL3_DESKTOP_LIBS SDL3)
    else()
        set(SDL3_DESKTOP_LIBS ${SDL3_LIBRARIES})
    endif()
    
    if(WIN32)
        target_link_libraries(gamecore PUBLIC
            ${SDL3_DESKTOP_LIBS}
            OpenGL::GL
            winmm
            imm32
            version
            setupapi
            gdi32
            user32
            shell32
            ole32
            oleaut32
            ws2_32
        )
        # 桌面动态链接需要确保运行时能找到 SDL3.dll
        # 尝试常见路径：MSYS2 UCRT64 / MINGW64
        set(SDL3_DLL_PATHS
            "E:/msys2/ucrt64/bin/SDL3.dll"
            "C:/msys64/ucrt64/bin/SDL3.dll"
            "C:/mingw64/bin/SDL3.dll"
        )
        foreach(dll_path ${SDL3_DLL_PATHS})
            if(TARGET dynamite_desktop AND EXISTS "${dll_path}")
                add_custom_command(TARGET dynamite_desktop POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${dll_path}"
                    $<TARGET_FILE_DIR:dynamite_desktop>
                )
                message(STATUS "Will copy SDL3.dll from: ${dll_path}")
                break()
            endif()
        endforeach()
    else()
        target_link_libraries(gamecore PUBLIC
            ${SDL3_DESKTOP_LIBS}
            OpenGL::GL
            dl
            pthread
        )
    endif()
endif()

# =============================================================================
# 桌面可执行文件（开发调试入口）
# =============================================================================
if(PLATFORM_DESKTOP AND BUILD_DESKTOP_EXE)
    add_executable(dynamite_desktop src/main.cpp)
    target_link_libraries(dynamite_desktop PRIVATE gamecore)
    
    # Windows 桌面调试保留控制台窗口，便于查看日志输出
    # 正式发布时可设置为 WIN32_EXECUTABLE TRUE
    # if(WIN32)
    #     set_target_properties(dynamite_desktop PROPERTIES WIN32_EXECUTABLE TRUE)
    # endif()
    
    # 复制资源到构建目录
    add_custom_command(TARGET dynamite_desktop POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/../assets
        $<TARGET_FILE_DIR:dynamite_desktop>/assets
        COMMENT "Copying assets to build directory"
    )
    
    message(STATUS "Desktop executable 'dynamite_desktop' will be built")
endif()

# =============================================================================
# 单元测试可执行文件
# =============================================================================
if(PLATFORM_DESKTOP)
    add_executable(test_core src/test_core.cpp)
    target_link_libraries(test_core PRIVATE gamecore)
    
    # 复制资源到测试可执行文件目录
    add_custom_command(TARGET test_core POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/../assets
        $<TARGET_FILE_DIR:test_core>/assets
        COMMENT "Copying assets to test directory"
    )
    
    message(STATUS "Test executable 'test_core' will be built")
endif()

# =============================================================================
# 编译器选项
# =============================================================================
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(gamecore PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wno-unused-parameter
    )
    
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_compile_options(gamecore PRIVATE -g -O0)
        # AddressSanitizer 可选
        # target_compile_options(gamecore PRIVATE -fsanitize=address)
        # target_link_options(gamecore PRIVATE -fsanitize=address)
    else()
        target_compile_options(gamecore PRIVATE -O3)
    endif()
elseif(MSVC)
    target_compile_options(gamecore PRIVATE /W4 /permissive-)
endif()

# =============================================================================
# 安装规则
# =============================================================================
install(TARGETS gamecore
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY src/
    DESTINATION include/gamecore
    FILES_MATCHING PATTERN "*.h"
)
