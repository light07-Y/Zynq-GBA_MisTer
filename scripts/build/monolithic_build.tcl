set script_dir [file dirname [file normalize [info script]]]
source [file join $script_dir rebuild_project.tcl]
