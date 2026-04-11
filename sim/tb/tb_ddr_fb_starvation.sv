`timescale 1ns / 1ps
// tb_ddr_fb_starvation.sv
// 复现固定优先级 ddram_mux 在 ch1 持续读压力下饿死 ch5 framebuffer 写的问题。
//
// 设计意图：
// 1. ch1 始终保持 level request，并且每次 ready 后立即切到下一个 cache line，
//    模拟 GBA 核连续取指/取 ROM 数据。
// 2. ch5 发出一笔 framebuffer 写请求并保持到 ready，模拟 fb_ddr_arbiter
//    在等待底层 accept 的行为。
// 3. 如果仲裁公平，ch5 应该在有限周期内完成；若一直被 ch1 抢占，则本测试会失败。

module tb_ddr_fb_starvation;

  logic clk = 1'b0;
  always #5 clk = ~clk;  // 100 MHz

  logic rst_n = 1'b0;

  // ddram_mux <-> ddr_axi_backend
  logic        ddram_busy;
  logic [7:0]  ddram_burstcnt;
  logic [28:0] ddram_addr;
  logic [63:0] ddram_dout;
  logic        ddram_dout_ready;
  logic        ddram_rd;
  logic [63:0] ddram_din;
  logic [7:0]  ddram_be;
  logic        ddram_we;

  // ch1: 持续 ROM 读
  logic [27:1] ch1_addr;
  logic [63:0] ch1_dout;
  logic [15:0] ch1_din;
  logic        ch1_req;
  logic        ch1_rnw;
  logic        ch1_ready;

  // ch5: framebuffer 写
  logic [27:1] ch5_addr;
  logic [63:0] ch5_dout;
  logic [63:0] ch5_din;
  logic        ch5_req;
  logic        ch5_rnw;
  logic        ch5_ready;

  // unused channels
  logic [27:1] ch2_addr;
  logic [31:0] ch2_dout;
  logic [31:0] ch2_din;
  logic        ch2_req;
  logic        ch2_rnw;
  logic        ch2_ready;

  logic [25:1] ch3_addr;
  logic [15:0] ch3_dout;
  logic [15:0] ch3_din;
  logic        ch3_req;
  logic        ch3_rnw;
  logic        ch3_ready;

  logic [27:1] ch4_addr;
  logic [63:0] ch4_dout;
  logic [63:0] ch4_din;
  logic        ch4_req;
  logic        ch4_rnw;
  logic [7:0]  ch4_be;
  logic        ch4_ready;

  // AXI
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

  // stimulus state
  logic [24:0] ch1_word_addr = 25'h30000;
  logic        ch1_stream_active = 1'b0;
  logic        ch5_write_active = 1'b0;

  integer ch1_done_count = 0;
  integer ch5_done_count = 0;
  integer ch5_wait_cycles = 0;

  assign ch1_addr = {1'b0, ch1_word_addr, 1'b0};
  assign ch1_din  = 16'd0;
  assign ch1_req  = ch1_stream_active;
  assign ch1_rnw  = 1'b1;

  assign ch5_req  = ch5_write_active;
  assign ch5_rnw  = 1'b0;

  assign ch2_addr = '0;
  assign ch2_din  = '0;
  assign ch2_req  = 1'b0;
  assign ch2_rnw  = 1'b1;

  assign ch3_addr = '0;
  assign ch3_din  = '0;
  assign ch3_req  = 1'b0;
  assign ch3_rnw  = 1'b1;

  assign ch4_addr = '0;
  assign ch4_din  = '0;
  assign ch4_req  = 1'b0;
  assign ch4_rnw  = 1'b1;
  assign ch4_be   = '0;

  ddram_mux u_mux (
    .DDRAM_CLK       (clk),
    .DDRAM_BUSY      (ddram_busy),
    .DDRAM_BURSTCNT  (ddram_burstcnt),
    .DDRAM_ADDR      (ddram_addr),
    .DDRAM_DOUT      (ddram_dout),
    .DDRAM_DOUT_READY(ddram_dout_ready),
    .DDRAM_RD        (ddram_rd),
    .DDRAM_DIN       (ddram_din),
    .DDRAM_BE        (ddram_be),
    .DDRAM_WE        (ddram_we),
    .ch1_addr        (ch1_addr),
    .ch1_dout        (ch1_dout),
    .ch1_din         (ch1_din),
    .ch1_req         (ch1_req),
    .ch1_rnw         (ch1_rnw),
    .ch1_ready       (ch1_ready),
    .ch2_addr        (ch2_addr),
    .ch2_dout        (ch2_dout),
    .ch2_din         (ch2_din),
    .ch2_req         (ch2_req),
    .ch2_rnw         (ch2_rnw),
    .ch2_ready       (ch2_ready),
    .ch3_addr        (ch3_addr),
    .ch3_dout        (ch3_dout),
    .ch3_din         (ch3_din),
    .ch3_req         (ch3_req),
    .ch3_rnw         (ch3_rnw),
    .ch3_ready       (ch3_ready),
    .ch4_addr        (ch4_addr),
    .ch4_dout        (ch4_dout),
    .ch4_din         (ch4_din),
    .ch4_req         (ch4_req),
    .ch4_rnw         (ch4_rnw),
    .ch4_be          (ch4_be),
    .ch4_ready       (ch4_ready),
    .ch5_addr        (ch5_addr),
    .ch5_dout        (ch5_dout),
    .ch5_din         (ch5_din),
    .ch5_req         (ch5_req),
    .ch5_rnw         (ch5_rnw),
    .ch5_ready       (ch5_ready)
  );

  ddr_axi_backend_sv #(
    .G_DDR_BASE(32'h1000_0000)
  ) u_backend (
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

  axi_mem_model_random #(
    .MEM_SIZE_BYTES(1 << 20),
    .SEED          (32'h1357_2468),
    .MIN_AR_DELAY  (1),
    .MAX_AR_DELAY  (3),
    .MIN_R_DELAY   (2),
    .MAX_R_DELAY   (8),
    .MIN_AW_DELAY  (1),
    .MAX_AW_DELAY  (2),
    .MIN_B_DELAY   (1),
    .MAX_B_DELAY   (4)
  ) u_axi_mem (
    .clk          (clk),
    .rst_n        (rst_n),
    .S_AXI_ARADDR (m_axi_araddr),
    .S_AXI_ARLEN  (m_axi_arlen),
    .S_AXI_ARSIZE (m_axi_arsize),
    .S_AXI_ARBURST(m_axi_arburst),
    .S_AXI_ARVALID(m_axi_arvalid),
    .S_AXI_ARREADY(m_axi_arready),
    .S_AXI_RDATA  (m_axi_rdata),
    .S_AXI_RRESP  (m_axi_rresp),
    .S_AXI_RLAST  (m_axi_rlast),
    .S_AXI_RVALID (m_axi_rvalid),
    .S_AXI_RREADY (m_axi_rready),
    .S_AXI_AWADDR (m_axi_awaddr),
    .S_AXI_AWLEN  (m_axi_awlen),
    .S_AXI_AWSIZE (m_axi_awsize),
    .S_AXI_AWBURST(m_axi_awburst),
    .S_AXI_AWVALID(m_axi_awvalid),
    .S_AXI_AWREADY(m_axi_awready),
    .S_AXI_WDATA  (m_axi_wdata),
    .S_AXI_WSTRB  (m_axi_wstrb),
    .S_AXI_WLAST  (m_axi_wlast),
    .S_AXI_WVALID (m_axi_wvalid),
    .S_AXI_WREADY (m_axi_wready),
    .S_AXI_BRESP  (m_axi_bresp),
    .S_AXI_BVALID (m_axi_bvalid),
    .S_AXI_BREADY (m_axi_bready)
  );

  always_ff @(posedge clk) begin
    if (!rst_n) begin
      ch1_done_count   <= 0;
      ch5_done_count   <= 0;
      ch5_wait_cycles  <= 0;
    end else begin
      if (ch1_ready) begin
        ch1_done_count <= ch1_done_count + 1;
        ch1_word_addr  <= ch1_word_addr + 25'd4;  // 8-byte stride，避免 cache hit 掩盖问题
      end

      if (ch5_write_active && !ch5_ready) begin
        ch5_wait_cycles <= ch5_wait_cycles + 1;
      end

      if (ch5_ready && ch5_write_active) begin
        ch5_done_count <= ch5_done_count + 1;
      end
    end
  end

  initial begin
    $display("=============================================");
    $display(" tb_ddr_fb_starvation: ch1持续读压力下的 ch5 饥饿复现 ");
    $display("=============================================");

    ch5_addr = 27'h0;
    ch5_din  = 64'h0;
    repeat (10) @(posedge clk);
    rst_n <= 1'b1;

    repeat (5) @(posedge clk);
    ch1_stream_active <= 1'b1;

    // 先让 ch1 流量占满几拍，再插入 framebuffer 写。
    repeat (20) @(posedge clk);
    ch5_addr         <= 27'h2000000;  // 对应 0x1800_0000 framebuffer 基址
    ch5_din          <= 64'h00FF_FFFF_00FF_FFFF;
    ch5_write_active <= 1'b1;
    $display("[TB] @%0t ch5 write injected, expecting bounded service latency", $time);

    fork
      begin : wait_ch5_done
        @(posedge clk iff ch5_ready);
        ch5_write_active <= 1'b0;
      end
      begin : sim_window
        repeat (1200) @(posedge clk);
      end
    join_any
    disable fork;
    repeat (20) @(posedge clk);

    $display("[TB] ch1_done=%0d ch5_done=%0d ch5_wait=%0d cyc ch5_pending=%0b ch_rq5=%0b mux_state=%0d",
             ch1_done_count, ch5_done_count, ch5_wait_cycles,
             ch5_write_active, u_mux.ch_rq[5], u_mux.state);

    if (ch1_done_count < 1) begin
      $fatal(1, "[FAIL] Testbench invalid: ch1 traffic too light (done=%0d)", ch1_done_count);
    end

    if (ch5_done_count == 0) begin
      $fatal(1,
             "[FAIL] ch5 framebuffer write was starved under ch1 load: ch1_done=%0d ch5_wait=%0d ch_rq5=%0b",
             ch1_done_count, ch5_wait_cycles, u_mux.ch_rq[5]);
    end

    if (ch5_wait_cycles > 128) begin
      $fatal(1,
             "[FAIL] ch5 latency too high under mixed load: wait=%0d cycles (expected <=128)",
             ch5_wait_cycles);
    end

    $display("[PASS] ch5 completed under sustained ch1 pressure without starvation.");
    $finish;
  end

endmodule
