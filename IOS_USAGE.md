# iOS 1.0.5 使用说明

本版本于 2026-11-10 由用户确认 iOS 验收成功。固件提供蓝牙键盘和相对鼠标，使用 AssistiveTouch 完成点击、拖动等操作。iOS 不提供本工程的原生触摸屏或绝对坐标指针接口。

## 固定版本

| 项目 | 值 |
| --- | --- |
| Git 分支 | `ios/v1.0.5` |
| Git 标签 | `ios-v1.0.5` |
| 固件版本 | `1.0.5` / `0x00010005` |
| 广播名称 | `CH592F-3IN1` |
| 冻结目录 | `releases/ios-v1.0.5/` |
| HID 输入报告 | 键盘 ID 1、相对鼠标 ID 2 |
| 正常状态 | `role=2 hid=1 controller=1 ready=0x03 profile=ios capabilities=0x03` |

目标使用范围为 iOS 16 及后续版本，本次实际验收设备为 iPhone 11，未记录其系统版本号；本次结果不能替代所有系统版本、机型的兼容性验证。Android 稍后进行实机测试。

## 配对与连接顺序

1. 首次使用时烧录冻结目录内的 `ch592f_3in1_factory.hex`；已安装本工程的设备可按 [OTA.md](OTA.md) 升级。
2. 先在 iPhone 的“设置 → 蓝牙”中配对并连接 `CH592F-3IN1`。第一条连接固定为 HID 被控端。
3. 打开“设置 → 辅助功能 → 触控 → 辅助触控”。
4. 再让电脑或其他控制设备连接 GATT。第二条连接固定为 controller，负责发送控制命令和 BLE OTA。
5. 在电脑查询状态，确认角色为 2、`hid=1`、`ready=0x03`，再开始操作。

两端已绑定时也要注意自动重连顺序。电脑先连接会占 HID 槽位；应断开电脑，让手机先连后再启动控制工具。断开另一端不会使已连接设备交换角色；空出的槽位由后续连接补位。

## 左右键的系统配置

进入“辅助触控 → 设备”，选择本设备后进入“自定按钮”（具体文案随系统版本变化）。若设备列表未显示，按系统界面进入蓝牙设备项确认连接。

| 固件按钮位 | 物理含义 | iOS 配置及验证 |
| --- | --- | --- |
| `01` | 左键 / 按钮 1 | 映射为“单点轻按”或对应的轻点动作，用于点击和按住拖动 |
| `02` | 右键 / 按钮 2 | 根据需要配置为打开辅助触控菜单、主屏幕或其他动作，按所选动作验收 |
| `04` | 中键 / 按钮 3 | 可按需要配置，未单独实机验收 |

固件分别上报各按钮的按下和释放。iOS 可以把左右键映射成不同动作，因此右键不一定与左键产生同一种界面效果。验证时分别检查两颗按钮的映射、按下/释放日志和画面；单键无响应时先检查系统配置与连接状态，再排查固件。

## 电脑控制

在项目目录安装依赖并扫描设备：

```powershell
python -m pip install -r requirements.txt
python -X utf8 tools/client.py scan
```

将以下地址替换为扫描结果。当前现场地址为 `38:3B:26:3B:7D:55`；其他板卡地址以实际结果为准。

```powershell
python -X utf8 tools/client.py --address 38:3B:26:3B:7D:55 info
python -X utf8 tools/client.py --address 38:3B:26:3B:7D:55 mouse 5 5
python -X utf8 tools/client.py --address 38:3B:26:3B:7D:55 click --button 1
python -X utf8 tools/client.py --address 38:3B:26:3B:7D:55 click --button 2
python -X utf8 tools/client.py --address 38:3B:26:3B:7D:55 drag 0 -100 --duration 600
python -X utf8 tools/client.py --address 38:3B:26:3B:7D:55 mouse 0 0 --wheel -1
python -X utf8 tools/client.py --address 38:3B:26:3B:7D:55 key 11 --modifiers 8
python -X utf8 tools/client.py --address 38:3B:26:3B:7D:55 release
```

`mouse x y` 的正方向为向右、向下，数值是相对位移；受 iOS 速度和加速度影响，不能直接当作屏幕像素。建议先用小位移观察指针，再调整到图标中心。`click` 在当前指针位置按下后抬起；`drag` 持续按住左键移动后释放。`key 11 --modifiers 8` 为 Command+H，本次用于返回主屏幕。

每次 CLI 操作结束会释放输入并断开电脑控制连接，手机的 HID 连接保持。需要连续操作时使用自己的长连接 GATT 客户端，命令格式见 [PROTOCOL.md](PROTOCOL.md)。显式释放会清队列，正常结束前应先等待队列归零。

iOS 构建不接受 `touch` / `swipe`，工具会提示使用 `mouse` / `click` / `drag`。旧 `touch-mode`、`--mode ios`、绝对指针 ID 5 已删除。

## 状态与排查

| 现象 | 检查内容 |
| --- | --- |
| `role=1` | 电脑抢先占用 HID 槽位；按手机先连的顺序重连 |
| `hid=0` | 手机 HID 链路未在线 |
| `ready=01` | 键盘就绪，鼠标尚未订阅；检查辅助触控、配对与缓存 |
| `ready=03` 但点击不符合预期 | 检查指针位置及左右键的系统动作；不能仅据 ready 判断 UI 成功 |
| 升级后 GATT 属性消失或句柄错误 | 完整重连并按 UUID 重新发现服务；必要时忽略该设备后重新配对 |
| 按键好像保持按下 | 发送 `release`，同时检查控制端是否已断开 |

PA8/PA9 日志为 UART1、115200、8N1；日志采集命令见 [OTA.md](OTA.md)。固件在鼠标按钮变化时输出 `buttons=01`、`02` 和释放 `00`，便于把固件上报与 iOS 映射分别核对。
