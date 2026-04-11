$ErrorActionPreference = "Stop"
$VIVADO = "D:\Tools\AMDDesignTools\2025.2.1\Vivado\bin"
$ROOT   = "F:\02_Projects\Zynq-GBA_MisTer-Vivado"
$RTL    = "$ROOT\src\rtl\common"
$SIM    = "$ROOT\sim"

Set-Location "$SIM\work"

Write-Host "=== FB Write Chain Simulation ===" -ForegroundColor Cyan

# Step 1: 编译 DUT 模块 (SystemVerilog)
Write-Host "=== Step 1: Compile DUT modules ===" -ForegroundColor Cyan
$dut_files = @(
    "$RTL\fb_ddr_arbiter.sv",
    "$RTL\ddram_mux.sv",
    "$RTL\ddr_axi_backend.sv"
)
foreach ($f in $dut_files) {
    Write-Host "  DUT: $(Split-Path $f -Leaf)"
    & "$VIVADO\xvlog.bat" -sv $f 2>&1 | Select-String -Pattern "ERROR" -SimpleMatch
    if ($LASTEXITCODE -ne 0) { Write-Host "FAILED: $f" -ForegroundColor Red; exit 1 }
}

# Step 2: 编译 Testbench
Write-Host "=== Step 2: Compile testbench ===" -ForegroundColor Cyan
& "$VIVADO\xvlog.bat" -sv "$SIM\tb\tb_fb_write_chain.sv" 2>&1 | Select-String -Pattern "ERROR" -SimpleMatch
if ($LASTEXITCODE -ne 0) { Write-Host "FAILED: tb_fb_write_chain.sv" -ForegroundColor Red; exit 1 }

# Step 3: Elaborate
Write-Host "=== Step 3: Elaborate ===" -ForegroundColor Cyan
& "$VIVADO\xelab.bat" -debug off --timescale "1ns/1ps" -s fb_chain_sim work.tb_fb_write_chain 2>&1 | Tee-Object -Variable elab_out
$elab_out | Select-String -Pattern "ERROR" -SimpleMatch
if ($LASTEXITCODE -ne 0) { Write-Host "ELABORATE FAILED" -ForegroundColor Red; exit 1 }

# Step 4: Simulate
Write-Host "=== Step 4: Simulate ===" -ForegroundColor Cyan
& "$VIVADO\xsim.bat" fb_chain_sim -runall 2>&1 | Tee-Object -Variable sim_out
$sim_out | Write-Host

Write-Host "=== Done ===" -ForegroundColor Green
