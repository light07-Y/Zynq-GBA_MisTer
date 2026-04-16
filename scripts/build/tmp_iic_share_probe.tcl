open_project F:/02_Projects/Zynq-GBA_MisTer-Vivado/Zynq-GBA_MisTer-Vivado.xpr
open_bd_design F:/02_Projects/Zynq-GBA_MisTer-Vivado/src/bd/zynq_gba_system/zynq_gba_system.bd
if {[llength [get_bd_intf_ports -quiet HDMI_DDC_IIC]] == 0} {
  create_bd_intf_port -mode Master -vlnv xilinx.com:interface:iic_rtl:1.0 HDMI_DDC_IIC
}
set ps_iic0 [get_bd_intf_pins ps_core/IIC_0]
set aud_iic [get_bd_intf_ports AUDIO_IIC]
set ddc_iic [get_bd_intf_ports HDMI_DDC_IIC]
set net0 [get_bd_intf_nets -quiet -of_objects $ps_iic0]
puts "Before net=$net0"
if {[llength $net0] > 0} {
  disconnect_bd_intf_net $net0 $ps_iic0
  disconnect_bd_intf_net $net0 $aud_iic
  catch {delete_bd_objs $net0}
}
connect_bd_intf_net $ps_iic0 $aud_iic $ddc_iic
puts "After net=[get_bd_intf_nets -of_objects $ps_iic0]"
validate_bd_design
close_project
exit
