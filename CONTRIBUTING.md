# 贡献指南

感谢你愿意参与 Fluent 串口助手。这个项目处于早期阶段，欢迎提交问题反馈、功能建议、文档改进和代码贡献。

## 开发准备

1. Fork 本仓库。
2. 克隆时拉取子模块：

```powershell
git clone --recursive <your-fork-url>
cd FluentSerialAssistant
```

如果已经克隆：

```powershell
git submodule update --init --recursive
```

3. 安装 Qt 6.5+，确保包含 `SerialPort` 和 `Svg` 模块。
4. 使用 CMake 构建：

```powershell
cmake --preset mingw-debug
cmake --build --preset mingw-debug --parallel
```

## 提交 Pull Request

- 保持改动聚焦，避免把无关重构混在同一个 PR。
- UI 改动请尽量附截图或录屏。
- 串口行为改动请说明测试设备、波特率和复现步骤。
- 新功能需要同步更新 README 或相关文档。
- 构建前请至少执行一次本地 Debug 构建。

## 代码风格

- 使用 C++17。
- 优先遵循现有 Qt Widgets 和 FluentQtWidgets 写法。
- 可见业务 UI 尽量使用 FluentQtWidgets 组件。
- 不引入与当前目标无关的大型依赖。
- 注释应解释必要的业务意图，避免重复代码字面含义。

## Issue 建议

提交 Bug 时请包含：

- 系统版本
- Qt 版本
- 构建方式
- 串口设备或虚拟串口工具
- 复现步骤
- 期望行为和实际行为
- 相关截图或日志

提交功能建议时请说明：

- 具体使用场景
- 预期工作流
- 是否有参考工具或截图

## 许可证

提交代码即表示你同意贡献内容按本项目的 GPL-3.0-or-later 许可证发布。
