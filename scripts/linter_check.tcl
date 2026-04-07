set script_dir [file dirname [file normalize [info script]]]
set repo_root  [file normalize [file join $script_dir ..]]
set project_file [file join $repo_root Zynq-GBA_MisTer-Vivado.xpr]

open_project $project_file
set_property source_mgmt_mode All [current_project]
update_compile_order -fileset sources_1
read_verilog [file join $repo_root scripts lint cyclonev_hps_stubs.v]

foreach pat [list \
	*src/rtl/core/gba_mister/rtl/SyncFifo.vhd \
	*src/rtl/core/gba_mister/rtl/SyncRam.vhd \
	*src/rtl/core/gba_mister/rtl/SyncRamDual.vhd \
	*src/rtl/core/gba_mister/rtl/SyncRamDualByteEnable.vhd \
	*src/rtl/core/gba_mister/rtl/SyncRamDualNotPow2.vhd
] {
	set f [get_files -of_objects [get_filesets sources_1] $pat]
	if {[llength $f] > 0} {
		set_property library mem $f
	}
}

update_compile_order -fileset sources_1

set linter_top [get_property top [get_filesets sources_1]]
if {$linter_top eq ""} {
	set linter_top zynq_gba_system_wrapper
}

synth_design -rtl -top $linter_top -part xc7z020clg400-1
report_methodology -file methodology_linter.txt
report_drc -file drc_linter.txt
close_project
