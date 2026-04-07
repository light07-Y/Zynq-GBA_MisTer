set script_dir [file dirname [file normalize [info script]]]
set repo_root  [file normalize [file join $script_dir .. ..]]
set project_file [file join $repo_root Zynq-GBA_MisTer-Vivado.xpr]
set ip_user_dir [file join $repo_root Zynq-GBA_MisTer-Vivado.ip_user_files]

file mkdir $ip_user_dir

open_project $project_file
set_property XPM_LIBRARIES {XPM_CDC XPM_MEMORY} [current_project]
update_compile_order -fileset sources_1
set_property top zynq_gba_top [current_fileset]

# Use an empty constraint set for top-only RTL checks to avoid wrapper pin
# constraints triggering false critical warnings on zynq_gba_top.
if {[llength [get_filesets -quiet constrs_syntax]] == 0} {
    create_fileset -constrset constrs_syntax
}

synth_design -rtl -top zynq_gba_top -part xc7z020clg400-1 -constrset constrs_syntax
close_project
exit
