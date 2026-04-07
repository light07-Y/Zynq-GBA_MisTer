open_project F:/02_Projects/Zynq-GBA_MisTer-Vivado/Zynq-GBA_MisTer-Vivado.xpr
foreach pat {*GBA.sv *hps_io.sv *sys_top.v *pll_hdmi.v *pll_audio.v} {
  set f [get_files -of_objects [get_filesets sources_1] $pat]
  puts "$pat=[llength $f] [join $f { | }]"
}
close_project
exit
