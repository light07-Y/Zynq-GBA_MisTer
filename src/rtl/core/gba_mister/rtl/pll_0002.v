`timescale 1 ps / 1 ps

// Xilinx 7-series replacement for the legacy Altera-generated pll_0002.
// Tuned close to original targets from legacy metadata:
// outclk_0 ~= 100.663296 MHz, outclk_1 ~= 50.331648 MHz @ -60 deg.
//
// This MMCM configuration yields:
// outclk_0 = 100.657895 MHz, outclk_1 = 50.328947 MHz.
module pll_0002 (
	input  wire refclk,
	input  wire rst,
	output wire outclk_0,
	output wire outclk_1,
	output wire locked
);

	wire refclk_bufg;
	wire clkfb;
	wire clkout0_pll;
	wire clkout1_pll;

	BUFG refclk_bufg_i (
		.I(refclk),
		.O(refclk_bufg)
	);

	MMCME2_ADV #(
		.BANDWIDTH("OPTIMIZED"),
		.CLKIN1_PERIOD(20.000),
		.CLKIN2_PERIOD(0.0),
		.CLKFBOUT_MULT_F(19.125),
		.CLKFBOUT_PHASE(0.0),
		.DIVCLK_DIVIDE(1),
		.CLKOUT0_DIVIDE_F(9.5),
		.CLKOUT0_PHASE(0.0),
		.CLKOUT0_DUTY_CYCLE(0.5),
		.CLKOUT1_DIVIDE(19),
		.CLKOUT1_PHASE(-60.0),
		.CLKOUT1_DUTY_CYCLE(0.5),
		.CLKOUT2_DIVIDE(1),
		.CLKOUT2_PHASE(0.0),
		.CLKOUT2_DUTY_CYCLE(0.5),
		.CLKOUT3_DIVIDE(1),
		.CLKOUT3_PHASE(0.0),
		.CLKOUT3_DUTY_CYCLE(0.5),
		.CLKOUT4_DIVIDE(1),
		.CLKOUT4_PHASE(0.0),
		.CLKOUT4_DUTY_CYCLE(0.5),
		.CLKOUT5_DIVIDE(1),
		.CLKOUT5_PHASE(0.0),
		.CLKOUT5_DUTY_CYCLE(0.5),
		.CLKOUT6_DIVIDE(1),
		.CLKOUT6_PHASE(0.0),
		.CLKOUT6_DUTY_CYCLE(0.5),
		.REF_JITTER1(0.010),
		.REF_JITTER2(0.010),
		.COMPENSATION("ZHOLD"),
		.IS_CLKINSEL_INVERTED(1'b0),
		.IS_PSEN_INVERTED(1'b0),
		.IS_PSINCDEC_INVERTED(1'b0),
		.IS_PWRDWN_INVERTED(1'b0),
		.IS_RST_INVERTED(1'b0),
		.STARTUP_WAIT("FALSE")
	) mmcm_i (
		.CLKIN1(refclk_bufg),
		.CLKIN2(1'b0),
		.CLKINSEL(1'b1),
		.CLKFBIN(clkfb),
		.RST(rst),
		.PWRDWN(1'b0),
		.PSCLK(1'b0),
		.PSEN(1'b0),
		.PSINCDEC(1'b0),
		.CLKFBOUT(clkfb),
		.CLKOUT0(clkout0_pll),
		.CLKOUT1(clkout1_pll),
		.CLKOUT2(),
		.CLKOUT3(),
		.CLKOUT4(),
		.CLKOUT5(),
		.CLKOUT6(),
		.DO(),
		.DRDY(),
		.PSDONE(),
		.LOCKED(locked)
	);

	BUFG outclk0_bufg_i (
		.I(clkout0_pll),
		.O(outclk_0)
	);

	BUFG outclk1_bufg_i (
		.I(clkout1_pll),
		.O(outclk_1)
	);

endmodule
