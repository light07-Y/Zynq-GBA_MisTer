$ErrorActionPreference = "Stop"
$VIVADO = "D:\Tools\AMDDesignTools\2025.2.1\Vivado\bin"
$ROOT   = "F:\02_Projects\Zynq-GBA_MisTer-Vivado"
$RTL    = "$ROOT\src\rtl\core\gba_mister\rtl"
$SIM    = "$ROOT\sim"

Set-Location "$SIM\work"

# 清理旧编译产物
if (Test-Path "xsim.dir") { Remove-Item -Recurse -Force "xsim.dir" }

# 预处理 gba_cpu.vhd: 注释掉 translate_off 调试段（is_simu=0 不会用到）
Write-Host "=== Step 0: Pre-process gba_cpu.vhd (remove debug trace) ===" -ForegroundColor Cyan
$cpu_src = Get-Content "$RTL\gba_cpu.vhd" -Raw
$cpu_patched = $cpu_src -replace '(?s)(-- synthesis translate_off.*?-- synthesis translate_on)', '-- [SIM PATCH] debug trace section removed for xsim compatibility'
$cpu_patched | Out-File "$SIM\work\gba_cpu_sim.vhd" -Encoding ascii
Write-Host "  Created gba_cpu_sim.vhd (debug section removed)"

Write-Host "=== Step 1: Compile MEM library VHDL ===" -ForegroundColor Cyan
$mem_files = @(
    "$RTL\SyncRam.vhd",
    "$RTL\SyncRamDual.vhd",
    "$RTL\SyncRamDualByteEnable.vhd",
    "$RTL\SyncRamDualNotPow2.vhd",
    "$RTL\SyncFifo.vhd"
)
foreach ($f in $mem_files) {
    Write-Host "  MEM: $(Split-Path $f -Leaf)"
    & "$VIVADO\xvhdl.bat" --work MEM $f 2>&1 | Select-String -Pattern "ERROR" -SimpleMatch
    if ($LASTEXITCODE -ne 0) { Write-Host "FAILED: $f" -ForegroundColor Red; exit 1 }
}

Write-Host "=== Step 2: Compile MEM library SV ===" -ForegroundColor Cyan
& "$VIVADO\xvlog.bat" -sv --work MEM "$RTL\SyncRamDualByteEnable_core.sv" 2>&1 | Select-String -Pattern "ERROR" -SimpleMatch
if ($LASTEXITCODE -ne 0) { Write-Host "FAILED: SyncRamDualByteEnable_core.sv" -ForegroundColor Red; exit 1 }

Write-Host "=== Step 3: Compile work library packages ===" -ForegroundColor Cyan
$pkg_files = @(
    "$RTL\proc_bus_gba.vhd",
    "$RTL\reg_savestates.vhd",
    "$RTL\reggba_display.vhd",
    "$RTL\reggba_dma.vhd",
    "$RTL\reggba_keypad.vhd",
    "$RTL\reggba_serial.vhd",
    "$RTL\reggba_sound.vhd",
    "$RTL\reggba_system.vhd",
    "$RTL\reggba_timer.vhd"
)
foreach ($f in $pkg_files) {
    Write-Host "  PKG: $(Split-Path $f -Leaf)"
    & "$VIVADO\xvhdl.bat" -L MEM $f 2>&1 | Select-String -Pattern "ERROR" -SimpleMatch
    if ($LASTEXITCODE -ne 0) { Write-Host "FAILED: $f" -ForegroundColor Red; exit 1 }
}

Write-Host "=== Step 4: Compile work library entities ===" -ForegroundColor Cyan
# 用预处理后的 gba_cpu_sim.vhd 替代原始文件
$entity_files = @(
    "$RTL\gba_bios.vhd",
    "$RTL\cache.vhd",
    "$RTL\gba_joypad.vhd",
    "$RTL\gba_serial.vhd",
    "$RTL\gba_reservedregs.vhd",
    "$RTL\gba_timer_module.vhd",
    "$RTL\gba_timer.vhd",
    "$RTL\gba_sound_ch1.vhd",
    "$RTL\gba_sound_ch3.vhd",
    "$RTL\gba_sound_ch4.vhd",
    "$RTL\gba_sound_dma.vhd",
    "$RTL\gba_sound.vhd",
    "$RTL\gba_dma_module.vhd",
    "$RTL\gba_dma.vhd",
    "$RTL\gba_savestates.vhd",
    "$RTL\gba_statemanager.vhd",
    "$RTL\gba_memorymux.vhd",
    "$RTL\gba_gpu_timing.vhd",
    "$RTL\gba_drawer_mode0.vhd",
    "$RTL\gba_drawer_mode2.vhd",
    "$RTL\gba_drawer_mode345.vhd",
    "$RTL\gba_drawer_obj.vhd",
    "$RTL\gba_drawer_merge.vhd",
    "$RTL\gba_gpu_drawer.vhd",
    "$RTL\gba_gpu_colorshade.vhd",
    "$RTL\gba_gpu.vhd",
    "$RTL\gba_cheats.vhd",
    "$RTL\gba_gpioRTCSolarGyro.vhd",
    "$RTL\gba_gpiodummy.vhd",
    "$SIM\work\gba_cpu_sim.vhd",
    "$RTL\gba_top.vhd"
)
foreach ($f in $entity_files) {
    Write-Host "  ENT: $(Split-Path $f -Leaf)"
    & "$VIVADO\xvhdl.bat" -L MEM $f 2>&1 | Select-String -Pattern "ERROR" -SimpleMatch
    if ($LASTEXITCODE -ne 0) { Write-Host "FAILED: $f" -ForegroundColor Red; exit 1 }
}

Write-Host "=== Step 5: Compile XPM + glbl ===" -ForegroundColor Cyan
$VIVADO_DATA = "D:\Tools\AMDDesignTools\2025.2.1\Vivado\data"
& "$VIVADO\xvlog.bat" -sv -L uvm "$VIVADO_DATA\ip\xpm\xpm_memory\hdl\xpm_memory.sv" 2>&1 | Select-String -Pattern "ERROR" -SimpleMatch
if ($LASTEXITCODE -ne 0) { Write-Host "FAILED: xpm_memory.sv" -ForegroundColor Red; exit 1 }
& "$VIVADO\xvlog.bat" "$VIVADO_DATA\verilog\src\glbl.v" 2>&1 | Select-String -Pattern "ERROR" -SimpleMatch
if ($LASTEXITCODE -ne 0) { Write-Host "FAILED: glbl.v" -ForegroundColor Red; exit 1 }

Write-Host "=== Step 6: Compile testbench (SV) ===" -ForegroundColor Cyan
& "$VIVADO\xvlog.bat" -sv -L MEM "$SIM\tb\tb_gba_fullchain.sv" 2>&1 | Select-String -Pattern "ERROR" -SimpleMatch
if ($LASTEXITCODE -ne 0) { Write-Host "FAILED: tb_gba_fullchain.sv" -ForegroundColor Red; exit 1 }

Write-Host "=== Step 7: Elaborate ===" -ForegroundColor Cyan
& "$VIVADO\xelab.bat" -debug off -L MEM -L xpm --timescale "1ns/1ps" -s fullchain_sim work.tb_gba_fullchain work.glbl 2>&1 | Tee-Object -Variable elab_out
$elab_out | Select-String -Pattern "ERROR" -SimpleMatch
if ($LASTEXITCODE -ne 0) { Write-Host "ELABORATE FAILED" -ForegroundColor Red; exit 1 }

Write-Host "=== Step 7: Simulate ===" -ForegroundColor Cyan
& "$VIVADO\xsim.bat" fullchain_sim -runall 2>&1 | Tee-Object -Variable sim_out
$sim_out | Write-Host

Write-Host "=== Done ===" -ForegroundColor Green
