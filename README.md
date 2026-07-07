<div align="center">
  <img src="logo.png" alt="Fluent 串口助手 Logo" width="96" height="96">
  <h1>Fluent 串口助手</h1>
  <p>基于 C++17、Qt 6 Widgets 和 FluentQtWidgets 的现代串口调试工具。</p>
  <p>
    <a href="LICENSE">GPL-3.0-or-later</a>
    ·
    <a href=".github/workflows/ci.yml">三平台 CI</a>
    ·
    <a href="CONTRIBUTING.md">贡献指南</a>
  </p>
</div>

Fluent 串口助手是一个基于 C++17、Qt 6 Widgets 和 FluentQtWidgets 的跨平台串口调试工具。项目目标是提供一个现代 Fluent 风格的串口终端，覆盖常用串口连接、收发、显示、导出和主题配置工作流。

当前版本仍处于早期开发阶段，功能以“终端工作台”和“设置”为主。

## 界面截图

<p align="center">
  <img src="docs/images/show.png" alt="Fluent 串口助手终端工作台截图" width="860">
</p>

## 功能特性

- 串口扫描：显示端口名、描述、厂商、VID/PID 和序列号。
- 连接参数：支持常用和扩展波特率、数据位、校验位、停止位、流控、RTS、DTR。
- 终端显示：支持文本、HEX、混合显示模式。
- 自动断帧：按接收间隔自动插入帧分隔。
- 记录显示：RX/TX 统一会话记录，支持暂停显示、自动滚动、清空和计数重置。
- TX 着色：可选择发送记录显示颜色。
- 数据发送：支持文本和 HEX 发送，支持 None、CR、LF、CRLF 行结束符。
- 发送历史：保留最近 20 条唯一发送记录。
- 循环发送：支持毫秒级发送间隔。
- 导出记录：支持 TXT、CSV、BIN。
- 接收保存：可将接收原始数据保存到文件。
- 外观设置：支持浅色、深色、跟随系统主题和主题色配置。

## 预览状态

已完成：

- 终端工作台
- 设置页
- Windows 紧凑发布包脚本
- GitHub Actions 三平台构建 CI
- GitHub Releases 三平台发布资产

暂未开放：

- 包列表
- 快速绘图
- 宏命令
- 测试序列
- 插件系统

## 技术栈

- C++17
- Qt 6.5+
- Qt Widgets
- Qt SerialPort
- Qt Svg
- FluentQtWidgets
- CMake
- Ninja

## 目录结构

```text
.
├── .github/workflows/        # GitHub Actions CI
├── docs/images/              # README 截图和文档图片
├── scripts/                  # 发布和辅助脚本
├── src/
│   ├── app/core/             # HEX 解析等通用逻辑
│   ├── app/resources/        # Qt 资源与 Windows 图标
│   ├── app/serial/           # QSerialPort 封装
│   └── app/view/             # 主窗口、终端页、设置页
├── third_party/FluentQtWidgets/
├── CMakeLists.txt
├── CMakePresets.json
└── README.md
```

## 获取源码

项目使用 `FluentQtWidgets` 作为 Git 子模块：

```powershell
git clone --recursive <repo-url>
cd FluentSerialAssistant
```

如果已经克隆但没有拉取子模块：

```powershell
git submodule update --init --recursive
```

## 本地构建

### 环境要求

- CMake 3.21+
- Ninja
- Qt 6.5+，需要包含 `SerialPort` 和 `Svg` 模块
- Windows 本地预设默认使用：
  - Qt：`C:/Qt/6.11.1/mingw_64`
  - MinGW：`C:/Qt/Tools/mingw1310_64`

### Debug

```powershell
cmake --preset mingw-debug
cmake --build --preset mingw-debug --parallel
```

运行：

```powershell
.\build\mingw-debug\FluentSerialAssistant.exe
```

### Release

```powershell
cmake --preset mingw-release
cmake --build --preset mingw-release --parallel
```

如果链接时报 `cannot open output file FluentSerialAssistant.exe: Permission denied`，通常是旧程序仍在运行，请关闭后重新构建。

## 打包发布

Windows 紧凑发布包：

```powershell
.\scripts\package_release.ps1
```

输出目录：

```text
dist/FluentSerialAssistant-release
```

脚本会执行：

- MinSizeRel 构建
- `windeployqt` 部署 Qt 依赖
- 排除未使用的插件、翻译和软件 OpenGL fallback
- 对 `.exe` 和 `.dll` 执行 `strip --strip-unneeded`

## CI

仓库提供 GitHub Actions 工作流：

```text
.github/workflows/ci.yml
```

CI 会在以下平台执行 Release 构建：

- Windows：`windows-latest`，MSVC 2022
- Linux：`ubuntu-latest`，GCC
- macOS：`macos-13`，Clang

CI 不使用本机 `CMakePresets.json` 中的固定 Qt 路径，而是在工作流中安装 Qt 并通过 CMake 直接配置。

## 使用说明

1. 选择串口端口，必要时点击刷新。
2. 配置波特率、数据位、校验位、停止位、流控。
3. 点击连接。
4. 在终端记录区查看 RX/TX。
5. 在发送区输入文本或 HEX 数据，选择行结束符后发送。
6. 可开启自动断帧、时间戳、自动滚动、循环发送等选项。
7. 使用 TXT、CSV、BIN 导出会话记录。

## 贡献

欢迎提交 Issue 和 Pull Request。请先阅读 [CONTRIBUTING.md](CONTRIBUTING.md)。

建议贡献方向：

- 串口协议解析器
- 更完善的 HEX 编辑体验
- 会话保存和恢复
- 自动测试
- 跨平台打包
- UI 可访问性和键盘操作

## 许可证

本项目采用 GNU General Public License v3.0 or later，详见 [LICENSE](LICENSE)。

由于本项目依赖的 `FluentQtWidgets` 也采用 GPL-3.0-or-later，分发本项目或其派生版本时需要遵守 GPL 的源代码开放和再分发要求。

## 相关项目

- [FluentQtWidgets](https://github.com/txp666/FluentQtWidgets)
