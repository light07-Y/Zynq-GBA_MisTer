set script_dir [file dirname [file normalize [info script]]]
set repo_root  [file normalize [file join $script_dir .. ..]]
set project_file [file join $repo_root Zynq-GBA_MisTer-Vivado.xpr]
set xsa_file [file join $repo_root zynq_gba_system_wrapper.xsa]

open_project $project_file
open_run impl_1
write_hw_platform -fixed -include_bit -force -file $xsa_file
close_project
exit
