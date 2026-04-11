# run_ch1_sim.tcl: 在 Vivado xsim 中运行 ch1 读链路仿真
# 用法 (命令行):
#   cd <项目根目录>/sim/work
#   xvhdl -work mem ../tb/SyncRamDual_sim.vhd
#   xvhdl -work work ../../src/rtl/core/gba_mister/rtl/cache.vhd
#   xvlog -sv ../../src/rtl/common/ddram_mux.sv
#   xvlog -sv ../../src/rtl/common/ddr_axi_backend.sv
#   xvlog -sv ../tb/axi_mem_model.sv
#   xvlog -sv ../tb/tb_ch1_read_chain.sv
#   xelab tb_ch1_read_chain -relax -debug typical -L mem -s ch1_sim
#   xsim ch1_sim -runall -log sim_ch1_read.log
#
# 或在 Vivado Tcl Console 中: source sim/scripts/run_ch1_sim.tcl

set proj_root [file normalize [file join [file dirname [info script]] ../..]]
set sim_dir    "$proj_root/sim"
set tb_dir     "$sim_dir/tb"
set rtl_dir    "$proj_root/src/rtl"
set rom_hex    "$sim_dir/rom_data/rom_first_4k.hex"
set work_dir   "$sim_dir/work"

puts "=== ch1 读链路仿真 ==="
puts "项目根目录: $proj_root"
puts "ROM hex:    $rom_hex"

# 创建工作目录
file mkdir $work_dir

# 切换到工作目录（仿真输出和 $readmemh 在此查找文件）
cd $work_dir

# 复制 ROM hex 到工作目录
file copy -force $rom_hex $work_dir/rom_first_4k.hex

# ========== 编译 ==========
puts "\n--- 编译 VHDL: SyncRamDual (仿真行为级, mem 库) ---"
xvhdl -work mem "$tb_dir/SyncRamDual_sim.vhd" \
    -log compile_mem.log 2>&1

puts "\n--- 编译 VHDL: cache (work 库) ---"
xvhdl -work work "$rtl_dir/core/gba_mister/rtl/cache.vhd" \
    -log compile_cache.log 2>&1

puts "\n--- 编译 SystemVerilog ---"
xvlog -sv "$rtl_dir/common/ddram_mux.sv" \
    -log compile_ddram_mux.log 2>&1

xvlog -sv "$rtl_dir/common/ddr_axi_backend.sv" \
    -log compile_backend.log 2>&1

xvlog -sv "$tb_dir/axi_mem_model.sv" \
    -log compile_axi_mem.log 2>&1

xvlog -sv "$tb_dir/tb_ch1_read_chain.sv" \
    -log compile_tb.log 2>&1

# ========== 链接 ==========
puts "\n--- 链接 (elaborate) ---"
xelab tb_ch1_read_chain \
    -relax -debug typical \
    -L mem \
    -s ch1_sim \
    -log elaborate.log 2>&1

# ========== 运行仿真 ==========
puts "\n--- 运行仿真 ---"
xsim ch1_sim \
    -runall \
    -log sim_ch1_read.log 2>&1

puts "\n=== 仿真完成 ==="
puts "日志文件: $work_dir/sim_ch1_read.log"
puts "编译日志: $work_dir/compile_*.log"
puts "链接日志: $work_dir/elaborate.log"
