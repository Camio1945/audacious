# Audacious [v4.6.2]

## 快速打包与运行（Windows MSYS2 UCRT64）

### 前置环境

- **MSYS2 UCRT64**（安装在 `F:\msys64`，PATH 包含 `F:\msys64\usr\bin;F:\msys64\ucrt64\bin`）
- Qt6：`pacman -S mingw-w64-ucrt-x86_64-qt6 mingw-w64-ucrt-x86_64-qt6-tools`
- 构建工具：`pacman -S mingw-w64-ucrt-x86_64-meson mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-gcc`
- NSIS（制作安装包）：`pacman -S mingw-w64-ucrt-x86_64-nsis`
- 主仓库：`D:\git\miscellaneous\audacious`
- 插件仓库：`D:\git\miscellaneous\audacious-plugins`

### 构建（MSYS2 UCRT64 终端）

```bash
# 1. 主程序
cd /d/git/miscellaneous/audacious
rm -rf build release-qt
meson setup build -D gtk=false -D dbus=false -D prefix="D:/git/miscellaneous/audacious/release-qt"
meson compile -C build
meson install -C build

# 2. 插件（audacious-plugins 仓库）
cd /d/git/miscellaneous/audacious-plugins
rm -rf build
meson setup build -D gtk=false -D dbus=false \
  -D audacious_libdir="D:/git/miscellaneous/audacious/release-qt/lib" \
  -D audacious_includedir="D:/git/miscellaneous/audacious/release-qt/include" \
  -D qtui=true
meson compile -C build
meson install -C build
```

### 直接运行

```
D:\git\miscellaneous\audacious\release-qt\bin\audacious.exe
```

### 制作 NSIS 安装包

```bash
# MSYS2 UCRT64 终端
cd /d/git/miscellaneous/audacious
meson compile -C build   # 先生成 win32/audacious.nsi
makensis -V3 build/win32/audacious.nsi
# 输出: audacious-4.6.2-win32.exe
```

### 便携版 zip

```powershell
# PowerShell
Compress-Archive -Path "D:\git\miscellaneous\audacious\release-qt\*" `
    -DestinationPath "D:\git\miscellaneous\audacious\audacious-4.6.2-portable.zip"
```

---
