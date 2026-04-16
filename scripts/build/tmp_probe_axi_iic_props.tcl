create_project -force _tmp_axi_iic_props . -part xc7z020clg400-1
create_bd_design "probe"
create_bd_cell -type ip -vlnv xilinx.com:ip:axi_iic:2.1 axi_iic_0
foreach p [lsort [list_property [get_bd_cells axi_iic_0]]] {
  if {[string match "CONFIG.*" $p]} {
    puts "$p = [get_property $p [get_bd_cells axi_iic_0]]"
  }
}
close_project
exit
