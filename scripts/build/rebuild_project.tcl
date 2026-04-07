set script_dir [file dirname [file normalize [info script]]]
set repo_root  [file normalize [file join $script_dir .. ..]]

set project_name  "Zynq-GBA_MisTer-Vivado"
set bd_file       [file join $repo_root src bd zynq_gba_system zynq_gba_system.bd]
set constr_file   [file join $repo_root src constraints zybo_z7_20.xdc]
set local_bd_dir  [file join $repo_root ${project_name}.srcs sources_1 bd zynq_gba_system]
set local_bd_file [file join $local_bd_dir zynq_gba_system.bd]

create_project -force $project_name $repo_root -part xc7z020clg400-1
set_property board_part digilentinc.com:zybo-z7-20:part0:1.2 [current_project]
set_property target_language Verilog [current_project]
set_property simulator_language Mixed [current_project]
set_property default_lib xil_defaultlib [current_project]
set_property source_mgmt_mode All [current_project]
set_property XPM_LIBRARIES {XPM_CDC XPM_MEMORY} [current_project]
# Keep synthesis on the balanced default recipe. The aggressive performance
# recipe disables resource sharing and globally biases FSM extraction toward
# one-hot, which pushes this design over the Z-7020 LUT budget.
set_property strategy {Vivado Synthesis Defaults} [get_runs synth_1]
set_property strategy Performance_ExplorePostRoutePhysOpt [get_runs impl_1]

set ip_repos {}
foreach repo [list D:/Tools/vivado-library-master D:/Tools/vivado-library-master/ip] {
    if {[file isdirectory $repo]} {
        lappend ip_repos $repo
    }
}
if {[llength $ip_repos] > 0} {
    set_property ip_repo_paths $ip_repos [current_project]
    update_ip_catalog
}

file mkdir $local_bd_dir
file copy -force $bd_file $local_bd_file

add_files -norecurse $local_bd_file
add_files -norecurse [glob -nocomplain [file join $repo_root src rtl top *.v]]
add_files -norecurse [glob -nocomplain [file join $repo_root src rtl common *.v]]
add_files -norecurse [glob -nocomplain [file join $repo_root src rtl common *.sv]]
add_files -norecurse [glob -nocomplain [file join $repo_root src rtl core gba_mister rtl *.v]]
add_files -norecurse [glob -nocomplain [file join $repo_root src rtl core gba_mister rtl *.sv]]
add_files -norecurse [glob -nocomplain [file join $repo_root src rtl core gba_mister rtl *.vhd]]
add_files -fileset constrs_1 -norecurse $constr_file

foreach relpath [list \
    src/rtl/core/gba_mister/rtl/SyncFifo.vhd \
    src/rtl/core/gba_mister/rtl/SyncRam.vhd \
    src/rtl/core/gba_mister/rtl/SyncRamDual.vhd \
    src/rtl/core/gba_mister/rtl/SyncRamDualByteEnable.vhd \
    src/rtl/core/gba_mister/rtl/SyncRamDualNotPow2.vhd \
] {
    set f [get_files -quiet [file join $repo_root $relpath]]
    if {[llength $f] > 0} {
        set_property library mem $f
    }
}

open_bd_design $local_bd_file

# Ensure PS-side mandatory peripherals for this project:
# - USB0 on MIO (for FreeRTOS USB stack usage)
# - F2P interrupt path enabled
set ps_core_cell [get_bd_cells -quiet ps_core]
if {[llength $ps_core_cell] > 0} {
    set_property CONFIG.PCW_EN_USB0 {1} $ps_core_cell
    set_property CONFIG.PCW_USB0_PERIPHERAL_ENABLE {1} $ps_core_cell
    set_property CONFIG.PCW_USB0_USB0_IO {MIO 28 .. 39} $ps_core_cell
    # NOTE: USB reset select/io knobs are disabled in current PS7 IP profile
    # (Vivado 2025.2.1 for this board setup), so no dedicated USB reset pin
    # can be configured here from BD.
    set_property CONFIG.PCW_IRQ_F2P_INTR {1} $ps_core_cell
}

if {[llength [get_bd_cells -quiet hdmi_rgb_pack]] > 0} {
    delete_bd_objs [get_bd_cells hdmi_rgb_pack]
}

create_bd_cell -type ip -vlnv xilinx.com:ip:axis_subset_converter:1.1 hdmi_rgb_pack
set_property -dict [list \
    CONFIG.S_TDATA_NUM_BYTES {4} \
    CONFIG.M_TDATA_NUM_BYTES {3} \
    CONFIG.S_TID_WIDTH {0} \
    CONFIG.M_TID_WIDTH {0} \
    CONFIG.S_TDEST_WIDTH {0} \
    CONFIG.M_TDEST_WIDTH {0} \
    CONFIG.S_TUSER_WIDTH {1} \
    CONFIG.M_TUSER_WIDTH {1} \
    CONFIG.S_HAS_TREADY {1} \
    CONFIG.S_HAS_TSTRB {0} \
    CONFIG.S_HAS_TKEEP {1} \
    CONFIG.S_HAS_TLAST {1} \
    CONFIG.M_HAS_TREADY {1} \
    CONFIG.M_HAS_TSTRB {0} \
    CONFIG.M_HAS_TKEEP {0} \
    CONFIG.M_HAS_TLAST {1} \
    CONFIG.TDATA_REMAP {tdata[23:0]} \
    CONFIG.TUSER_REMAP {tuser[0:0]} \
    CONFIG.TID_REMAP {1'b0} \
    CONFIG.TDEST_REMAP {1'b0} \
    CONFIG.TKEEP_REMAP {1'b0} \
    CONFIG.TSTRB_REMAP {1'b0} \
    CONFIG.TLAST_REMAP {tlast[0]} \
] [get_bd_cells hdmi_rgb_pack]

connect_bd_intf_net [get_bd_intf_pins hdmi_vdma/M_AXIS_MM2S] [get_bd_intf_pins hdmi_rgb_pack/S_AXIS]
connect_bd_intf_net [get_bd_intf_pins hdmi_rgb_pack/M_AXIS] [get_bd_intf_pins hdmi_vid_out/video_in]
connect_bd_net [get_bd_pins ps_core/FCLK_CLK1] [get_bd_pins hdmi_rgb_pack/aclk]
connect_bd_net [get_bd_pins rst_100M/peripheral_aresetn] [get_bd_pins hdmi_rgb_pack/aresetn]

if {[llength [get_bd_cells -quiet rst_serial]] == 0} {
    create_bd_cell -type ip -vlnv xilinx.com:ip:proc_sys_reset:5.0 rst_serial
}

foreach pin_path [list \
    hdmi_dvi_enc/aRst_n \
    rst_serial/slowest_sync_clk \
    rst_serial/ext_reset_in \
    rst_serial/dcm_locked \
] {
    set pin [get_bd_pins $pin_path]
    set net [get_bd_nets -quiet -of_objects $pin]
    if {[llength $net] > 0} {
        disconnect_bd_net $net $pin
    }
}

connect_bd_net [get_bd_pins hdmi_clk_gen/clk_out2] [get_bd_pins rst_serial/slowest_sync_clk]
connect_bd_net [get_bd_pins ps_core/FCLK_RESET0_N] [get_bd_pins rst_serial/ext_reset_in]
connect_bd_net [get_bd_pins hdmi_clk_gen/locked] [get_bd_pins rst_serial/dcm_locked]
connect_bd_net [get_bd_pins rst_serial/peripheral_aresetn] [get_bd_pins hdmi_dvi_enc/aRst_n]

# Route required PL interrupts to PS for FreeRTOS:
# - In0: gba_pl_top_0/irq
# - In1: hdmi_vdma/mm2s_introut
if {[llength [get_bd_cells -quiet pl_irq_concat]] == 0} {
    create_bd_cell -type ip -vlnv xilinx.com:ip:xlconcat:2.1 pl_irq_concat
}
set_property -dict [list CONFIG.NUM_PORTS {2}] [get_bd_cells pl_irq_concat]

foreach pin_path [list \
    pl_irq_concat/In0 \
    pl_irq_concat/In1 \
    ps_core/IRQ_F2P \
] {
    set pin [get_bd_pins -quiet $pin_path]
    if {[llength $pin] > 0} {
        set net [get_bd_nets -quiet -of_objects $pin]
        if {[llength $net] > 0} {
            disconnect_bd_net $net $pin
        }
    }
}

connect_bd_net [get_bd_pins gba_pl_top_0/irq] [get_bd_pins pl_irq_concat/In0]
connect_bd_net [get_bd_pins hdmi_vdma/mm2s_introut] [get_bd_pins pl_irq_concat/In1]
connect_bd_net [get_bd_pins pl_irq_concat/dout] [get_bd_pins ps_core/IRQ_F2P]

validate_bd_design
save_bd_design
file copy -force $local_bd_file $bd_file
generate_target all [get_files $local_bd_file]

# Avoid TIMING-4/TIMING-27 from clk_wiz scoped primary-clock definition on
# hierarchical pin. The design already has a top-level PS FCLK source clock.
set hdmi_clk_legacy_xdc [get_files -quiet *zynq_gba_system_hdmi_clk_gen_0.xdc]
if {[llength $hdmi_clk_legacy_xdc] > 0} {
    set_property used_in_synthesis false $hdmi_clk_legacy_xdc
    set_property used_in_implementation false $hdmi_clk_legacy_xdc
}

make_wrapper -files [get_files $local_bd_file] -top -import -force
close_bd_design [current_bd_design]

set_property top zynq_gba_system_wrapper [get_filesets sources_1]
set_property top zynq_gba_system_wrapper [get_filesets sim_1]
update_compile_order -fileset sources_1
update_compile_order -fileset sim_1
close_project
exit
