# 项目维护约定

- 回复使用简体中文；新增和修改的文本使用 UTF-8 无 BOM。
- 禁止参考本机其他 CH592 工程。仅使用当前工程、本次下载的固定官方 SDK 和用户授权的网上参考。
- `ios/v1.0.5` 为 2026-11-10 用户确认验收的 iOS 版本；标签为 `ios-v1.0.5`。
- `releases/ios-v1.0.5/` 保存当时实际安装的固件和校验值，不用后续构建结果覆盖。
- iOS 仅保留键盘与相对鼠标；Android 保留原生单点触控，目前仅构建和主机回归通过，实机测试待进行。
- 后续 Android 测试与修复使用独立分支，不能改写已验收的 iOS 标签或冻结产物。
- 验证左右键时先检查 iOS AssistiveTouch 各按钮的系统映射；单键无画面响应不能直接归因为固件故障。
- 代码手工编辑使用 apply_patch，避免回退用户其他改动。
- 构建：`python -X utf8 tools/build.py --profile ios` 或 `--profile android`。生成的 `dist/` 会被下次构建覆盖。
- 回归：`python -X utf8 tools/test.py`，需要官方 SDK、主机 C 编译器以及已生成的 `dist/`。
- 使用与升级步骤分别见 `IOS_USAGE.md`、`OTA.md`，协议见 `PROTOCOL.md`。
