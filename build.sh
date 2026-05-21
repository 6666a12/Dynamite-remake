#!/usr/bin/env bash
# Dynamite Rebuild - 跨平台构建脚本
# 支持 Windows (Git Bash/MSYS2)、Linux、macOS

set -euo pipefail

# 获取脚本所在目录作为项目根目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${SCRIPT_DIR}"

echo "========================================"
echo "Dynamite Rebuild - Build Script"
echo "========================================"

# ------------------------------------------------------------------
# 1. 标准化资源
# ------------------------------------------------------------------
echo ""
echo "[Step 1] Normalizing assets..."
if [[ -d "${SCRIPT_DIR}/../apk_extracted/assets" ]]; then
    INPUT_DIR="${SCRIPT_DIR}/../apk_extracted/assets"
elif [[ -d "${SCRIPT_DIR}/apk_extracted/assets" ]]; then
    INPUT_DIR="${SCRIPT_DIR}/apk_extracted/assets"
else
    echo "[warn] apk_extracted/assets not found, skipping normalization."
    INPUT_DIR=""
fi

if [[ -n "${INPUT_DIR}" ]]; then
    python3 "${SCRIPT_DIR}/tools/normalize.py" \
        --input "${INPUT_DIR}" \
        --output "${SCRIPT_DIR}/assets/songs"
    echo "[Step 1] Done."
else
    echo "[Step 1] Skipped."
fi

# ------------------------------------------------------------------
# 2. 构建 C++ Core（Desktop 模式）
# ------------------------------------------------------------------
echo ""
echo "[Step 2] Building C++ Core (Desktop)..."
if command -v cmake &> /dev/null; then
    mkdir -p "${SCRIPT_DIR}/core/build_desktop"
    cd "${SCRIPT_DIR}/core/build_desktop"
    cmake .. \
        -DCMAKE_BUILD_TYPE=Release \
        -DPLATFORM_DESKTOP=ON
    cmake --build . --parallel "$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"
    echo "[Step 2] C++ Core built successfully."
else
    echo "[warn] cmake not found, skipping C++ Core build."
fi
cd "${SCRIPT_DIR}"

# ------------------------------------------------------------------
# 3. 构建 Go DataLayer（gomobile bind stub）
# ------------------------------------------------------------------
echo ""
echo "[Step 3] Building Go DataLayer..."
if command -v go &> /dev/null && command -v gomobile &> /dev/null; then
    cd "${SCRIPT_DIR}/server"
    if [[ "$OSTYPE" == "darwin"* ]]; then
        echo "[info] Building iOS Framework..."
        gomobile bind -target=ios -iosversion=13.0 \
            -o "${SCRIPT_DIR}/ios/Frameworks/GoData.framework" \
            ./mobile
    elif [[ "$OSTYPE" == "linux-android"* || "$OSTYPE" == "msys" || "$OSTYPE" == "cygwin" ]]; then
        echo "[info] Building Android AAR..."
        gomobile bind -target=android -androidapi=26 \
            -o "${SCRIPT_DIR}/android/app/libs/go_data.aar" \
            ./mobile
    else
        echo "[info] Desktop/unknown platform - gomobile bind skipped."
        echo "       To build manually:"
        echo "         cd server && gomobile bind -target=android ./mobile"
        echo "         cd server && gomobile bind -target=ios ./mobile"
    fi
    cd "${SCRIPT_DIR}"
    echo "[Step 3] Done."
else
    echo "[warn] go or gomobile not found."
    echo "       Install gomobile: go install golang.org/x/mobile/cmd/gomobile@latest"
    echo "       Then run: gomobile init"
fi

# ------------------------------------------------------------------
# 4. 运行桌面测试
# ------------------------------------------------------------------
echo ""
echo "[Step 4] Running desktop tests..."
TEST_BIN="${SCRIPT_DIR}/core/build_desktop/test_runner"
if [[ -x "${TEST_BIN}" ]]; then
    "${TEST_BIN}"
elif [[ -x "${SCRIPT_DIR}/core/build_desktop/GameCoreTest" ]]; then
    "${SCRIPT_DIR}/core/build_desktop/GameCoreTest"
else
    echo "[warn] Test binary not found. Skipping tests."
    echo "       Make sure your CMake builds a test target."
fi

echo ""
echo "========================================"
echo "Build finished."
echo "========================================"
