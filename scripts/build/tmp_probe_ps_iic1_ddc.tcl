set repo_root [file normalize .]
set bd_file [file join $repo_root src bd zynq_gba_system zynq_gba_system.bd]
create_project -force _tmp_ps_iic1_probe $repo_root -part xc7z020clg400-1
set_property board_part digilentinc.com:zybo-z7-20:part0:1.2 [current_project]
add_files -norecurse $bd_file
open_bd_design $bd_file
puts "Before set_property: IIC_1 pin count=[llength [get_bd_intf_pins -quiet ps_core/IIC_1]]"
set ps_core_cell [get_bd_cells -quiet ps_core]
set_property -dict [list \
    CONFIG.PCW_EN_I2C1 {1} \
    CONFIG.PCW_I2C1_PERIPHERAL_ENABLE {1} \
    CONFIG.PCW_EN_EMIO_I2C1 {1} \
    CONFIG.PCW_I2C1_I2C1_IO {EMIO} \
] $ps_core_cell
puts "After set_property: IIC_1 pin count=[llength [get_bd_intf_pins -quiet ps_core/IIC_1]]"
if {[llength [get_bd_intf_ports -quiet HDMI_DDC_IIC]] == 0} {
    create_bd_intf_port -mode Master -vlnv xilinx.com:interface:iic_rtl:1.0 HDMI_DDC_IIC
}
set iic1_pin [get_bd_intf_pins -quiet ps_core/IIC_1]
set ddc_if [get_bd_intf_ports -quiet HDMI_DDC_IIC]
if {[llength $iic1_pin] > 0 && [llength $ddc_if] > 0} {
    set ddc_net [get_bd_intf_nets -quiet -of_objects $ddc_if]
    if {[llength $ddc_net] > 0} {
        disconnect_bd_intf_net $ddc_net $ddc_if
    }
    connect_bd_intf_net $iic1_pin $ddc_if
    puts "Connected ps_core/IIC_1 to HDMI_DDC_IIC"
}
if {[llength [get_bd_ports -quiet hdmi_tx_hpd]] == 0} {
    create_bd_port -dir I hdmi_tx_hpd
}
puts "validate attempt"
catch {validate_bd_design} r
puts "validate result=$r"
close_project
exit
