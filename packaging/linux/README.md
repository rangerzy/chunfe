# Linux Flatpak

春风的 Linux 包面向 x86_64，应用 ID 为 `io.github.rangerzy.chunfe`，
可执行文件为 `Chunfe`。打包在 KDE SDK 内构建根工程的最小 Chunfe 应用壳，宿主机无需另装 Qt。
根工程与正式发布工作流不包含 `examples/`；独立检查工作流会构建示例并运行其可用测试。应用尚未实现对话辅助功能。
安装规则提供 `bin/Chunfe`、desktop 文件与 SVG 图标。

## 运行时依赖

配置沿用参考项目的 Flathub Beta 运行时：`org.kde.Platform//6.12-beta`
和 `org.kde.Sdk//6.12-beta`。运行时分发分支仍为 beta；构建脚本每次都会检查
SDK 提供的是正式版本号 `6.12.x` 的 Qt，否则构建失败。

运行时的远程可用性由实际构建确认。迁移至稳定分支时，需要同步修改 manifest、
打包脚本和下方安装命令，并重新验证。工程继续使用 Qt 6.12 和 C++23，最低需要 CMake 4.3.6。

manifest 在 SDK 内先安装通过 SHA-256 校验的官方 **CMake 4.3.6**，再用该工具构建应用；宿主机的 CMake 不参与沙盒构建。CMake 模块仅用于构建，完成后清理，不进入最终应用包。通用构建检查另以 **4.4.5** 验证较新版本兼容性。

## 构建与包内容验证

在 Ubuntu 24.04 x86_64 上：

```sh
sudo apt-get update
sudo apt-get install -y flatpak flatpak-builder python3 desktop-file-utils \
  coreutils elfutils librsvg2-common
bash scripts/ci/build-flatpak.sh
```

脚本在当前用户的 Flatpak 配置中添加 Flathub 和 Flathub Beta，安装 SDK/运行时，
从项目根目录构建，并生成 `dist/chunfe-<版本>-linux-x64.flatpak`。
版本由 `cmake/AppVersion.cmake` 中四个 `VERSION_*` 数值组成，初始为 `0.1.0.0`。打包脚本通过共享的 `scripts/ci/app_version.py` 读取声明版本，不需要先配置 Qt；构建后会核对实际版本。版本标签与安装包名使用完整四段，Flatpak 的 `stable` 分支保持独立。

构建后会为当前用户安装生成的包，检查应用 ref、运行时、启动命令等元数据，
并比较安装后的可执行文件、版本文件、desktop 文件和图标的 SHA-256。
当前没有自动退出的测试模式，因此**打包流程不启动应用，也未执行自动启动测试**。
脚本会保留已安装的包，便于后续手动验证。

当前沙盒仅允许显示、共享内存和图形设备访问，未开启网络、主目录或音频权限。
对话辅助所需的联网、录音等能力尚未实现；后续加入这些能力时需同步评估沙盒权限。

## 安装下载的包

应用包包含程序，运行时需要另行下载：

```sh
flatpak remote-add --user --if-not-exists flathub \
  https://flathub.org/repo/flathub.flatpakrepo
flatpak remote-add --user --if-not-exists flathub-beta \
  https://flathub.org/beta-repo/flathub-beta.flatpakrepo
flatpak install --user flathub-beta org.kde.Platform//6.12-beta
flatpak install --user ./chunfe-0.1.0-linux-x64.flatpak
flatpak run io.github.rangerzy.chunfe
```

卸载应用：

```sh
flatpak uninstall --user io.github.rangerzy.chunfe
```

本配置用于分发单文件应用包，尚未提交 Flathub 应用目录，也未配置 AppStream
应用目录元数据。SVG 使用已确认的错位双引号图形，正式源文件位于 `resources/branding/`。
