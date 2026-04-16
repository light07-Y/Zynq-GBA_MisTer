create_project -force _tmp_axi_iic_probe . -part xc7z020clg400-1
create_bd_design "probe"
create_bd_cell -type ip -vlnv xilinx.com:ip:axi_iic:2.1 axi_iic_0
puts "INTF_PINS:" 
foreach p [lsort [get_bd_intf_pins -of_objects [get_bd_cells axi_iic_0]]] { puts "  $p" }
puts "PINS:"
foreach p [lsort [get_bd_pins -of_objects [get_bd_cells axi_iic_0]]] { puts "  $p" }
close_project
exit
