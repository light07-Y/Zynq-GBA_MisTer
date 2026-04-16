open_project F:/02_Projects/Zynq-GBA_MisTer-Vivado/Zynq-GBA_MisTer-Vivado.xpr
open_bd_design [get_files *zynq_gba_system.bd]
puts "BD_PORTS:" 
puts [lsort [get_bd_ports -quiet *AUDIO*]]
puts "BD_PINS_AXIIIC?" 
puts [lsort [get_bd_pins -quiet *scl*]]
close_project
exit
