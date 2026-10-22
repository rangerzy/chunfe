# 春风 · Chunfe

**遇事不决，可问春风。**

春风是一个基于 **Qt Quick** 的桌面端 AI 面试辅助工具项目，帮助面试者理解面试官的问题、组织回答思路，并获得参考回答。

## 当前阶段

当前版本：`0.1.0`。项目目前只有 Qt Quick 界面骨架，面试问题识别、AI 回答建议与面试上下文处理均尚未实现。

本次初始化参考 `jian` 项目的构建、打包和发布配置，保留已有 C++ / QML 代码。后续开发围绕面试问题理解、回答建议和表达辅助展开。

## 开发环境

- Qt **6.12+**，需要 Qt Quick；macOS / Windows CI 固定使用 `6.12.0`。
- 支持 **C++23** 的编译器。
- CMake **3.24+**、Ninja。
- macOS 使用 Apple Clang；Windows 使用 Qt 配套的 MinGW x64；Linux Flatpak 使用 KDE SDK 中的编译器。

通过 `CMAKE_PREFIX_PATH` 指定 Qt 安装路径，机器专属路径不提交到仓库。

### macOS ARM64

```sh
cmake --preset macos-arm64-release \
  -DCMAKE_PREFIX_PATH="$HOME/Qt/6.12.0/macos"
cmake --build --preset macos-arm64-release
open build/macos-arm64-release/Chunfe.app
```

macOS 发布包面向 Apple Silicon，最低部署目标为 **14.4**。

### Windows x64

在已配置 Qt 6.12 及其配套 MinGW 的 PowerShell 中执行：

```powershell
cmake --preset windows-x64-release "-DCMAKE_PREFIX_PATH=$env:QTDIR"
cmake --build --preset windows-x64-release
.\build\windows-x64-release\Chunfe.exe
```

`QTDIR` 指向 Qt 的 MinGW 安装目录，Qt 和匹配 MinGW 的 `bin` 目录需位于 `PATH`。CI 使用 Qt 6.12.0 配套的 MinGW 13.1.0。

### Linux

本地已安装 Qt 6.12 开发环境时：

```sh
cmake --preset linux-x64-release -DCMAKE_PREFIX_PATH="$QTDIR"
cmake --build --preset linux-x64-release
./build/linux-x64-release/Chunfe
```

发布包使用 Flatpak，在 KDE SDK 内构建。初始化配置沿用 `jian` 的 **Flathub Beta `6.12-beta`** 运行时，允许 Qt 6.12 补丁更新；构建时会检查 SDK 的 Qt 版本。构建及安装步骤见 [Linux Flatpak 说明](packaging/linux/README.md)。

### Debug 与检查

```sh
cmake --preset debug -DCMAKE_PREFIX_PATH="/path/to/Qt/6.12.0/platform"
cmake --build --preset debug
cmake --build --preset debug --target Chunfe_qmllint
python3 -B -m unittest discover -s tests -v
```

Python 测试覆盖 Release 发布逻辑，无需网络或 GitHub 凭据。当前没有应用自动退出的测试入口，因此未配置 CTest 启动测试；打包检查验证依赖部署和安装文件，不代表面试功能已通过测试。

## 三平台安装包

| 平台 | 发布文件 | 内容 |
| --- | --- | --- |
| macOS ARM64 | `chunfe-0.1.0-macos-arm64.dmg` | 含 Qt / QML 依赖的 `Chunfe.app` |
| Windows x64 | `chunfe-0.1.0-windows-x64-setup.exe` | Inno Setup 安装包，含 Qt / QML 和 MinGW 运行库 |
| Linux x64 | `chunfe-0.1.0-linux-x64.flatpak` | Flatpak 单文件应用包，依赖 KDE `6.12-beta` 运行时 |

本地打包入口：

```sh
# macOS：先完成 macos-arm64-release 构建
bash packaging/macos/package.sh

# Windows PowerShell：先完成 windows-x64-release 构建，安装 Inno Setup 6.3+
./scripts/ci/package-windows.ps1

# Linux：依赖与安装步骤见 packaging/linux/README.md
bash scripts/ci/build-flatpak.sh
```

macOS 打包会部署 Qt / QML、进行 ad hoc 签名并检查 DMG；Windows 会检查依赖并验证静默安装后的文件；Linux 会检查 Flatpak 的构建、导出和安装内容。这些流程尚未执行应用启动测试。

macOS 尚未配置开发者证书签名或 Apple 公证，Windows 尚未配置代码签名证书。Linux 使用独立 Flatpak 包分发，尚未提交到 Flathub 应用目录。

## 分支与自动发布

日常在 **`dev`** 开发，合并到 **`main`** 后自动构建并发布。

```sh
git switch dev
# 开发、验证、提交
git push origin dev

# 准备发布；也可以通过 dev → main 的 Pull Request 合并
git switch main
git pull --ff-only origin main
git merge --ff-only dev
git push origin main
git switch dev
```

- 推送 `dev`、创建或更新 PR 均不会触发构建或发布。
- 合并或推送到 `main`：自动构建、打包 macOS / Windows / Linux，三端全部成功后自动发布 GitHub Release。
- 也可在 GitHub Actions 中选择 `main` 手动运行；选择其他分支时所有任务跳过。
- 工作流默认只有 `contents: read`，仅 Release 任务使用 `contents: write`，通过内置 `github.token` 发布，无需额外发布 Secret。

连接 GitHub 远端并将这些配置推送到 `main` 后，自动化流程才会在 GitHub 上运行。

版本唯一来源为 `CMakeLists.txt` 的 `project(Chunfe VERSION ...)`。Release 标签为 `v<版本>`，标题为 `Chunfe <版本>`。

**同版本再次发布会更新同一个 Release、替换三平台附件，并将标签移动到最新成功发布的提交。** 需要保留独立版本记录时，先更新 CMake 中的版本号。

发布先进入草稿状态，三个附件上传并通过 SHA-256 校验后公开。发布说明记录提交及工作流信息，同版本旧提交的运行不会覆盖较新的发布，旧版本发布不会回退 Latest 标记，中断后可重跑恢复。

## 工程结构

```text
CMakeLists.txt             # 项目、版本、Qt Quick 模块与安装规则
CMakePresets.json          # 三平台 Release 与 Debug 构建预设
main.cpp                   # 现有应用入口
Main.qml / Test.qml        # 现有界面骨架
packaging/                 # DMG、Inno Setup、Flatpak 配置
scripts/ci/                # Windows / Linux 打包脚本
.github/workflows/         # main 分支构建与自动发布
.github/scripts/           # Release 发布脚本
tests/                     # Release 发布脚本离线测试
```
