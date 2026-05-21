cmake_minimum_required(VERSION 3.25)

# Android NDK 自动设置 ANDROID 变量
# 此处引入 Dynamite GameCore，它会同时构建 SDL3

# 设置 gamecore 为 SHARED，桌面 exe 关闭
set(BUILD_STATIC OFF CACHE BOOL "" FORCE)
set(BUILD_DESKTOP_EXE OFF CACHE BOOL "" FORCE)

# 使用绝对路径引用 core，避免 CMake add_subdirectory 的目录限制
get_filename_component(CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../core" ABSOLUTE)
add_subdirectory(${CORE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/gamecore)

# Android 入口共享库：SDLActivity 会加载 "main" 并调用 SDL_main
add_library(main SHARED)

target_sources(main PRIVATE
    ${CORE_DIR}/src/main.cpp
)

target_link_libraries(main PRIVATE
    gamecore
    SDL3::SDL3
)

target_compile_features(main PRIVATE cxx_std_17)
