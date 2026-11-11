<div align=center>
  <img src="assets/branding/AppIcon.png" width=130 />
<h1>Penumbra<br />沉浸式桌面</h1>

**为你的桌面添加暗角与模糊，把专注交给前台窗口**
<div>
  <img src="https://github.com/user-attachments/assets/fa8d5b00-ac71-4e28-a2b2-a290efa95ec7" width=600/>
</div>
专注 · 自然 · 聚焦

**[下载最新版 ↗](https://github.com/Coldtea233/Penumbra/releases)  ·  [反馈问题](https://github.com/Coldtea233/Penumbra/issues)**

<div align=left>

- **优秀的性能**：约30MB内存占用 0.02% CPU占用
- **仅在必要时生效**：仅在前台存在窗口时启用 暗角/模糊 效果（可开关）
- **非线性动画**：暗角切入/切出动画使用720 毫秒强缓出曲线
- **原生UI**：设置界面采用Windows原生WinUI3设计语言

## 效果预览

大家可以使用我的参数喵，模糊半径3px，饱和度60%，四个暗角参数依次为60、50、100、100喵
<p align="center">
  <img width="2880" height="1620" alt="image" src="https://github.com/user-attachments/assets/6636dff0-8c32-4dd5-a07a-c3026a6b61ca" />

</p>

## 性能报告

- **测试平台**：CPU:i5-12500H (2.50 GHz) / GPU:Intel Iris Xe Graphics/ RAM：16GB / 2880×1620、120 Hz
- **性能表现**: 后台静止：整机 CPU 约 0.02%，工作集约 28.5 MiB，私有内存约 1.6 MiB。
- 连续 24 次动画：整机 CPU 平均约 0.24%，平均帧提交耗时 0.46 毫秒
- WinUI 设置窗口另占约 119 MiB 私有内存，关闭界面即可释放。

后台不加载 .NET、WinUI 或 WebView2；开启模糊后使用系统 Composition 图形资源。静止时无应用侧逐帧绘制、持续壁纸抓取或忙循环。
参数、屏幕布局或图标遮罩变化时生成完整分辨率的预乘 BGRA 位图，通过 `UpdateLayeredWindow` 提交后释放临时 CPU 位图和 GDI 资源。
图标遮罩常驻缓存每像素 1 位；2880×1620 约 0.56 MiB。读取图标和提交表面时有临时位图成本。
动画期间不分配全屏 CPU 帧或纹理，不逐帧缩放与重新上传位图；只更改已提交表面的常量透明度。系统表面在隐藏时仍缓存，代价是系统合成内存不降到销毁窗口时的水平；退出、暂停、恢复桌面和重建时释放。
前景、最小化及桌面层级事件触发合并后的层级调整；每 10 秒执行一次恢复检查，静止时不重绘。其他应用的普通对象事件会被过滤。
Windows 合成器另持有每屏透明表面；3840×2160 的 32 位表面约 31.6 MiB，不能用后台私有内存代表全部成本。
发行包含自包含 UI 运行库，磁盘大小与常驻内存是两回事。关闭设置窗口可释放 UI 进程。

## 构建

需要 .NET 8 SDK、可访问 NuGet 的网络、Windows x64 和 llvm-mingw UCRT x64 工具链。本次使用 20261020 版 llvm-mingw。

```powershell
./build.ps1 -Toolchain 'D:\Tools\llvm-mingw-20261020-ucrt-x86_64'
# 生成内部 Windows Installer 载荷，再打包为对外发布的 EXE
./installer/build-installer.ps1 -Toolchain 'D:\Tools\llvm-mingw-20261020-ucrt-x86_64'
./installer/build-exe-installer.ps1 -Toolchain 'D:\Tools\llvm-mingw-20261020-ucrt-x86_64' -PayloadPath './dist/release-v1.8.1/Penumbra-v1.8.1-Setup-x64.msi' -OutputDirectory './dist/release-v1.8.1' -IconPath './assets/branding/AppIcon.ico'
# 原生动画、图标保护、快速反向和资源释放测试，不操作现有应用窗口
./tests/run.ps1 -Toolchain 'D:\Tools\llvm-mingw-20261020-ucrt-x86_64'
```

程序产物为 `dist\Penumbra-v1.8.1` 完整目录，入口为顶层的 `Penumbra.exe`；安装包为 `dist\release-v1.8.1\Penumbra-v1.8.1-Setup-x64.exe`。打包脚本需 PowerShell 7；其构建中间文件不是发行附件。安装包与便携构建均未签名。`app\Nocturne.Renderer.exe --self-test` 仅验证暗角和动画数学性质，不能替代桌面视觉验收。需要排查时，可用 `--trace-animation` 单次启动后台，在配置目录生成数字帧日志与窗口诊断；默认关闭，不记录屏幕图像。GitHub 与 bilibili 图标许可见 `docs\THIRD-PARTY-NOTICES.txt`。

## 发行目录

```text
Penumbra-v1.8.1/
  Penumbra.exe   启动入口（启动界面后立即退出，不常驻）
  README.md      使用说明
  app/           程序本体、后台、全部 DLL、语言和头像资源
  docs/          验证记录和第三方许可
```

语言资源保留运行库要求的相对路径，统一收纳在 `app` 内，不手动改名或打散 DLL。后台的托盘设置入口与登录启动仍指向 `app` 内的程序；移动整个发行文件夹后，应重新应用登录启动选项。

安装版可从 Windows 设置的“已安装的应用”中卸载。卸载前请退出设置和后台；卸载保留 `%LOCALAPPDATA%\Nocturne` 中的个人参数，只清理指向本次安装位置的登录启动项，不删除其他便携目录或它们的启动项。安装版与便携版共用设置，不建议同时运行。

体积优化使用 WinUI 组件包代替全量 Windows App SDK 包，不分发本软件未使用的 ML、AI、Widgets 等组件；启用 .NET 部分裁剪并关闭调试符号。仍为自包含便携版，不要求另装 .NET 或 Windows App Runtime。本次没有将 DLL 压缩为运行时自解压包，避免额外解压缓存。

## 预览与窗口外观

窗口支持拖动边框调整宽高、最大化和还原；最小尺寸为 480×320 个有效像素，并随当前屏幕 DPI 换算。宽度不足 920 个有效像素时，参数区自动移到预览下方；空间不足时可纵向滚动，底部操作按钮保持可见。预览随宽度保持壁纸比例，窗口缩放不触发壁纸捕获，也不重新计算暗角像素。

预览在滑块拖动期间节流更新，避免为每个指针事件重新分配位图。预览图按主屏比例铺满，不留两侧底色，不叠加壁纸名称或时间标签。右侧参数区无外围描边，滑块标题与对应控件成组排列。Windows 11 使用 WinUI 3 Mica 系统背景；旧版 Windows 使用系统提供的回退外观。

刷新使用 Windows Graphics Capture 读取 Explorer 桌面宿主，普通应用遮挡桌面时也可获取壁纸。只创建短暂的一帧捕获会话，获取后关闭 Direct3D 设备与帧池；没有持续捕获。系统可能短暂显示捕获提示边框。壁纸自身暂停时读取最后可用画面；锁屏、桌面不可用或捕获超时会提示失败，并保留上次成功的预览。

## 桌面效果

原先挂在 Explorer 内的分层子窗口，在本机 Wallpaper Engine 下没有显示。本版改为由桌面宿主拥有、非置顶、点击穿透的顶层透明窗口，并把它放在桌面上方、普通应用下方。

这类窗口也处于图标上方，因此本版从 Explorer 单独读取图标与文字的绘制结果，按实际像素生成透明遮罩，让图标保留亮度，避免整块矩形区域露出明亮壁纸。图标创建、移动、重命名、选择等事件经过 350 毫秒合并后更新遮罩；结果相同则不重绘。预览也使用图标遮罩。

每次应用会先请求旧后台正常退出，再启动同目录新版，避免旧程序继续处理新版设置。


## 兼容范围

已在本机运行 Wallpaper Engine 时实际验证壁纸变暗与图标亮度保护。Explorer 桌面结构、壁纸渲染模式及 Windows 版本会影响结果；未验证所有场景、视频、网页壁纸和混合 DPI 多屏。具体测试及限制见 TEST-REPORT.md。

桌面宿主不可用或首次图标遮罩读取失败时，不显示暗角并报告状态。Explorer 重启后会低频尝试恢复，也可从托盘“重新挂接桌面”。图标变化后存在短暂更新延迟。

## 文件与隐私

配置与状态：`%LOCALAPPDATA%\Nocturne\settings.ini` 和 `status.txt`。异常时可能写入 `ui-error.txt` / `startup-error.txt`。

程序不修改壁纸文件。启动设置窗口或点击“检查更新”时会访问 GitHub 官方 Release API；API 不可用时使用同仓库的官方最新发布链接进行只读检查。不携带账号令牌，不上传配置、桌面图像或使用数据，不自动下载安装；常驻后台不联网检查。主动点击作者主页或更新发布页链接才会交给浏览器打开。桌面预览在打开设置、点击刷新或应用时读取，包含图标，只保留在设置进程内存。捕获目标为桌面窗口树，已有的独立暗角表面与普通应用窗口不属于该窗口树；刷新期间不隐藏暗角、不模拟快捷键、不改变应用窗口状态。

登录启动使用当前用户的 `HKCU\Software\Microsoft\Windows\CurrentVersion\Run\Nocturne`。移动发行目录后需重新应用此选项；取消勾选并应用可移除启动项，关闭暗角本身不取消登录启动。退出后台后透明表面随进程释放。
</div>
