# iOS 1.0.5 冻结记录

用户于 2026-11-10 确认“iOS 验收成功”，要求固定当前版本。分支 `ios/v1.0.5`，标签 `ios-v1.0.5`。本目录保存实际通过 BLE OTA 安装的 iOS APP、同次构建的 factory 文件及调试信息，不重新编译替换本目录的固件。

## 版本身份

| 项目 | 值 |
| --- | --- |
| APP 版本 | `1.0.5` / `0x00010005` |
| Profile | `ios` / Status byte 17 = 1 |
| 能力 | 键盘 + 相对鼠标 / Status byte 18 = 3 |
| 输入报告 | ID 1（8 字节键盘）、ID 2（7 字节鼠标） |
| Report Map | 140 字节 |
| APP 大小 | 156832 字节 |
| APP CRC32 | `304d4e1e` |
| 板号 | `0x592F0301` |
| SDK 提交 | `a46e0086f1ffb5e5502703970bff94888e67f4cb` |
| 工具链 | xPack RISC-V GCC 8.2.0 |

## 验收与限制

- iPhone 11 + 电脑控制端双连接，读回 `role=2 hid=1 controller=1 ready=03 version=1.0.5`。
- 键盘已实际打开搜索、启动 App 和返回主屏幕。
- 相对鼠标移动、左键打开 Safari、按住左键拖动网页有画面证据，用户确认 iOS 验收。
- 左右键分别产生 `buttons=01 → 00`、`buttons=02 → 00` 的 HID 日志；右键的系统动作按 iOS 自定按钮配置判断，不额外声称某一固定右键动作已单独验收。
- 本次 BLE OTA 从 1.0.4 安装到 1.0.5，启动和运行版本均已读回。UART OTA 在此前 1.0.3 → 1.0.4 通过，本次初始请求无回复后改走 BLE，未再次完成 UART 全流程。
- 两个 profile 的目标构建、C 回归和 16 项 Python 测试通过；Android 实机稍后测试，不能按 iOS 验收结果标记 Android 通过。
- 现场 Jump/IAP 仍为 1.0.1；本目录 factory 文件内为本次构建的 1.0.5 Jump/IAP，尚未整体重烧。OTA 只更新 APP。
- 未记录该 iPhone 的系统版本，未完成所有 iOS 16 及后续版本的兼容性矩阵、真实硬件断电测试和运行峰值测试。

原始 `manifest.json` 保留构建时的 `hardware_validated: false`，表示全部平台及整机项目未全部验收；iOS 平台使用验收以上述记录为准。现场截图不上传仓库，避免包含手机个人内容；本机记录位于 `build/ios-v105-*.png`、`build/uart-ios-v105-boot.log` 和 `build/uart-ios-v105-buttons.log`。

## 使用入口

使用步骤见 [IOS_USAGE.md](../../IOS_USAGE.md)，首次烧录、BLE OTA、串口 OTA 与恢复说明见 [OTA.md](../../OTA.md)，自定义控制端按 [PROTOCOL.md](../../PROTOCOL.md) 实现。

`SHA256SUMS` 覆盖本目录冻结的构建文件。后续维护请新建分支和版本，不改写标签或覆盖本目录。Android 在独立分支进行测试，保留本版本作为 iOS 恢复点。
