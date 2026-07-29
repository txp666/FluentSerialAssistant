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
fluentserial-cli plot --plot-protocol keyValue --plot-field 'free sram' --clear
```

启用二进制协议模板后，`records` 的 RX 项会增加结构化的 `protocol` 对象，包括命令字、载荷、帧长和校验结果。

绘图解析由独立的解析层完成，界面只负责展示。文本记录会保留行边界，避免一次串口读取包含多行时把通道错位。快速绘图窗口的“解析设置”使用同一配置模型：文本/JSON 可直接筛选字段，二进制可选择完整帧或协议载荷，并在表格中配置类型、字节序、比例和加值。界面应用的配置会持久化，之后 CLI/MCP 修改时也会同步到窗口。

- `numbers`：提取每行的全部数字。
- `delimited`：提取逗号、分号或空白分隔的纯数字。
- `keyValue`：提取 `name=value` 或 `name:value`，支持多词和 Unicode 字段名，例如 `free sram: 43815`。
- `json`：递归提取对象和数组中的数值，嵌套字段使用路径名，例如 `system.free`、`channels[0]`。
- `binary`：从完整帧或启用的协议模板载荷中提取二进制字段。

`--plot-field` 可重复指定，只保留目标字段。二进制未提供字段定义时，每个字节自动映射为 `CH1`、`CH2` 等无符号曲线。结构化二进制字段通过可重复的 JSON 参数定义：

```bash
fluentserial-cli plot \
  --plot-protocol binary \
  --binary-source payload \
  --binary-field '{"name":"temperature","byteOffset":0,"type":"i16","byteOrder":"little","scale":0.1}' \
  --binary-field '{"name":"pressure","byteOffset":2,"type":"u32","byteOrder":"big","scale":0.01,"add":-100}' \
  --clear
```

- `binarySource`/`--binary-source` 为 `frame` 或 `payload`；使用 `payload` 前必须选择并启用二进制协议模板。
- `byteOffset` 相对于所选来源从 `0` 开始。
- `type` 支持 `u8`、`i8`、`u16`、`i16`、`u32`、`i32`、`u64`、`i64`、`f32` 和 `f64`。
- `byteOrder` 支持 `little` 和 `big`；单字节字段会忽略字节序。
- 最终值按 `decoded * scale + add` 计算，`scale` 和 `add` 默认分别为 `1` 和 `0`。

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
| `plot_open` | 打开实时曲线，配置文本/JSON 字段筛选或二进制字段解码 |

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
