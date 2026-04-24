# Zynq USB Host + XInput 适配说明（中文）

## 1. 目标

本适配在 `ps/app` 内完成了：

1. CherryUSB Host 最小子集移植（EHCI + Hub + Xbox Class + FreeRTOS OSAL）。
2. Zynq-7020 PS USB Host 低层初始化（主机模式 + IRQ 接入）。
3. `8BitDo Ultimate 2 Wireless PC` 接收器（XInput/Xbox360 风格）输入上报接入 `Input` 模块。
4. 新增未知厂商 XInput 紧急兜底：当设备表现为 `vendor-specific(interface class 0xFF)` 且 `interface=0` 时，允许 xbox class 先绑定并进入输入链路。
5. 新增 `057E:2009` Switch HID 紧急兜底，覆盖部分接收器切到 Switch 形态时的接入。
6. GBA 键位映射沿用 `Input` 现有解析器；新增控制台震动测试入口。

## 2. 关键目录

- `ThirdParty/CherryUSB/`：移植后的 CherryUSB 最小代码。
- `UsbHost/Inc/ps_app_usbhost*.h`：USB Host 模块接口与上下文。
- `UsbHost/Src/ps_app_usbhost.c`：Host 初始化、IRQ、xbox class 回调、报告转发、震动输出。

## 3. 运行时链路

1. `PsAppRuntime_InitSystem()` 中调用 `PsAppUsbHost_Init()`。
2. CherryUSB Hub 线程启动后，检测到设备并枚举。
3. 匹配到 xbox class 后进入 `usbh_xbox_run()`，当前支持四类绑定路径：
   - `known 8BitDo`
   - `standard XInput`（FF/5D/01）
   - `vendor XInput fallback`（紧急兜底）
   - `switch HID fallback`（057E:2009）
4. `interrupt in` 报告在 `PsAppUsbHost_IntInComplete()` 中转发到：
   - `PsAppInput_OnInterruptInReport()`
5. `Input` 模块完成 XInput 报告解析与 GBA 按键映射。
6. Runtime 每个 tick 将映射后的键值提交到 `config->keys`。

## 4. 控制台命令

- `usb status`
  - 查看 host 初始化状态、接口信息、收包统计、错误统计。
- `xdiag`（或 `usb diag`）
  - 一键输出可回传的 XInput 诊断包（USB 状态 + Input 状态 + 最近报告信息）。
  - 含 `sticky` 粘性诊断字段：即使当前已断开，也保留最近一次设备/事件/首包痕迹。
- `057E:2009` 进入 `switch HID fallback` 后会自动发送 Switch HID 启动序列，尝试启用 `0x30` 标准输入报告。
- `usb rumble <large 0-255> <small 0-255>`
  - 发送 XInput 风格震动输出包（优先验证 OUT 通道）。
- 既有命令 `input status`/`input inject` 仍可用于解析链路验证。
- 新增首包日志会打印 `len + 前16字节raw`，用于远程分析未知报告格式。

## 5. G30S TE 现场使用建议

- 接收器插入后，先按手柄 `View + Menu` 切到 `XInput` 模式。
- 建议模式切换后重插接收器，再观察串口是否出现：
  - `XBOX bind mode=...`
  - `xbox run ...`
  - `first input report len=... raw=...`
- 若仍无法控制菜单，请回传：
  - `usb status`
  - `input status`
  - 首包 `raw` 日志
  便于继续做针对该固件版本的精确解析。

## 6. 热插拔鲁棒性策略

为减少“插上不识别 / 拔掉后逻辑未释放 / 抖动导致反复复位”，`UsbHost` 侧新增了行业常见的分层策略：

1. 物理层去抖：对 `PORTSC.CCS` 的连接/断开分别做稳定窗口确认，再触发后续动作。
2. 枚举观察窗口：检测到稳定连接后，先进行 root hub 扫描与观察，不立即做端口复位。
3. 分级恢复 + 退避：超时仍未枚举时才执行 `PortReset`，并采用指数退避冷却，避免抖动期反复 reset。
4. 断开兜底：若物理层已稳定断开但上层事件漏报，超时后主动执行逻辑 detach，防止按键状态“卡住”。

可在 `Common/Inc/ps_project_config.h` 调整阈值：

- `PS_APP_USBHOST_CONNECT_DEBOUNCE_TICKS`
- `PS_APP_USBHOST_DISCONNECT_DEBOUNCE_TICKS`
- `PS_APP_USBHOST_ENUM_OBSERVE_TICKS`
- `PS_APP_USBHOST_SCAN_RETRY_TICKS`
- `PS_APP_USBHOST_STALE_DETACH_TICKS`
- `PS_APP_USBHOST_ENUM_RETRY_COOLDOWN_BASE_TICKS`
- `PS_APP_USBHOST_ENUM_RETRY_COOLDOWN_MAX_TICKS`
