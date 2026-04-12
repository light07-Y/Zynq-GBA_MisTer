# run_tb.ps1 — 使用 Vivado xsim 运行 tb_hdmi_black_screen 仿真
# 用法: powershell -File sim\run_tb.ps1

$ErrorActionPreference = "Stop"
$simDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$projRoot = Split-Path -Parent $simDir

# 源文件
$srcFiles = @(
    "$projRoot\src\rtl\common\fb_ddr_arbiter.sv",      # 含 gba_frame_capture_bram
    "$projRoot\src\rtl\common\axi_lite_ctrl_regs.sv",   # 含 axi_lite_ctrl_regs
    "$simDir\tb_hdmi_black_screen.sv"
)

$workDir = "$simDir\xsim_work"
if (Test-Path $workDir) { Remove-Item -Recurse -Force $workDir }
New-Item -ItemType Directory -Path $workDir | Out-Null

Write-Host "=== [1/3] xvlog 编译 ===" -ForegroundColor Cyan
$xvlogArgs = @("--sv", "--work", "work=$workDir\work", "--log", "$workDir\xvlog.log")
foreach ($f in $srcFiles) {
    $xvlogArgs += $f
}
& xvlog @xvlogArgs
if ($LASTEXITCODE -ne 0) {
    Write-Host "xvlog 编译失败！查看 $workDir\xvlog.log" -ForegroundColor Red
    exit 1
}

Write-Host "=== [2/3] xelab 链接 ===" -ForegroundColor Cyan
& xelab work.tb_hdmi_black_screen `
    --debug off `
    --snapshot tb_snap `
    --lib "work=$workDir\work" `
    --log "$workDir\xelab.log"
if ($LASTEXITCODE -ne 0) {
    Write-Host "xelab 链接失败！查看 $workDir\xelab.log" -ForegroundColor Red
    exit 1
}

Write-Host "=== [3/3] xsim 运行 ===" -ForegroundColor Cyan
& xsim tb_snap `
    --runall `
    --log "$workDir\xsim.log"

Write-Host "=== 仿真完成，日志: $workDir\xsim.log ===" -ForegroundColor Green
