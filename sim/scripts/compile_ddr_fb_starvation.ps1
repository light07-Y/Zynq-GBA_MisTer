$ErrorActionPreference = "Stop"
$VIVADO = "D:\Tools\AMDDesignTools\2025.2.1\Vivado\bin"
$ROOT   = "F:\02_Projects\Zynq-GBA_MisTer-Vivado"
$RTL    = "$ROOT\src\rtl\common"
$SIM    = "$ROOT\sim"

Set-Location "$SIM\work"

Write-Host "=== DDR FB Starvation Simulation ===" -ForegroundColor Cyan

$files = @(
    "$RTL\ddram_mux.sv",
    "$RTL\ddr_axi_backend.sv",
    "$SIM\tb\axi_mem_model_random.sv",
    "$SIM\tb\tb_ddr_fb_starvation.sv"
)

foreach ($f in $files) {
    Write-Host "  Compile: $(Split-Path $f -Leaf)"
    & "$VIVADO\xvlog.bat" -sv $f 2>&1 | Tee-Object -Variable out | Out-Null
    if ($LASTEXITCODE -ne 0) {
        $out | Write-Host
        Write-Host "FAILED: $f" -ForegroundColor Red
        exit 1
    }
}

Write-Host "=== Elaborate ===" -ForegroundColor Cyan
& "$VIVADO\xelab.bat" -debug off --timescale "1ns/1ps" -s ddr_fb_starvation_sim work.tb_ddr_fb_starvation 2>&1 | Tee-Object -Variable elab_out
if ($LASTEXITCODE -ne 0) {
    $elab_out | Write-Host
    Write-Host "ELABORATE FAILED" -ForegroundColor Red
    exit 1
}

Write-Host "=== Simulate ===" -ForegroundColor Cyan
& "$VIVADO\xsim.bat" ddr_fb_starvation_sim -runall 2>&1 | Tee-Object -Variable sim_out
$sim_out | Write-Host

Write-Host "=== Done ===" -ForegroundColor Green
