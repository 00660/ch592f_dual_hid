# iOS 1.0.5 烧录与 OTA 说明

使用冻结版本 `releases/ios-v1.0.5/` 中的文件。BLE OTA 与串口 OTA 使用同一个 APP 升级包；它们只替换 APP，不更新 Jump、IAP 或分区布局。

## 文件选择

| 文件 | 用途 |
| --- | --- |
| `ch592f_3in1_factory.hex` | 首次烧录完整 Jump + APP + IAP，自带地址 |
| `ch592f_3in1_factory.bin` | 同一完整镜像，起始地址 `0x00000000` |
| `ch592f_3in1.ota` | BLE/UART APP 升级包，包含 32 字节包头 |
| `app.bin` / `app.hex` | APP 裸产物，仅供调试；OTA 工具使用 `.ota` |
| `manifest.json` | 版本、板号、SDK 提交、大小、CRC32 与原始构建哈希 |
| `SHA256SUMS` | 冻结文件的 SHA-256 校验值 |
| `app.elf` / `app.map` | 对应该次固件的符号和地址，用于异常定位 |

首次安装，或修改了 Jump/IAP/分区时，使用 WCH 烧录工具写完整 factory HEX。不要把 `.ota` 当作裸 BIN 写入，也不要将 factory 文件传给 APP OTA 命令。

## BLE OTA

1. 先让 iPhone 连接并占用第一条 HID 链路。
2. 电脑通过第二条 controller 链路查询状态，确认 `role=2`。
3. 上传冻结的 `.ota` 包；升级期间 HID 命令会返回 BUSY。
4. 等待设备重启，让 iPhone 先重连，再查询运行版本。

```powershell
python -m pip install -r requirements.txt
python -X utf8 tools/client.py scan
python -X utf8 tools/client.py --address 38:3B:26:3B:7D:55 info
python -X utf8 tools/client.py --address 38:3B:26:3B:7D:55 ota releases/ios-v1.0.5/ch592f_3in1.ota
python -X utf8 tools/client.py --address 38:3B:26:3B:7D:55 info
```

地址以扫描结果为准。升级后预期读回 `version=1.0.5 profile=ios capabilities=0x03`；手机在线且完成订阅时 `ready=0x03`。

本次 1.0.4 → 1.0.5 的 BLE OTA 已完成，串口日志确认 `install result=1`、APP `fw=00010005`，重连读回 1.0.5。Windows 在设备复位前最后一次状态读取可能报连接取消或失效；先重连检查实际版本和启动日志，不能仅凭这次异常判定失败或重复升级。

## 串口 OTA

使用 UART1，PA8 RX、PA9 TX，115200 / 8N1 / 无流控。现场适配器为 COM7，其他机器先枚举确认端口。关闭占用同一端口的日志工具后运行：

```powershell
python -X utf8 tools/monitor.py --list
python -X utf8 tools/client.py --port COM7 info
python -X utf8 tools/client.py --port COM7 ota releases/ios-v1.0.5/ch592f_3in1.ota
python -X utf8 tools/client.py --port COM7 info
```

串口只接受 OTA、INFO、ABORT，不接收键盘或鼠标控制。两种 OTA 不能同时占用事务。

UART OTA 在 1.0.3 → 1.0.4 已实机通过。升级 1.0.5 时一次 UART 初始请求没有有效回复，随后改用 BLE 完成；本次未重新完成 1.0.5 的 UART 上传。若无回复，先检查端口占用、只读日志和 APP 启动状态，保留实际错误，不将其写成已通过。

IAP 正常启动保留约 500 ms 串口恢复窗口；APP 无效时持续等待串口升级。已经能运行但不响应协议的 APP，可在复位窗口中尝试 UART OTA。现场 IAP 为 1.0.1，UART 状态在 IAP 阶段返回它自身的版本；最终应在 APP 启动后读回 1.0.5。

## 日志采集

```powershell
python -u -X utf8 tools/monitor.py --port COM7 --seconds 30 --output build/uart-check.log
```

`monitor.py` 只读，不发送协议帧，输出文件必须不存在。`--seconds 0` 持续采集到 Ctrl+C。串口收到第一帧合法 OTA/INFO 后会将普通文本日志静默至下次复位，以免混入二进制回复；先运行 `info` 后看不到心跳不等于设备未运行。异常日志仍优先输出。

## 升级过程与恢复边界

固件依次擦 staging、接收分块、校验 CRC、写 pending，然后约 1.5 秒后复位。IAP 校验 staging，复制并校验 APP，更新 active，再清 pending。READY 表示提交已持久化，此后不能 ABORT。

未提交的 BLE 事务会在控制端断开时中止；UART 约 30 秒无有效协议帧会释放事务。没有跨掉电续传偏移，未提交升级需重新 BEGIN。复制阶段的断电重装已通过主机模拟，真实硬件断电测试未完成。本版本没有新 APP 健康确认或自动回滚，CRC 用于完整性检查。

```powershell
python -X utf8 tools/client.py --address 38:3B:26:3B:7D:55 abort
python -X utf8 tools/client.py --port COM7 abort
```

## 冻结产物核对

本次 APP 长度为 `156832`，CRC32 为 `304d4e1e`；`.ota` 的 SHA-256 为：

```text
e0de158744897c5e2e739298d86cb1f6d3c65e2af719338f85ca23ecf878c269
```

```powershell
Get-FileHash -Algorithm SHA256 releases/ios-v1.0.5/ch592f_3in1.ota
```

后续 Android 构建会覆盖 `dist/`。需要恢复当前 iOS 时始终使用冻结目录中的 `.ota`，保持已验收字节不变。
