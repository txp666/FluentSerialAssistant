<div align="center">
  <img src="logo.png" alt="Fluent Serial Assistant logo" width="96" height="96">
  <h1>Fluent Serial Assistant</h1>
  <p>A cross-platform serial debugging tool built with C++17, Qt 6 Widgets, and FluentQtWidgets.</p>
  <p>
    <a href="https://github.com/txp666/FluentSerialAssistant/releases/latest"><img src="https://img.shields.io/github/v/release/txp666/FluentSerialAssistant" alt="Latest release"></a>
    <a href="LICENSE"><img src="https://img.shields.io/github/license/txp666/FluentSerialAssistant" alt="License"></a>
    <img src="https://img.shields.io/badge/platform-Windows%20%7C%20macOS%20%7C%20Linux-0a7aca" alt="Platforms">
  </p>
  <p>
    English · <a href="README.md">简体中文</a> ·
    <a href="https://github.com/txp666/FluentSerialAssistant/releases/latest">Download</a> ·
    <a href="CHANGELOG.md">Changelog</a> ·
    <a href="CONTRIBUTING.md">Contributing</a> ·
    <a href="CODE_SIGNING_POLICY.md">Code signing policy</a>
  </p>
</div>

Fluent Serial Assistant is a modern Fluent-style serial terminal for Windows, macOS, and Linux. It covers the everyday debugging workflow: connecting, sending and receiving, viewing, protocol parsing, plotting, automation, and export.

## Preview

<p align="center">
  <img src="docs/images/show.gif" alt="Fluent Serial Assistant in use" width="860">
</p>

<table>
  <tr>
    <td width="50%"><img src="docs/images/main-window.png" alt="Terminal workspace"><p align="center">Terminal workspace with tabbed sessions, log coloring, and traffic statistics</p></td>
    <td width="50%"><img src="docs/images/plot-window.png" alt="Plot window"><p align="center">Plot window that extracts fields by protocol and plots them live</p></td>
  </tr>
  <tr>
    <td width="50%"><img src="docs/images/protocol-editor.png" alt="Frame protocol editor"><p align="center">Frame protocol editor showing byte layout, fields, and checksum</p></td>
    <td width="50%"><img src="docs/images/data-table.png" alt="Frame table"><p align="center">Frame table with HEX, text, and checksum results</p></td>
  </tr>
</table>

The screenshots show the Simplified Chinese interface; English is available in Settings or from the title bar.

## Download

Download the package for your platform from [Releases](https://github.com/txp666/FluentSerialAssistant/releases/latest). Every package comes with a `.sha256` checksum file.

| Platform | Package | Notes |
| --- | --- | --- |
| Windows x64 | `FluentSerialAssistant-X.Y.Z-windows-x64-setup.exe` | Installer; needs administrator rights and installs to Program Files by default |
| macOS (Apple silicon) | `FluentSerialAssistant-X.Y.Z-macos-arm64.dmg` | Open it and drag the app to the Applications folder |
| Linux x64 / arm64 | `FluentSerialAssistant-X.Y.Z-linux-{x64,arm64}.deb` | `sudo apt install ./FluentSerialAssistant-X.Y.Z-linux-x64.deb` |

The macOS build is not notarized by Apple. If macOS blocks it on first launch, click **Open Anyway** in **System Settings → Privacy & Security**. From v0.1.14 on, the app checks for updates itself and downloads, verifies, and installs them after you confirm.

## Features

### Connecting and sending

- Serial-port discovery with port name, description, vendor, VID/PID, and serial number; common and extended baud rates, data bits, parity, stop bits, flow control, RTS, and DTR.
- Text or HEX sending with None, CR, LF, or CRLF line endings; separate UTF-8, GBK, ASCII, and Latin-1 encodings for receiving and sending.
- Loop sending at millisecond intervals, the last 20 unique sends in history, and chunked file sending with configurable chunk size and interval.
- Tabbed sessions, each with its own serial port; a new tab copies the current configuration.
- A built-in virtual serial pair: once enabled, two sessions can connect to `VIRTUAL-A` and `VIRTUAL-B` and exchange data without installing drivers.

### Viewing and analysis

- Text, HEX, and mixed terminal display with timestamps, automatic scrolling, pausing, a custom color for sent records, and live totals, rates, and connection time.
- Frame splitting by receive interval, header, trailer, or fixed length.
- Terminal search by keyword or regular expression, with case sensitivity, highlighted matches, and RX/TX filtering.
- Content coloring with ESP-IDF, log level, success/failure, and AT response presets, plus your own keyword or regex rules.
- Plot windows: open several independent windows for numbers, delimited values, key-value pairs, JSON, or binary fields; preview samples, filter fields, and pause, clear, or export CSV.
- A frame table showing time, direction, source, length, HEX, text, and checksum, with sorting, filtering, copying, and navigation back to the terminal.

### Protocols and tools

- Frame protocol templates: define header, length, command, payload, checksum, and frame tail in a visual editor, with the checksum starting at a chosen offset, and check them against a sample or the latest received frame. Once enabled, received records show the command and checksum status.
- Checksums: CRC16-Modbus, CRC16-CCITT, CRC32, LRC, XOR, and SUM8, calculated by hand or added when sending. Bytes at the start and end (such as a header or tail) can be skipped; the checksum goes in front of the skipped end bytes and the line ending.
- Modbus RTU: build common read and write requests with CRC and summarize responses.

### Automation

- Packets with groups, notes, enable/disable, batch sending, ordering, and JSON import/export.
- Macros with multiple steps, delays, expected text or HEX responses, loops, stop on failure, and CSV result export.
- Auto-replies matched by text, HEX, or regular expression, sent immediately or after a delay and labeled in the records.
- A JavaScript runner with a controlled `serial` API for sending, reading records, and logging.
- AI control through local IPC, a CLI, and an MCP server that reuse the active GUI session; see [AI, CLI, and MCP control](#ai-cli-and-mcp-control).

### Logging and export

- Export session records as TXT, CSV, or BIN.
- Automatic TXT, CSV, or BIN logs named with the time, port, and baud rate, rolled over by file size.
- Save raw received data to a file.

### Appearance and settings

- Light, dark, or system theme with an accent color; UI font, monospace terminal font, font size, and custom font import.
- Simplified Chinese and English, switchable without restarting.
- Restores the last serial settings, send text, packets, macros, auto-reply rules, and file-send settings, with optional connection at startup; side cards remember whether they are collapsed.
- Settings are stored in the standard per-user configuration directory and never in the Windows registry; settings from older installations are migrated automatically.

## Getting started

1. Choose a port, set the baud rate, data bits, parity, stop bits, and flow control, and click **Connect**.
2. Watch traffic in the terminal; turn on timestamps, frame splitting, or HEX display as needed.
3. Type text or HEX in the send area, pick a line ending and encoding, and send; checksums and loop sending are optional.
4. Click the plot icon in the terminal toolbar, choose a protocol, and confirm to open a plot window.
5. Under **Protocol Template** on the left, click **Edit protocol** to define the frame format; once saved and enabled, received records show the parsed result.
6. Automate repeated steps with packets, macros, auto-replies, or scripts.
7. The broom button at the left of the terminal toolbar clears the terminal and the traffic counters; export records as TXT, CSV, or BIN.

### Plot windows

- Every confirmation opens a new window, so several fields can be plotted side by side. The button at the top left shows the current protocol and opens the parser settings.
- Preview a text or HEX sample before plotting. Binary fields take a type, byte offset, byte order, scale, and offset, read from the whole frame or from the payload of a protocol template.
- A window plots only data received while it is open; nothing is backfilled after it was hidden, minimized, or closed. A plot shows up to 32 channels.

### Built-in virtual serial pair

1. Open **Settings** and enable **Built-in virtual serial pair**.
2. Connect one session to `VIRTUAL-A` and another to `VIRTUAL-B`; refresh the port list if needed.
3. Data sent from either end arrives at the other, which also works with auto-reply rules for request and response tests.

The pair exists only inside one running app and does not affect physical ports. It forwards bytes as they are, without simulating baud rate or flow control; data sent while the other end is disconnected is dropped.

### In-app updates

1. The app checks for updates at startup and stays quiet when there is nothing new or no network. **Settings → App Updates** checks on demand.
2. A new version first shows its number and release notes; downloading starts only after **Download and install**.
3. Downloads use four parallel connections, show progress, and can be cancelled; servers without range support fall back to one connection.
4. After the size and SHA-256 check, the installer takes over, asking for permission if needed, and the app saves its settings and quits.

## AI, CLI, and MCP control

While the app runs, `fluentserial-cli` and `fluentserial-mcp` control the active GUI session. The GUI stays the only user of the serial port; the CLI and MCP server reach it through user-private local IPC and reuse its connection, protocol parsing, records, and plot windows.

```bash
fluentserial-cli ports
fluentserial-cli status
fluentserial-cli send-hex --hex "01 03 00 00 00 02 C4 0B"
fluentserial-cli records --direction rx --limit 20
fluentserial-cli plot --plot-protocol keyValue --plot-field "free sram" --clear
```

Use the CLI and MCP server from the same version as the app. See [AI control, CLI, and MCP](docs/ai-control.md) for the architecture, MCP client setup, all tools, and the IPC contract.

## Building from source

### Getting the source

The project includes `FluentQtWidgets` as a Git submodule:

```bash
git clone --recursive https://github.com/txp666/FluentSerialAssistant.git
cd FluentSerialAssistant
```

For an existing clone without submodules:

```bash
git submodule update --init --recursive
```

### Requirements

- CMake 3.21+ and Ninja
- Qt 6.5+ with the Widgets, SerialPort, Svg, Network, Qml, and Core5Compat modules (select Qt Serial Port and Qt 5 Compatibility Module in the Qt installer)
- On Linux, also `libgl1-mesa-dev`, `libxcb-cursor0`, and `libxkbcommon-x11-0`

### Windows (MinGW)

`CMakePresets.json` expects `C:/Qt/6.11.1/mingw_64` and `C:/Qt/Tools/mingw1310_64`; adjust the presets if yours differ.

```powershell
cmake --preset mingw-release
cmake --build --preset mingw-release --parallel
.\build\mingw-release\FluentSerialAssistant.exe
```

Use the `mingw-debug` preset for a debug build. If linking reports `cannot open output file FluentSerialAssistant.exe: Permission denied`, the previous build is usually still running.

### macOS and Linux

```bash
cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x/<platform>
cmake --build build/release --parallel
```

Leave out `CMAKE_PREFIX_PATH` when Qt is already on the default search path.

### Running the tests

```bash
ctest --test-dir build/release --output-on-failure
```

Tests are built with the project by default and run on Qt's offscreen platform, so they need neither a display nor a real serial port.

### VS Code

The repository includes `.vscode/tasks.json`: `Ctrl+Shift+B` / `Cmd+Shift+B` runs the debug build and `F5` builds and starts debugging. Windows reuses the MinGW presets; macOS and Linux build in `build/vscode-debug` or `build/vscode-release`.

## Releasing

GitHub Actions runs only when a GitHub Release is published.

1. Update the version in `CMakeLists.txt`, `CHANGELOG.md`, and `docs/release-notes/vX.Y.Z.md`, and push them to main.
2. Publish a GitHub Release with `vX.Y.Z` as both tag and title.
3. The workflow first marks the release as a prerelease, then runs the tests and builds the packages on all three platforms. Once everything is verified and uploaded, it becomes a full release and is marked as the latest. If a build fails, it stays a prerelease and in-app updates never see it.

The assets are the Windows x64 installer, the macOS arm64 DMG, the Linux x64/arm64 DEB packages, and a `.sha256` file for each. Windows releases support free open-source Authenticode signing from SignPath Foundation: once configured, `FluentSerialAssistant.exe` is signed first, then the installer is built and signed. See the [SignPath setup guide](docs/signing/SIGNPATH_SETUP.md).

For local packaging, `scripts/package_release.ps1` builds a portable Windows package; the platform installers come from `scripts/package_windows.ps1`, `scripts/package_unix.sh`, and `scripts/create_deb_package.sh`.

## Repository layout

```text
.
├── .github/workflows/   # Release tests and packaging
├── docs/
│   ├── ai-control.md    # AI, CLI, and MCP control
│   ├── images/          # README screenshots
│   ├── release-notes/   # Release notes for each version
│   └── signing/         # Code signing setup
├── packaging/windows/   # Inno Setup definition and translations
├── scripts/             # Packaging scripts
├── signing/signpath/    # SignPath artifact configurations
├── src/
│   ├── app/control/     # Session control boundary and local IPC
│   ├── app/core/        # Protocols, checksums, plot parsing, encodings, updates, and other shared logic
│   ├── app/resources/   # Qt resources, icons, and translations
│   ├── app/serial/      # Serial port wrapper and the built-in virtual pair
│   ├── app/view/        # Main window, settings, plots, protocol editor, and other UI
│   │   └── workbench/   # Terminal workspace, split by feature: connection, terminal, sending, macros, scripts, and more
│   ├── cli/             # Command-line tool
│   └── mcp/             # MCP stdio server
├── tests/               # Qt Test suites
└── third_party/FluentQtWidgets/
```

## License

This project is licensed under the GNU General Public License v3.0 or later; see [LICENSE](LICENSE). `FluentQtWidgets`, which it depends on, is also GPL-3.0-or-later, so distributing this project or a derivative must follow the GPL's source and redistribution terms.

## Related project

- [FluentQtWidgets](https://github.com/txp666/FluentQtWidgets)

## Code signing policy

Free code signing is provided by [SignPath.io](https://signpath.io/), with a
certificate from [SignPath Foundation](https://signpath.org/), after the
project's open-source application is approved. Team roles, release controls,
signed artifact scope, and the privacy statement are documented in the
[code signing policy](CODE_SIGNING_POLICY.md) and [privacy policy](PRIVACY.md).
