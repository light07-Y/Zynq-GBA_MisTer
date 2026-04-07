module SyncRamDualByteEnable_core #(
  parameter int BYTE_WIDTH = 8,
  parameter int ADDR_WIDTH = 6,
  parameter int BYTES = 4
) (
  input  wire                           clk,
  input  wire [ADDR_WIDTH-1:0]         addr_a,
  input  wire [BYTES*BYTE_WIDTH-1:0]   datain_a,
  output wire [BYTES*BYTE_WIDTH-1:0]   dataout_a,
  input  wire                           we_a,
  input  wire [BYTES-1:0]              be_a,
  input  wire [ADDR_WIDTH-1:0]         addr_b,
  input  wire [BYTES*BYTE_WIDTH-1:0]   datain_b,
  output wire [BYTES*BYTE_WIDTH-1:0]   dataout_b,
  input  wire                           we_b,
  input  wire [BYTES-1:0]              be_b
);

  localparam int DATA_WIDTH  = BYTES * BYTE_WIDTH;
  localparam int MEMORY_SIZE = DATA_WIDTH * (1 << ADDR_WIDTH);

  wire [BYTES-1:0] wea = we_a ? be_a : {BYTES{1'b0}};
  wire [BYTES-1:0] web = we_b ? be_b : {BYTES{1'b0}};

  // Industry practice for Vivado: use XPM memory macros for deterministic BRAM inference.
  xpm_memory_tdpram #(
    .ADDR_WIDTH_A            (ADDR_WIDTH),
    .ADDR_WIDTH_B            (ADDR_WIDTH),
    .AUTO_SLEEP_TIME         (0),
    .BYTE_WRITE_WIDTH_A      (BYTE_WIDTH),
    .BYTE_WRITE_WIDTH_B      (BYTE_WIDTH),
    .CASCADE_HEIGHT          (0),
    .CLOCKING_MODE           ("common_clock"),
    .ECC_MODE                ("no_ecc"),
    .MEMORY_INIT_FILE        ("none"),
    .MEMORY_INIT_PARAM       (""),
    .MEMORY_OPTIMIZATION     ("true"),
    .MEMORY_PRIMITIVE        ("block"),
    .MEMORY_SIZE             (MEMORY_SIZE),
    .MESSAGE_CONTROL         (0),
    .READ_DATA_WIDTH_A       (DATA_WIDTH),
    .READ_DATA_WIDTH_B       (DATA_WIDTH),
    .READ_LATENCY_A          (1),
    .READ_LATENCY_B          (1),
    .READ_RESET_VALUE_A      ("0"),
    .READ_RESET_VALUE_B      ("0"),
    .RST_MODE_A              ("SYNC"),
    .RST_MODE_B              ("SYNC"),
    .SIM_ASSERT_CHK          (0),
    .USE_EMBEDDED_CONSTRAINT (0),
    .USE_MEM_INIT            (1),
    .WAKEUP_TIME             ("disable_sleep"),
    .WRITE_DATA_WIDTH_A      (DATA_WIDTH),
    .WRITE_DATA_WIDTH_B      (DATA_WIDTH),
    .WRITE_MODE_A            ("write_first"),
    .WRITE_MODE_B            ("write_first")
  ) i_tdpram (
    .addra          (addr_a),
    .addrb          (addr_b),
    .clka           (clk),
    .clkb           (clk),
    .dina           (datain_a),
    .dinb           (datain_b),
    .douta          (dataout_a),
    .doutb          (dataout_b),
    .ena            (1'b1),
    .enb            (1'b1),
    .injectdbiterra (1'b0),
    .injectdbiterrb (1'b0),
    .injectsbiterra (1'b0),
    .injectsbiterrb (1'b0),
    .regcea         (1'b1),
    .regceb         (1'b1),
    .rsta           (1'b0),
    .rstb           (1'b0),
    .sleep          (1'b0),
    .wea            (wea),
    .web            (web),
    .dbiterra       (),
    .dbiterrb       (),
    .sbiterra       (),
    .sbiterrb       ()
  );

endmodule
