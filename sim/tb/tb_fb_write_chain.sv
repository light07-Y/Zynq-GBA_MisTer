`timescale 1ns / 1ps
// tb_fb_write_chain.sv
// 验证 fb_native_scale_writer → fb_ddr_arbiter → ddram_mux → ddr_axi_backend_sv
// 全链路帧缓冲写入路径的正确性。
//
// 检查项:
//   1. fb_writer 是否对每行 240 像素正确生成 wr_req
//   2. arbiter 是否正确传递 ch5_req 给 ddram_mux
//   3. ddram_mux 是否服务 ch5 写请求
//   4. ddr_axi_backend 是否生成正确的 AXI 写事务
//   5. AXI 写地址是否落在 0x1800_0000 帧缓冲区域
//   6. AXI 写数据是否为预期的像素值（非零）
//   7. 完整 1 行 (240px * 2 stage = 480 writes) 是否全部完成

module tb_fb_write_chain;

  // ========== 时钟与复位 ==========
  logic clk = 0;
  logic rst_n = 0;
  always #5 clk = ~clk; // 100 MHz

  // ========== fb_native_scale_writer 端口 ==========
  logic [1:0]  display_frame_idx;
  logic [15:0] pixel_addr;
  logic [17:0] pixel_data;
  logic        pixel_we;
  logic [27:1] fb_wr_addr;
  logic [63:0] fb_wr_data;
  logic        fb_wr_req;
  logic        fb_wr_ack;

  // ========== fb_ddr_arbiter → ddram_mux ==========
  logic [27:1] ch5_addr;
  logic [63:0] ch5_din;
  logic        ch5_req;
  logic        ch5_rnw;
  logic        ch5_ready;

  // ========== ddram_mux → ddr_axi_backend ==========
  logic        DDRAM_BUSY;
  logic [7:0]  DDRAM_BURSTCNT;
  logic [28:0] DDRAM_ADDR;
  logic [63:0] DDRAM_DOUT;
  logic        DDRAM_DOUT_READY;
  logic        DDRAM_RD;
  logic [63:0] DDRAM_DIN;
  logic [7:0]  DDRAM_BE;
  logic        DDRAM_WE;

  // ========== AXI4 接口（监控目标）==========
  logic [31:0] M_AXI_AWADDR;
  logic [7:0]  M_AXI_AWLEN;
  logic [2:0]  M_AXI_AWSIZE;
  logic [1:0]  M_AXI_AWBURST;
  logic        M_AXI_AWVALID;
  logic        M_AXI_AWREADY;

  logic [63:0] M_AXI_WDATA;
  logic [7:0]  M_AXI_WSTRB;
  logic        M_AXI_WLAST;
  logic        M_AXI_WVALID;
  logic        M_AXI_WREADY;

  logic [1:0]  M_AXI_BRESP;
  logic        M_AXI_BVALID;
  logic        M_AXI_BREADY;

  logic [31:0] M_AXI_ARADDR;
  logic [7:0]  M_AXI_ARLEN;
  logic [2:0]  M_AXI_ARSIZE;
  logic [1:0]  M_AXI_ARBURST;
  logic        M_AXI_ARVALID;
  logic        M_AXI_ARREADY;

  logic [63:0] M_AXI_RDATA;
  logic [1:0]  M_AXI_RRESP;
  logic        M_AXI_RLAST;
  logic        M_AXI_RVALID;
  logic        M_AXI_RREADY;

  logic [31:0] ERR_VEC;
  logic        ERR_PULSE;

  // ========== ddram_mux 其他通道（全部空闲）==========
  logic [27:1] ch1_addr = '0;
  logic [63:0] ch1_dout;
  logic [15:0] ch1_din  = '0;
  logic        ch1_req  = 0;
  logic        ch1_rnw  = 1;
  logic        ch1_ready;

  logic [27:1] ch2_addr = '0;
  logic [31:0] ch2_dout;
  logic [31:0] ch2_din  = '0;
  logic        ch2_req  = 0;
  logic        ch2_rnw  = 1;
  logic        ch2_ready;

  logic [25:1] ch3_addr = '0;
  logic [15:0] ch3_dout;
  logic [15:0] ch3_din  = '0;
  logic        ch3_req  = 0;
  logic        ch3_rnw  = 1;
  logic        ch3_ready;

  logic [27:1] ch4_addr = '0;
  logic [63:0] ch4_dout;
  logic [63:0] ch4_din  = '0;
  logic        ch4_req  = 0;
  logic        ch4_rnw  = 1;
  logic [7:0]  ch4_be   = '0;
  logic        ch4_ready;

  logic [63:0] ch5_dout;

  // ========== DUT 实例化 ==========

  fb_native_scale_writer u_fb_writer (
    .clk              (clk),
    .rst_n            (rst_n),
    .display_frame_idx(display_frame_idx),
    .pixel_addr       (pixel_addr),
    .pixel_data       (pixel_data),
    .pixel_we         (pixel_we),
    .wr_addr          (fb_wr_addr),
    .wr_data          (fb_wr_data),
    .wr_req           (fb_wr_req),
    .wr_ack           (fb_wr_ack)
  );

  fb_ddr_arbiter u_fb_arb (
    .clk       (clk),
    .rst_n     (rst_n),
    .wr_addr   (fb_wr_addr),
    .wr_data   (fb_wr_data),
    .wr_req    (fb_wr_req),
    .wr_ack    (fb_wr_ack),
    .ch5_addr  (ch5_addr),
    .ch5_din   (ch5_din),
    .ch5_req   (ch5_req),
    .ch5_rnw   (ch5_rnw),
    .ch5_ready (ch5_ready)
  );

  ddram_mux u_mux (
    .DDRAM_CLK       (clk),
    .DDRAM_BUSY      (DDRAM_BUSY),
    .DDRAM_BURSTCNT  (DDRAM_BURSTCNT),
    .DDRAM_ADDR      (DDRAM_ADDR),
    .DDRAM_DOUT      (DDRAM_DOUT),
    .DDRAM_DOUT_READY(DDRAM_DOUT_READY),
    .DDRAM_RD        (DDRAM_RD),
    .DDRAM_DIN       (DDRAM_DIN),
    .DDRAM_BE        (DDRAM_BE),
    .DDRAM_WE        (DDRAM_WE),
    .ch1_addr (ch1_addr),  .ch1_dout (ch1_dout),  .ch1_din (ch1_din),
    .ch1_req  (ch1_req),   .ch1_rnw  (ch1_rnw),   .ch1_ready(ch1_ready),
    .ch2_addr (ch2_addr),  .ch2_dout (ch2_dout),  .ch2_din (ch2_din),
    .ch2_req  (ch2_req),   .ch2_rnw  (ch2_rnw),   .ch2_ready(ch2_ready),
    .ch3_addr (ch3_addr),  .ch3_dout (ch3_dout),  .ch3_din (ch3_din),
    .ch3_req  (ch3_req),   .ch3_rnw  (ch3_rnw),   .ch3_ready(ch3_ready),
    .ch4_addr (ch4_addr),  .ch4_dout (ch4_dout),  .ch4_din (ch4_din),
    .ch4_req  (ch4_req),   .ch4_rnw  (ch4_rnw),   .ch4_be  (ch4_be),
    .ch4_ready(ch4_ready),
    .ch5_addr (ch5_addr),  .ch5_dout (ch5_dout),  .ch5_din (ch5_din),
    .ch5_req  (ch5_req),   .ch5_rnw  (ch5_rnw),   .ch5_ready(ch5_ready)
  );

  ddr_axi_backend_sv #(
    .G_DDR_BASE(32'h1000_0000)
  ) u_backend (
    .clk              (clk),
    .rst_n            (rst_n),
    .DDRAM_BUSY       (DDRAM_BUSY),
    .DDRAM_BURSTCNT   (DDRAM_BURSTCNT),
    .DDRAM_ADDR       (DDRAM_ADDR),
    .DDRAM_DOUT       (DDRAM_DOUT),
    .DDRAM_DOUT_READY (DDRAM_DOUT_READY),
    .DDRAM_RD         (DDRAM_RD),
    .DDRAM_DIN        (DDRAM_DIN),
    .DDRAM_BE         (DDRAM_BE),
    .DDRAM_WE         (DDRAM_WE),
    .M_AXI_AWADDR    (M_AXI_AWADDR),
    .M_AXI_AWLEN     (M_AXI_AWLEN),
    .M_AXI_AWSIZE    (M_AXI_AWSIZE),
    .M_AXI_AWBURST   (M_AXI_AWBURST),
    .M_AXI_AWVALID   (M_AXI_AWVALID),
    .M_AXI_AWREADY   (M_AXI_AWREADY),
    .M_AXI_WDATA     (M_AXI_WDATA),
    .M_AXI_WSTRB     (M_AXI_WSTRB),
    .M_AXI_WLAST     (M_AXI_WLAST),
    .M_AXI_WVALID    (M_AXI_WVALID),
    .M_AXI_WREADY    (M_AXI_WREADY),
    .M_AXI_BRESP     (M_AXI_BRESP),
    .M_AXI_BVALID    (M_AXI_BVALID),
    .M_AXI_BREADY    (M_AXI_BREADY),
    .M_AXI_ARADDR    (M_AXI_ARADDR),
    .M_AXI_ARLEN     (M_AXI_ARLEN),
    .M_AXI_ARSIZE    (M_AXI_ARSIZE),
    .M_AXI_ARBURST   (M_AXI_ARBURST),
    .M_AXI_ARVALID   (M_AXI_ARVALID),
    .M_AXI_ARREADY   (M_AXI_ARREADY),
    .M_AXI_RDATA     (M_AXI_RDATA),
    .M_AXI_RRESP     (M_AXI_RRESP),
    .M_AXI_RLAST     (M_AXI_RLAST),
    .M_AXI_RVALID    (M_AXI_RVALID),
    .M_AXI_RREADY    (M_AXI_RREADY),
    .ERR_VEC         (ERR_VEC),
    .ERR_PULSE       (ERR_PULSE)
  );

  // ========== AXI Slave 简易响应模型 ==========
  // 模拟 Zynq HP 端口：接受写事务，固定延迟返回 BRESP=OKAY
  // 同时记录所有写事务到数组用于验证

  localparam int AXI_RESP_DELAY = 4; // 模拟 DDR 写延迟（时钟周期）

  // 写事务记录
  typedef struct {
    logic [31:0] addr;
    logic [63:0] data;
    logic [7:0]  strb;
  } axi_wr_record_t;

  axi_wr_record_t axi_wr_log[0:1023];
  int axi_wr_count = 0;

  // AXI 写地址通道
  logic aw_pending = 0;
  logic [31:0] aw_addr_captured;
  // AXI 写数据通道
  logic w_pending = 0;
  logic [63:0] w_data_captured;
  logic [7:0]  w_strb_captured;
  // AXI 写响应通道
  int bresp_delay_cnt = 0;
  logic bresp_armed = 0;

  assign M_AXI_AWREADY = !aw_pending;
  assign M_AXI_WREADY  = !w_pending;

  // AXI 读响应模型：接受读请求，延迟返回数据
  logic ar_pending = 0;
  int   ar_delay_cnt = 0;
  logic rvalid_reg = 0;
  logic rlast_reg  = 0;

  assign M_AXI_ARREADY = !ar_pending;
  assign M_AXI_RDATA   = 64'hDEAD_BEEF_CAFE_F00D; // 可识别的假数据
  assign M_AXI_RRESP   = 2'b00;
  assign M_AXI_RLAST   = rlast_reg;
  assign M_AXI_RVALID  = rvalid_reg;

  always_ff @(posedge clk) begin
    if (!rst_n) begin
      ar_pending   <= 0;
      ar_delay_cnt <= 0;
      rvalid_reg   <= 0;
      rlast_reg    <= 0;
    end else begin
      // 清除已被接收的 RVALID
      if (rvalid_reg && M_AXI_RREADY) begin
        rvalid_reg <= 1'b0;
        rlast_reg  <= 1'b0;
      end

      // 捕获 AR
      if (M_AXI_ARVALID && M_AXI_ARREADY) begin
        ar_pending   <= 1;
        ar_delay_cnt <= AXI_RESP_DELAY;
      end

      // 延迟后返回读数据
      if (ar_pending) begin
        if (ar_delay_cnt > 0)
          ar_delay_cnt <= ar_delay_cnt - 1;
        else begin
          rvalid_reg <= 1'b1;
          rlast_reg  <= 1'b1;
          ar_pending <= 0;
        end
      end
    end
  end

  always_ff @(posedge clk) begin
    if (!rst_n) begin
      aw_pending     <= 0;
      w_pending      <= 0;
      bresp_armed    <= 0;
      bresp_delay_cnt <= 0;
      M_AXI_BVALID   <= 0;
      M_AXI_BRESP    <= 2'b00;
    end else begin
      // 默认拉低 BVALID
      if (M_AXI_BVALID && M_AXI_BREADY)
        M_AXI_BVALID <= 1'b0;

      // 捕获 AW
      if (M_AXI_AWVALID && M_AXI_AWREADY) begin
        aw_addr_captured <= M_AXI_AWADDR;
        aw_pending <= 1;
      end

      // 捕获 W
      if (M_AXI_WVALID && M_AXI_WREADY) begin
        w_data_captured <= M_AXI_WDATA;
        w_strb_captured <= M_AXI_WSTRB;
        w_pending <= 1;
      end

      // AW + W 都收到后，启动延迟响应
      if (aw_pending && w_pending && !bresp_armed) begin
        // 记录写事务
        if (axi_wr_count < 1024) begin
          axi_wr_log[axi_wr_count].addr = aw_addr_captured;
          axi_wr_log[axi_wr_count].data = w_data_captured;
          axi_wr_log[axi_wr_count].strb = w_strb_captured;
          axi_wr_count++;
        end
        bresp_armed <= 1;
        bresp_delay_cnt <= AXI_RESP_DELAY;
        aw_pending <= 0;
        w_pending  <= 0;
      end

      // 延迟后发送 BRESP
      if (bresp_armed) begin
        if (bresp_delay_cnt > 0)
          bresp_delay_cnt <= bresp_delay_cnt - 1;
        else begin
          M_AXI_BVALID <= 1'b1;
          M_AXI_BRESP  <= 2'b00; // OKAY
          bresp_armed  <= 0;
        end
      end
    end
  end

  // ========== 像素生成任务 ==========
  // 模拟 GBA GPU 输出一行 240 像素

  task automatic feed_pixel_line(
    input int line_num,
    input int gap_cycles  // 像素之间的间隔周期
  );
    int px;
    for (px = 0; px < 240; px++) begin
      @(posedge clk);
      pixel_addr <= line_num * 240 + px;
      // 使用非零可识别数据: R=line+1, G=px+1, B=(line^px)|1 确保永远非零
      pixel_data <= {(line_num[5:0] + 6'd1), (px[5:0] + 6'd1), ((line_num ^ px) & 6'h3E) | 6'h01};
      pixel_we   <= 1'b1;
      @(posedge clk);
      pixel_we <= 1'b0;
      // 像素间隔（模拟 GPU 时序）
      repeat (gap_cycles) @(posedge clk);
    end
  endtask

  // ========== 统计与断言 ==========
  int fb_wr_req_count = 0;
  int ch5_req_count   = 0;
  int axi_aw_count    = 0;
  int axi_w_count     = 0;
  int axi_b_count     = 0;
  int err_count       = 0;

  always_ff @(posedge clk) begin
    if (rst_n) begin
      if (fb_wr_req)                        fb_wr_req_count++;
      if (ch5_req)                          ch5_req_count++;
      if (M_AXI_AWVALID && M_AXI_AWREADY)  axi_aw_count++;
      if (M_AXI_WVALID  && M_AXI_WREADY)   axi_w_count++;
      if (M_AXI_BVALID  && M_AXI_BREADY)   axi_b_count++;
      if (ERR_PULSE)                        err_count++;
    end
  end

  // ========== 主测试流程 ==========
  initial begin
    // 初始化
    display_frame_idx = 2'd0;  // 显示帧0 → 写入帧2（backbuffer）
    pixel_addr = 0;
    pixel_data = 0;
    pixel_we   = 0;

    // 复位
    rst_n = 0;
    repeat (20) @(posedge clk);
    rst_n = 1;
    repeat (5) @(posedge clk);

    $display("========================================");
    $display("[TB] 开始帧缓冲写入全链路仿真");
    $display("[TB] display_frame_idx=%0d → backbuffer=frame2", display_frame_idx);
    $display("[TB] 预期 AXI 基地址范围: 0x1840_0000 ~ 0x185F_FFFF (frame2)");
    $display("========================================");

    // --- 测试 1: 发送第 0 行（GBA y=0）---
    $display("\n[TEST1] 发送 GBA line 0 (240 像素, gap=2)");
    feed_pixel_line(0, 2);

    // 等待所有 DDR 写完成（240 px * 2 stage = 480 次 AXI 写）
    // 每次写约 10 周期（arbiter + mux + backend 延迟），预留充足时间
    repeat (480 * 20) @(posedge clk);

    $display("[TEST1] 完成等待");
    $display("[TEST1] fb_wr_req=%0d  ch5_req=%0d  axi_aw=%0d  axi_w=%0d  axi_b=%0d  err=%0d",
             fb_wr_req_count, ch5_req_count, axi_aw_count, axi_w_count, axi_b_count, err_count);
    $display("[TEST1] axi_wr_log 记录数=%0d", axi_wr_count);

    // 检查是否产生了 480 次 AXI 写（240 px * 2 stage）
    if (axi_wr_count != 480) begin
      $display("[TEST1] *** FAIL *** 预期 480 次 AXI 写，实际 %0d", axi_wr_count);
    end else begin
      $display("[TEST1] PASS: AXI 写次数正确 (480)");
    end

    // 检查第一笔写地址
    if (axi_wr_count > 0) begin
      $display("[TEST1] 第 0 笔 AXI 写: addr=0x%08x data=0x%016x strb=0x%02x",
               axi_wr_log[0].addr, axi_wr_log[0].data, axi_wr_log[0].strb);
      // 预期地址: frame2 base + Y_OFFSET + X_OFFSET
      // frame2 offset = FB_FRAME_STRIDE*2 = 0x100000
      // AXI base for FB = 0x18000000
      // 预期: 0x18000000 + 0x100000*4 + 51200*4 + 80*4 = ?
      // 实际用 write_addr_core 单位（32-bit words）:
      //   write_addr_core = 0x2000000 + 0x100000 + 51200 + 0 + 0 + 80 + 0 = 0x210C850
      //   wr_addr[27:1] = {0x210C850, 0} = 0x4219_0A0... 我们直接检查范围
      if (axi_wr_log[0].addr >= 32'h1840_0000 && axi_wr_log[0].addr < 32'h1860_0000)
        $display("[TEST1] PASS: 第 0 笔地址在 frame2 范围内");
      else
        $display("[TEST1] *** FAIL *** 第 0 笔地址 0x%08x 不在 frame2 范围 [0x18400000, 0x18600000)",
                 axi_wr_log[0].addr);

      // 检查数据非零
      if (axi_wr_log[0].data != 64'h0)
        $display("[TEST1] PASS: 第 0 笔数据非零 (0x%016x)", axi_wr_log[0].data);
      else
        $display("[TEST1] *** FAIL *** 第 0 笔数据为全零");

      // 额外打印前 8 笔写数据，验证非零和像素多样性
      begin
        int k;
        for (k = 0; k < 8 && k < axi_wr_count; k++)
          $display("[TEST1]   wr[%0d] addr=0x%08x data=0x%016x", k, axi_wr_log[k].addr, axi_wr_log[k].data);
      end

      // 检查 WSTRB = 0xFF
      if (axi_wr_log[0].strb == 8'hFF)
        $display("[TEST1] PASS: WSTRB=0xFF");
      else
        $display("[TEST1] *** FAIL *** WSTRB=0x%02x (预期 0xFF)", axi_wr_log[0].strb);
    end

    // 检查地址连续性和 stage0/stage1 交替
    if (axi_wr_count >= 4) begin
      logic [31:0] a0, a1, a2, a3;
      a0 = axi_wr_log[0].addr;
      a1 = axi_wr_log[1].addr;
      a2 = axi_wr_log[2].addr;
      a3 = axi_wr_log[3].addr;
      $display("[TEST1] 前4笔地址: 0x%08x  0x%08x  0x%08x  0x%08x", a0, a1, a2, a3);
      // stage0 和 stage1 地址差 = FB_LINE_STRIDE * 4 = 640 * 4 = 2560 = 0xA00
      $display("[TEST1] a1-a0 = %0d (预期 2560=0xA00, stage0→stage1 同像素)", a1 - a0);
      // 同 stage 相邻像素差 = 8 (64-bit = 2 个 32-bit pixel)
      $display("[TEST1] a2-a0 = %0d (预期 8, 相邻像素 stage0)", a2 - a0);
    end

    // 打印 backend 错误
    if (err_count > 0)
      $display("[TEST1] *** WARNING *** backend 报告 %0d 个错误, ERR_VEC=0x%08x", err_count, ERR_VEC);

    // --- 测试 2: 发送第 1 行，验证地址 Y 偏移 ---
    $display("\n[TEST2] 发送 GBA line 1 (240 像素, gap=2)");
    begin
      int prev_count;
      prev_count = axi_wr_count;
      feed_pixel_line(1, 2);
      repeat (480 * 20) @(posedge clk);
      $display("[TEST2] 新增 AXI 写 %0d 笔 (预期 480)", axi_wr_count - prev_count);
      if (axi_wr_count > prev_count) begin
        $display("[TEST2] line1 第 0 笔 addr=0x%08x", axi_wr_log[prev_count].addr);
        // line1 相对 line0: Y 偏移 = FB_FIELD_STRIDE * 4 = 1280 * 4 = 5120 = 0x1400
        $display("[TEST2] line1[0]-line0[0] = %0d (预期 5120=0x1400)",
                 axi_wr_log[prev_count].addr - axi_wr_log[0].addr);
      end
    end

    // --- 测试 3: ch1 竞争下 ch5 是否仍能完成 ---
    $display("\n[TEST3] 发送 GBA line 2 + 同时注入 ch1 读请求（优先级竞争）");
    begin
      int prev_b_count;
      prev_b_count = axi_b_count;

      fork
        // 像素流
        feed_pixel_line(2, 2);
        // ch1 读请求脉冲（模拟 ROM fetch 干扰）
        begin
          int i;
          for (i = 0; i < 100; i++) begin
            @(posedge clk);
            ch1_addr <= 27'h0006000 + i;
            ch1_req  <= 1'b1;
            ch1_rnw  <= 1'b1;
            @(posedge clk);
            ch1_req <= 1'b0;
            // 等待 ch1 完成后再发下一个
            wait (ch1_ready);
            @(posedge clk);
          end
        end
      join

      repeat (480 * 60) @(posedge clk);
      begin
        int test3_writes;
        test3_writes = axi_b_count - prev_b_count;
        $display("[TEST3] 新增 AXI 写(BRESP) %0d 笔 (预期 480，有 ch1 竞争)", test3_writes);
        if (test3_writes == 480)
          $display("[TEST3] PASS: ch1 竞争下 ch5 全部完成");
        else
          $display("[TEST3] *** FAIL *** ch1 竞争下 ch5 只完成了 %0d/480", test3_writes);
      end
    end

    // ========== 总结 ==========
    $display("\n========================================");
    $display("[SUMMARY] 总 AXI 写事务: %0d", axi_wr_count);
    $display("[SUMMARY] fb_wr_req=%0d  ch5_req=%0d", fb_wr_req_count, ch5_req_count);
    $display("[SUMMARY] axi_aw=%0d  axi_w=%0d  axi_b=%0d", axi_aw_count, axi_w_count, axi_b_count);
    $display("[SUMMARY] backend errors=%0d", err_count);
    if (axi_b_count >= 1440 && err_count == 0)
      $display("[SUMMARY] ===== ALL TESTS PASSED =====");
    else
      $display("[SUMMARY] ===== SOME TESTS FAILED =====");
    $display("========================================");

    #100;
    $finish;
  end

  // ========== 超时保护 ==========
  initial begin
    #20_000_000;  // 20ms
    $display("[TB] *** TIMEOUT *** 仿真超时退出");
    $display("[TB] axi_wr_count=%0d  fb_wr_req=%0d  ch5_req=%0d",
             axi_wr_count, fb_wr_req_count, ch5_req_count);
    $finish;
  end

endmodule
