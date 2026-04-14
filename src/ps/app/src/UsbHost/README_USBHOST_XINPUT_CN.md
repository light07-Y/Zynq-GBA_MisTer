# Zynq USB Host + 8BitDo XInput 适配说明（中文）

## 1. 目标

本适配在 `ps/app` 内完成了：

1. CherryUSB Host 最小子集移植（EHCI + Hub + Xbox Class + FreeRTOS OSAL）。
2. Zynq-7020 PS USB Host 低层初始化（主机模式 + IRQ 接入）。
3. `8BitDo Ultimate 2 Wireless PC` 接收器（XInput/Xbox360 风格）输入上报接入 `Input` 模块。
4. GBA 键位映射沿用 `Input` 现有解析器；新增控制台震动测试入口。

## 2. 关键目录

- `ThirdParty/CherryUSB/`：移植后的 CherryUSB 最小代码。
- `UsbHost/Inc/ps_app_usbhost*.h`：USB Host 模块接口与上下文。
- `UsbHost/Src/ps_app_usbhost.c`：Host 初始化、IRQ、xbox class 回调、报告转发、震动输出。

## 3. 运行时链路

1. `PsAppRuntime_InitSystem()` 中调用 `PsAppUsbHost_Init()`。
2. CherryUSB Hub 线程启动后，检测到设备并枚举。
3. 匹配到 xbox class（`FF/5D/01` + VID/PID）后进入 `usbh_xbox_run()`。
4. `interrupt in` 报告在 `PsAppUsbHost_IntInComplete()` 中转发到：
   - `PsAppInput_OnInterruptInReport()`
5. `Input` 模块完成 XInput 报告解析与 GBA 按键映射。
6. Runtime 每个 tick 将映射后的键值提交到 `config->keys`。

## 4. 控制台命令

- `usb status`
  - 查看 host 初始化状态、接口信息、收包统计、错误统计。
- `usb rumble <large 0-255> <small 0-255>`
  - 发送 XInput 风格震动输出包（优先验证 OUT 通道）。
- 既有命令 `input status`/`input inject` 仍可用于解析链路验证。

## 5. 已完成与未完成

已完成：

1. 编译通过（`app.elf`）。
2. Host 栈、IRQ、xbox class、输入映射、震动发送路径全部连通到应用代码。

未在当前提交内自动验证（需上板）：

1. 实机插接收器后的枚举时序与端口供电状态。
2. 实机 `interrupt in` 连续收包稳定性。
3. 实机 `usb rumble` 的设备响应一致性。

## 6. 推荐上板验证步骤

1. 上电后串口执行 `usb status`，确认 `init=1`、`irq=1`。
2. 插入接收器并连接手柄，重复 `usb status`，确认 `dev=1`、`xbox=1`、`in_ok` 持续增长。
3. 按键观察 `[INPUT] gba_keys=...` 输出是否符合预期。
4. 执行 `usb rumble 120 60`，确认手柄震动反馈。
