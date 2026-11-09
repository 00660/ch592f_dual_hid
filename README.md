# CH592F 双连接键盘、鼠标、单点触控固件

本项目使用 WCH 官方 CH592 SDK 的协议栈、启动代码和 Flash 驱动，自行实现双连接角色隔离、组合 HID、GATT 控制及 BLE/UART OTA。工程位于独立目录，未参考电脑上已有的 CH592 工程。官方 SDK 是本次从 `openwch/ch592` 下载并固定到提交 `a46e0086f1ffb5e5502703970bff94888e67f4cb` 的副本。

当前版本为 1.0.4。APP、IAP、Jump 已交叉编译，C 回归与 16 项 Python 测试通过。实板已完成 BLE OTA 和 UART OTA，并读回 `role=2 hid=1 controller=1 ready=0x0F version=1.0.4`。键盘已在 iPhone 上打开搜索并启动 Safari；iPhone 坐标点击目前仍未得到画面成功证据，Android 输入、真实 Flash 断电恢复及整机验收也未完成，`dist/manifest.json` 保持 `hardware_validated: false`。采集记录见 `STARTUP_DEBUG.md`。

启动诊断沿用 1.0.1 的修复：Jump/IAP 使用官方 IAP 启动文件，APP 使用官方 OTA 启动文件，均在 `mret` 前设置 `mstatus=0x1888`。PA8/PA9 UART1 输出启动和异常日志。本次 BLE OTA 已采到 Jump、IAP 安装和新 APP 启动的完整链路；OTA 仅更新 APP，修改 Jump/IAP 时仍需烧录完整 factory 镜像。

## 已实现行为

- CH592F 同时作为两个 BLE Central 的外设，广播名称 `CH592F-3IN1`。
- 上电后第一个连接占用 HID 槽位，第二个连接占用 GATT 控制槽位；满员时不再开放第三条连接。
- 已连接设备的角色在该连接期间固定，另一端断开不会导致角色互换。空出的槽位可被后续连接补位。角色不按 MAC 永久绑定，重启后重新按连接顺序分配。
- HID 包含六键无冲键盘、五键相对鼠标、原生单点触摸屏及绝对指针，Report ID 分别为 1、2、3、5；ID 4 为触点数 Feature，支持键盘 LED 输出。
- 仅 HID 连接可订阅输入报告，仅控制连接可写控制和 BLE OTA。两者使用独立连接句柄、CCCD 和权限检查。
- 输入、控制、BLE OTA 要求加密；使用 Just Works 配对和 bonding。当前没有 MITM 认证、固件签名或防降级机制。
- 控制端断开、显式释放及进入 OTA 时释放键盘、鼠标按钮和活动触点；队列满返回 BUSY，通知暂时发不出去时保留待发报告。
- BLE OTA 与 UART OTA 共用升级状态机，并互斥占用升级事务。

## Android、iPhone 和 iPad

| 被控平台 | 本项目提供的控制路径 | 使用条件与边界 |
| --- | --- | --- |
| Android | 蓝牙键盘、鼠标指针；单点绝对触控 | 外部触控由系统输入驱动与厂商映射决定，需要在目标机型验证；鼠标路径可单独使用 |
| iPhone / iOS | 蓝牙键盘、AssistiveTouch 绝对定位与单点点击、拖动路径 | 开启“设置 → 辅助功能 → 触控 → 辅助触控”，触控指令指定 `--mode ios`；当前手机实际效果待验收 |
| iPad / iPadOS | 蓝牙键盘、鼠标指针、点击与拖动 | 由系统指针功能处理，必要时配置辅助触控 |
| 其他 HID 主机 | 同一套标准 HOGP 报告 | 具体报告的识别情况以目标系统为准 |

iOS 模式将 `touch` / `swipe` 的 0–32767 坐标转换为绝对指针定位，并通过独立鼠标报告控制按下、抬起；原生触摸屏模式保留给 Android 等支持 Digitizer 的主机。HID 描述符变更后应在 iPhone 忽略旧设备并重新配对，再观察实际点击和滑动；`ready=0F` 仅证明订阅与加密就绪。`mouse` / `click` / `drag` 仍可独立使用，相对位移受系统指针速度和加速度影响。

固件已将两条连接的参数请求设为 15–30 ms、latency 0、supervision timeout 6 s，并补充设备信息和 Battery Service。实际连接参数由 Central 协商决定。Battery Level 固定为 100，表示本构建按外部供电设备处理，未实现电池采样。PnP ID 沿用官方开发例程的 WCH `0x07D7` / product `0x0000` 身份，未申请本项目的独立产品标识。

## 首次烧录和连接顺序

1. 首次通过 WCH 烧录工具写入 `dist/ch592f_3in1_factory.hex`，HEX 已包含 Jump、APP、IAP 的地址。使用 BIN 时选择 `dist/ch592f_3in1_factory.bin`，起始地址为 `0x00000000`。
2. 先让被控电脑或手机在系统蓝牙设置中配对并连接 `CH592F-3IN1`，占用 HID 槽位。
3. 再由另一个设备作为 GATT 控制端连接。控制工具应读到 `role=2`、`hid=1`；`ready` 的 bit0/1/2/3 分别表示键盘、相对鼠标、原生触摸屏、绝对指针报告已就绪。
4. iPhone 开启 AssistiveTouch 后使用 `touch --mode ios` / `swipe --mode ios`；Android 使用 `--mode screen`。观察屏幕完成实际效果验证。

如果 HID 主机还未连接就运行控制工具，该工具会占用第一槽位，并报告角色错误。断开它后按上述顺序连接。双方都已 bonding 时，重启后的自动重连顺序也可能影响角色。

首次烧录必须安装完整 factory 镜像。`.ota` 是带头的 APP 升级包，不能直接作为裸 BIN 烧录。OTA 只替换 APP，修改 Jump、IAP 或分区布局后需要重新烧录 factory。

## 构建与测试

在项目目录执行，Python 需支持 `asyncio.to_thread`：

```powershell
python -m pip install -r requirements.txt
python -X utf8 tools/fetch_sdk.py
python -X utf8 tools/build.py --toolchain C:/tc/gcc/bin
python -X utf8 tools/test.py
```

构建默认使用 `riscv-none-embed-gcc`，也可通过 `WCH_TOOLCHAIN` 或 `--prefix` 指定工具链。此交付使用 xPack RISC-V GCC 8.2.0。源码使用 UTF-8 无 BOM；原版 vendor 文件保持官方原始编码与内容。

`tools/test.py` 使用主机 C 编译器：先查 `CC` / `cc` / `gcc`，本工作区另有本次下载的 `build/host-tools/tcc/tcc.exe`。换机器时可使用 `python tools/test.py --cc <主机编译器>`。测试需要先构建，因为会验证生成的 HEX 和 OTA 包。

固件版本在 `include/layout.h` 的 `FW_VERSION` 中，格式为 `0xMMMMmmpp`。修改后重新构建会同步生成运行版本、OTA 包和 manifest。

## 控制工具

以下地址和端口是占位值，替换为 `scan` 的实际结果。被控手机必须已经作为第一条连接接入。电脑端依赖 Bleak；工具每次操作使用一条控制连接，并在动作完成后释放输入。

```powershell
python -X utf8 tools/client.py scan
python -X utf8 tools/client.py --address AA:BB:CC:DD:EE:FF info
python -X utf8 tools/client.py --address AA:BB:CC:DD:EE:FF key 4
python -X utf8 tools/client.py --address AA:BB:CC:DD:EE:FF key 6 --modifiers 1
python -X utf8 tools/client.py --address AA:BB:CC:DD:EE:FF mouse 100 50
python -X utf8 tools/client.py --address AA:BB:CC:DD:EE:FF click
python -X utf8 tools/client.py --address AA:BB:CC:DD:EE:FF drag 0 -300 --duration 600
python -X utf8 tools/client.py --address AA:BB:CC:DD:EE:FF mouse 0 0 --wheel -3
python -X utf8 tools/client.py --address AA:BB:CC:DD:EE:FF touch 16384 16384
python -X utf8 tools/client.py --address AA:BB:CC:DD:EE:FF swipe 16384 28000 16384 6000 --duration 600
python -X utf8 tools/client.py --address AA:BB:CC:DD:EE:FF touch 16384 16384 --mode ios
python -X utf8 tools/client.py --address AA:BB:CC:DD:EE:FF swipe 16384 28000 16384 6000 --duration 600 --mode ios
python -X utf8 tools/client.py --address AA:BB:CC:DD:EE:FF touch-mode screen
python -X utf8 tools/client.py --address AA:BB:CC:DD:EE:FF release
```

`key 4` 是键盘 Usage A，`key 6 --modifiers 1` 是 Ctrl+C，字符解释取决于主机布局和输入法。`click` 点击当前鼠标指针位置；`drag` 从当前位置按住左键相对移动后释放；`touch` 是单点按下/抬起；`swipe` 在绝对坐标之间保持同一触点并滑动。坐标范围为 0–32767，当前只支持单点。

工具会等待固件输出队列排空，再发送后续报告和最终释放，避免尾部动作被清空。手势时长是目标发送节奏，加上蓝牙事务时间后实际时长可能更长。需要持续交互时，可按 `PROTOCOL.md` 在自己的 GATT 客户端中保持长连接；本交付没有手机端专用 App。

1.0.4 在每个角色首次完成加密后发送一次 Service Changed，刷新升级后的服务缓存；同一角色重连时不重复发送。控制工具在发送动作前检查状态，必要时重建一次连接，发送过的动作不会自动重放。

## BLE OTA 与串口 OTA

BLE 升级使用第二连接，保持第一条 HID 连接便于角色分配。进入升级后 HID 控制返回 BUSY。

```powershell
python -X utf8 tools/client.py --address AA:BB:CC:DD:EE:FF ota dist/ch592f_3in1.ota
python -X utf8 tools/client.py --port COM8 ota dist/ch592f_3in1.ota
python -X utf8 tools/client.py --port COM8 info
python -X utf8 tools/client.py --port COM8 abort
```

UART 使用 UART1、PA8 RX / PA9 TX、115200 8N1，数据协议见 `PROTOCOL.md`。串口仅用于升级、状态查询和中止，不接受 HID 控制指令。

诊断版本上电时同一串口会输出 ASCII 日志，运行时每 5 秒输出心跳。收到第一帧合法 UART 协议数据后，普通文本日志静默至复位，避免插入 OTA 回复。异常日志优先输出。`tools/monitor.py` 只读取日志，不发送协议帧。

升级先擦写 staging，再分块校验 CRC，最后持久化 pending 标记并复位。IAP 校验 staging 后复制到 APP，校验目标并更新 active 标记后才清 pending。复制过程中断电会在下次启动重新安装完整 staging。校验或复制失败时不会无条件跳转到损坏 APP。

IAP 在正常启动时保留约 500 ms 串口恢复窗口；APP 无效时持续等待串口升级。有效 CRC 但无法正常工作的 APP 需要在复位恢复窗口中启动 UART 升级。当前没有新 APP 健康确认或自动回滚；CRC 用于传输完整性校验，不是固件签名。

上传工具显示提交成功只表示 CRC 与 pending 已确认。重启后按正确连接顺序重新连接，再用 `info` 检查实际运行版本。若升级改变 HID 描述符或 GATT 表，主机可能需要删除原配对后重新配对以刷新缓存。

串口擦写期间状态查询可能超时；工具会重试只读状态查询直至状态等待期限，不会因单次超时重复 BEGIN。

## 分区

| 区域 | 起始地址 | 容量 |
| --- | --- | --- |
| Jump | `0x00000` | 4 KB |
| APP | `0x01000` | 216 KB |
| staging | `0x37000` | 216 KB |
| IAP | `0x6D000` | 12 KB |
| active metadata | DataFlash 偏移 `0x6000` | 256 B |
| pending metadata | DataFlash 偏移 `0x6100` | 256 B |
| BLE SNV | DataFlash 偏移 `0x7000` | 2 个官方 SNV 块 |

CodeFlash 擦除按 4 KB，写入按 4 字节对齐。应用容量检查和 RAM/栈不重叠断言包含在构建脚本内。`dist/manifest.json` 记录镜像大小、CRC32、SHA-256 和官方 SDK 提交。

## 验证记录

2026-11-09：目标构建通过，自有 C 文件启用 `-Wall -Wextra -Werror`。C 测试覆盖角色分配与隔离、断连释放、报告合法性、队列压力、OTA 范围/归属/重传/CRC、UART 帧解析，以及 IAP 安装中全部 39 个擦写/元数据操作的半写半擦断电模拟。另用官方 SDK 类型测试实际 GATT 属性回调、CCCD、权限、长 Report Map 读取和通知失败重试。

Python 测试覆盖报告编码、手势位移和抬起、控制队列排空、OTA 文件破损、HEX 地址和校验、HID 描述符、最小 MTU 分块及丢 ACK 重试。实板验收应继续验证两台 Central 并发连接、各目标 OS 的报告处理、两种 OTA、断电恢复和 RAM/栈运行峰值；这些尚未由当前测试证明。

## 官方资料

SDK 与数据手册来源：

```text
https://github.com/openwch/ch592
vendor/ch592/Datasheet/CH592DS1_EN.PDF
```

本次参考 `MultiCentPeri`、`HID_Keyboard`、`HID_Mouse`、`HID_Touch` 和 `BackupUpgrade_*`，全部来自上述新下载的固定版本。官方 SDK 保持原版权声明，芯片库按其 WCH 芯片使用条件引用。

移动平台及触控依据：

```text
https://developer.apple.com/accessories/Accessory-Design-Guidelines.pdf
  本次下载版本 2026-10-30，第 57 章，尤其 57.6、57.12、57.13。
https://support.apple.com/en-us/111775
https://source.android.com/docs/core/interaction/input/touch-devices
https://learn.microsoft.com/en-us/windows-hardware/design/component-guidelines/supporting-usages-in-digitizer-report-descriptors
```

iOS 绝对定位与独立按钮的实现参考本次从 GitHub 下载的 `xhoantran/iphone-use`，固定提交 `bc0fe36e78ec5a3af095c813574e3a8b7bbea4dd`，主要对照 `HIDReportMap.swift` 和 `Input.swift`。许可见 `THIRD_PARTY_NOTICES.md`。开源实现的实测结果不替代本工程在当前手机上的验证。
