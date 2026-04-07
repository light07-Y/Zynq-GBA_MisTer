set script_dir [file dirname [file normalize [info script]]]
set project_file [file join $script_dir Zynq-GBA_MisTer-Vivado.xpr]
set ip_user_dir [file join $script_dir Zynq-GBA_MisTer-Vivado.ip_user_files]

file mkdir $ip_user_dir

open_project $project_file

if {[catch {check_syntax -fileset sources_1} err]} {
    puts "SYNTAX CHECK FAILED: $err"
} else {
    puts "SYNTAX CHECK PASSED: sources_1"
}

close_project
exit
