# 项目维护约定

- 本副本为硬件引脚变体：UART1 改用 PB12(RX)/PB13(TX)，PA8 改为推挽输出的常亮指示灯。日志、串口 OTA 与 `tools/monitor.py` 均按 PB12/PB13 描述，不要改回 PA8/PA9 默认映射。
- PA8 指示灯默认低电平点亮，极性由 `src/led_ch592.c` 的 `LED_ACTIVE_LOW` 决定；`led_init()` 须在 APP 与 IAP 的 `main()` 中最早调用，保证复位后立即常亮。
- 回复使用简体中文；新增和修改的文本使用 UTF-8 无 BOM。
- 禁止参考本机其他 CH592 工程。仅使用当前工程、本次下载的固定官方 SDK 和用户授权的网上参考。
- `ios/v1.0.5` 为 2026-11-10 用户确认验收的 iOS 版本；标签为 `ios-v1.0.5`。
- `releases/ios-v1.0.5/` 保存当时实际安装的固件和校验值，不用后续构建结果覆盖。
- iOS 仅保留键盘与相对鼠标。Android 1.0.10 已在 M2006C3LI / Android 16 实测通过单点点击、连续滑动、打开 App、键盘输入与控制端断连释放；此机型的相对鼠标未映射为输入源，不能标记通过。
- Android 开发分支为 `android/dev`，冻结分支为 `android/v1.0.10`，标签为 `android-v1.0.10`；实际安装的 APP 及同次构建文件保存在 `releases/android-v1.0.10/`。
- Android Contact Count Maximum 的 Feature 返回值和 Logical Maximum 均须为 2，使主机分配两个 MT 槽位；控制协议仍为单点 id 0，不可将 Feature 改回 1 或删除 Contact ID / Count。
- 实机验收须结合 CH592F 对应输入节点、串口日志及界面变化；ADB 准备页面或注入输入不算 HID 验收，ready mask 也不代表系统已映射所有输入源。
- 后续 Android 测试与修复使用独立分支，不能改写已验收的 iOS 标签或冻结产物。
- 验证左右键时先检查 iOS AssistiveTouch 各按钮的系统映射；单键无画面响应不能直接归因为固件故障。
- 代码手工编辑使用 apply_patch，避免回退用户其他改动。
- 构建：`python -X utf8 tools/build.py --profile ios` 或 `--profile android`。生成的 `dist/` 会被下次构建覆盖。
- 回归：`python -X utf8 tools/test.py`，需要官方 SDK、主机 C 编译器以及已生成的 `dist/`。
- 用户说明统一维护在 `README.md`，按章节组织使用、协议、OTA、构建、排障和许可；不要再拆分平台说明或发布流水账 Markdown。
- 用户说明使用通用参数与可复用步骤，不包含本机路径、实际 BLE 地址、串口号、ADB 地址、调试时间线或会话经过。仅保留必要的能力与兼容边界。
- `AGENTS.md` 仅存开发约束；发布目录保存固件、机器可读清单和校验值。旧版本标签中的文档保持其历史状态，当前分支统一从 README 阅读。
