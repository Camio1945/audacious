# Audacious [v4.6.3]

## 快速打包与运行（Windows MSYS2 UCRT64）

### 前置环境

| 组件 | 安装命令 |
|---|---|
| MSYS2 UCRT64 | 安装在 `F:\msys64` |
| Qt6 | `pacman -S mingw-w64-ucrt-x86_64-qt6 mingw-w64-ucrt-x86_64-qt6-tools` |
| 构建工具 | `pacman -S mingw-w64-ucrt-x86_64-meson mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-gcc` |
| NSIS（可选，做安装包） | `pacman -S mingw-w64-ucrt-x86_64-nsis` |
| 7-Zip（可选，查安装包内容） | 装到默认位置即可 |

仓库位置：
- 主仓库：`D:\git\miscellaneous\audacious`
- 插件仓库：`D:\git\miscellaneous\audacious-plugins`

---

### 方式 A：PowerShell（推荐，一台电脑里就能搞定）

打开 **PowerShell**，复制粘贴以下脚本即可。把 `$msysRoot` 和仓库路径按你电脑实际情况改。

> ⚠️ **致命警告：PATH 污染！** 如果你的系统 PATH 里有 MSVC 版库（如 `F:\gstreamer\1.0\msvc_x86_64\bin`），**绝对不能**用 `+ $env:Path` 前置——这会让 GCC linker 误找到 MSVC 版同名库（比如 `glib-2.0-0.dll` 不带 `lib` 前缀），导致产物 import 表写错 DLL 名，安装后崩溃。**必须覆盖 PATH 为干净的最小集**。

```powershell
# ===== 0. 设置变量 =====
$msysRoot   = "F:\msys64"                          # MSYS2 根目录
$msysUsrBin = "$msysRoot\usr\bin"                  # meson/ninja/pkg-config/bash 在这里
$msysBin    = "$msysRoot\ucrt64\bin"               # Qt6/g++/所有运行时 dll 在这里
$qtPlugins  = "$msysRoot\ucrt64\share\qt6\plugins" # Qt6 插件目录
$audRoot    = "D:\git\miscellaneous\audacious"
$plugRoot   = "D:\git\miscellaneous\audacious-plugins"
$prefix     = "$audRoot\release-qt"
$bin        = "$prefix\bin"

# ===== 1. 关键！用干净 PATH（覆盖，不追加）=====
# 排除系统 PATH 里的 gstreamer、GraphicsMagick 等可能污染 GCC linker 的东西
$env:Path = "$msysUsrBin;$msysBin;C:\Windows\System32;C:\Windows"
# 额外验证：确保 PATH 里没有 gstreamer
if ($env:Path -match "gstreamer") {
    Write-Host "❌ PATH 里还有 gstreamer！请手动检查！" -ForegroundColor Red
} else {
    Write-Host "✅ PATH 干净（无 gstreamer 等污染）"
}

# ===== 2. 构建主程序 =====
cd $audRoot
Remove-Item -Recurse -Force build, $prefix -ErrorAction SilentlyContinue

meson setup build -D gtk=false -D dbus=false -D prefix=$prefix
if ($LASTEXITCODE -ne 0) { Write-Host "❌ meson setup 失败"; exit 1 }
meson compile -C build
if ($LASTEXITCODE -ne 0) { Write-Host "❌ meson compile 失败"; exit 1 }
meson install -C build

# ===== 3. 构建插件 =====
$env:PKG_CONFIG_PATH = "$prefix\lib\pkgconfig"
cd $plugRoot
Remove-Item -Recurse -Force build -ErrorAction SilentlyContinue

meson setup build -D prefix=$prefix -D gtk=false -D gtkui=false -D skins=false
if ($LASTEXITCODE -ne 0) { Write-Host "❌ plugins meson setup 失败"; exit 1 }
meson compile -C build
meson install -C build

# ===== 4. 把所有运行时 dll 拷到 release-qt/bin =====
#    这一步是做便携版和安装包的关键！开发模式可以跳过（PATH 里有 $msysBin 即可），
#    但做安装包必须拷全——一共约 280 个 dll + Qt plugins
Write-Host ""
Write-Host "===== 打包运行时 ====="

# 4a. 所有 MSYS2 UCRT64 运行时 dll（约 280 个，包括 libstdc++-6、libgcc、libglib、libicu 等）
Write-Host "复制 ucrt64 运行时 dll..."
Get-ChildItem "$msysBin\*.dll" | Copy-Item -Destination $bin -Force

# 4b. Qt6 plugins（platforms/qwindows.dll 等 8 个目录）
Write-Host "复制 Qt6 plugins..."
Get-ChildItem $qtPlugins -Directory | ForEach-Object {
    Copy-Item $_.FullName $bin -Recurse -Force
}

Write-Host ""
Write-Host "release-qt\bin 现状:"
Write-Host "  dll 数量: $((Get-ChildItem "$bin\*.dll").Count)"
Write-Host "  总大小:   $([math]::Round((Get-ChildItem $prefix -Recurse | Measure-Object Length -Sum).Sum/1MB, 1)) MB"
Write-Host "  子目录:   $(Get-ChildItem $bin -Directory | Select-Object -ExpandProperty Name)"

# ===== 5. 验证构建 =====
Write-Host ""
Write-Host "===== 验证 ====="
cd $bin
$missing = (ldd .\audacious.exe 2>&1 | Select-String "not found").Count
Write-Host "ldd audacious.exe 缺失: $missing 个 $(if($missing -eq 0){'✅'}else{'❌'})"

# ===== 6. 纯 release-qt PATH 启动验证（确保没有隐式依赖 MSYS2）=====
Write-Host ""
Write-Host "===== 纯 release-qt PATH 启动测试 ====="
$savedPath = $env:Path
$env:Path = "$bin;C:\Windows\System32;C:\Windows"
$proc = Start-Process ".\audacious.exe" -PassThru
Start-Sleep -Seconds 5
$alive = Get-Process -Id $proc.Id -ErrorAction SilentlyContinue
if ($alive) {
    Write-Host "✅ 启动成功（纯 release-qt PATH，无 MSYS2）"
    Write-Host "   关闭 audacious 继续..."
    taskkill /F /IM audacious.exe 2>&1 | Out-Null
} else {
    Write-Host "❌ 启动失败！请检查上面 ldd 输出"
}
$env:Path = $savedPath   # 恢复，后面 makensis 需要

# ===== 7. 做安装包（可选）=====
Write-Host ""
Write-Host "===== 生成 NSIS 安装包 ====="
cd $audRoot
meson compile -C build 2>&1 | Select-Object -Last 1
& "$msysBin\makensis.exe" -V3 "build\win32\audacious.nsi"
$installer = "$audRoot\audacious-4.6.3-win32.exe"
if (Test-Path $installer) {
    Write-Host "✅ 安装包: $installer ($([math]::Round((Get-Item $installer).Length/1MB, 1)) MB)"
}

# ===== 8. 做便携版 zip（可选）=====
Write-Host ""
Write-Host "===== 生成便携版 zip ====="
Compress-Archive -Path "$prefix\*" -DestinationPath "$audRoot\audacious-4.6.3-portable.zip" -Force
Write-Host "✅ 便携版: $audRoot\audacious-4.6.3-portable.zip"
```

> 💡 **为什么要覆盖 PATH 而不是追加？** 你的系统 PATH 里很可能有 `F:\gstreamer\1.0\msvc_x86_64\bin`（MSVC 版的 glib/pcre2 等）。追加的话 GCC linker 在 `meson setup` 阶段会优先找到 MSVC 版（不带 `lib` 前缀），导致 audacious.exe 的 DLL 导入表写成 `glib-2.0-0.dll` 而不是正确的 `libglib-2.0-0.dll`。覆盖成干净 PATH 就不会有这个问题。

---

### 方式 B：MSYS2 UCRT64 终端（bash 风格）

打开 **MSYS2 UCRT64** 终端（图标通常叫「MSYS2 UCRT64」），执行：

```bash
# MSYS2 UCRT64 的 PATH 默认就是干净的（只包含 /usr/bin /ucrt64/bin /c/Windows）
# 不会受 Windows 系统 PATH 里 gstreamer 的影响
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

# 打包运行时（NSIS 安装包由 win32/audacious.nsi 自动处理这一步）
cp -u /ucrt64/bin/*.dll D:/git/miscellaneous/audacious/release-qt/bin/
cp -ru /ucrt64/share/qt6/plugins/* D:/git/miscellaneous/audacious/release-qt/bin/

# 验证
cd /d/git/miscellaneous/audacious/release-qt/bin
ldd audacious.exe | grep "not found"    # 应该输出 0 行
./audacious.exe

# 做安装包
cd /d/git/miscellaneous/audacious
makensis -V3 build/win32/audacious.nsi
# 产物: audacious-4.6.3-win32.exe
```

---

### 构建完成后你能拿到的产物

| 你想要的东西 | 在哪 | 大小 |
|---|---|---|
| **直接运行** | `release-qt\bin\audacious.exe` | — |
| **便携版文件夹** | 整个 `release-qt\` 目录 | ~520 MB |
| **NSIS 安装包** | `audacious-4.6.3-win32.exe` | ~116 MB（lzma 压缩） |
| **便携版 zip** | `audacious-4.6.3-portable.zip` | 比安装包稍大 |

> 📦 `release-qt\` 目录结构（自包含，拷到任何 Windows 电脑都能跑）：
> ```
> release-qt\
> ├── bin\
> │   ├── audacious.exe               ← 主程序
> │   ├── Qt6Core.dll, Qt6Gui.dll ...  ← 14 个 Qt6 dll（从 ucrt64/bin 拷来）
> │   ├── libstdc++-6.dll              ← GCC C++ 运行时
> │   ├── libgcc_s_seh-1.dll           ← GCC 运行时
> │   ├── libglib-2.0-0.dll            ← GLib（带 lib 前缀！区别于 MSVC 版）
> │   ├── libintl-8.dll, libiconv-2.dll ← 国际化
> │   ├── libicuin78.dll, libicuuc78.dll, libicudt78.dll ← ICU Unicode
> │   ├── libb2-1.dll, libdouble-conversion.dll, libpcre2-*.dll ...  ← 其他运行时
> │   ├── platforms\qwindows.dll       ← Qt6 平台插件（没有就是黑窗口）
> │   ├── imageformats\...             ← Qt6 图片格式插件
> │   ├── styles\...                   ← Qt6 样式
> │   └── tls\...                      ← Qt6 TLS 插件
> ├── lib\
> │   └── audacious\
> │       ├── General\qtui.dll        ← Qt UI 插件（没有就是黑窗口）
> │       ├── Input\*                  ← 解码器插件
> │       ├── Effect\*                 ← 音效插件
> │       └── ...
> ├── include\                         ← C 头文件（给第三方开发用）
> └── share\                           ← 图标、翻译、metainfo
> ```

---

### 制作 NSIS 安装包

前提：`pacman -S mingw-w64-ucrt-x86_64-nsis`

```powershell
# 方式 A 脚本里已包含，单独运行也可以：
$msysBin = "F:\msys64\ucrt64\bin"
$env:Path = "$msysBin;" + $env:Path   # 做安装包只需 makensis，不需要完整干净 PATH

cd D:\git\miscellaneous\audacious
meson compile -C build                # 生成 win32/audacious.nsi 并安装所有文件到 release-qt
& "$msysBin\makensis.exe" -V3 "build\win32\audacious.nsi"
# 输出在: D:\git\miscellaneous\audacious\build\win32\audacious-4.6.3-win32.exe
# makensis 会自动处理 OutFile 路径
```

NSIS 安装包特点（已配置好）：
- ✅ **自动检测旧版安装路径**（从注册表 `HKLM\...\Uninstall\Audacious\InstallLocation` 读取）
- ✅ **默认覆盖升级**，不重装到 C 盘
- ✅ **安装后写回注册表**，下次升级能继续找到
- ✅ **lzma 压缩**（22% 压缩率，520 MB → 116 MB）

---

### 常见问题

| 症状 | 根因 | 解决 |
|---|---|---|
| `meson setup` 报 `dependency 'audacious' not found` | `PKG_CONFIG_PATH` 没设，或主程序还没 `meson install` | 先装主程序再装插件，`$env:PKG_CONFIG_PATH = "$prefix\lib\pkgconfig"` |
| 运行直接崩/弹 System Error 找不到 dll | **PATH 污染导致编译产物 import 表写错** | 方式 A 脚本已覆盖 PATH，重跑 step 1 再从 step 2 开始 |
| `The code execution cannot proceed because glib-2.0-0.dll was not found` | GCC 误链接到了 MSVC 版 glib（带 gstreamer 前缀污染的 PATH） | **彻底清 PATH！** 用方式 A 脚本的 step 1，不要追加系统 PATH |
| `Entry Point Not Found _ZSt15__get_once_callv` | `libstdc++-6.dll` 没打包或者是错版本 | 确认 bin 里有 libstdc++-6.dll（从 ucrt64\bin 拷来的那个），不是系统目录里别的 |
| 标题栏只显示 `[audacious]` 没 UI | `qtui.dll` 没装上 | 确认 plugins 用了 `-D gtk=false -D gtkui=false -D skins=false`，且 `release-qt\lib\audacious\General\qtui.dll` 存在 |
| 黑窗口/`Qt.qpa.plugin: Could not load the Qt platform plugin "windows"` | `platforms\qwindows.dll` 没拷 | 重新跑 step 4b 拷 Qt6 plugins |
| plugins install 装到了 `c:/share` 之类的奇怪路径 | 漏了 `-D prefix=$prefix` | 重跑 step 3 |
| `ldd audacious.exe` 有 `not found` | step 4 没拷全运行时 | 重跑 step 4（拷 280+ ucrt64 dll + Qt plugins），不要只拷 Qt6*.dll |
| 便携版拷到别的电脑上不能跑 | 运行时 dll 没拷全，或者那台电脑没装 Visual C++ 运行库 | 确保拷了所有 ucrt64 dll；ucrt64 GCC 产物不依赖 MSVC 运行库，纯 dll 自包含即可 |

---

### 调试技巧

```powershell
# 1. 快速看 audacious.exe 的 DLL 导入表（确认 glib 有没有 lib 前缀）
$env:Path = "F:\msys64\usr\bin;F:\msys64\ucrt64\bin;" + $env:Path
objdump -p "release-qt\bin\audacious.exe" 2>&1 | Select-String "DLL Name"
# ✅ 正确: DLL Name: libglib-2.0-0.dll
# ❌ 错误: DLL Name: glib-2.0-0.dll     ← 说明 PATH 污染了，重编译！

# 2. 看哪个 dll 依赖缺失
ldd "release-qt\bin\audacious.exe" 2>&1 | Select-String "not found"

# 3. 纯 release-qt PATH 启动（模拟用户安装后的环境）
$env:Path = "D:\git\miscellaneous\audacious\release-qt\bin;C:\Windows\System32;C:\Windows"
.\release-qt\bin\audacious.exe

# 4. 用 7z 查 NSIS 安装包内容
& "C:\Program Files\7-Zip\7z.exe" l "audacious-4.6.3-win32.exe" | Select-String "glib|libstdc|platforms|qwindows"

# 5. PowerShell 脚本一步步跑，不要跳过任何一步
```
