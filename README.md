# Audacious [v4.6.2]

## 快速打包与运行（Windows MSYS2 UCRT64）

### 前置环境

| 组件 | 安装命令 |
|---|---|
| MSYS2 UCRT64 | 安装在 `F:\msys64` |
| Qt6 | `pacman -S mingw-w64-ucrt-x86_64-qt6 mingw-w64-ucrt-x86_64-qt6-tools` |
| 构建工具 | `pacman -S mingw-w64-ucrt-x86_64-meson mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-gcc` |
| NSIS（可选，做安装包） | `pacman -S mingw-w64-ucrt-x86_64-nsis` |

仓库位置：
- 主仓库：`D:\git\miscellaneous\audacious`
- 插件仓库：`D:\git\miscellaneous\audacious-plugins`

---

### 方式 A：PowerShell（推荐，一台电脑里就能搞定）

打开 **PowerShell**，复制粘贴以下脚本即可。注意 `$msysBin` 和路径按你电脑实际情况改。

```powershell
# ===== 1. 设置环境（每次开新 PowerShell 都要跑一次）=====
$msysBin   = "F:\msys64\ucrt64\bin"
$msysUsrBin = "F:\msys64\usr\bin"
$audRoot   = "D:\git\miscellaneous\audacious"
$plugRoot  = "D:\git\miscellaneous\audacious-plugins"
$prefix    = "$audRoot\release-qt"

$env:Path = "$msysUsrBin;$msysBin;" + $env:Path

# ===== 2. 构建主程序 =====
cd $audRoot
Remove-Item -Recurse -Force build, $prefix -ErrorAction SilentlyContinue

meson setup build -D gtk=false -D dbus=false -D prefix=$prefix
meson compile -C build
meson install -C build

# ===== 3. 构建插件 =====
$env:PKG_CONFIG_PATH = "$prefix\lib\pkgconfig"
cd $plugRoot
Remove-Item -Recurse -Force build -ErrorAction SilentlyContinue

# plugins 不需要手动指定 audacious_libdir/includedir —— 用 PKG_CONFIG_PATH 自动查找
# 关掉 GTK/GtkUI/Skins，只留 Qt UI
meson setup build -D prefix=$prefix -D gtk=false -D gtkui=false -D skins=false
meson compile -C build
meson install -C build

# ===== 4. 把运行所需的 Qt6 和 MSYS2 dll 拷到 release-qt/bin =====
# (不拷也能跑——只要启动时 PATH 里有 $msysBin)
$bin = "$prefix\bin"
Get-ChildItem "$msysBin\Qt6*.dll" | Copy-Item -Destination $bin -Force

# ===== 5. 运行 =====
cd $bin
.\audacious.exe
```

> 💡 **为什么要设 `$msysUsrBin` + `$msysBin` 两个？** MSYS2 的 `meson`/`ninja`/`pkg-config` 在 `usr\bin`，Qt6/g++ 在 `ucrt64\bin`。

---

### 方式 B：MSYS2 UCRT64 终端（bash 风格）

打开 **MSYS2 UCRT64** 终端（图标通常叫「MSYS2 UCRT64」），执行：

```bash
cd /d/git/miscellaneous/audacious
rm -rf build release-qt
meson setup build -D gtk=false -D dbus=false -D prefix="D:/git/miscellaneous/audacious/release-qt"
meson compile -C build
meson install -C build

export PKG_CONFIG_PATH="D:/git/miscellaneous/audacious/release-qt/lib/pkgconfig"
cd /d/git/miscellaneous/audacious-plugins
rm -rf build
meson setup build -D prefix="D:/git/miscellaneous/audacious/release-qt" \
  -D gtk=false -D gtkui=false -D skins=false
meson compile -C build
meson install -C build

cd /d/git/miscellaneous/audacious/release-qt/bin
./audacious.exe
```

---

### 制作 NSIS 安装包

```powershell
# PowerShell（需要 pacman -S mingw-w64-ucrt-x86_64-nsis）
cd D:\git\miscellaneous\audacious
meson compile -C build   # 先生成 win32/audacious.nsi
& "$msysBin\makensis.exe" -V3 build\win32\audacious.nsi
# 输出: audacious-4.6.2-win32.exe
```

### 制作便携版 zip

```powershell
Compress-Archive -Path "$prefix\*" -DestinationPath "audacious-4.6.2-portable.zip"
```

---

### 常见问题

| 症状 | 解决 |
|---|---|
| `meson setup` 报 `dependency 'audacious' not found` | `PKG_CONFIG_PATH` 没设，或主程序还没 `meson install` |
| 运行直接崩/报缺少 dll | bin 里缺 Qt6 dll。要么把 `$msysBin` 加 PATH，要么 `Copy-Item "$msysBin\Qt6*.dll" $prefix\bin` |
| 标题栏只显示 `[audacious]` 没 UI | `qtui.dll` 没装上。确认 plugins 用了 `-D gtk=false -D gtkui=false -D skins=false`（qt 和 qtui 默认 true），且装到了 `$prefix\lib\audacious\General\qtui.dll` |
| plugins install 装到了 `c:/share` 之类的奇怪路径 | 漏了 `-D prefix=$prefix` |

---
