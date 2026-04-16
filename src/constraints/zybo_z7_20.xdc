## Zynq-GBA_MiSTer Vivado constraints
## Target board: Digilent Zybo Z7-20
## Canonical top: zynq_gba_system_wrapper

## Bitstream
set_property BITSTREAM.GENERAL.COMPRESS TRUE [current_design]

## HDMI TX
set_property -dict {PACKAGE_PIN H16 IOSTANDARD TMDS_33} [get_ports tmds_tx_clk_p]
set_property -dict {PACKAGE_PIN H17 IOSTANDARD TMDS_33} [get_ports tmds_tx_clk_n]
set_property -dict {PACKAGE_PIN D19 IOSTANDARD TMDS_33} [get_ports {tmds_tx_data_p[0]}]
set_property -dict {PACKAGE_PIN D20 IOSTANDARD TMDS_33} [get_ports {tmds_tx_data_n[0]}]
set_property -dict {PACKAGE_PIN C20 IOSTANDARD TMDS_33} [get_ports {tmds_tx_data_p[1]}]
set_property -dict {PACKAGE_PIN B20 IOSTANDARD TMDS_33} [get_ports {tmds_tx_data_n[1]}]
set_property -dict {PACKAGE_PIN B19 IOSTANDARD TMDS_33} [get_ports {tmds_tx_data_p[2]}]
set_property -dict {PACKAGE_PIN A20 IOSTANDARD TMDS_33} [get_ports {tmds_tx_data_n[2]}]
set_property -dict {PACKAGE_PIN E18 IOSTANDARD LVCMOS33} [get_ports hdmi_tx_hpd]
set_property -dict {PACKAGE_PIN G17 IOSTANDARD LVCMOS33} [get_ports HDMI_DDC_IIC_scl_io]
set_property -dict {PACKAGE_PIN G18 IOSTANDARD LVCMOS33} [get_ports HDMI_DDC_IIC_sda_io]

## Audio codec SSM2603 PCM data
set_property -dict {PACKAGE_PIN R17 IOSTANDARD LVCMOS33} [get_ports ac_mclk]
set_property -dict {PACKAGE_PIN R19 IOSTANDARD LVCMOS33} [get_ports ac_bclk]
set_property -dict {PACKAGE_PIN T19 IOSTANDARD LVCMOS33} [get_ports ac_pblrc]
set_property -dict {PACKAGE_PIN R18 IOSTANDARD LVCMOS33} [get_ports ac_pbdat]
set_property -dict {PACKAGE_PIN P18 IOSTANDARD LVCMOS33} [get_ports ac_muten]

## Audio codec I2C
set_property -dict {PACKAGE_PIN N18 IOSTANDARD LVCMOS33} [get_ports AUDIO_IIC_scl_io]
set_property -dict {PACKAGE_PIN N17 IOSTANDARD LVCMOS33} [get_ports AUDIO_IIC_sda_io]

## Buttons
set_property -dict {PACKAGE_PIN K18 IOSTANDARD LVCMOS33} [get_ports {btns[0]}]
set_property -dict {PACKAGE_PIN P16 IOSTANDARD LVCMOS33} [get_ports {btns[1]}]
set_property -dict {PACKAGE_PIN K19 IOSTANDARD LVCMOS33} [get_ports {btns[2]}]
set_property -dict {PACKAGE_PIN Y16 IOSTANDARD LVCMOS33} [get_ports {btns[3]}]

## Switches
set_property -dict {PACKAGE_PIN G15 IOSTANDARD LVCMOS33} [get_ports {sws[0]}]
set_property -dict {PACKAGE_PIN P15 IOSTANDARD LVCMOS33} [get_ports {sws[1]}]
set_property -dict {PACKAGE_PIN W13 IOSTANDARD LVCMOS33} [get_ports {sws[2]}]
set_property -dict {PACKAGE_PIN T16 IOSTANDARD LVCMOS33} [get_ports {sws[3]}]

## LEDs
set_property -dict {PACKAGE_PIN M14 IOSTANDARD LVCMOS33} [get_ports {leds[0]}]
set_property -dict {PACKAGE_PIN M15 IOSTANDARD LVCMOS33} [get_ports {leds[1]}]
set_property -dict {PACKAGE_PIN G14 IOSTANDARD LVCMOS33} [get_ports {leds[2]}]
set_property -dict {PACKAGE_PIN D18 IOSTANDARD LVCMOS33} [get_ports {leds[3]}]

## Clock relationship cleanup
## PS FCLK0/FCLK1 are treated as asynchronous domains in this design.
set_clock_groups -asynchronous -group {clk_fpga_0} -group {clk_fpga_1}

## Async GPIO / board-level debug/status pins: no external timing budget is enforced.
set_false_path -from [get_ports {btns[*] sws[*] hdmi_tx_hpd HDMI_DDC_IIC_scl_io HDMI_DDC_IIC_sda_io}]
set_false_path -to [get_ports {leds[*] ac_mclk ac_bclk ac_pblrc ac_pbdat ac_muten}]
set_false_path -to [get_ports {tmds_tx_clk_p tmds_tx_clk_n tmds_tx_data_p[*] tmds_tx_data_n[*]}]
set_false_path -to [get_ports {HDMI_DDC_IIC_scl_io HDMI_DDC_IIC_sda_io}]

