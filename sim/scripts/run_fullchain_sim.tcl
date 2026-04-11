# run_fullchain_sim.tcl
# 全链路仿真: 在 Vivado 项目上下文中编译 gba_top + testbench
# 用法: vivado -mode batch -source sim/scripts/run_fullchain_sim.tcl

set proj_root [file normalize [file join [file dirname [info script]] ../..]]
set proj_file [file join $proj_root Zynq-GBA_MisTer-Vivado.xpr]
set tb_file   [file join $proj_root sim/tb/tb_gba_fullchain.sv]

puts "=== Opening project: $proj_file ==="
open_project $proj_file

# 添加 testbench 到 sim_1 fileset（如果尚未添加）
set fs [get_filesets sim_1]
set existing [get_files -of_objects $fs -filter "NAME =~ *tb_gba_fullchain*" -quiet]
if {$existing eq ""} {
    add_files -fileset $fs $tb_file
    puts "Added testbench: $tb_file"
} else {
    puts "Testbench already in project"
}

# 设置仿真顶层
set_property top tb_gba_fullchain $fs
set_property top_lib xil_defaultlib $fs

# 所有 VHDL 文件设为 VHDL 2008（gba_cpu.vhd 的 to_hstring 需要）
foreach f [get_files -of_objects $fs -filter "FILE_TYPE == VHDL"] {
    set_property file_type {VHDL 2008} $f
}
puts "Set all VHDL files to VHDL 2008"

# xvhdl 编译选项: -relax 放宽类型检查（gba_cpu.vhd 的 to_hstring 问题）
set_property -name {xsim.compile.xvhdl.more_options} -value {-relax} -objects $fs

# 仿真运行时间设为 100ms（足够跑2帧）
set_property -name {xsim.simulate.runtime} -value {100ms} -objects $fs

# 启动仿真
puts "=== Launching simulation ==="
launch_simulation

# 运行
run all

puts "=== Simulation complete ==="
close_sim
close_project
