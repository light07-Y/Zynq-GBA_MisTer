`timescale 1 ps / 1 ps

// Xilinx-compatible replacement for Intel/Altera altddio_out.
module altddio_out #(
	parameter extend_oe_disable      = "OFF",
	parameter intended_device_family = "Cyclone V",
	parameter invert_output          = "OFF",
	parameter lpm_hint               = "UNUSED",
	parameter lpm_type               = "altddio_out",
	parameter oe_reg                 = "UNREGISTERED",
	parameter power_up_high          = "OFF",
	parameter integer width                 = 1
) (
	input  wire [width-1:0] datain_h,
	input  wire [width-1:0] datain_l,
	input  wire             outclock,
	output wire [width-1:0] dataout,
	input  wire             aclr,
	input  wire             aset,
	input  wire             oe,
	input  wire             outclocken,
	input  wire             sclr,
	input  wire             sset
);

	wire rst_i = aclr | sclr;
	wire set_i = (aset | sset) & ~rst_i;
	wire ce_i  = oe & outclocken;

	genvar i;
	generate
		for (i = 0; i < width; i = i + 1) begin : g_oddr
			ODDR #(
				.DDR_CLK_EDGE("SAME_EDGE"),
				.INIT(1'b0),
				.SRTYPE("ASYNC")
			) oddr_i (
				.C(outclock),
				.CE(ce_i),
				.D1(datain_h[i]),
				.D2(datain_l[i]),
				.Q(dataout[i]),
				.R(rst_i),
				.S(set_i)
			);
		end
	endgenerate

endmodule
