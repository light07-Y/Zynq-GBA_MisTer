# XInput/Xbox360 输入模块说明（中文）

## 1. 目标

本目录提供一个面向 `8BitDo Ultimate 2 Wireless PC` 接收器的主机侧输入解析模块，重点是：

- 复用 Xbox360/XInput 风格 `interrupt in` 报告格式
- 将报告映射到 GBA 10 键位（`A/B/Select/Start/Right/Left/Up/Down/R/L`）
- 保持与上层 `App`/`Runtime` 解耦，便于后续接入任意 USB Host 后端

## 2. 当前文件

- `Inc/ps_xinput_xbox360.h` + `Src/ps_xinput_xbox360.c`
  - 协议常量、按钮位定义
  - XInput 输入报告解析
  - 震动/LED 输出报告构造
- `Inc/ps_app_input.h` + `Inc/ps_app_input_context.h` + `Src/ps_app_input.c`
  - 输入状态机
  - USB 连接/断开事件入口
  - 原始报告更新入口
  - GBA 键位映射逻辑

## 3. 与 USB Host 的对接方式

你现有的 USB Host 层（无论是 CherryUSB 还是你自己的 host 栈）只需要在合适时机调用以下接口：

1. 枚举到目标接口后：
   - 调用 `PsAppInput_OnUsbXInputAttached(...)`
2. 收到 interrupt IN 报告后：
   - 调用 `PsAppInput_OnInterruptInReport(...)`
3. 设备断开后：
   - 调用 `PsAppInput_OnUsbDetached(...)`

> 另外保留了 `PsAppInput_BackendPoll()` 弱符号，后续你也可以在别的 `.c` 文件里覆盖它，让 `PsAppInput_Service()` 主动轮询你的 host 后端。

## 4. 默认 GBA 映射策略

- D-Pad -> GBA 方向键
- Left Stick（超过死区）-> GBA 方向键
- Back -> Select
- Start -> Start
- LB 或 LT(>=阈值) -> L
- RB 或 RT(>=阈值) -> R
- 默认 `PS_APP_INPUT_MAP_AB_BY_POSITION=1`：
  - GBA A <= XInput B（右侧按键）
  - GBA B <= XInput A（下侧按键）

可通过 `Common/Inc/ps_project_config.h` 调整：

- `PS_APP_INPUT_MAP_AB_BY_POSITION`
- `PS_APP_INPUT_LSTICK_DEADZONE`
- `PS_APP_INPUT_TRIGGER_THRESHOLD`

## 5. 调试命令

串口控制台新增：

- `input status`
- `input inject <hex>`
- `input detach`

其中 `input inject` 可在 Host 后端尚未联通时做离线验证（将抓包得到的报告十六进制注入解析链路）。
