# run_ddr_backend_sim.tcl: ddram_mux + ddr_axi_backend 握手竞态仿真
# 用法:
#   cd <项目根目录>/sim/work
#   vivado -mode batch -source ../scripts/run_ddr_backend_sim.tcl

set proj_root [file normalize [file join [file dirname [info script]] ../..]]
set sim_dir    "$proj_root/sim"
set tb_dir     "$sim_dir/tb"
set rtl_dir    "$proj_root/src/rtl"
set rom_hex    "$sim_dir/rom_data/rom_first_4k.hex"
set work_dir   "$sim_dir/work"

puts "=== ddram_mux + ddr_axi_backend 握手竞态仿真 ==="
puts "项目根目录: $proj_root"

file mkdir $work_dir
cd $work_dir

file copy -force $rom_hex $work_dir/rom_first_4k.hex

# ========== 编译 ==========
puts "\n--- 编译 SystemVerilog ---"
xvlog -sv "$rtl_dir/common/ddram_mux.sv" \
    -log compile_ddram_mux.log 2>&1

xvlog -sv "$rtl_dir/common/ddr_axi_backend.sv" \
    -log compile_backend.log 2>&1

xvlog -sv "$tb_dir/axi_mem_model.sv" \
    -log compile_axi_mem.log 2>&1

xvlog -sv "$tb_dir/tb_ddr_backend.sv" \
    -log compile_tb.log 2>&1

# ========== 链接 ==========
puts "\n--- 链接 (elaborate) ---"
xelab tb_ddr_backend \
    -relax -debug typical \
    -s ddr_backend_sim \
    -log elaborate_ddr_backend.log 2>&1

# ========== 运行仿真 ==========
puts "\n--- 运行仿真 ---"
xsim ddr_backend_sim \
    -runall \
    -log sim_ddr_backend.log 2>&1

puts "\n=== 仿真完成 ==="
puts "日志文件: $work_dir/sim_ddr_backend.log"
