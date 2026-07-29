# AI 控制、CLI 与 MCP

Fluent 串口助手提供本机控制接口，使脚本和 AI 客户端能够复用 GUI 中已经打开的串口会话、协议模板、收发记录和快速绘图窗口。

## 架构

```text
AI 客户端 ── MCP stdio ── fluentserial-mcp ─┐
                                             ├── 用户级本地 JSON IPC ── GUI 会话 ── 串口设备
脚本/终端 ───────────── fluentserial-cli ───┘                         ├── 协议解析
                                                                       └── 实时曲线
```

- GUI 是串口的唯一所有者，CLI 和 MCP 不会再次打开同一串口，因此不会与 GUI 抢占设备。
- `SessionControl` 定义会话领域能力；界面控件、IPC 和 MCP 不互相依赖。
- 本地 IPC 使用 `QLocalServer`/`QLocalSocket` 和单行 JSON，仅允许当前系统用户访问。
- CLI 和 MCP 都是薄客户端。CLI 适合人工调试、Shell 脚本和自动测试；MCP 适合支持工具调用的 AI 客户端。
- MCP 使用标准 stdio 传输，兼容协议版本 `2025-11-25`、`2025-06-18`、`2025-03-26` 和 `2024-11-05`。

使用 CLI 或 MCP 前，需要先启动 Fluent 串口助手 GUI。

## CLI

开发构建中的程序位于构建目录。安装后的位置如下：

- Windows：安装目录中的 `fluentserial-cli.exe`
- macOS：`FluentSerialAssistant.app/Contents/MacOS/fluentserial-cli`
- Linux：`fluentserial-cli`

所有正常输出和业务错误都是紧凑 JSON。成功退出码为 `0`；命令参数错误为 `2`；GUI/IPC 不可用为 `2`；GUI 返回业务错误为 `4`。

### 查询

```bash
fluentserial-cli ping
fluentserial-cli ports
fluentserial-cli sessions
fluentserial-cli status
fluentserial-cli records --direction rx --limit 20
fluentserial-cli protocols
```

除端口扫描和会话列表外，命令默认操作 GUI 当前会话。多会话场景可显式指定：

```bash
fluentserial-cli status --session session-2
fluentserial-cli select --session session-2
```

### 连接与发送

```bash
fluentserial-cli connect \
  --port cu.usbmodem13101 \
  --baud 115200 \
  --data-bits 8 \
  --parity none \
  --stop-bits 1 \
  --flow-control none

fluentserial-cli send-text --text 'read temperature' --encoding utf-8 --line-ending crlf
fluentserial-cli send-hex --hex '01 03 00 00 00 02 C4 0B'
fluentserial-cli disconnect
```

HEX 发送默认逐字节发送输入内容，不会隐式应用 GUI 的“自动追加校验”设置；这保证 AI 调用的结果可预测。

### 协议与曲线

```bash
fluentserial-cli protocol-use --name 'Example Template'
fluentserial-cli records --direction rx --limit 10
fluentserial-cli plot --plot-protocol keyValue
```

启用二进制协议模板后，`records` 的 RX 项会增加结构化的 `protocol` 对象，包括命令字、载荷、帧长和校验结果。

当前有两类独立的“协议”：

- 二进制协议模板：从一帧字节中解析帧头、长度、命令、载荷和校验。
- 绘图取值协议：从接收文本中按 `numbers`、`delimited`、`keyValue` 或 `json` 提取数值并显示曲线。

二进制模板目前没有字段类型、缩放系数或单位定义，因此不会猜测如何把任意载荷字节变成物理量曲线。对于文本遥测，可直接使用绘图协议；对于二进制遥测，应先在协议层增加字段定义后再扩展为字段曲线。

### 原始动作调用

新增 IPC 动作时，可以先通过通用入口验证，无需立即增加 CLI 子命令：

```bash
fluentserial-cli call session.status --params '{"session":"session-1"}'
```

## MCP

MCP 服务程序安装位置：

- Windows：安装目录中的 `fluentserial-mcp.exe`
- macOS：`FluentSerialAssistant.app/Contents/MacOS/fluentserial-mcp`
- Linux：`fluentserial-mcp`

在支持 stdio MCP 的客户端中加入类似配置：

```json
{
  "mcpServers": {
    "fluent-serial-assistant": {
      "command": "/absolute/path/to/fluentserial-mcp"
    }
  }
}
```

MCP 服务遵循官方的 [stdio 传输](https://modelcontextprotocol.io/specification/2025-11-25/basic/transports)、[生命周期](https://modelcontextprotocol.io/specification/2025-11-25/basic/lifecycle)和[工具](https://modelcontextprotocol.io/specification/2025-11-25/server/tools)规范。标准输出只写 MCP JSON-RPC 消息，诊断信息写入标准错误。

### MCP 工具

| 工具 | 用途 |
| --- | --- |
| `session_list` | 列出 GUI 会话及其状态 |
| `session_select` | 切换当前 GUI 会话 |
| `serial_list_ports` | 扫描串口及 USB 标识 |
| `serial_get_status` | 获取连接、计数和协议状态 |
| `serial_connect` | 在目标 GUI 会话中连接串口 |
| `serial_disconnect` | 断开串口 |
| `serial_send_text` | 按指定编码发送文本 |
| `serial_send_hex` | 发送精确 HEX 字节 |
| `serial_get_records` | 获取收发记录和协议解析结果 |
| `protocol_list` | 获取二进制协议模板 |
| `protocol_select` | 选择并启用协议模板 |
| `plot_open` | 打开实时曲线并选择文本取值协议 |

工具调用同时返回 MCP `content` 和 `structuredContent`，业务错误通过 `isError: true` 返回，方便模型自行修正参数。

## IPC 协议

IPC 版本为 `1`，每个连接发送一个以换行结束的 JSON 请求，并接收一个 JSON 响应。消息上限为 1 MiB。

请求示例：

```json
{"id":"request-1","version":1,"action":"session.status","params":{"session":"current"}}
```

成功响应：

```json
{"id":"request-1","ok":true,"result":{"session":"session-1","connected":false}}
```

错误响应使用稳定的错误码：

```json
{"id":"request-1","ok":false,"error":{"code":"SESSION_NOT_FOUND","message":"Session was not found: session-9"}}
```

IPC 是内部版本化接口。外部自动化优先使用 CLI 或 MCP，以便后续协议升级时保持兼容。
