# PA8/PA9 启动诊断与输入验证

UART1，PA8 为 RX，PA9 为 TX，115200 / 8N1 / 无流控。固件显式清除 UART1 引脚重映射，使用这两个默认引脚。日志为 ASCII，以 CRLF 换行。

## 当前冻结版本：iOS 1.0.5

2026-11-10 用户确认 iOS 验收成功，固定分支 `ios/v1.0.5`、标签 `ios-v1.0.5`。Android 实机测试稍后进行；下方 1.0.1 至 1.0.4 的触控记录均为历史排查，不代表当前版本仍使用该方案。

iOS 已删除触摸屏、触控板、绝对指针与触摸转换，只输出键盘 ID 1 和相对鼠标 ID 2。BLE OTA 从 1.0.4 安装本版本后，`build/uart-ios-v105-boot.log` 记录：

```text
[IAP] pending valid=1 size=156832 crc=304D4E1E
[IAP] install result=1; active check
[APP] entry fw=00010005 reset=00 chip=92
[HID] profile=1 inputs=2 map=140 touch=0
[APP] alive hid=0001 ctrl=FFFF ready=03 ota=0
```

电脑重连后读回 `role=2 hid=1 controller=1 ready=03 version=1.0.5 profile=ios capabilities=03`。升级末尾的 Windows 状态读取被设备复位中断；依据 IAP 安装日志和新 APP 的版本读回确认成功，没有因客户端异常重复上传。

投屏观察到指针移动、左键打开 Safari、按住左键向上拖动使网页滚动。`build/uart-ios-v105-buttons.log` 分别记录 `buttons=01 → 00` 和 `buttons=02 → 00`。右键的具体系统动作由 iOS 自定按钮决定，不能把某一按键无 UI 响应直接判为固件故障。用户随后确认 iOS 验收完成并要求冻结版本，已停止继续测试手机。

本轮先尝试 UART OTA，首个协议请求未收到有效回复，随后使用 BLE OTA 完成升级。此前 1.0.3 → 1.0.4 的 UART OTA 已成功；1.0.5 没有再次完成 UART 上传。串口日志、BLE OTA、UART OTA 的实际验证范围详见 `OTA.md` 和冻结目录 `RELEASE.md`。

冻结固件保存于 `releases/ios-v1.0.5/`。现场 Jump/IAP 仍为 1.0.1，APP 为 1.0.5；本次构建的 factory 内 Jump/IAP 为 1.0.5，尚未在现场整体重烧。真实断电恢复、Android 和运行峰值等整机项目仍未全部验收，因此保留构建 manifest 中的 `hardware_validated: false`。

## 已修正的启动问题

之前的构建将 `EVT/EXAM/SRC/Startup/startup_CH592.S` 用于全部镜像。该文件在 `mret` 前写入 `mstatus=0x88`，MPP 为 0；随后我们的 Jump / IAP 代码执行机器 CSR 操作和下一镜像的启动代码。这与多镜像启动需要的权限不一致。

同一份官方 SDK 的 `BackupUpgrade_IAP/Startup/startup_CH592.S` 和 `BackupUpgrade_OTA/Startup/startup_CH592.S` 使用 `mstatus=0x1888`，MPP 为 3。构建已改用这两个官方文件，不修改 vendor。现在构建会对实际 ELF 中 `handle_reset` 的反汇编进行检查，拒绝未保留机器模式的镜像；测试也会确认旧 `0x88` 路径被拒绝。

这是根据原 ELF、官方启动文件和数据手册定位的启动链错误；没有采到实板异常记录，不能认定它是现场唯一原因。注意 `mret` 会改变 MPP，入口日志中的 mstatus 数值不等于启动代码写入值，也不能单独用来推断当前特权级。

## 启动修复时的烧录文件

使用 `dist/ch592f_3in1_factory.hex`，或从地址 `0x00000000` 烧录 `dist/ch592f_3in1_factory.bin`。需要完整更新 Jump、IAP 和 APP。仅更新 `app.hex` 或发送 `.ota` 无法修正旧 Jump/IAP。

旧版源码与产物已集中保存到 `.backups/startup-uart-20261109/before.zip`，备份清单为同目录 `manifest.json`。如需对照旧 ELF，先解压到独立目录，不要覆盖新构建。

## 采集日志

在项目目录运行；COM8 只是命令示例，应使用实际 UART 适配器端口：

```powershell
python -X utf8 tools/monitor.py --list
python -X utf8 tools/monitor.py --port COM8 --seconds 30
```

先启动采集，再复位目标。工具不发送任何协议字节，日志保存在 `build/uart-<UTC时间>.log`，同名文件不会覆盖。`--seconds 0` 可以持续采集到 Ctrl+C。串口不能同时被升级工具和日志工具占用。

## 2026-11-09 实板验证

设备上线后枚举到 `COM7 USB-SERIAL CH340`，蓝牙地址为 `38:3B:26:3B:7D:55`，名称为 `CH592F-3IN1`。本轮未执行烧录、复位或 OTA。

用户于北京时间 20:02:04 提供 APP 心跳，前面带有无法从显示文本还原的异常字符。最初 COM7 被占用，打开返回拒绝访问；用户关闭串口工具后，第一次 20 秒采集只收到 10 字节残缺文本，保存在 `build/uart-20261109-120519.log`。第二次在北京时间 20:06:49 开始的 20 秒采集收到 188 字节，即连续 4 条完整心跳，保存在 `build/uart-20261109-120649.log`：

```text
[APP] alive hid=0001 ctrl=FFFF ready=07 ota=0
```

第二轮原始接收内容均为 ASCII，换行为 CRLF，没有乱码。第一轮残缺和用户日志前缀异常的原因尚未定位，不能据此断言已修复串口问题。采集未覆盖复位过程，因此没有 Jump/IAP 启动现场。

在原 HID 主机保持连接时，从电脑执行状态查询：

```powershell
python -X utf8 tools/client.py --address 38:3B:26:3B:7D:55 info
```

三次短时查询均读到：

```text
role=2 hid=1 controller=1 ready=0x07 queue=0 version=1.0.1
```

这确认电脑连接被分配为控制端，查询时原 HID 槽位仍在线；控制端退出后可再次进入原控制角色。查询连接均已退出。`ready=07` 由 HID 链路加密、未 suspend 和三个报告 CCCD 已订阅共同决定，不能代替目标屏幕上的输入效果验证。`ctrl=FFFF` 是采集到的心跳时刻没有控制端，不与短时查询中的 `controller=1` 冲突。

在 2026-11-09 21:48 左右重新上电后的复测日志为 `build/uart-20261109-134842.log`。HID 槽位保持 `0001`，控制端多次断开并重新连接都记录为 `handle=0002 role=2`，重新连接后仍为 `ready=07`。通过控制端发送单点触控按下/抬起，控制状态返回 `result=0` 且队列归零；在手机主屏点击 Safari 图标位置及另一种横竖屏坐标方向后，投屏画面均没有变化。截图证据为 `build/ios-touch-before.png`、`build/ios-touch-after-safari.png` 和 `build/ios-reboot-after-touch.png`。

这证明当前连接分配、GATT 控制写入、触控队列和 HID 通知就绪状态均正常，但不能证明 iOS 接受了本项目的通用 `Touch Screen` Digitizer 报告。iOS 的外部控制应优先按 AssistiveTouch 支持的鼠标/触控板路径验证；不要仅凭这次无画面变化就盲目替换触控报告描述符。Android 触控路径和 iOS 指针路径需要分别实测。整体 `hardware_validated` 保持 false，固件产物保持 1.0.1。本次仅更新 README 和本诊断记录，改动前文档已保存到 `.backups/startup-uart-20261109/before-online-docs.zip`。

## 1.0.4 触控修复进展

2026-11-09 已实际修改并升级 APP。新增 ID 5 的五字节绝对指针报告，保留 ID 3 原生触摸屏；`05 01` 选择 iOS 绝对定位与独立按钮路径，`05 00` 选择原生触摸屏。首次定位与按钮按下间隔至少 60 ms。Service Changed 在每个角色首次加密后发送一次，电脑连接工具在动作前处理服务对象失效，动作本身不自动重放。

BLE OTA 已完成 1.0.1 → 1.0.2 → 1.0.3，串口 OTA 已完成 1.0.3 → 1.0.4。早期串口上传在擦除期间的一次状态查询超时后退出；修正工具重试只读状态后，完整上传、CRC 和提交通过。运行状态实际读回为：

```text
role=2 hid=1 controller=1 ready=0x0F queue=0 version=1.0.4 touch_mode=1
```

`build/uart-touch-ota-v102.log` 包含 Jump、IAP 安装 staging 和 APP `fw=00010002` 的完整启动链。`build/uart-touch-ota-v103.log` 记录 Service Changed 成功及 `ready=0F`。C 测试与 16 项 Python 测试通过，包括绝对坐标、首次定位等待、按钮时序、队列原子接收、拖动与抬起、服务刷新重连及 UART 状态查询丢失。

触摸尚未验收成功：`build/ios-v104-phone-layer.png` 为直接抓取的手机画面，点击 Safari 后桌面仍未变化。保持电脑 controller 在线，下一步需在手机忽略旧设备并重新配对，以排除 iOS HID 描述符缓存，再复测点击和滑动。投屏随后达到免费时长限制，手机画面窗口消失；`build/ios-v104-current.png` 记录当时的电脑端提示。仍须恢复可用投屏；不得把 GATT 写入成功或 `ready=0F` 当作手机触摸成功。`hardware_validated` 保持 false。

原始产物和本轮修改前文件集中保存在 `.backups/startup-uart-20261109/touch-baseline.zip`，其中 `v102/` 另保留 BLE OTA 已验证的 1.0.2 APP 和清单。当前交付产物位于 `dist/`，版本 1.0.4；实板 Jump/IAP 仍为先前烧录的 1.0.1，APP OTA 不会替换它们。

## 正常路径的日志标记

下面是预期阶段标记，不是实测日志。CRC、地址字、句柄和状态值取决于实际镜像和设备。

```text
[JUMP] entry fw=00010001 ...
[JUMP] target=0006D000 word=...
[IAP] entry fw=00010001 ...
[IAP] clock=60000000; pending check
[IAP] pending valid=... size=... crc=...
[IAP] install result=...; active check
[IAP] active valid=...
[IAP] UART recovery window 500ms
[IAP] jump APP=00001000
[APP] entry fw=00010001 ...
[APP] clock=60000000
[APP] BLE begin heap=12288 buffers=6 links=2
[APP] BLE OK; HAL begin
[APP] HAL OK; peripheral begin
[APP] peripheral rc=00
[BLE] task=...
[BLE] ... rc=00
[BLE] adv enable=1 data_rc=00 enable_rc=00
[APP] scheduler enter
[BLE] start rc=00
[BLE] state=... event=... status=00
[APP] alive hid=FFFF ctrl=FFFF ready=00 ota=0
```

第一次启动没有有效 active metadata 时，还会输出 factory 镜像长度、预期 CRC、实际 CRC 和元数据写入结果。`install result` 为 0 表示没有有效 pending，1 表示安装完成，-1 表示安装失败。

## 根据最后一条日志定位

| 最后一条或现象 | 排查范围 |
| --- | --- |
| 完全无输出 | 无法仅凭静默定位；确认实际烧入完整新 factory，且采集覆盖复位过程 |
| 只有 JUMP，target word 为全 FF | IAP 区未烧入或读取内容异常 |
| JUMP 有输出，IAP 没有 entry | IAP 镜像入口、启动复制或跨镜像跳转 |
| 停在 IAP pending / active check | Flash 读取、CRC 检查或 pending 安装路径 |
| factory expected 与 actual 不同 | APP 与 IAP 所属构建不匹配，或烧录内容不完整 |
| `APP invalid; UART recovery waiting` | IAP 正在恢复模式，不会启动无效 APP |
| `APP BLE begin` 后无 `BLE OK` | BLE 库初始化；查看紧邻的 SDK 错误日志 |
| `head file error...` | BLE 头文件与库版本不匹配 |
| `SNV config error...` | SNV 地址或容量配置错误 |
| `LIB init error code: ...` | BLE_LibInit 返回错误；保留错误码 |
| `BLE OK; HAL begin` 后不再前进 | HAL / RTC 初始化阶段 |
| 任意 `rc` 非 00 | 对应配置或服务注册调用失败 |
| 有 `scheduler enter`，没有 `start` | TMOS 未调度 START_EVENT |
| 有 `start`，无心跳 | TMOS 定时事件或后续异常，需要结合 FAULT 日志 |
| 心跳持续，但无法发现广播 | APP 已运行；查看 adv 返回码及 GAP state/event/status |

`hid=FFFF` / `ctrl=FFFF` 表示对应槽位无连接；`ready=00` 在主机未连接或未订阅时正常。运行心跳每 5 秒一条。连接、断连和配对也会输出事件及句柄。

## 异常日志

HardFault 和 NMI 使用独立处理入口，记录异常现场后停机：

```text
[FAULT APP] cause=... pc=... value=... sp=...
```

前缀还可能是 `JUMP` 或 `IAP`。用相同构建、相同阶段的 ELF 查询 PC；下列地址仅为用法示例：

```powershell
C:/tc/gcc/bin/riscv-none-embed-addr2line.exe -e dist/app.elf -f -C 0x00001234
```

`dist/*.startup.txt` 保存本次入口反汇编。必须保留报错固件对应的 ELF，后续重新构建后地址可能变化。

## 日志与串口 OTA

收到第一帧合法 UART 协议帧后，普通文本日志一直静默到复位，因此 `client.py --port ... info` 也会触发静默。这样后续二进制 OTA 回复不会夹入心跳。异常日志优先输出并停机。升级客户端已测试能跳过帧外启动文本。

日志实现不用完整 printf 运行库、不在正常中断里输出。SDK 的 PRINT 被转接到轻量格式化函数，使 BLE 初始化内部原先被编译掉的失败原因可见。Jump/IAP 跳转和改变系统时钟前会等待发送完成；IAP 转入 APP 前停止并清除 SysTick。
