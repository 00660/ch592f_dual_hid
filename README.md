# CH592F 蓝牙键盘、鼠标与触摸控制固件

CH592F 将 BLE GATT 命令转换为键盘、鼠标或触摸 HID 输入。可以由另一台设备控制手机，也可以在 Android 手机上录制动作宏，上传到板卡后回放到同一部手机。支持蓝牙 OTA。

## 目录

1. [功能与固件选择](#firmware)
2. [首次安装与连接](#setup)
3. [键盘、鼠标与触摸操作](#control)
4. [GATT 控制协议](#gatt)
5. [固件升级与 OTA 协议](#ota)
6. [日志与故障排查](#diagnostics)
7. [源码构建与存储布局](#development)
8. [参考资料与第三方许可](#licenses)

<a id="firmware"></a>

## 1. 功能与固件选择

### 1.1 平台能力

| 平台 | 推荐操作方式 | 支持范围 |
| --- | --- | --- |
| Android | 绝对坐标触摸、键盘 | 单点点击、长按、滑动与拖动；不支持多指缩放 |
| iPhone / iOS | 键盘、AssistiveTouch 相对鼠标 | 指针移动、点击、按住拖动；不提供绝对触摸坐标 |
| iPad / 其他 HID 主机 | 键盘、相对鼠标 | 依赖主机 HID 支持，未单独验证 |

Android 的触摸和键盘已在 M2006C3LI / Android 16 验证；该设备的系统未将组合描述符中的鼠标映射为鼠标输入源，操作应使用触摸。iPhone 11 已验证键盘和相对鼠标。iOS 目标范围为 16 及后续版本，但尚未完成逐版本兼容验证。其他机型和横屏坐标映射需单独检查。

### 1.2 发布版本

| 平台 | 版本 | 冻结分支 / 标签 | 固件目录 |
| --- | --- | --- | --- |
| Android | 1.0.10 | `android/v1.0.10` / `android-v1.0.10` | [releases/android-v1.0.10](releases/android-v1.0.10/) |
| iOS | 1.0.5 | `ios/v1.0.5` / `ios-v1.0.5` | [releases/ios-v1.0.5](releases/ios-v1.0.5/) |

Android 开发分支为 `android/dev`。两个平台在编译时选择，不能通过控制命令切换；切换平台需安装相应固件。发布目录保存固定产物，`dist/` 是可被下次构建覆盖的临时输出。

同机宏功能位于 `feature/self-control-macros` 分支，配套“HID 宏工作台”App。宏回放要求 Android 1.1.0 或更新固件；每轮坐标漂移和轮间隔要求 1.1.1 或更新固件。上表中的旧版不支持宏协议；iOS 冻结版保持独立。同机触摸录制 App 仅用于 Android，不适用于 iPhone。

本工程为串口改到 PB12/PB13、PA8 作常亮指示灯的硬件变体。`releases/` 下的既有固件属于 PA8/PA9 串口版本，与本变体的引脚定义不一致，不能直接烧到本变体硬件上，请以重新构建的 `dist/` 产物为准。

| 文件 | 用途 |
| --- | --- |
| `ch592f_3in1_factory.hex` | 首次烧录的完整 Jump + APP + IAP 镜像，自带地址 |
| `ch592f_3in1_factory.bin` | 同一完整镜像，烧录起始地址为 `0x00000000` |
| `ch592f_3in1.ota` | BLE / UART APP 升级包，含 32 字节包头 |
| `app.bin` / `app.hex` | APP 裸产物，用于调试；升级工具使用 .ota |
| `*.elf` / `*.map` / `*.startup.txt` | 对应构建的调试符号、地址映射和入口反汇编 |
| `manifest.json` / `SHA256SUMS` | 版本、SDK 提交、大小、CRC32 与文件校验值 |

构建清单中的 `hardware_validated: false` 表示尚未完成全部平台与整机项目，不代替本章的功能兼容说明。BLE OTA 已在上述发布 APP 上验证；串口 OTA 已有实机验证，但未对这两个发布 APP 分别重新完成上传全流程。真实断电恢复与 RAM/栈峰值尚未完成硬件验证。

<a id="setup"></a>

## 2. 首次安装与连接

### 2.1 安装工具与固件

在项目根目录安装 Python 依赖：

```console
python -m pip install -r requirements.txt
```

首次安装时，用 WCH 烧录工具写入目标平台的 factory HEX。已有本项目引导程序时，可以按第 5 章升级 APP。修改了 Jump、IAP 或分区布局后，应重新烧录完整 factory 镜像；APP OTA 不更新引导程序。

本文命令使用以下占位参数，运行前替换为实际值：

| 参数 | 含义 |
| --- | --- |
| `BLE_ADDRESS` | `scan` 返回的板卡地址或设备标识 |
| `SERIAL_PORT` | `monitor.py --list` 返回的串口名 |
| `OTA_FILE` | 所选平台的 `ch592f_3in1.ota` 文件路径 |
| `TOOLCHAIN_BIN` | RISC-V 工具链可执行文件所在目录 |

### 2.2 双连接顺序

1. 给板卡上电，在被控手机或电脑的蓝牙设置中配对并连接 `CH592F-3IN1`。第一条连接占用 HID 槽位。
2. 再让另一台设备运行控制工具，连接第二条 GATT controller 链路。
3. 查询状态，确认角色、HID 在线与对应输入报告就绪后再操作。

```console
python -X utf8 tools/client.py scan
python -X utf8 tools/client.py --address BLE_ADDRESS info
```

| 字段 | 期望值 |
| --- | --- |
| `role` | 2：当前连接是控制端 |
| `hid` / `controller` | 均为 1：两端在线 |
| `ready` | iOS 完整订阅为 0x03，Android 为 0x07 |
| `profile` / `version` | 与安装的平台和版本相符 |
| `queue` | 操作完成后为 0 |

角色在一次连接期间固定，另一端断开不会触发角色交换。空槽可以补位；角色不按 MAC 永久绑定，重启后的自动重连也需保证被控设备先连。两条连接占满后不接受第三条连接。若控制工具读到 `role=1`，应断开控制端并按上述顺序重连。

`ready` 只表示加密、订阅和 HID 状态满足发送条件，不能证明系统已经建立对应输入源，或界面已经执行动作。更换平台或更新 HID 描述符、触点 Feature 后，在被控设备删除旧配对并重新配对，以刷新缓存。

### 2.3 iOS 设置

进入 iPhone 设置 → 辅助功能 → 触控 → 辅助触控，启用 AssistiveTouch，再进入设备的自定按钮设置。系统文案可能随版本变化。

| 按钮 | 建议设置 |
| --- | --- |
| 按钮 1 / 左键 | 单点轻按，用于点击及按住拖动 |
| 按钮 2 / 右键 | 按需选择辅助触控菜单、主屏幕等动作 |
| 按钮 3 / 中键 | 按需配置 |

左右键分别验证。右键是否产生预期动作取决于其系统映射，不能要求它与左键执行相同操作。

<a id="control"></a>

## 3. 键盘、鼠标与触摸操作

CLI 每次操作连接控制槽位，发送报告，等待输出队列排空，再释放输入并断开。持续交互时可按第 4 章实现保持连接的 GATT 客户端。

### 3.1 键盘

先聚焦目标输入框，再发送 USB HID 键盘 Usage。`key` 接受最多 6 个普通按键，`--modifiers` 为修饰键位。

```console
python -X utf8 tools/client.py --address BLE_ADDRESS key 4
python -X utf8 tools/client.py --address BLE_ADDRESS key 4 --modifiers 1
python -X utf8 tools/client.py --address BLE_ADDRESS key 42
python -X utf8 tools/client.py --address BLE_ADDRESS key 40
```

以上依次为 A、Ctrl+A、Backspace、Enter。字符结果由手机的输入法与键盘布局决定；输入网址或 ASCII 字符时，应选择对应的英文输入模式。

### 3.2 Android 触摸

触摸坐标范围为 0–32767，原点在左上角，X 向右、Y 向下。对于竖屏截图宽 W、高 H，可用 `round(x * 32767 / W)` 和 `round(y * 32767 / H)` 将像素坐标近似转换为 HID 坐标，并限制在有效范围。屏幕旋转或映射变化后应重新校验。

```console
python -X utf8 tools/client.py --address BLE_ADDRESS touch 16384 16384
python -X utf8 tools/client.py --address BLE_ADDRESS touch 16384 16384 --hold 800
python -X utf8 tools/client.py --address BLE_ADDRESS swipe 16384 28000 16384 9000 --duration 700
python -X utf8 tools/client.py --address BLE_ADDRESS swipe 16384 9000 16384 28000 --duration 700
```

以上依次为中心点击、中心长按、向上滑动、向下滑动；`--hold` 和 `--duration` 单位为毫秒。点击 App 图标即可打开应用；在可滚动区域滑动可操作列表和网页。系统返回、桌面和最近任务手势取决于导航设置及手势速度。

`swipe` 保持同一触点，依次发送按下、移动、终点和抬起，因此也可用于单点拖动。工具在链路延迟较高时合并过期的中间绝对坐标，保留手势端点与触点状态变化；实际耗时可能长于指定时长。当前只支持单点，不支持双指缩放。

### 3.3 相对鼠标

```console
python -X utf8 tools/client.py --address BLE_ADDRESS mouse 20 10
python -X utf8 tools/client.py --address BLE_ADDRESS click --button 1
python -X utf8 tools/client.py --address BLE_ADDRESS click --button 2
python -X utf8 tools/client.py --address BLE_ADDRESS drag 0 -100 --duration 600
python -X utf8 tools/client.py --address BLE_ADDRESS mouse 0 0 --wheel -1
```

`mouse x y` 是相对位移，正方向为向右、向下，受系统指针速度和加速度影响，不能直接视为屏幕像素。`click` 在当前指针位置点击；按钮 1、2、4 分别表示左、右、中键。`drag` 按住左键移动后释放。iOS 使用这条控制路径；Android 若没有鼠标输入源，应使用触摸。

### 3.4 释放输入

```console
python -X utf8 tools/client.py --address BLE_ADDRESS release
```

显式释放会清空待发动作，并释放键盘、鼠标按钮和已发送的活动触点。控制连接断开或开始 OTA 时，固件也会释放输入。正常结束动作时应先等待队列排空，再发 RELEASE；不要用 RELEASE 替代正常的触点抬起。

### Android 手机录制与回放

1. 将板卡升级为支持宏的 Android 固件。在手机系统蓝牙设置中先配对并连接 `CH592F-3IN1`。
2. 安装“HID 宏工作台”，允许附近设备权限，点击“选择板卡”，选择已配对的板卡。状态应显示“本机触摸已就绪”。App 与系统 HID 共用本机 BLE 连接，不占用第二个连接名额。
3. 新建动作宏，在“设置”中选择执行一次、指定次数或持续循环直到停止；按需设置启动延时、每轮结束后的等待时间和坐标漂移范围。点击步骤可编辑时长、坐标和顺序，也可以用“画手势”录入点按、长按和滑动。
4. 点击“开启悬浮工具条”，切换到目标应用，在工具条中直接录制或播放。工具条显示当前宏，可切换宏和修改参数，无需回到主界面；播放时缩小为暂停/继续和停止。拖动工具条标题可调整位置。

录制时，在目标应用内点击悬浮工具条“录制”，工具条切换为固定的小型录制条，显示已录制步数和“完成录制”。可连续录入点按、长按和滑动，中途不会逐步弹出编辑面板或要求确认。松手后由板卡执行刚录入的动作；录制条短暂显示“正在执行动作”，完成后自动恢复为“继续操作”。这段时间采集层透明并放行板卡输入，下一步在“继续操作”时录入。最后点击“完成录制”一次保存整段操作并选中新宏，仍留在目标应用中，可直接播放。新录制宏沿用当前执行参数，录制预览只执行一次，不改变正式回放的次数。步骤间的人为等待也会记录，保存后可编辑。

执行次数和间隔由用户设置：持续模式在板卡内循环，直到停止、HID 断线或板卡断电；指定次数执行完自动停止。暂停等待当前手势释放后生效。悬浮工具条和通知栏均可停止并释放输入；关闭工具条也会停止当前任务。录制与回放时工具条保持屏幕点亮。

坐标漂移按屏幕像素配置水平和垂直范围，0 表示关闭。固件每轮随机选择一个偏移，对该轮所有触摸报告使用同一偏移，保持手势轨迹与按下/抬起关系；下一轮重新选择，并按整条宏的坐标边界限制偏移，避免越界。漂移范围应小于目标区域的有效操作范围。工具条会避开手势区域；若路径与漂移范围覆盖所有停靠位置，需缩小漂移范围或调整动作后再播放。

宏保存在手机 App 中；板卡 RAM 同时容纳一个宏，最多 64 条输入记录。一次滑动会编译为多条移动与释放记录，界面显示实际容量；较长宏需拆分。板卡断电或 HID 连接断开后需重新上传。已经开始的回放不依赖控制 App 持续发送动作；保持手机与板卡的 HID 连接和屏幕点亮。

录制与回放应使用相同方向、页面布局和坐标映射。回放前可在“输入验证页”检查板卡实际输入的设备名和起终点；屏幕方向匹配并不保证不同手机或横屏的坐标一致。

连接成功后，“正在使用的板卡”显示当前设备的实际名称和地址；尚未连接时显示“已选择的板卡”及连接进度或失败原因。App 记住用户选择的设备，再次打开时自动恢复控制连接，可通过“选择板卡”切换设备，不绑定固定 MAC 或设备名称。附近扫描按控制服务或 HID 服务发现候选设备，连接后校验本项目的控制协议；普通蓝牙键盘和鼠标不具备宏控制服务。

### 获取与构建 App

在仓库 Actions 的 `Android Macro App` 构建中下载 `ch592-macro-android` 产物，安装 `app-debug.apk`；`app-debug-androidTest.apk` 仅用于开发验收。源代码在 `android/`，工作流会运行宏编码单元测试、Android lint，并生成两个 APK。

<a id="gatt"></a>

## 4. GATT 控制协议

所有多字节整数为小端。控制和 BLE OTA 使用带响应的 Write Request，不支持 Prepare Write / 长写事务。两条连接使用独立句柄、CCCD 与权限检查；输入、控制和 OTA 要求加密，使用 Just Works 配对与 bonding。

### 4.1 服务与特征

| 名称 | UUID | 操作与权限 |
| --- | --- | --- |
| 控制服务 | `59200001-3c7f-4a9e-8c51-3b9d4b6a1200` | 发现服务 |
| Command | `59200002-3c7f-4a9e-8c51-3b9d4b6a1200` | 加密写，HID 本机或 controller |
| Status | `59200003-3c7f-4a9e-8c51-3b9d4b6a1200` | 读取当前连接角色与运行状态 |
| OTA | `59200004-3c7f-4a9e-8c51-3b9d4b6a1200` | 加密读写，仅 controller |
| MacroStatus | `59200005-3c7f-4a9e-8c51-3b9d4b6a1200` | 加密读，HID 本机或 controller |
| HID | `0x1812` | 仅 HID 角色读取报告、订阅通知、写输出报告 |
| Device Information | `0x180A` | 型号、版本、序列号与 PnP 信息 |
| Battery | `0x180F` | 固定返回 100，未实现电池采样 |

两端可发现同一 GATT 表，访问时按角色鉴权。1.1.0 起 HID 本机允许写 Command，以支持手机控制自身；controller 仍不能读 HID 或写其 CCCD，HID 本机不能访问 OTA。蓝牙升级仍由第二条 controller 链路执行。

### 4.2 Status：20 字节

| 偏移 | 长度 | 含义 |
| --- | --- | --- |
| 0 | 1 | 协议版本：1 |
| 1 | 1 | 当前角色：0 无、1 HID、2 controller |
| 2 | 1 | HID 在线标志 |
| 3 | 1 | controller 在线标志 |
| 4 | 1 | ready：bit0 键盘、bit1 鼠标、bit2 触摸；要求 CCCD、加密且未 suspend |
| 5 | 1 | 待发报告数：0–16 |
| 6 | 1 | 最近一次控制结果码 |
| 7 | 1 | 键盘 LED：bit0–4 |
| 8 | 4 | APP 版本：`0xMMMMmmpp` |
| 12 | 4 | board ID：`0x592F0301` |
| 16 | 1 | OTA 状态，见第 5 章 |
| 17 | 1 | Profile：0 Android、1 iOS |
| 18 | 1 | 固件能力：bit0 键盘、bit1 鼠标、bit2 触摸 |
| 19 | 1 | 宏状态：0 空、1 上传中、2 就绪、3 回放中、4 暂停、5 等待手势结束后暂停 |

写入成功表示命令已进入队列。队列归零表示报告已交给 BLE 通知接口或因链路状态被丢弃，不等于 UI 操作成功；控制端还需检查 HID 在线与对应 ready 位。

### 4.3 Command

| 指令 | 格式 | 总字节数 |
| --- | --- | --- |
| 键盘 | `01 modifiers 00 key0 key1 key2 key3 key4 key5` | 9 |
| 鼠标 | `02 buttons x:i16 y:i16 wheel:i8 pan:i8` | 8 |
| Android 单点触摸 | `03 tip id x:u16 y:u16 count` | 8 |
| 全部释放 | `04` | 1 |

键盘普通按键 Usage 为 0–101，0 表示空位。modifiers 的 bit0–7 依次为左 Ctrl、左 Shift、左 Alt、左 GUI、右 Ctrl、右 Shift、右 Alt、右 GUI；iOS 将 GUI 对应为 Command。

鼠标 buttons 仅接受低 5 位：bit0 左键、bit1 右键、bit2 中键、bit3–4 附加键。X/Y 为 -32768–32767，wheel/pan 为 -127–127。

触摸 tip 为 1 表示按下或移动，0 表示抬起；id 固定 0，X/Y 为 0–32767，count 固定 1，包括抬起。固件抬起时保留最后的触点坐标。完整手势需发送按下、零个或多个移动、抬起。

Android 的 Contact Count Maximum Feature ID 4 与其 Logical Maximum 均为 2，使主机分配两个 MT 槽位并正确识别 slots 协议。该兼容声明不表示支持双指：控制命令仍只发送一个活动触点。维护描述符时应保留 Contact ID、Contact Count 及上述 Feature 值。

iOS 不接受触摸命令。旧指令 `05` 已移除，返回 VALUE。操作码不是 HOGP 通知中的 Report ID 前缀：通知载荷不带 ID，主机通过 Report Reference 识别键盘 ID 1、鼠标 ID 2，以及 Android 触摸 ID 3。

十六进制示例：

```text
01 00 00 04 00 00 00 00 00   按下 A
01 00 00 00 00 00 00 00 00   释放键盘
02 01 00 00 00 00 00 00      左键按下
02 02 00 00 00 00 00 00      右键按下，左键释放
02 00 00 00 00 00 00 00      释放所有鼠标按钮
03 01 00 00 40 00 40 01      在 (16384, 16384) 按下触点
03 00 00 00 40 00 40 01      抬起触点
04                            全部释放
```

### 4.4 结果码

| 值 | 名称 | 含义 |
| --- | --- | --- |
| 0 | OK | 成功 |
| 1 | LENGTH | 长度错误 |
| 2 | VALUE | 字段值或目标板号错误 |
| 3 | ROLE | 角色错误 |
| 4 | NOT_READY | HID 未就绪 |
| 5 | BUSY | 队列满、OTA 忙或事务被其他端占用 |
| 6 | OFFSET | 升级偏移错误 |
| 7 | FLASH | 擦写或读回失败 |
| 8 | CRC | 镜像校验失败 |
| 9 | STATE | 状态不允许 |

应用错误通过 ATT 返回 `0x80 + result`；GATT 权限等基础错误使用标准 ATT 错误码。相对鼠标位移在结果未知时不应盲目重发，以免重复移动。

### 4.5 宏协议（1.1.0 起）

通过 Command 特征按顺序发送，每包等待写入响应。多字节数值均为小端。

| 操作 | 格式 |
| --- | --- |
| 开始上传 | `10 count:u8 crc32:u32`；count 为 1–64 |
| 上传记录 | `11 offset:u16 record[11]`；offset 为字节偏移，按 11 递增 |
| 提交 | `12`；校验所有记录及 CRC32 |
| 开始回放 | `13 repeats:u16 [initial_delay_ms:u16 [jitter_x:u16 jitter_y:u16 loop_delay_ms:u32]]`；总长 3、5、9 或 13 字节；repeats=0 持续循环 |
| 停止 | `14`；停止并释放输入，可重复调用 |
| 暂停 | `15`；当前输入释放后暂停 |
| 继续 | `16` |

每条记录 11 字节：偏移 0–1 为发送后的等待毫秒数；偏移 2 为报告类型（0 等待、1 键盘、2 鼠标、3 触摸）；偏移 3–10 为 HID 载荷，鼠标和触摸使用 7 字节并补零。类型 0 不发送报告。CRC32 使用与 OTA 相同的 IEEE 算法，覆盖顺序拼接的完整记录。宏必须包含输入，且所有按键、按钮和触点在末尾释放。

上传过程只接受同一个连接的 DATA/COMMIT；相同偏移、相同内容的已收记录允许重传。提交前不会执行。回放或暂停期间拒绝新的实时输入，`04` 和 `14` 都可以停止。HID 断开清空宏；第二控制端断开不会中断已经启动的回放。OTA 或 HID suspend 会停止宏。

1.1.1 起，9 字节格式继续兼容仅含两轴坐标漂移半径的旧请求；13 字节格式在其后增加轮间隔。漂移半径使用 0–32767 的 HID 坐标，像素值按 `round(radius_px * 32767 / (screen_pixels - 1))` 换算。固件按整条宏的坐标边界限制偏移，同一轮所有触点共用偏移，暂停后继续不重新选择；下一轮开始时重新选择。轮间隔为 0–3,600,000 毫秒，只发生在相邻两轮之间，不占用宏记录；暂停期间保留剩余等待时间。旧的 3/5/9 字节格式没有轮间隔，按 0 处理。

MacroStatus 为 20 字节：0 协议版本 1；1 状态（同 Status[19]）；2 最近宏结果码；3 下一记录索引；4 总记录数；5 已接收记录数；6–7 剩余循环（0 持续）；8–9 当前等待毫秒数（超过 65535 时饱和）；10–11 最大记录数 64；12–13 每记录大小 11；14–15 当前 X 偏移 i16；16–17 当前 Y 偏移 i16；18–19 保留。实时监控应同时检查 HID 在线、ready 和 queue；宏回到就绪且队列清空后，再通过画面确认应用效果。

<a id="ota"></a>

## 5. 固件升级与 OTA 协议

### 5.1 蓝牙 OTA

保持被控设备位于第一条 HID 链路，控制端位于第二条链路。确认角色为 2 后上传目标平台的 .ota 包：

```console
python -X utf8 tools/client.py --address BLE_ADDRESS info
python -X utf8 tools/client.py --address BLE_ADDRESS ota OTA_FILE
```

上传期间 HID 命令返回 BUSY。设备提交升级后自动重启；让被控设备先重连，再运行 `info` 核对 APP 版本、profile 和能力位。上传工具报告提交成功只表示 CRC 与 pending 标记已确认，运行版本读回才确认新 APP 已启动。复位附近的最后一次 BLE 读取可能断开，应先重连核对再决定是否重试。

### 5.2 串口 OTA

串口为 UART1、PB12 RX / PB13 TX、115200、8N1、无流控。先关闭占用同一串口的日志工具：

```console
python -X utf8 tools/monitor.py --list
python -X utf8 tools/client.py --port SERIAL_PORT info
python -X utf8 tools/client.py --port SERIAL_PORT ota OTA_FILE
python -X utf8 tools/client.py --port SERIAL_PORT info
```

串口仅支持 OTA、INFO、ABORT，不接收 HID 控制命令。BLE 与 UART 共用互斥升级事务。IAP 正常启动时保留约 500 ms 串口恢复窗口，APP 无效时持续等待串口升级；可运行但不响应的 APP 可在复位恢复窗口中升级。IAP 阶段的状态版本属于 IAP，最终应在 APP 启动后确认目标版本。

### 5.3 提交、中止与恢复

升级依次执行：擦除 staging、接收分块、校验 CRC、持久化 pending、复位。IAP 再校验 staging、复制到 APP、校验目标、更新 active，最后清除 pending。复制中断时可在下次启动重新安装完整 staging。

提交前可用原事务端中止：

```console
python -X utf8 tools/client.py --address BLE_ADDRESS abort
python -X utf8 tools/client.py --port SERIAL_PORT abort
```

READY 表示提交已持久化，此后不能 ABORT。未提交的 BLE 事务在控制端断开时中止；UART 约 30 秒无有效帧后释放事务。没有跨掉电续传偏移，未提交升级需重新 BEGIN。当前不提供新 APP 健康确认、自动回滚、固件签名或防降级；CRC 仅用于完整性校验。

### 5.4 OTA 指令与状态

| 指令 | 小端字节格式 |
| --- | --- |
| INFO | `80` |
| BEGIN | `81 size:u32 crc32:u32 version:u32 board:u32` |
| DATA | `82 offset:u32 data` |
| COMMIT | `83` |
| ABORT | `84` |

size 为 256–221184 字节，且为 4 的倍数；board 固定为 `0x592F0301`。DATA 从偏移 0 顺序写入，每块 4–128 字节并按 4 字节对齐。BLE 每块还需满足 `floor((ATT_MTU - 8) / 4) * 4`，MTU 23 时最大数据块为 12 字节。BEGIN 为 17 字节，可在默认 MTU 下发送。

BEGIN 后等待 RECEIVING，再写 DATA。已写范围内内容相同的重传可接受，偏移跳跃或内容冲突会失败。全部发送后 COMMIT，等待 READY；设备约 1.5 秒后复位。只有事务所有者可修改升级状态。

读取 OTA 特征或接收 UART 回复得到 20 字节：

| 偏移 | 长度 | 含义 |
| --- | --- | --- |
| 0 | 1 | APP：01，IAP：81 |
| 1 | 1 | 最近指令；UART 回复回显请求指令 |
| 2 | 1 | 最近结果码，INFO 不清除失败状态 |
| 3 | 1 | 0 IDLE、1 ERASING、2 RECEIVING、3 VERIFYING、4 READY、5 FAILED |
| 4 | 4 | 已接收数据长度 |
| 8 | 4 | 镜像大小 |
| 12 | 4 | 镜像 CRC32 |
| 16 | 4 | 当前运行 APP 或 IAP 的版本 |

### 5.5 UART 帧与 OTA 文件

```text
A5 5A | payload_length:u16 | payload | CRC32(payload):u32
```

payload 为 OTA 指令，长度 1–133；回复使用相同帧格式，payload 为 20 字节状态。采用发送一帧、等待回复的方式；半帧停顿超过约 100 ms 会重置解析器。CRC 为 IEEE CRC32，与 Python zlib.crc32 一致。客户端应按帧头、长度和 CRC 同步，忽略帧外启动文本。

.ota 文件头为 32 字节，采用 Python `<8s6I` 格式：

| 偏移 | 长度 | 内容 |
| --- | --- | --- |
| 0 | 8 | ASCII `VHIDOTA1` |
| 8 | 4 | board ID |
| 12 | 4 | APP 起始地址：0x1000 |
| 16 | 4 | APP payload 长度 |
| 20 | 4 | APP 版本 |
| 24 | 4 | APP payload CRC32 |
| 28 | 4 | 前 28 字节的 CRC32 |
| 32 | 可变 | APP payload |

升级工具校验头部和内容后，仅发送 APP payload，不把包头写入 APP。.ota 不能作为裸 BIN 烧录，factory 文件也不能作为 APP OTA 数据发送。

<a id="diagnostics"></a>

## 6. 日志与故障排查

### 6.1 采集日志

PB12/PB13 使用 UART1 的备用引脚映射（`R16_PIN_ALTERNATE |= RB_PIN_UART1`），115200 / 8N1，无流控。日志为 ASCII、CRLF 换行，运行心跳间隔为 5 秒。PA8 不再复用为串口接收，而是配置为推挽输出的状态指示灯，固件启动后保持常亮；指示灯默认低电平点亮，若板上为高电平点亮，将 `src/led_ch592.c` 中的 `LED_ACTIVE_LOW` 改为 0 后重新构建。

```console
python -X utf8 tools/monitor.py --list
python -u -X utf8 tools/monitor.py --port SERIAL_PORT --seconds 30
```

先启动采集，再复位板卡。工具只读日志，默认生成带时间戳的文件；`--seconds 0` 可持续采集到 Ctrl+C。日志工具与串口 OTA 不能同时占用一个端口。

收到第一帧合法 UART 协议数据后，普通日志静默至复位，以免混入 OTA 回复；`info` 也会触发静默。因此，查询串口状态后看不到心跳不等于固件停止。异常日志仍优先输出。

### 6.2 输入与连接问题

| 现象 | 检查方法 |
| --- | --- |
| 控制端 role=1 | 断开控制端，让被控设备先连接 |
| hid=0 或 NOT_READY | 检查被控端连接、配对加密与对应报告订阅 |
| 升级后句柄错误或报告不符 | 完整重连并按 UUID 发现服务；描述符变化后删除配对再重新配对 |
| iOS 有指针但点击无效 | 分别检查左右键的 AssistiveTouch 系统映射和指针位置 |
| Android 键盘正常但触摸异常 | 确认 Android profile、刷新配对缓存；系统应识别为直接触摸屏，MT 槽位范围为 0–1 |
| Android 鼠标没有响应 | 检查系统是否建立鼠标输入源；未映射时用原生触摸操作 |
| 触摸坐标偏移 | 核对截图尺寸、旋转方向和系统显示映射 |
| 标点或字符不正确 | 核对主机键盘布局、输入法及中英文模式 |
| 按键或触点保持按下 | 发送 release，检查控制端是否漏发抬起或断连事件尚未完成 |
| 写入返回 BUSY | 等待队列排空或 OTA 结束；确认事务没有被另一端占用 |

界面测试可使用 `tests/android_input.html` 观察点击计数、输入类型、文本与滚动变化。`tools/android_evidence.py` 可通过已授权 ADB 读取截图与输入事件；节点号随系统变化，应按设备名识别。ADB 注入的操作不能作为 BLE HID 成功依据，需结合板端报告、原生输入事件和界面变化。网页滚动接管手势时可能出现 pointercancel，应结合滚动与抬起结果判断。

### 6.3 启动与异常定位

启动阶段依次输出 `[JUMP]`、`[IAP]`、`[APP]`，蓝牙事件输出 `[BLE]`。`hid=FFFF` 或 `ctrl=FFFF` 表示对应槽位未连接。

| 最后阶段或现象 | 定位方向 |
| --- | --- |
| 完全无输出 | 确认烧入完整 factory 且采集覆盖复位；排除合法 UART 帧导致的日志静默 |
| JUMP 输出后没有 IAP entry | 检查 IAP 区内容、镜像入口与跨镜像跳转 |
| IAP 停在 pending / active check | 检查 Flash、CRC 和 staging 安装路径 |
| factory expected 与 actual 不同 | 检查 APP/IAP 是否属于同次构建、烧录内容是否完整 |
| APP invalid; UART recovery waiting | 引导程序拒绝启动无效 APP，使用串口恢复 |
| BLE begin 后没有 BLE OK | 检查紧邻的 SDK 初始化错误、头文件与库版本及 SNV 配置 |
| scheduler enter 后没有 start | 检查 TMOS 调度和 START_EVENT |
| 有心跳但找不到广播 | 检查连接是否已满、广播返回码和 GAP 状态 |

`install result` 为 0 表示无有效 pending，1 表示安装完成，-1 表示安装失败。HardFault/NMI 会记录 `[FAULT APP]`、`[FAULT IAP]` 或 `[FAULT JUMP]` 的 cause、pc、value、sp 后停机。使用对应阶段、对应构建的 ELF 定位异常 PC：

```console
riscv-none-embed-addr2line -e IMAGE_ELF -f -C FAULT_PC
```

将 `IMAGE_ELF` 替换为异常固件的 ELF，`FAULT_PC` 替换为日志地址。构建已使用官方 IAP/OTA 启动文件并检查跨镜像跳转前的机器模式；不要用普通单镜像启动文件替换。修复引导链必须重烧完整 factory，不能仅更新 APP。

<a id="development"></a>

## 7. 源码构建与存储布局

### 7.1 构建

需要 Git、支持 asyncio.to_thread 的 Python、RISC-V GCC 工具链和主机 C 编译器。固件工具链使用 xPack RISC-V GCC 8.2.0 验证。官方 SDK 固定为 `openwch/ch592` 的提交 `a46e0086f1ffb5e5502703970bff94888e67f4cb`：

```console
python -m pip install -r requirements.txt
python -X utf8 tools/fetch_sdk.py
python -X utf8 tools/build.py --profile android --toolchain TOOLCHAIN_BIN
python -X utf8 tools/test.py
```

构建 iOS 时将 `--profile android` 改为 `--profile ios`。应显式选择平台；省略 profile 默认 iOS。工具链也可通过 `WCH_TOOLCHAIN` 或 PATH 提供，工具前缀可用 `--prefix` 指定。

主机测试优先使用 CC / cc / gcc，也可通过 `python tools/test.py --cc HOST_CC` 指定编译器路径。测试依赖已生成的 dist 文件，覆盖角色隔离、输入释放、GATT 权限、描述符、OTA 状态机、断电模拟、UART 帧与报告时序；测试中的模拟上传进度不代表实际板卡升级。

版本在 `include/layout.h` 的 FW_VERSION 中定义，构建同步生成版本信息、OTA 包与清单。复现发布版应使用对应标签及固定 SDK；从 Android 开发分支构建 iOS 不等于复现 iOS 1.0.5。修改源码与文档统一使用 UTF-8 无 BOM，vendor 保留官方原始内容。

### 7.2 存储布局

| 区域 | 起始位置 | 容量 |
| --- | --- | --- |
| Jump | CodeFlash 0x00000 | 4 KB |
| APP | CodeFlash 0x01000 | 216 KB |
| staging | CodeFlash 0x37000 | 216 KB |
| IAP | CodeFlash 0x6D000 | 12 KB |
| active metadata | DataFlash 偏移 0x6000 | 256 B |
| pending metadata | DataFlash 偏移 0x6100 | 256 B |
| BLE SNV | DataFlash 偏移 0x7000 | 2 个官方 SNV 块 |

CodeFlash 擦除按 4 KB，写入按 4 字节对齐。构建检查 APP/IAP 容量与 RAM/栈不重叠。两条连接请求 15–30 ms 间隔、latency 0、6 秒 supervision timeout，最终参数由 Central 决定。PnP 身份沿用官方示例的 WCH vendor 0x07D7 / product 0x0000，未分配独立产品标识。

<a id="licenses"></a>

## 8. 参考资料与第三方许可

固件使用 WCH 官方 SDK 的 BLE 协议栈、启动文件和外设驱动，参考其 MultiCentPeri、HID_Keyboard、HID_Mouse、HID_Touch 和 BackupUpgrade 示例。SDK 保留原版权声明和 WCH 芯片使用条件。

```text
https://github.com/openwch/ch592
vendor/ch592/Datasheet/CH592DS1_EN.PDF
https://developer.apple.com/accessories/Accessory-Design-Guidelines.pdf
https://support.apple.com/en-us/111775
https://source.android.com/docs/core/interaction/input/touch-devices
https://raw.githubusercontent.com/LineageOS/android_frameworks_native/lineage-23.0/services/inputflinger/reader/mapper/MultiTouchInputMapper.cpp
https://raw.githubusercontent.com/torvalds/linux/v4.19/drivers/hid/hid-multitouch.c
```

相对鼠标报告和按钮时序对照了 `onehumanbeing/esp32-iphone-agent` 的 `legacy/hardware/hid_bridge_ble_c3/ble_hid.h`，未复制该项目源码。历史绝对指针实现参考了 `xhoantran/iphone-use` 的提交 `bc0fe36e78ec5a3af095c813574e3a8b7bbea4dd`；相关实现已移除，以下保留其派生工作许可声明：

```text
MIT License

Copyright (c) 2026 NewWave AI

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
