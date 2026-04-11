# run_gpu_timing_sim.tcl
# GPU timing + cycling 行为仿真脚本
# 用法: vivado -mode batch -source sim/scripts/run_gpu_timing_sim.tcl

set proj_root [file normalize [file join [file dirname [info script]] ../..]]

# 纯行为级 SV，无需 RTL 源文件
set tb_file [file join $proj_root sim/tb/tb_gpu_timing_cycle.sv]

create_project -in_memory -part xc7z020clg400-1
add_files -fileset sim_1 $tb_file
set_property top tb_gpu_timing_cycle [get_filesets sim_1]
set_property -name {xsim.simulate.runtime} -value {200ms} -objects [get_filesets sim_1]

launch_simulation
run all
close_sim
close_project
