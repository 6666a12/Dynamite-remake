with open(r"C:\Users\fzm12\Desktop\Dynamite app\dynamite-remake\core\src\main.cpp", "r", encoding="utf-8") as f:
    c = f.read()

# Replace SDL_GL_SetSwapInterval(0) with ConfigManager-based VSync
old = '    SDL_GL_SetSwapInterval(0); // Android 涓?VSync 鍙兘寮傚父闃诲锛屽叧闂悗鎵嬪姩闄愬抚'
# Let me find the actual content
idx = c.find("SDL_GL_SetSwapInterval")
print("Found at", idx, ":", repr(c[idx:idx+120]))
