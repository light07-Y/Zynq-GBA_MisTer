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
  input  logic [9:0]           stat_physical_keys,
  input  logic [31:0]          stat_debug_cpu_pc,
  input  logic [31:0]          stat_debug_cpu_mixed,
  input  logic [31:0]          stat_debug_irq,
  input  logic [31:0]          stat_debug_dma,
  input  logic [31:0]          stat_debug_mem,
  input  logic [31:0]          stat_dbg_chain_flags,
  input  logic [31:0]          stat_dbg_chain_counts0,
  input  logic [31:0]          stat_dbg_chain_counts1,
  input  logic [31:0]          stat_dbg_ch1_first_addr,
  input  logic [31:0]          stat_dbg_ch1_first_meta,
  input  logic [31:0]          stat_dbg_ch1_last_addr,
  input  logic [31:0]          stat_dbg_ch1_last_meta,
  input  logic [31:0]          stat_dbg_ddr_first_addr,
  input  logic [31:0]          stat_dbg_ddr_first_meta,
  input  logic [31:0]          stat_dbg_ddr_last_addr,
  input  logic [31:0]          stat_dbg_ddr_last_meta,
  input  logic [31:0]          stat_dbg_axi_ar_first_addr,
  input  logic [31:0]          stat_dbg_axi_ar_first_meta,
  input  logic [31:0]          stat_dbg_axi_ar_last_addr,
  input  logic [31:0]          stat_dbg_axi_ar_last_meta,
  input  logic [31:0]          stat_dbg_axi_r_first_addr,
  input  logic [31:0]          stat_dbg_axi_r_first_meta,
  input  logic [31:0]          stat_dbg_axi_r_last_addr,
  input  logic [31:0]          stat_dbg_axi_r_last_meta,
  input  logic [31:0]          stat_dbg_done_first_addr,
  input  logic [31:0]          stat_dbg_done_first_meta,
  input  logic [31:0]          stat_dbg_done_last_addr,
  input  logic [31:0]          stat_dbg_done_last_meta,
  
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
  output logic [1:0]           cfg_display_frame_idx,
  output logic                 cfg_sw_reset,
  output logic                 cfg_commit_toggle,
  input  logic [31:0]          stat_fbcap_frame_seq,
  input  logic                 stat_fbcap_frame_buf_idx
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
  localparam logic [ADDR_W-1:0] REG_DEBUG_CPU_PC    = 12'h034;
  localparam logic [ADDR_W-1:0] REG_DEBUG_CPU_MIX   = 12'h038;
  localparam logic [ADDR_W-1:0] REG_DEBUG_IRQ       = 12'h03C;
  localparam logic [ADDR_W-1:0] REG_DEBUG_DMA       = 12'h040;
  localparam logic [ADDR_W-1:0] REG_DEBUG_MEM       = 12'h044;
  localparam logic [ADDR_W-1:0] REG_DISPLAY_FRAME   = 12'h048;
  localparam logic [ADDR_W-1:0] REG_DBG_CHAIN_FLAGS       = 12'h04C;
  localparam logic [ADDR_W-1:0] REG_DBG_CHAIN_COUNTS0     = 12'h050;
  localparam logic [ADDR_W-1:0] REG_DBG_CHAIN_COUNTS1     = 12'h054;
  localparam logic [ADDR_W-1:0] REG_DBG_CH1_FIRST_ADDR    = 12'h058;
  localparam logic [ADDR_W-1:0] REG_DBG_CH1_FIRST_META    = 12'h05C;
  localparam logic [ADDR_W-1:0] REG_DBG_CH1_LAST_ADDR     = 12'h060;
  localparam logic [ADDR_W-1:0] REG_DBG_CH1_LAST_META     = 12'h064;
  localparam logic [ADDR_W-1:0] REG_DBG_DDR_FIRST_ADDR    = 12'h068;
  localparam logic [ADDR_W-1:0] REG_DBG_DDR_FIRST_META    = 12'h06C;
  localparam logic [ADDR_W-1:0] REG_DBG_DDR_LAST_ADDR     = 12'h070;
  localparam logic [ADDR_W-1:0] REG_DBG_DDR_LAST_META     = 12'h074;
  localparam logic [ADDR_W-1:0] REG_DBG_AXI_AR_FIRST_ADDR = 12'h078;
  localparam logic [ADDR_W-1:0] REG_DBG_AXI_AR_FIRST_META = 12'h07C;
  localparam logic [ADDR_W-1:0] REG_DBG_AXI_AR_LAST_ADDR  = 12'h080;
  localparam logic [ADDR_W-1:0] REG_DBG_AXI_AR_LAST_META  = 12'h084;
  localparam logic [ADDR_W-1:0] REG_DBG_AXI_R_FIRST_ADDR  = 12'h088;
  localparam logic [ADDR_W-1:0] REG_DBG_AXI_R_FIRST_META  = 12'h08C;
  localparam logic [ADDR_W-1:0] REG_DBG_AXI_R_LAST_ADDR   = 12'h090;
  localparam logic [ADDR_W-1:0] REG_DBG_AXI_R_LAST_META   = 12'h094;
  localparam logic [ADDR_W-1:0] REG_DBG_DONE_FIRST_ADDR   = 12'h098;
  localparam logic [ADDR_W-1:0] REG_DBG_DONE_FIRST_META   = 12'h09C;
  localparam logic [ADDR_W-1:0] REG_DBG_DONE_LAST_ADDR    = 12'h0A0;
  localparam logic [ADDR_W-1:0] REG_DBG_DONE_LAST_META    = 12'h0A4;
  localparam logic [ADDR_W-1:0] REG_FB_CAP_STATUS         = 12'h0A8;
  localparam logic [ADDR_W-1:0] REG_FB_CAP_SEQ            = 12'h0AC;

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
      shadow_ctrl          <= 32'h0000_1612;
      shadow_keys          <= '0;
      shadow_max_pak_addr  <= '0;
      shadow_cycle_precalc <= 16'd100;
      shadow_rtc_timestamp <= '0;

      cfg_ctrl             <= 32'h0000_1612;
      cfg_keys             <= '0;
      cfg_max_pak_addr     <= '0;
      cfg_cycle_precalc    <= 16'd100;
      cfg_rtc_timestamp    <= '0;
      cfg_display_frame_idx <= 2'd0;
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
          REG_DISPLAY_FRAME: begin
            cfg_display_frame_idx <= s_axi_wdata[1:0];
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
          REG_STATUS0:       rdata_next = {22'd0, stat_physical_keys};
          REG_STATUS1:       rdata_next = {stat_cycles_vsync_speed[15:0], stat_cycles_missing[13:0], stat_fb_frame_idx};
          REG_SW_RESET:      rdata_next = {31'd0, cfg_sw_reset};
          REG_ROM_STATUS:    rdata_next = {31'd0, sys_rom_loading};
          REG_ERROR_LATCH:   rdata_next = error_latch;
          REG_IRQ_EN:        rdata_next = irq_en;
          REG_IRQ_STS:       rdata_next = {30'd0, irq_sts};
          REG_DEBUG_CPU_PC:  rdata_next = stat_debug_cpu_pc;
          REG_DEBUG_CPU_MIX: rdata_next = stat_debug_cpu_mixed;
          REG_DEBUG_IRQ:     rdata_next = stat_debug_irq;
          REG_DEBUG_DMA:     rdata_next = stat_debug_dma;
          REG_DEBUG_MEM:     rdata_next = stat_debug_mem;
          REG_DISPLAY_FRAME: rdata_next = {30'd0, cfg_display_frame_idx};
          REG_DBG_CHAIN_FLAGS:       rdata_next = stat_dbg_chain_flags;
          REG_DBG_CHAIN_COUNTS0:     rdata_next = stat_dbg_chain_counts0;
          REG_DBG_CHAIN_COUNTS1:     rdata_next = stat_dbg_chain_counts1;
          REG_DBG_CH1_FIRST_ADDR:    rdata_next = stat_dbg_ch1_first_addr;
          REG_DBG_CH1_FIRST_META:    rdata_next = stat_dbg_ch1_first_meta;
          REG_DBG_CH1_LAST_ADDR:     rdata_next = stat_dbg_ch1_last_addr;
          REG_DBG_CH1_LAST_META:     rdata_next = stat_dbg_ch1_last_meta;
          REG_DBG_DDR_FIRST_ADDR:    rdata_next = stat_dbg_ddr_first_addr;
          REG_DBG_DDR_FIRST_META:    rdata_next = stat_dbg_ddr_first_meta;
          REG_DBG_DDR_LAST_ADDR:     rdata_next = stat_dbg_ddr_last_addr;
          REG_DBG_DDR_LAST_META:     rdata_next = stat_dbg_ddr_last_meta;
          REG_DBG_AXI_AR_FIRST_ADDR: rdata_next = stat_dbg_axi_ar_first_addr;
          REG_DBG_AXI_AR_FIRST_META: rdata_next = stat_dbg_axi_ar_first_meta;
          REG_DBG_AXI_AR_LAST_ADDR:  rdata_next = stat_dbg_axi_ar_last_addr;
          REG_DBG_AXI_AR_LAST_META:  rdata_next = stat_dbg_axi_ar_last_meta;
          REG_DBG_AXI_R_FIRST_ADDR:  rdata_next = stat_dbg_axi_r_first_addr;
          REG_DBG_AXI_R_FIRST_META:  rdata_next = stat_dbg_axi_r_first_meta;
          REG_DBG_AXI_R_LAST_ADDR:   rdata_next = stat_dbg_axi_r_last_addr;
          REG_DBG_AXI_R_LAST_META:   rdata_next = stat_dbg_axi_r_last_meta;
          REG_DBG_DONE_FIRST_ADDR:   rdata_next = stat_dbg_done_first_addr;
          REG_DBG_DONE_FIRST_META:   rdata_next = stat_dbg_done_first_meta;
          REG_DBG_DONE_LAST_ADDR:    rdata_next = stat_dbg_done_last_addr;
          REG_DBG_DONE_LAST_META:    rdata_next = stat_dbg_done_last_meta;
          REG_FB_CAP_STATUS:         rdata_next = {31'd0, stat_fbcap_frame_buf_idx};
          REG_FB_CAP_SEQ:            rdata_next = stat_fbcap_frame_seq;
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
