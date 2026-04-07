// Lint-only stubs for Cyclone V HPS interface primitives used by MiSTer sources.
// These modules are placeholders to allow Vivado RTL elaboration when
// vendor-specific Intel primitives are not available in this project.

module cyclonev_hps_interface_mpu_general_purpose (
    input  wire [31:0] gp_in,
    output wire [31:0] gp_out
);
    assign gp_out = 32'h00000000;
endmodule

module cyclonev_hps_interface_peripheral_uart (
    input  wire ri,
    input  wire dsr,
    input  wire dcd,
    output wire dtr,
    input  wire cts,
    output wire rts,
    input  wire rxd,
    output wire txd
);
    assign dtr = 1'b0;
    assign rts = 1'b0;
    assign txd = 1'b1;
endmodule

module cyclonev_hps_interface_interrupts (
    input wire [63:0] irq
);
endmodule

module cyclonev_hps_interface_peripheral_i2c (
    output wire out_clk,
    inout  wire scl,
    output wire out_data,
    inout  wire sda
);
    assign out_clk = 1'b0;
    assign out_data = 1'b0;
endmodule

module cyclonev_hps_interface_peripheral_spi_master (
    output wire sclk_out,
    output wire txd,
    input  wire rxd,
    output wire ss_0_n,
    input  wire ss_in_n
);
    assign sclk_out = 1'b0;
    assign txd = 1'b0;
    assign ss_0_n = 1'b1;
endmodule

module cyclonev_hps_interface_clocks_resets (
    input wire [0:0] f2h_warm_rst_req_n,
    input wire [0:0] f2h_pending_rst_ack,
    input wire [0:0] f2h_dbg_rst_req_n,
    input wire [0:0] h2f_rst_n,
    input wire [0:0] f2h_cold_rst_req_n,
    input wire [0:0] h2f_user0_clk
);
endmodule

module cyclonev_hps_interface_dbg_apb (
    input wire [0:0] DBG_APB_DISABLE,
    input wire [0:0] P_CLK_EN
);
endmodule

module cyclonev_hps_interface_tpiu_trace (
    input wire [0:0] traceclk_ctl
);
endmodule

module cyclonev_hps_interface_boot_from_fpga (
    input wire [0:0] boot_from_fpga_ready,
    input wire [0:0] boot_from_fpga_on_failure,
    input wire [0:0] bsel_en,
    input wire [0:0] csel_en,
    input wire [1:0] csel,
    input wire [2:0] bsel
);
endmodule

module cyclonev_hps_interface_fpga2hps (
    input wire [1:0] port_size_config
);
endmodule

module cyclonev_hps_interface_hps2fpga (
    input wire [1:0] port_size_config
);
endmodule

module cyclonev_hps_interface_fpga2sdram (
    input wire [15:0] cfg_rfifo_cport_map,
    input wire [15:0] cfg_wfifo_cport_map,
    input wire [0:0] rd_ready_3,
    input wire [0:0] cmd_port_clk_2,
    input wire [0:0] rd_ready_2,
    input wire [0:0] cmd_port_clk_1,
    input wire [0:0] rd_ready_1,
    input wire [0:0] cmd_port_clk_0,
    input wire [0:0] rd_ready_0,
    input wire [0:0] wrack_ready_2,
    input wire [0:0] wrack_ready_1,
    input wire [0:0] wrack_ready_0,
    input wire [0:0] cmd_ready_2,
    input wire [0:0] cmd_ready_1,
    input wire [0:0] cmd_ready_0,
    input wire [11:0] cfg_port_width,
    input wire [0:0] rd_valid_3,
    input wire [0:0] rd_valid_2,
    input wire [0:0] rd_valid_1,
    input wire [0:0] rd_clk_3,
    input wire [63:0] rd_data_3,
    input wire [0:0] rd_clk_2,
    input wire [63:0] rd_data_2,
    input wire [0:0] rd_clk_1,
    input wire [63:0] rd_data_1,
    input wire [0:0] rd_clk_0,
    input wire [63:0] rd_data_0,
    input wire [5:0] cfg_axi_mm_select,
    input wire [0:0] cmd_valid_2,
    input wire [0:0] cmd_valid_1,
    input wire [0:0] cmd_valid_0,
    input wire [17:0] cfg_cport_rfifo_map,
    input wire [89:0] wr_data_3,
    input wire [89:0] wr_data_2,
    input wire [89:0] wr_data_1,
    input wire [11:0] cfg_cport_type,
    input wire [89:0] wr_data_0,
    input wire [17:0] cfg_cport_wfifo_map,
    input wire [0:0] wr_clk_3,
    input wire [0:0] wr_clk_2,
    input wire [0:0] wr_clk_1,
    input wire [0:0] wr_clk_0,
    input wire [59:0] cmd_data_2,
    input wire [59:0] cmd_data_1,
    input wire [59:0] cmd_data_0
);
endmodule

module cyclonev_clkselect (
    input  wire [1:0] clkselect,
    input  wire [3:0] inclk,
    output wire       outclk
);
    assign outclk = clkselect[0] ? inclk[1] : inclk[0];
endmodule

module pll_hdmi_0002 (
    input  wire        refclk,
    input  wire        rst,
    output wire        outclk_0,
    output wire        locked,
    input  wire [63:0] reconfig_to_pll,
    output wire [63:0] reconfig_from_pll
);
    assign outclk_0 = refclk;
    assign locked = ~rst;
    assign reconfig_from_pll = 64'h0;
endmodule

module pll_audio_0002 (
    input  wire refclk,
    input  wire rst,
    output wire outclk_0,
    output wire locked
);
    assign outclk_0 = refclk;
    assign locked = ~rst;
endmodule

module pll_0002 (
    input  wire refclk,
    input  wire rst,
    output wire outclk_0,
    output wire outclk_1,
    output wire locked
);
    wire clkfb;
    wire clkfb_buf;
    wire clk0_raw;
    wire clk1_raw;

    MMCME2_ADV #(
        .BANDWIDTH("OPTIMIZED"),
        .CLKFBOUT_MULT_F(20.0),
        .CLKFBOUT_PHASE(0.0),
        .CLKIN1_PERIOD(20.000),
        .CLKIN2_PERIOD(0.000),
        .CLKOUT0_DIVIDE_F(10.0),
        .CLKOUT0_DUTY_CYCLE(0.5),
        .CLKOUT0_PHASE(0.0),
        .CLKOUT1_DIVIDE(20),
        .CLKOUT1_DUTY_CYCLE(0.5),
        .CLKOUT1_PHASE(0.0),
        .COMPENSATION("ZHOLD"),
        .DIVCLK_DIVIDE(1),
        .REF_JITTER1(0.010),
        .STARTUP_WAIT("FALSE")
    ) mmcm_i (
        .CLKFBIN(clkfb_buf),
        .CLKFBOUT(clkfb),
        .CLKFBOUTB(),
        .CLKIN1(refclk),
        .CLKIN2(1'b0),
        .CLKINSEL(1'b1),
        .CLKOUT0(clk0_raw),
        .CLKOUT0B(),
        .CLKOUT1(clk1_raw),
        .CLKOUT1B(),
        .CLKOUT2(),
        .CLKOUT2B(),
        .CLKOUT3(),
        .CLKOUT3B(),
        .CLKOUT4(),
        .CLKOUT5(),
        .CLKOUT6(),
        .DADDR(7'h00),
        .DCLK(1'b0),
        .DEN(1'b0),
        .DI(16'h0000),
        .DO(),
        .DRDY(),
        .DWE(1'b0),
        .LOCKED(locked),
        .PWRDWN(1'b0),
        .RST(rst),
        .PSCLK(1'b0),
        .PSEN(1'b0),
        .PSINCDEC(1'b0),
        .PSDONE(),
        .CLKINSTOPPED(),
        .CLKFBSTOPPED()
    );

    BUFG clkfb_bufg (
        .I(clkfb),
        .O(clkfb_buf)
    );

    BUFG clk0_bufg (
        .I(clk0_raw),
        .O(outclk_0)
    );

    BUFG clk1_bufg (
        .I(clk1_raw),
        .O(outclk_1)
    );
endmodule

module pll_cfg_hdmi (
    input  wire        mgmt_clk,
    input  wire        mgmt_reset,
    output wire [63:0] reconfig_to_pll,
    input  wire [63:0] reconfig_from_pll,
    output wire        mgmt_waitrequest,
    input  wire [5:0]  mgmt_address,
    input  wire        mgmt_write,
    input  wire [31:0] mgmt_writedata
);
    assign reconfig_to_pll = 64'h0;
    assign mgmt_waitrequest = 1'b0;
endmodule

module altddio_out #(
    parameter extend_oe_disable = "OFF",
    parameter intended_device_family = "Cyclone V",
    parameter invert_output = "OFF",
    parameter lpm_hint = "UNUSED",
    parameter lpm_type = "altddio_out",
    parameter oe_reg = "UNREGISTERED",
    parameter power_up_high = "OFF",
    parameter width = 1
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
    assign dataout = datain_h;
endmodule

module altsyncram #(
    parameter address_reg_b = "CLOCK1",
    parameter clock_enable_input_a = "BYPASS",
    parameter clock_enable_input_b = "BYPASS",
    parameter clock_enable_output_a = "BYPASS",
    parameter clock_enable_output_b = "BYPASS",
    parameter indata_reg_b = "CLOCK1",
    parameter intended_device_family = "Cyclone V",
    parameter lpm_type = "altsyncram",
    parameter numwords_a = 256,
    parameter numwords_b = 256,
    parameter operation_mode = "BIDIR_DUAL_PORT",
    parameter outdata_aclr_a = "NONE",
    parameter outdata_aclr_b = "NONE",
    parameter outdata_reg_a = "UNREGISTERED",
    parameter outdata_reg_b = "UNREGISTERED",
    parameter power_up_uninitialized = "FALSE",
    parameter read_during_write_mode_port_a = "NEW_DATA_NO_NBE_READ",
    parameter read_during_write_mode_port_b = "NEW_DATA_NO_NBE_READ",
    parameter widthad_a = 8,
    parameter widthad_b = 8,
    parameter width_a = 16,
    parameter width_b = 16,
    parameter width_byteena_a = 1,
    parameter width_byteena_b = 1
) (
    input  wire [widthad_a-1:0]  address_a,
    input  wire [widthad_b-1:0]  address_b,
    input  wire                  clock0,
    input  wire                  clock1,
    input  wire [width_a-1:0]    data_a,
    input  wire [width_b-1:0]    data_b,
    input  wire                  wren_a,
    input  wire                  wren_b,
    output wire [width_a-1:0]    q_a,
    output wire [width_b-1:0]    q_b,
    input  wire [width_byteena_a-1:0] byteena_a,
    input  wire [width_byteena_b-1:0] byteena_b,
    input  wire                  clocken0,
    input  wire                  clocken1,
    input  wire                  clocken2,
    input  wire                  clocken3,
    input  wire                  aclr0,
    input  wire                  aclr1,
    input  wire                  addressstall_a,
    input  wire                  addressstall_b,
    input  wire                  rden_a,
    input  wire                  rden_b,
    output wire [15:0]           eccstatus
);
    localparam integer MEMORY_SIZE = width_a * numwords_a;
    wire [width_byteena_a-1:0] wea = wren_a ? byteena_a : {width_byteena_a{1'b0}};
    wire [width_byteena_b-1:0] web = wren_b ? byteena_b : {width_byteena_b{1'b0}};

    assign eccstatus = 16'h0000;

    xpm_memory_tdpram #(
        .ADDR_WIDTH_A(widthad_a),
        .ADDR_WIDTH_B(widthad_b),
        .AUTO_SLEEP_TIME(0),
        .BYTE_WRITE_WIDTH_A(width_a),
        .BYTE_WRITE_WIDTH_B(width_b),
        .CASCADE_HEIGHT(0),
        .CLOCKING_MODE("independent_clock"),
        .ECC_MODE("no_ecc"),
        .MEMORY_INIT_FILE("none"),
        .MEMORY_INIT_PARAM(""),
        .MEMORY_OPTIMIZATION("true"),
        .MEMORY_PRIMITIVE("block"),
        .MEMORY_SIZE(MEMORY_SIZE),
        .MESSAGE_CONTROL(0),
        .READ_DATA_WIDTH_A(width_a),
        .READ_DATA_WIDTH_B(width_b),
        .READ_LATENCY_A(1),
        .READ_LATENCY_B(1),
        .READ_RESET_VALUE_A("0"),
        .READ_RESET_VALUE_B("0"),
        .RST_MODE_A("SYNC"),
        .RST_MODE_B("SYNC"),
        .SIM_ASSERT_CHK(0),
        .USE_EMBEDDED_CONSTRAINT(0),
        .USE_MEM_INIT(1),
        .WAKEUP_TIME("disable_sleep"),
        .WRITE_DATA_WIDTH_A(width_a),
        .WRITE_DATA_WIDTH_B(width_b),
        .WRITE_MODE_A("write_first"),
        .WRITE_MODE_B("write_first")
    ) i_tdpram (
        .addra(address_a),
        .addrb(address_b),
        .clka(clock0),
        .clkb(clock1),
        .dina(data_a),
        .dinb(data_b),
        .douta(q_a),
        .doutb(q_b),
        .ena(clocken0),
        .enb(clocken1),
        .injectdbiterra(1'b0),
        .injectdbiterrb(1'b0),
        .injectsbiterra(1'b0),
        .injectsbiterrb(1'b0),
        .regcea(clocken0),
        .regceb(clocken1),
        .rsta(aclr0),
        .rstb(aclr1),
        .sleep(1'b0),
        .wea(wea),
        .web(web),
        .dbiterra(),
        .dbiterrb(),
        .sbiterra(),
        .sbiterrb()
    );
endmodule
