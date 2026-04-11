`timescale 1ns/1ps

module tb_fb_largeimg_chain;

  logic clk = 1'b0;
  logic rst_n = 1'b0;
  always #5 clk = ~clk;

  logic [1:0]  display_frame_idx;
  logic [25:0] largeimg_addr;
  logic [63:0] largeimg_data;
  logic        largeimg_req;
  logic        largeimg_done;

  logic [27:1] wr_addr;
  logic [63:0] wr_data;
  logic        wr_req;
  logic        wr_ack;

  logic [27:1] ch5_addr;
  logic [63:0] ch5_dout;
  logic [63:0] ch5_din;
  logic        ch5_req;
  logic        ch5_rnw;
  logic        ch5_ready;

  logic [27:1] ch1_addr = '0;
  logic [63:0] ch1_dout;
  logic [15:0] ch1_din  = '0;
  logic        ch1_req  = 1'b0;
  logic        ch1_rnw  = 1'b1;
  logic        ch1_ready;

  logic [27:1] ch2_addr = '0;
  logic [31:0] ch2_dout;
  logic [31:0] ch2_din  = '0;
  logic        ch2_req  = 1'b0;
  logic        ch2_rnw  = 1'b1;
  logic        ch2_ready;

  logic [25:1] ch3_addr = '0;
  logic [15:0] ch3_dout;
  logic [15:0] ch3_din  = '0;
  logic        ch3_req  = 1'b0;
  logic        ch3_rnw  = 1'b1;
  logic        ch3_ready;

  logic [27:1] ch4_addr = '0;
  logic [63:0] ch4_dout;
  logic [63:0] ch4_din  = '0;
  logic        ch4_req  = 1'b0;
  logic        ch4_rnw  = 1'b1;
  logic [7:0]  ch4_be   = '0;
  logic        ch4_ready;

  logic        ddram_busy;
  logic [7:0]  ddram_burstcnt;
  logic [28:0] ddram_addr;
  logic [63:0] ddram_dout;
  logic        ddram_dout_ready;
  logic        ddram_rd;
  logic [63:0] ddram_din;
  logic [7:0]  ddram_be;
  logic        ddram_we;

  logic [31:0] m_axi_awaddr;
  logic [7:0]  m_axi_awlen;
  logic [2:0]  m_axi_awsize;
  logic [1:0]  m_axi_awburst;
  logic        m_axi_awvalid;
  logic        m_axi_awready;

  logic [63:0] m_axi_wdata;
  logic [7:0]  m_axi_wstrb;
  logic        m_axi_wlast;
  logic        m_axi_wvalid;
  logic        m_axi_wready;

  logic [1:0]  m_axi_bresp;
  logic        m_axi_bvalid;
  logic        m_axi_bready;

  logic [31:0] m_axi_araddr;
  logic [7:0]  m_axi_arlen;
  logic [2:0]  m_axi_arsize;
  logic [1:0]  m_axi_arburst;
  logic        m_axi_arvalid;
  logic        m_axi_arready;

  logic [63:0] m_axi_rdata;
  logic [1:0]  m_axi_rresp;
  logic        m_axi_rlast;
  logic        m_axi_rvalid;
  logic        m_axi_rready;

  logic [31:0] err_vec;
  logic        err_pulse;

  typedef struct packed {
    logic [31:0] addr;
    logic [63:0] data;
  } axi_wr_t;

  axi_wr_t axi_wr_log[0:31];
  int axi_wr_count;

  logic        aw_seen;
  logic [31:0] aw_addr_seen;
  logic        b_pending;
  int          b_delay;

  fb_largeimg_bridge dut_bridge (
    .clk              (clk),
    .rst_n            (rst_n),
    .display_frame_idx(display_frame_idx),
    .largeimg_addr    (largeimg_addr),
    .largeimg_data    (largeimg_data),
    .largeimg_req     (largeimg_req),
    .wr_addr          (wr_addr),
    .wr_data          (wr_data),
    .wr_req           (wr_req)
  );

  fb_ddr_arbiter dut_arb (
    .clk      (clk),
    .rst_n    (rst_n),
    .wr_addr  (wr_addr),
    .wr_data  (wr_data),
    .wr_req   (wr_req),
    .wr_ack   (wr_ack),
    .ch5_addr (ch5_addr),
    .ch5_din  (ch5_din),
    .ch5_req  (ch5_req),
    .ch5_rnw  (ch5_rnw),
    .ch5_ready(ch5_ready)
  );

  assign largeimg_done = wr_ack;

  ddram_mux dut_mux (
    .DDRAM_CLK        (clk),
    .DDRAM_BUSY       (ddram_busy),
    .DDRAM_BURSTCNT   (ddram_burstcnt),
    .DDRAM_ADDR       (ddram_addr),
    .DDRAM_DOUT       (ddram_dout),
    .DDRAM_DOUT_READY (ddram_dout_ready),
    .DDRAM_RD         (ddram_rd),
    .DDRAM_DIN        (ddram_din),
    .DDRAM_BE         (ddram_be),
    .DDRAM_WE         (ddram_we),
    .ch1_addr         (ch1_addr),
    .ch1_dout         (ch1_dout),
    .ch1_din          (ch1_din),
    .ch1_req          (ch1_req),
    .ch1_rnw          (ch1_rnw),
    .ch1_ready        (ch1_ready),
    .ch2_addr         (ch2_addr),
    .ch2_dout         (ch2_dout),
    .ch2_din          (ch2_din),
    .ch2_req          (ch2_req),
    .ch2_rnw          (ch2_rnw),
    .ch2_ready        (ch2_ready),
    .ch3_addr         (ch3_addr),
    .ch3_dout         (ch3_dout),
    .ch3_din          (ch3_din),
    .ch3_req          (ch3_req),
    .ch3_rnw          (ch3_rnw),
    .ch3_ready        (ch3_ready),
    .ch4_addr         (ch4_addr),
    .ch4_dout         (ch4_dout),
    .ch4_din          (ch4_din),
    .ch4_req          (ch4_req),
    .ch4_rnw          (ch4_rnw),
    .ch4_be           (ch4_be),
    .ch4_ready        (ch4_ready),
    .ch5_addr         (ch5_addr),
    .ch5_dout         (ch5_dout),
    .ch5_din          (ch5_din),
    .ch5_req          (ch5_req),
    .ch5_rnw          (ch5_rnw),
    .ch5_ready        (ch5_ready)
  );

  ddr_axi_backend_sv #(
    .G_DDR_BASE(32'h1000_0000)
  ) dut_backend (
    .clk              (clk),
    .rst_n            (rst_n),
    .DDRAM_BUSY       (ddram_busy),
    .DDRAM_BURSTCNT   (ddram_burstcnt),
    .DDRAM_ADDR       (ddram_addr),
    .DDRAM_DOUT       (ddram_dout),
    .DDRAM_DOUT_READY (ddram_dout_ready),
    .DDRAM_RD         (ddram_rd),
    .DDRAM_DIN        (ddram_din),
    .DDRAM_BE         (ddram_be),
    .DDRAM_WE         (ddram_we),
    .M_AXI_AWADDR     (m_axi_awaddr),
    .M_AXI_AWLEN      (m_axi_awlen),
    .M_AXI_AWSIZE     (m_axi_awsize),
    .M_AXI_AWBURST    (m_axi_awburst),
    .M_AXI_AWVALID    (m_axi_awvalid),
    .M_AXI_AWREADY    (m_axi_awready),
    .M_AXI_WDATA      (m_axi_wdata),
    .M_AXI_WSTRB      (m_axi_wstrb),
    .M_AXI_WLAST      (m_axi_wlast),
    .M_AXI_WVALID     (m_axi_wvalid),
    .M_AXI_WREADY     (m_axi_wready),
    .M_AXI_BRESP      (m_axi_bresp),
    .M_AXI_BVALID     (m_axi_bvalid),
    .M_AXI_BREADY     (m_axi_bready),
    .M_AXI_ARADDR     (m_axi_araddr),
    .M_AXI_ARLEN      (m_axi_arlen),
    .M_AXI_ARSIZE     (m_axi_arsize),
    .M_AXI_ARBURST    (m_axi_arburst),
    .M_AXI_ARVALID    (m_axi_arvalid),
    .M_AXI_ARREADY    (m_axi_arready),
    .M_AXI_RDATA      (m_axi_rdata),
    .M_AXI_RRESP      (m_axi_rresp),
    .M_AXI_RLAST      (m_axi_rlast),
    .M_AXI_RVALID     (m_axi_rvalid),
    .M_AXI_RREADY     (m_axi_rready),
    .ERR_VEC          (err_vec),
    .ERR_PULSE        (err_pulse)
  );

  assign m_axi_awready = 1'b1;
  assign m_axi_wready  = 1'b1;
  assign m_axi_bresp   = 2'b00;
  assign m_axi_arready = 1'b1;
  assign m_axi_rdata   = 64'h0;
  assign m_axi_rresp   = 2'b00;
  assign m_axi_rlast   = 1'b0;
  assign m_axi_rvalid  = 1'b0;

  always_ff @(posedge clk) begin
    logic aw_fire;
    logic w_fire;
    logic [31:0] txn_addr;

    if (!rst_n) begin
      axi_wr_count <= 0;
      aw_seen      <= 1'b0;
      aw_addr_seen <= '0;
      b_pending    <= 1'b0;
      b_delay      <= 0;
      m_axi_bvalid <= 1'b0;
    end else begin
      aw_fire  = m_axi_awvalid && m_axi_awready;
      w_fire   = m_axi_wvalid && m_axi_wready;
      txn_addr = aw_addr_seen;

      if (m_axi_bvalid && m_axi_bready) begin
        m_axi_bvalid <= 1'b0;
      end

      if (aw_fire) begin
        aw_seen      <= 1'b1;
        aw_addr_seen <= m_axi_awaddr;
        txn_addr     = m_axi_awaddr;
      end

      if (w_fire) begin
        if (!(aw_fire || aw_seen)) begin
          $fatal(1, "[TB] write data arrived before address");
        end
        axi_wr_log[axi_wr_count].addr <= txn_addr;
        axi_wr_log[axi_wr_count].data <= m_axi_wdata;
        axi_wr_count <= axi_wr_count + 1;
        aw_seen   <= 1'b0;
        b_pending <= 1'b1;
        b_delay   <= 2;
      end

      if (b_pending) begin
        if (b_delay > 0) begin
          b_delay <= b_delay - 1;
        end else begin
          m_axi_bvalid <= 1'b1;
          b_pending    <= 1'b0;
        end
      end
    end
  end

  function automatic logic [31:0] frame_base(input logic [1:0] frame_idx);
    case (frame_idx)
      2'd0:    frame_base = 32'h1800_0000;
      2'd1:    frame_base = 32'h1820_0000;
      default: frame_base = 32'h1840_0000;
    endcase
  endfunction

  function automatic logic [1:0] backbuffer_frame_idx(input logic [1:0] frame_idx);
    case (frame_idx)
      2'd0:    backbuffer_frame_idx = 2'd2;
      2'd1:    backbuffer_frame_idx = 2'd0;
      default: backbuffer_frame_idx = 2'd1;
    endcase
  endfunction

  function automatic logic [31:0] expected_axi_addr(
    input logic [1:0] frame_idx,
    input int unsigned row_idx,
    input int unsigned pixel_x
  );
    expected_axi_addr =
      frame_base(frame_idx)
      + 32'd205120
      + (row_idx * 32'd2560)
      + (pixel_x * 32'd4);
  endfunction

  task automatic send_largeimg_word(
    input logic [1:0]  disp_idx,
    input logic [25:0] addr_in,
    input logic [63:0] data_in
  );
  begin
    @(posedge clk);
    display_frame_idx <= disp_idx;
    largeimg_addr     <= addr_in;
    largeimg_data     <= data_in;
    largeimg_req      <= 1'b1;
    @(posedge clk);
    largeimg_req      <= 1'b0;
    largeimg_addr     <= '0;
    largeimg_data     <= '0;
    while (!largeimg_done) @(posedge clk);
  end
  endtask

  initial begin
    display_frame_idx = 2'd0;
    largeimg_addr     = '0;
    largeimg_data     = '0;
    largeimg_req      = 1'b0;

    repeat (5) @(posedge clk);
    rst_n = 1'b1;
    @(posedge clk);

    send_largeimg_word(2'd0, {1'b1, 5'd0, 20'd0},   64'h1122_3344_5566_7788);
    send_largeimg_word(2'd1, {1'b1, 5'd0, 20'd2},   64'h0102_0304_0506_0708);
    send_largeimg_word(2'd1, {1'b1, 5'd0, 20'd512}, 64'h8899_AABB_CCDD_EEFF);
    send_largeimg_word(2'd1, {1'b1, 5'd0, 20'd0},   64'hDEAD_BEEF_0123_4567);

    repeat (10) @(posedge clk);

    if (axi_wr_count != 4) begin
      $fatal(1, "[TB] expected 4 AXI writes, got %0d", axi_wr_count);
    end

    if (axi_wr_log[0].addr !== expected_axi_addr(backbuffer_frame_idx(2'd0), 0, 0)) begin
      $fatal(1, "[TB] wr0 addr mismatch exp=0x%08x got=0x%08x",
             expected_axi_addr(backbuffer_frame_idx(2'd0), 0, 0),
             axi_wr_log[0].addr);
    end
    if (axi_wr_log[1].addr !== expected_axi_addr(backbuffer_frame_idx(2'd0), 0, 2)) begin
      $fatal(1, "[TB] wr1 addr mismatch exp=0x%08x got=0x%08x",
             expected_axi_addr(backbuffer_frame_idx(2'd0), 0, 2),
             axi_wr_log[1].addr);
    end
    if (axi_wr_log[2].addr !== expected_axi_addr(backbuffer_frame_idx(2'd0), 1, 0)) begin
      $fatal(1, "[TB] wr2 addr mismatch exp=0x%08x got=0x%08x",
             expected_axi_addr(backbuffer_frame_idx(2'd0), 1, 0),
             axi_wr_log[2].addr);
    end
    if (axi_wr_log[3].addr !== expected_axi_addr(backbuffer_frame_idx(2'd1), 0, 0)) begin
      $fatal(1, "[TB] wr3 addr mismatch exp=0x%08x got=0x%08x",
             expected_axi_addr(backbuffer_frame_idx(2'd1), 0, 0),
             axi_wr_log[3].addr);
    end

    if (axi_wr_log[0].data !== 64'h1122_3344_5566_7788 ||
        axi_wr_log[1].data !== 64'h0102_0304_0506_0708 ||
        axi_wr_log[2].data !== 64'h8899_AABB_CCDD_EEFF ||
        axi_wr_log[3].data !== 64'hDEAD_BEEF_0123_4567) begin
      $fatal(1, "[TB] write data pass-through mismatch");
    end

    if (err_pulse !== 1'b0 && err_vec !== 32'd0) begin
      $fatal(1, "[TB] unexpected backend error vec=0x%08x", err_vec);
    end

    $display("tb_fb_largeimg_chain PASS");
    $finish;
  end

endmodule
