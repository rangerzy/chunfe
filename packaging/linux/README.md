# Linux Flatpak

春风的 Linux 包面向 x86_64，应用 ID 为 `io.github.rangerzy.chunfe`，
可执行文件为 `Chunfe`。打包在 KDE SDK 内构建当前工程，宿主机无需另装 Qt。

## 运行时依赖

配置沿用参考项目的 Flathub Beta 运行时：`org.kde.Platform//6.12-beta`
和 `org.kde.Sdk//6.12-beta`。运行时分发分支仍为 beta；构建脚本每次都会检查
SDK 提供的是正式版本号 `6.12.x` 的 Qt，否则构建失败。

运行时的远程可用性由实际构建确认。迁移至稳定分支时，需要同步修改 manifest、
打包脚本和下方安装命令，并重新验证。工程继续使用 Qt 6.12 和 C++23。

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
版本取自 `CMakeLists.txt` 的 `project(Chunfe VERSION ...)`，初始版本为 `0.1.0`。

构建后会为当前用户安装生成的包，检查应用 ref、运行时、启动命令等元数据，
并比较安装后的可执行文件、desktop 文件和图标的 SHA-256。
当前没有自动退出的测试模式，因此**打包流程不启动应用，也未执行自动启动测试**。
脚本会保留已安装的包，便于后续手动验证。

当前沙盒仅允许显示、共享内存和图形设备访问，未开启网络、主目录或音频权限。
面试辅助所需的联网、录音等能力尚未实现；后续加入这些能力时需同步评估沙盒权限。

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
应用目录元数据。SVG 是工程初始化阶段的通用窗口占位图标。
