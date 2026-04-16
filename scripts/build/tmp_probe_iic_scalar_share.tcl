open_project F:/02_Projects/Zynq-GBA_MisTer-Vivado/Zynq-GBA_MisTer-Vivado.xpr
open_bd_design [get_files *zynq_gba_system.bd]
if {[llength [get_bd_ports -quiet hdmi_tx_scl_i]] == 0} {create_bd_port -dir I hdmi_tx_scl_i}
if {[llength [get_bd_ports -quiet hdmi_tx_scl_o]] == 0} {create_bd_port -dir O hdmi_tx_scl_o}
if {[llength [get_bd_ports -quiet hdmi_tx_scl_t]] == 0} {create_bd_port -dir O hdmi_tx_scl_t}
catch {connect_bd_net [get_bd_ports AUDIO_IIC_scl_o] [get_bd_ports hdmi_tx_scl_o]} r1
catch {connect_bd_net [get_bd_ports AUDIO_IIC_scl_t] [get_bd_ports hdmi_tx_scl_t]} r2
catch {connect_bd_net [get_bd_ports AUDIO_IIC_scl_i] [get_bd_ports hdmi_tx_scl_i]} r3
puts "R1=$r1"
puts "R2=$r2"
puts "R3=$r3"
close_project
exit
