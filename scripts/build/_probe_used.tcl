open_project F:/02_Projects/Zynq-GBA_MisTer-Vivado/Zynq-GBA_MisTer-Vivado.xpr
foreach path {F:/02_Projects/Zynq-GBA_MisTer-Vivado/src/rtl/core/gba_mister/GBA.sv F:/02_Projects/Zynq-GBA_MisTer-Vivado/src/rtl/core/gba_mister/sys/hps_io.sv} {
  set f [get_files -quiet $path]
  puts "PATH=$path COUNT=[llength $f]"
  foreach x $f {
    puts "USED=[get_property used_in_synthesis $x] TYPE=[get_property file_type $x]"
  }
}
close_project
exit
