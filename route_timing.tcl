open_project Zynq-GBA_MisTer-Vivado.xpr
update_compile_order -fileset sources_1
reset_run synth_1
reset_run impl_1
launch_runs impl_1 -to_step route_design -jobs 8
wait_on_run impl_1
open_run impl_1 -name impl_1
report_timing_summary -delay_type max -report_unconstrained -check_timing_verbose -max_paths 20 -file routed_timing_summary.rpt
report_timing -delay_type max -max_paths 20 -nworst 3 -input_pins -routable_nets -file routed_timing_paths.rpt
report_clock_interaction -file routed_clock_interaction.rpt
report_utilization -file routed_utilization.rpt
close_project
exit
