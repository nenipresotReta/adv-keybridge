# ADV KeyBridge

**For Cardputer ADV.** 基于 ZMK 的 USB HID 转 BLE HID 键盘桥接固件，支持外接 USB 键盘与 ADV 内置键盘输入。

![ADV KeyBridge on Cardputer ADV](assets/cardputer-adv.jpg)

[下载固件](https://github.com/nenipresotReta/adv-keybridge/releases/latest) · [版本记录](https://github.com/nenipresotReta/adv-keybridge/releases)

基于 ZMK / Zephyr，支持 USB 外接键盘、媒体键和五个蓝牙配对槽。ADV 的屏幕用来查看连接状态、管理槽位、调整显示，以及观察电池电压曲线。

## 开始使用

1. 开机后，在电脑、平板或手机的蓝牙设置中连接 **ADV KeyBridge**。屏幕显示 `CONNECTED` 后即可输入。
2. 使用外接键盘时，在 ADV 上按 **Fn+U** 开启 USB Host，再把键盘接到 ADV 的 USB-C 接口。
3. 再按 **Fn+U** 关闭 USB Host，继续使用内置键盘。外接键盘的功能取决于其 USB HID 报告，已支持常规按键和媒体控制。

外接键盘由 ADV 供电，会增加耗电。SD 卡用于保存日志和电压历史，键盘功能不依赖 SD 卡。

## 操作

以下快捷键均在 **ADV 内置键盘**上操作。

| 按键 | 功能 |
| --- | --- |
| Fn+U | 开启 / 关闭 USB Host |
| Fn+I | 打开设置 |
| Fn+Q/W/E/R/T | 按主页显示顺序选择第 1–5 个已启用槽位 |
| G0 短按 | 主界面切换槽位；设置或曲线页返回；熄屏时仅唤醒 |
| G0 长按约 1.5 秒，松开后再短按 | 主界面确认清除当前槽的配对 |

清除配对后，也请在电脑或手机端忽略旧设备，再重新配对。

### 设置

**W/S** 上下循环滚动，**A/D** 调整，**Enter** 确认，**Backspace / G0** 返回。

| 选项 | 功能 |
| --- | --- |
| BRIGHTNESS | 1%、5%、10%，20%–100% 每档增加 10% |
| SLEEP TIME | 10s、30s、1min、5min、10min、30min、60min、INF；INF 表示不自动熄屏 |
| SCREEN OFF | 立即熄屏，键盘与蓝牙继续运行；按 G0 唤醒 |
| SLOTS | A/D 选择 P1–P5，Enter 开关；绿点开启，灰点关闭 |
| BATTERY | 查看电压曲线与时间估算 |

推荐关闭不使用的槽位以获得最佳的设备切换体验。
关闭槽位会隐藏入口并断开对应连接，配对记录仍保留；至少保留一个槽位。快捷键随显示顺序排列，例如只开启 P1、P3 时，Fn+Q 选择 P1，Fn+W 选择 P3。

## 键位

![基础层与 Fn 层键位](assets/keymap.png)

## 刷写

从 [Releases](https://github.com/nenipresotReta/adv-keybridge/releases/latest) 下载 `.bin` 和 `SHA256SUMS`。当前仅面向 **Cardputer ADV**，使用从 **0x0** 写入的完整原生镜像，不能作为 M5Launcher 应用安装；直接刷入会覆盖 Launcher。

将 ADV 连接电脑；无法进入下载模式时，按住 G0 再插入 USB，随后松开。使用支持 ESP32-S3 的刷写工具，选择固件并将写入地址设为 `0x0`。正常升级本项目时无需全片擦除，以保留配对与设置。

刷写校验完成后，若设备仍停在下载模式，拔掉 USB，关机几秒，再松开 G0 用电池开机。

## 开发

依赖版本固定在 [west.yml](west.yml)，构建入口为 [tools/build.sh](tools/build.sh)，本地测试入口为 [tools/test.sh](tools/test.sh)。构建脚本会应用本项目针对蓝牙、USB 和 ESP32-S3 的适配。

## License

[MIT](LICENSE). Third-party components retain their [original licenses](LICENSES/README.md).
