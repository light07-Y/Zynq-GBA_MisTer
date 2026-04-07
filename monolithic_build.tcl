set script_dir [file dirname [file normalize [info script]]]
source [file join $script_dir scripts build rebuild_project.tcl]
