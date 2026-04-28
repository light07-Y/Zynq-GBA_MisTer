set script_dir [file dirname [file normalize [info script]]]
set repo_root  [file normalize [file join $script_dir .. ..]]

set project_name  "Zynq-GBA_MisTer-Vivado"
set bd_dir        [file join $repo_root src bd zynq_gba_system]
set bd_file       [file join $repo_root src bd zynq_gba_system zynq_gba_system.bd]
set constr_file   [file join $repo_root src constraints zybo_z7_20.xdc]
set local_bd_dir  [file join $repo_root ${project_name}.srcs sources_1 bd zynq_gba_system]
set local_bd_file [file join $local_bd_dir zynq_gba_system.bd]
set local_gen_bd_dir [file join $repo_root ${project_name}.gen sources_1 bd zynq_gba_system]
set src_ip_dir    [file join $bd_dir ip]
set local_ip_dir  [file join $local_bd_dir ip]
set src_shared_dir   [file join $bd_dir ipshared]
set local_shared_dir [file join $local_gen_bd_dir ipshared]

source [file join $script_dir bd_ip_maintenance.tcl]

proc add_globbed_files {pattern} {
    set files [glob -nocomplain $pattern]
    if {[llength $files] > 0} {
        add_files -norecurse $files
    }
}

proc upgrade_locked_project_ips {} {
    set locked_ips [get_ips -quiet -filter {IS_LOCKED == 1}]
    if {[llength $locked_ips] == 0} {
        return
    }

    puts "INFO: Found locked IP(s); attempting upgrade: $locked_ips"
    upgrade_ip $locked_ips

    set still_locked [get_ips -quiet -filter {IS_LOCKED == 1}]
    if {[llength $still_locked] > 0} {
        puts "ERROR: IP(s) are still locked after upgrade: $still_locked"
        report_ip_status
        error "Locked IPs remain after upgrade."
    }
}

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
# Known-benign module_ref interface propagation warning; keep batch logs clean.
set_msg_config -id {BD 41-926} -suppress

set ip_repos {}
foreach repo [list D:/Tools/vivado-library-master] {
    if {[file isdirectory $repo]} {
        lappend ip_repos $repo
    }
}
if {[llength $ip_repos] > 0} {
    set_property ip_repo_paths $ip_repos [current_project]
    update_ip_catalog
}

# Harvest any previously generated local BD IP into source snapshot first.
repair_bd_ip_snapshot $repo_root $project_name zynq_gba_system
if {[file exists $local_bd_dir]} {
    file delete -force $local_bd_dir
}
file mkdir $local_bd_dir
file mkdir $local_gen_bd_dir
file copy -force $bd_file $local_bd_file
if {[file isdirectory $src_ip_dir]} {
    file copy -force $src_ip_dir $local_ip_dir
}
if {[file isdirectory $src_shared_dir]} {
    file copy -force $src_shared_dir $local_shared_dir
}
normalize_xci_shareddir $local_ip_dir

add_files -norecurse $local_bd_file
add_globbed_files [file join $repo_root src rtl top *.v]
add_globbed_files [file join $repo_root src rtl common *.v]
add_globbed_files [file join $repo_root src rtl common *.sv]
add_globbed_files [file join $repo_root src rtl common *.vh]
add_globbed_files [file join $repo_root src rtl common *.mem]
add_globbed_files [file join $repo_root src rtl core gba_mister rtl *.v]
add_globbed_files [file join $repo_root src rtl core gba_mister rtl *.sv]
add_globbed_files [file join $repo_root src rtl core gba_mister rtl *.vhd]
add_globbed_files [file join $repo_root src rtl core gba_mister rtl *.mem]
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

upgrade_locked_project_ips

open_bd_design $local_bd_file

# Legacy monitor outputs were removed from zynq_gba_top; clean any stale
# top-level BD ports left by old snapshots.
foreach legacy_port_name [list audio_l audio_r ch1_req ch2_req ch3_req ch4_req] {
    set legacy_port [get_bd_ports -quiet $legacy_port_name]
    if {[llength $legacy_port] > 0} {
        set legacy_net [get_bd_nets -quiet -of_objects $legacy_port]
        if {[llength $legacy_net] > 0} {
            disconnect_bd_net $legacy_net $legacy_port
        }
        delete_bd_objs $legacy_port
    }
}

if {[llength [get_bd_cells -quiet fb_cap_bram_ctrl]] == 0} {
    create_bd_cell -type ip -vlnv xilinx.com:ip:axi_bram_ctrl:4.1 fb_cap_bram_ctrl
}
set_property -dict [list CONFIG.DATA_WIDTH {32} CONFIG.SINGLE_PORT_BRAM {1}] [get_bd_cells fb_cap_bram_ctrl]

foreach pin_path [list \
    axi_ctrl_ic/M02_AXI \
    fb_cap_bram_ctrl/S_AXI \
    fb_cap_bram_ctrl/BRAM_PORTA \
    gba_pl_top_0/fb_cap_bram \
] {
    set pin [get_bd_intf_pins -quiet $pin_path]
    if {[llength $pin] > 0} {
        set net [get_bd_intf_nets -quiet -of_objects $pin]
        if {[llength $net] > 0} {
            disconnect_bd_intf_net $net $pin
        }
    }
}

foreach net_name [list axi_ctrl_ic_M02_AXI fb_cap_bram_ctrl_BRAM_PORTA axi_ctrl_ic_M02_AXI1 fb_cap_bram_ctrl_BRAM_PORTA1] {
    set net [get_bd_intf_nets -quiet $net_name]
    if {[llength $net] > 0} {
        delete_bd_objs $net
    }
}

connect_bd_intf_net -intf_net [get_bd_intf_nets -quiet axi_ctrl_ic_M02_AXI] [get_bd_intf_pins axi_ctrl_ic/M02_AXI] [get_bd_intf_pins fb_cap_bram_ctrl/S_AXI]
connect_bd_intf_net -intf_net [get_bd_intf_nets -quiet fb_cap_bram_ctrl_BRAM_PORTA] [get_bd_intf_pins fb_cap_bram_ctrl/BRAM_PORTA] [get_bd_intf_pins gba_pl_top_0/fb_cap_bram]

foreach pin_path [list \
    axi_ctrl_ic/M02_ACLK \
    axi_ctrl_ic/M02_ARESETN \
    fb_cap_bram_ctrl/s_axi_aclk \
    fb_cap_bram_ctrl/s_axi_aresetn \
] {
    set pin [get_bd_pins -quiet $pin_path]
    if {[llength $pin] > 0} {
        set net [get_bd_nets -quiet -of_objects $pin]
        if {[llength $net] > 0} {
            disconnect_bd_net $net $pin
        }
    }
}

connect_bd_net [get_bd_pins ps_core/FCLK_CLK0] [get_bd_pins axi_ctrl_ic/M02_ACLK]
connect_bd_net [get_bd_pins rst_50M/peripheral_aresetn] [get_bd_pins axi_ctrl_ic/M02_ARESETN]
connect_bd_net [get_bd_pins ps_core/FCLK_CLK0] [get_bd_pins fb_cap_bram_ctrl/s_axi_aclk]
connect_bd_net [get_bd_pins rst_50M/peripheral_aresetn] [get_bd_pins fb_cap_bram_ctrl/s_axi_aresetn]

set fb_cap_mem_seg [get_bd_addr_segs -quiet fb_cap_bram_ctrl/S_AXI/Mem0]
set fb_cap_ps_seg [get_bd_addr_segs -quiet -filter {NAME == "SEG_fb_cap_bram_ctrl_Mem0"}]
if {[llength $fb_cap_mem_seg] > 0} {
    if {[llength $fb_cap_ps_seg] == 0} {
        create_bd_addr_seg -range 256K -offset 0x44000000 [get_bd_addr_spaces ps_core/Data] $fb_cap_mem_seg SEG_fb_cap_bram_ctrl_Mem0
    } else {
        set_property offset 0x44000000 $fb_cap_ps_seg
        set_property range 256K $fb_cap_ps_seg
    }
}

# Ensure PS-side mandatory peripherals for this project:
# - USB0 on MIO (for FreeRTOS USB host stack usage)
# - USB PHY reset on MIO46 (matches Zybo Z7-20 board preset)
# - BTN4/BTN5 on MIO50/51 via PS GPIO
# - Ethernet/MDIO disabled because this project does not use GEM0
# - F2P interrupt path enabled
set ps_core_cell [get_bd_cells -quiet ps_core]
if {[llength $ps_core_cell] > 0} {
    set_property -dict [list \
        CONFIG.PCW_EN_USB0 {1} \
        CONFIG.PCW_EN_USB1 {0} \
        CONFIG.PCW_USB0_PERIPHERAL_ENABLE {1} \
        CONFIG.PCW_USB1_PERIPHERAL_ENABLE {0} \
        CONFIG.PCW_USB0_USB0_IO {MIO 28 .. 39} \
        CONFIG.PCW_USB0_RESET_ENABLE {1} \
        CONFIG.PCW_USB0_RESET_IO {MIO 46} \
        CONFIG.PCW_USB_RESET_ENABLE {1} \
        CONFIG.PCW_USB_RESET_POLARITY {Active Low} \
        CONFIG.PCW_USB_RESET_SELECT {Share reset pin} \
        CONFIG.PCW_ENET0_GRP_MDIO_ENABLE {0} \
        CONFIG.PCW_ENET0_PERIPHERAL_ENABLE {0} \
        CONFIG.PCW_ENET1_PERIPHERAL_ENABLE {0} \
        CONFIG.PCW_ENET_RESET_ENABLE {0} \
        CONFIG.PCW_EN_ENET0 {0} \
        CONFIG.PCW_EN_ENET1 {0} \
        CONFIG.PCW_EN_I2C0 {1} \
        CONFIG.PCW_I2C0_PERIPHERAL_ENABLE {1} \
        CONFIG.PCW_EN_EMIO_I2C0 {1} \
        CONFIG.PCW_I2C0_I2C0_IO {EMIO} \
        CONFIG.PCW_EN_I2C1 {1} \
        CONFIG.PCW_I2C1_PERIPHERAL_ENABLE {1} \
        CONFIG.PCW_EN_EMIO_I2C1 {1} \
        CONFIG.PCW_I2C1_I2C1_IO {EMIO} \
        CONFIG.PCW_EN_GPIO {1} \
        CONFIG.PCW_GPIO_PERIPHERAL_ENABLE {1} \
        CONFIG.PCW_GPIO_MIO_GPIO_ENABLE {1} \
        CONFIG.PCW_GPIO_MIO_GPIO_IO {MIO} \
        CONFIG.PCW_EN_EMIO_GPIO {1} \
        CONFIG.PCW_GPIO_EMIO_GPIO_ENABLE {1} \
        CONFIG.PCW_GPIO_EMIO_GPIO_IO {EMIO} \
    ] $ps_core_cell
    # BTN4/BTN5 对应 MIO50/MIO51。这里必须关闭内部 pull-up，
    # 否则 platform/ps7_init 可能把按键输入长期钉在错误电平，
    # 最终表现为 PS 端 raw50/raw51 不变化、UART 无按键日志。
    set_property CONFIG.PCW_MIO_50_PULLUP {disabled} $ps_core_cell
    set_property CONFIG.PCW_MIO_51_PULLUP {disabled} $ps_core_cell
    set_property CONFIG.PCW_IRQ_F2P_INTR {1} $ps_core_cell
}

# HDMI TX auxiliary signals (Zybo Z7):
# - AUDIO_IIC stays on PS IIC_0 (EMIO)
# - DDC moves to PS IIC_1 (EMIO)
# - HPD is sampled through PS GPIO EMIO[0] (pin index 54 in XGpioPs API)
if {[llength [get_bd_ports -quiet hdmi_tx_hpd]] == 0} {
    create_bd_port -dir I hdmi_tx_hpd
}
if {[llength [get_bd_intf_ports -quiet AUDIO_IIC]] == 0} {
    create_bd_intf_port -mode Master -vlnv xilinx.com:interface:iic_rtl:1.0 AUDIO_IIC
}
if {[llength [get_bd_intf_ports -quiet HDMI_DDC_IIC]] == 0} {
    create_bd_intf_port -mode Master -vlnv xilinx.com:interface:iic_rtl:1.0 HDMI_DDC_IIC
}
set ps_iic0_pin  [get_bd_intf_pins  -quiet ps_core/IIC_0]
set ps_iic1_pin  [get_bd_intf_pins  -quiet ps_core/IIC_1]
set audio_iic_if [get_bd_intf_ports -quiet AUDIO_IIC]
set hdmi_ddc_if  [get_bd_intf_ports -quiet HDMI_DDC_IIC]

if {[llength $ps_iic0_pin] > 0 && [llength $audio_iic_if] > 0} {
    foreach intf_obj [list $ps_iic0_pin $audio_iic_if] {
        set net [get_bd_intf_nets -quiet -of_objects $intf_obj]
        if {[llength $net] > 0} {
            disconnect_bd_intf_net $net $intf_obj
        }
    }
    connect_bd_intf_net $ps_iic0_pin $audio_iic_if
}

if {[llength $ps_iic1_pin] > 0 && [llength $hdmi_ddc_if] > 0} {
    foreach intf_obj [list $ps_iic1_pin $hdmi_ddc_if] {
        set net [get_bd_intf_nets -quiet -of_objects $intf_obj]
        if {[llength $net] > 0} {
            disconnect_bd_intf_net $net $intf_obj
        }
    }
    connect_bd_intf_net $ps_iic1_pin $hdmi_ddc_if
}

if {[llength [get_bd_cells -quiet hdmi_hpd_gpio_concat]] == 0} {
    create_bd_cell -type ip -vlnv xilinx.com:ip:xlconcat:2.1 hdmi_hpd_gpio_concat
}
if {[llength [get_bd_cells -quiet hdmi_hpd_gpio_zero]] == 0} {
    create_bd_cell -type ip -vlnv xilinx.com:ip:xlconstant:1.1 hdmi_hpd_gpio_zero
}
set_property -dict [list \
    CONFIG.NUM_PORTS {2} \
    CONFIG.IN0_WIDTH {1} \
    CONFIG.IN1_WIDTH {63} \
] [get_bd_cells hdmi_hpd_gpio_concat]
set_property -dict [list \
    CONFIG.CONST_WIDTH {63} \
    CONFIG.CONST_VAL {0} \
] [get_bd_cells hdmi_hpd_gpio_zero]

foreach pin_path [list \
    ps_core/GPIO_I \
    hdmi_hpd_gpio_concat/In0 \
    hdmi_hpd_gpio_concat/In1 \
    hdmi_hpd_gpio_concat/dout \
    hdmi_hpd_gpio_zero/dout \
] {
    set pin [get_bd_pins -quiet $pin_path]
    if {[llength $pin] > 0} {
        set net [get_bd_nets -quiet -of_objects $pin]
        if {[llength $net] > 0} {
            disconnect_bd_net $net $pin
        }
    }
}
set hpd_port [get_bd_ports -quiet hdmi_tx_hpd]
if {[llength $hpd_port] > 0} {
    set hpd_net [get_bd_nets -quiet -of_objects $hpd_port]
    if {[llength $hpd_net] > 0} {
        disconnect_bd_net $hpd_net $hpd_port
    }
}

if {[llength [get_bd_pins -quiet ps_core/GPIO_I]] > 0 && [llength $hpd_port] > 0} {
    foreach fixed_net_name [list hdmi_tx_hpd_net hdmi_hpd_gpio_zero_net hdmi_hpd_gpio_gpioi_net] {
        set fixed_net [get_bd_nets -quiet $fixed_net_name]
        if {[llength $fixed_net] > 0} {
            delete_bd_objs $fixed_net
        }
    }

    connect_bd_net -net hdmi_tx_hpd_net $hpd_port [get_bd_pins hdmi_hpd_gpio_concat/In0]
    connect_bd_net -net hdmi_hpd_gpio_zero_net \
        [get_bd_pins hdmi_hpd_gpio_zero/dout] \
        [get_bd_pins hdmi_hpd_gpio_concat/In1]
    connect_bd_net -net hdmi_hpd_gpio_gpioi_net \
        [get_bd_pins hdmi_hpd_gpio_concat/dout] \
        [get_bd_pins ps_core/GPIO_I]
}

# Clean up any orphaned interface net left by reconnect operations.
foreach intf_net [get_bd_intf_nets -quiet] {
    set intf_pins [get_bd_intf_pins -quiet -of_objects $intf_net]
    set intf_ports [get_bd_intf_ports -quiet -of_objects $intf_net]
    if {[llength $intf_pins] == 0 && [llength $intf_ports] == 0} {
        delete_bd_objs $intf_net
    }
}

# Clean up stale HPD helper nets left by previous reconnect operations.
foreach net [get_bd_nets -quiet {hdmi_tx_hpd* hdmi_hpd_gpio_concat_dout* hdmi_hpd_gpio_zero_dout*}] {
    set pins [get_bd_pins -quiet -of_objects $net]
    set ports [get_bd_ports -quiet -of_objects $net]
    if {([llength $pins] + [llength $ports]) < 2} {
        delete_bd_objs $net
    }
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
    CONFIG.M_HAS_TKEEP {1} \
    CONFIG.M_HAS_TLAST {1} \
    CONFIG.TDATA_REMAP {tdata[23:16],tdata[7:0],tdata[15:8]} \
    CONFIG.TUSER_REMAP {tuser[0:0]} \
    CONFIG.TID_REMAP {1'b0} \
    CONFIG.TDEST_REMAP {1'b0} \
    CONFIG.TKEEP_REMAP {tkeep[2:0]} \
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

if {[llength [get_bd_cells -quiet hdmi_vid_reset_inv]] == 0} {
    create_bd_cell -type ip -vlnv xilinx.com:ip:util_vector_logic:2.0 hdmi_vid_reset_inv
}
set_property -dict [list \
    CONFIG.C_OPERATION {not} \
    CONFIG.C_SIZE {1} \
] [get_bd_cells hdmi_vid_reset_inv]

set_property -dict [list \
    CONFIG.HAS_AXI4_LITE {false} \
    CONFIG.VIDEO_MODE {480p} \
    CONFIG.enable_detection {false} \
] [get_bd_cells hdmi_tc]

set_property -dict [list \
    CONFIG.CLKOUT1_REQUESTED_OUT_FREQ {25.175} \
    CONFIG.CLKOUT2_REQUESTED_OUT_FREQ {125.875} \
    CONFIG.MMCM_CLKFBOUT_MULT_F {17.625} \
    CONFIG.MMCM_CLKOUT0_DIVIDE_F {35.000} \
    CONFIG.MMCM_CLKOUT1_DIVIDE {7} \
    CONFIG.NUM_OUT_CLKS {2} \
    CONFIG.RESET_TYPE {ACTIVE_LOW} \
    CONFIG.USE_LOCKED {true} \
    CONFIG.USE_RESET {true} \
] [get_bd_cells hdmi_clk_gen]

foreach pin_path [list \
    hdmi_clk_gen/clk_in1 \
    hdmi_vid_out/aclken \
    hdmi_vid_out/vid_io_out_ce \
    hdmi_vid_out/vid_io_out_reset \
    hdmi_tc/clken \
    hdmi_tc/gen_clken \
    hdmi_dvi_enc/aRst_n \
    hdmi_vid_reset_inv/Op1 \
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

connect_bd_net [get_bd_pins ps_core/FCLK_CLK0] [get_bd_pins hdmi_clk_gen/clk_in1]
connect_bd_net [get_bd_pins hdmi_clk_gen/locked] [get_bd_pins hdmi_vid_out/aclken]
connect_bd_net [get_bd_pins hdmi_clk_gen/locked] [get_bd_pins hdmi_vid_out/vid_io_out_ce]
connect_bd_net [get_bd_pins hdmi_clk_gen/locked] [get_bd_pins hdmi_tc/clken]
connect_bd_net [get_bd_pins hdmi_clk_gen/locked] [get_bd_pins hdmi_vid_reset_inv/Op1]
connect_bd_net [get_bd_pins hdmi_vid_reset_inv/Res] [get_bd_pins hdmi_vid_out/vid_io_out_reset]
connect_bd_net [get_bd_pins hdmi_vid_out/vtg_ce] [get_bd_pins hdmi_tc/gen_clken]
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

upgrade_locked_project_ips
validate_bd_design
save_bd_design
file copy -force $local_bd_file $bd_file
normalize_xci_shareddir $local_ip_dir
set bd_obj [get_files $local_bd_file]
reset_target all $bd_obj
generate_target all $bd_obj
export_ip_user_files -of_objects $bd_obj -no_script -sync -force

repair_bd_ip_snapshot $repo_root $project_name zynq_gba_system

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
