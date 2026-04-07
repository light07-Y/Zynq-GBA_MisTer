module axi_lite_ctrl_regs #(
  parameter int ADDR_W = 12,
  parameter int DATA_W = 32
) (
  input  logic                 clk,
  input  logic                 rst_n,

  input  logic [ADDR_W-1:0]    s_axi_awaddr,
  input  logic                 s_axi_awvalid,
  output logic                 s_axi_awready,
  input  logic [DATA_W-1:0]    s_axi_wdata,
  input  logic [(DATA_W/8)-1:0] s_axi_wstrb,
  input  logic                 s_axi_wvalid,
  output logic                 s_axi_wready,
  output logic [1:0]           s_axi_bresp,
  output logic                 s_axi_bvalid,
  input  logic                 s_axi_bready,

  input  logic [ADDR_W-1:0]    s_axi_araddr,
  input  logic                 s_axi_arvalid,
  output logic                 s_axi_arready,
  output logic [DATA_W-1:0]    s_axi_rdata,
  output logic [1:0]           s_axi_rresp,
  output logic                 s_axi_rvalid,
  input  logic                 s_axi_rready,

  input  logic [31:0]          stat_cycles_missing,
  input  logic [31:0]          stat_cycles_vsync_speed,
  input  logic                 stat_fb_underflow,
  input  logic [1:0]           stat_fb_frame_idx,
  
  // High-level status
  input  logic                 sys_rom_loading,
  input  logic [31:0]          sys_error_in,

  // Granular Interrupt signals
  input  logic                 irq_vsync_pulse,
  input  logic                 irq_error_pulse,
  output logic                 irq_out,

  output logic [31:0]          cfg_ctrl,
  output logic [9:0]           cfg_keys,
  output logic [24:0]          cfg_max_pak_addr,
  output logic [15:0]          cfg_cycle_precalc,
  output logic [31:0]          cfg_rtc_timestamp,
  output logic                 cfg_sw_reset,
  output logic                 cfg_commit_toggle
);

  localparam logic [ADDR_W-1:0] REG_CTRL            = 12'h000;
  localparam logic [ADDR_W-1:0] REG_KEYS            = 12'h004;
  localparam logic [ADDR_W-1:0] REG_MAX_PAK_ADDR    = 12'h008;
  localparam logic [ADDR_W-1:0] REG_CYCLE_PRECALC   = 12'h00C;
  localparam logic [ADDR_W-1:0] REG_RTC_TIMESTAMP   = 12'h010;
  localparam logic [ADDR_W-1:0] REG_COMMIT          = 12'h014;
  localparam logic [ADDR_W-1:0] REG_STATUS0         = 12'h018;
  localparam logic [ADDR_W-1:0] REG_STATUS1         = 12'h01C;
  localparam logic [ADDR_W-1:0] REG_SW_RESET        = 12'h020;
  localparam logic [ADDR_W-1:0] REG_ROM_STATUS      = 12'h024;
  localparam logic [ADDR_W-1:0] REG_ERROR_LATCH     = 12'h028;
  localparam logic [ADDR_W-1:0] REG_IRQ_EN          = 12'h02C;
  localparam logic [ADDR_W-1:0] REG_IRQ_STS         = 12'h030;

  logic [31:0] shadow_ctrl;
  logic [9:0]  shadow_keys;
  logic [24:0] shadow_max_pak_addr;
  logic [15:0] shadow_cycle_precalc;
  logic [31:0] shadow_rtc_timestamp;
  
  logic [31:0] irq_en;
  logic [1:0]  irq_sts; // [1] error, [0] vsync
  logic [31:0] error_latch;
  
  assign irq_out = |(irq_sts & irq_en[1:0]);

  logic [ADDR_W-1:0] awaddr_latched;
  logic awaddr_valid;

  function automatic logic [31:0] apply_wstrb(
    input logic [31:0] old_v,
    input logic [31:0] new_v,
    input logic [3:0]  wstrb
  );
    logic [31:0] mask;
  begin
    mask = {
      {8{wstrb[3]}},
      {8{wstrb[2]}},
      {8{wstrb[1]}},
      {8{wstrb[0]}}
    };
    apply_wstrb = (old_v & ~mask) | (new_v & mask);
  end
  endfunction

  always_ff @(posedge clk) begin
    logic wr_fire;
    logic rd_fire;
    logic [31:0] rdata_next;
    logic [31:0] merged32;

    if (!rst_n) begin
      shadow_ctrl          <= 32'h0000_1600;
      shadow_keys          <= '0;
      shadow_max_pak_addr  <= '0;
      shadow_cycle_precalc <= 16'd100;
      shadow_rtc_timestamp <= '0;

      cfg_ctrl             <= 32'h0000_1600;
      cfg_keys             <= '0;
      cfg_max_pak_addr     <= '0;
      cfg_cycle_precalc    <= 16'd100;
      cfg_rtc_timestamp    <= '0;
      cfg_sw_reset         <= 1'b0;
      cfg_commit_toggle    <= 1'b0;

      irq_en               <= 32'h0;
      irq_sts              <= 2'b0;
      error_latch          <= 32'h0;

      s_axi_awready        <= 1'b0;
      s_axi_wready         <= 1'b0;
      s_axi_bresp          <= 2'b00;
      s_axi_bvalid         <= 1'b0;

      s_axi_arready        <= 1'b0;
      s_axi_rresp          <= 2'b00;
      s_axi_rvalid         <= 1'b0;
      s_axi_rdata          <= 32'h0;

      awaddr_latched       <= '0;
      awaddr_valid         <= 1'b0;
    end else begin
      s_axi_awready <= 1'b0;
      s_axi_wready  <= 1'b0;
      s_axi_arready <= 1'b0;

      wr_fire = 1'b0;
      rd_fire = 1'b0;

      // Handle IRQ pulses (sticky status)
      if (irq_vsync_pulse) irq_sts[0] <= 1'b1;
      if (irq_error_pulse) irq_sts[1] <= 1'b1;

      // Capture error if latch is empty
      if (sys_error_in != 0 && error_latch == 0) begin
        error_latch <= sys_error_in;
      end

      if (!awaddr_valid && s_axi_awvalid) begin
        awaddr_latched <= s_axi_awaddr;
        awaddr_valid   <= 1'b1;
        s_axi_awready  <= 1'b1;
      end

      if (awaddr_valid && s_axi_wvalid && !s_axi_bvalid) begin
        s_axi_wready <= 1'b1;
        wr_fire      = 1'b1;
      end

      if (wr_fire) begin
        unique case (awaddr_latched)
          REG_CTRL: begin
            shadow_ctrl <= apply_wstrb(shadow_ctrl, s_axi_wdata, s_axi_wstrb[3:0]);
          end
          REG_KEYS: begin
            merged32   = apply_wstrb({22'd0, shadow_keys}, s_axi_wdata, s_axi_wstrb[3:0]);
            shadow_keys <= merged32[9:0];
          end
          REG_MAX_PAK_ADDR: begin
            merged32          = apply_wstrb({7'd0, shadow_max_pak_addr}, s_axi_wdata, s_axi_wstrb[3:0]);
            shadow_max_pak_addr <= merged32[24:0];
          end
          REG_CYCLE_PRECALC: begin
            merged32            = apply_wstrb({16'd0, shadow_cycle_precalc}, s_axi_wdata, s_axi_wstrb[3:0]);
            shadow_cycle_precalc <= merged32[15:0];
          end
          REG_RTC_TIMESTAMP: begin
            shadow_rtc_timestamp <= apply_wstrb(shadow_rtc_timestamp, s_axi_wdata, s_axi_wstrb[3:0]);
          end
          REG_COMMIT: begin
            cfg_ctrl          <= shadow_ctrl;
            cfg_keys          <= shadow_keys;
            cfg_max_pak_addr  <= shadow_max_pak_addr;
            cfg_cycle_precalc <= shadow_cycle_precalc;
            cfg_rtc_timestamp <= shadow_rtc_timestamp;
            cfg_commit_toggle <= ~cfg_commit_toggle;
          end
          REG_SW_RESET: begin
            cfg_sw_reset <= s_axi_wdata[0];
          end
          REG_ERROR_LATCH: begin
            // Write 1 to clear current error latch
            if (s_axi_wdata[0]) error_latch <= 32'h0;
          end
          REG_IRQ_EN: begin
            irq_en <= apply_wstrb(irq_en, s_axi_wdata, s_axi_wstrb[3:0]);
          end
          REG_IRQ_STS: begin
            // W1C for status bits
            if (s_axi_wdata[0]) irq_sts[0] <= 1'b0;
            if (s_axi_wdata[1]) irq_sts[1] <= 1'b0;
          end
          default: begin
          end
        endcase

        s_axi_bvalid   <= 1'b1;
        s_axi_bresp    <= 2'b00;
        awaddr_valid   <= 1'b0;
      end

      if (s_axi_bvalid && s_axi_bready) begin
        s_axi_bvalid <= 1'b0;
      end

      if (!s_axi_rvalid && s_axi_arvalid) begin
        s_axi_arready <= 1'b1;
        rd_fire       = 1'b1;
      end

      if (rd_fire) begin
        unique case (s_axi_araddr)
          REG_CTRL:          rdata_next = cfg_ctrl;
          REG_KEYS:          rdata_next = {22'd0, cfg_keys};
          REG_MAX_PAK_ADDR:  rdata_next = {7'd0, cfg_max_pak_addr};
          REG_CYCLE_PRECALC: rdata_next = {16'd0, cfg_cycle_precalc};
          REG_RTC_TIMESTAMP: rdata_next = cfg_rtc_timestamp;
          REG_STATUS0:       rdata_next = {31'd0, stat_fb_underflow};
          REG_STATUS1:       rdata_next = {stat_cycles_vsync_speed[15:0], stat_cycles_missing[13:0], stat_fb_frame_idx};
          REG_SW_RESET:      rdata_next = {31'd0, cfg_sw_reset};
          REG_ROM_STATUS:    rdata_next = {31'd0, sys_rom_loading};
          REG_ERROR_LATCH:   rdata_next = error_latch;
          REG_IRQ_EN:        rdata_next = irq_en;
          REG_IRQ_STS:       rdata_next = {30'd0, irq_sts};
          default:           rdata_next = 32'h0;
        endcase

        s_axi_rdata  <= rdata_next;
        s_axi_rresp  <= 2'b00;
        s_axi_rvalid <= 1'b1;
      end

      if (s_axi_rvalid && s_axi_rready) begin
        s_axi_rvalid <= 1'b0;
      end
    end
  end

endmodule
