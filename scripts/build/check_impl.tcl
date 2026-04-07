set script_dir [file dirname [file normalize [info script]]]
set repo_root  [file normalize [file join $script_dir .. ..]]
set project_file [file join $repo_root Zynq-GBA_MisTer-Vivado.xpr]

open_project $project_file
set_property XPM_LIBRARIES {XPM_CDC XPM_MEMORY} [current_project]
update_compile_order -fileset sources_1
launch_runs synth_1 -jobs 2
wait_on_run synth_1
launch_runs impl_1 -to_step write_bitstream -jobs 2
wait_on_run impl_1
close_project
exit
