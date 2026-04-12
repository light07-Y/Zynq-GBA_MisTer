// ============================================================================
// tb_hdmi_black_screen.sv
// 
// 用于精确定位 HDMI 黑屏根因的综合仿真测试平台。
// 测试覆盖：
//   1. gba_frame_capture_bram：像素写入 → frame_seq 递增 → port B 读回验证
//   2. axi_lite_ctrl_regs：FB_CAP_SEQ / FB_CAP_STATUS 寄存器可读性
//   3. core_fb_newframe 信号：largeimg_out_done=1 下的行为
//   4. PS blit 路径等效验证：BRAM 地址映射正确性
// ============================================================================
`timescale 1ns / 1ps

module tb_hdmi_black_screen;

  // -------------------------------------------------------
  // 参数
  // -------------------------------------------------------
  localparam int FRAME_WIDTH   = 240;
  localparam int FRAME_HEIGHT  = 160;
  localparam int FRAME_PIXELS  = FRAME_WIDTH * FRAME_HEIGHT;  // 38400
  localparam int FRAME_WORDS   = FRAME_PIXELS / 2;            // 19200
  localparam int CLK_PERIOD    = 10;  // 100 MHz

  // -------------------------------------------------------
  // 信号声明
  // -------------------------------------------------------
  logic        clk = 0;
  logic        rst_n = 0;

  // -- gba_frame_capture_bram 接口 (写侧 = 模拟 GPU)
  logic [15:0] pixel_addr;
  logic [17:0] pixel_data;
  logic        pixel_we;
  logic [31:0] frame_seq;
  logic        frame_buf_idx;

  // -- gba_frame_capture_bram 接口 (读侧 = 模拟 PS BRAM 控制器)
  logic        bram_clk_b;
  logic        bram_rst_b = 0;
  logic        bram_en_b  = 0;
  logic [3:0]  bram_we_b  = 4'b0;
  logic [17:0] bram_addr_b = 18'd0;
  logic [31:0] bram_din_b  = 32'd0;
  logic [31:0] bram_dout_b;

  // -- axi_lite_ctrl_regs 接口（简化 AXI）
  logic [11:0] axi_awaddr = 0;
  logic        axi_awvalid = 0;
  logic        axi_awready;
  logic [31:0] axi_wdata = 0;
  logic [3:0]  axi_wstrb = 4'hF;
  logic        axi_wvalid = 0;
  logic        axi_wready;
  logic [1:0]  axi_bresp;
  logic        axi_bvalid;
  logic        axi_bready = 1;
  logic [11:0] axi_araddr = 0;
  logic        axi_arvalid = 0;
  logic        axi_arready;
  logic [31:0] axi_rdata;
  logic [1:0]  axi_rresp;
  logic        axi_rvalid;
  logic        axi_rready = 1;

  // -- axi_lite_ctrl_regs 输出
  logic [31:0] cfg_ctrl;
  logic [9:0]  cfg_keys;
  logic [24:0] cfg_max_pak_addr;
  logic [15:0] cfg_cycle_precalc;
  logic [31:0] cfg_rtc_timestamp;
  logic [1:0]  cfg_display_frame_idx;
  logic        cfg_sw_reset;
  logic        cfg_commit_toggle;
  logic        irq_out;

  // -- 同步到 AXI 时钟域的 fbcap 信号
  logic [31:0] fbcap_seq_axi;
  logic        fbcap_buf_idx_axi;

  // -- largeimg 模拟信号（用于 core_fb_newframe 测试）
  logic        unused_core_fb_req = 0;
  logic [25:0] unused_core_fb_addr = 26'd0;
  logic        core_fb_newframe;

  // -- 统计与断言计数器
  int          test_pass = 0;
  int          test_fail = 0;
  int          newframe_count = 0;

  // -------------------------------------------------------
  // 时钟生成
  // -------------------------------------------------------
  always #(CLK_PERIOD/2) clk = ~clk;
  assign bram_clk_b = clk;  // 读侧同频

  // -------------------------------------------------------
  // DUT 实例化：gba_frame_capture_bram
  // -------------------------------------------------------
  gba_frame_capture_bram u_capture (
    .wr_clk        (clk),
    .wr_rst_n      (rst_n),
    .pixel_addr    (pixel_addr),
    .pixel_data    (pixel_data),
    .pixel_we      (pixel_we),
    .frame_seq     (frame_seq),
    .frame_buf_idx (frame_buf_idx),
    .bram_clk_b    (bram_clk_b),
    .bram_rst_b    (bram_rst_b),
    .bram_en_b     (bram_en_b),
    .bram_we_b     (bram_we_b),
    .bram_addr_b   (bram_addr_b),
    .bram_din_b    (bram_din_b),
    .bram_dout_b   (bram_dout_b)
  );

  // -------------------------------------------------------
  // DUT 实例化：axi_lite_ctrl_regs
  // -------------------------------------------------------
  axi_lite_ctrl_regs u_regs (
    .clk             (clk),
    .rst_n           (rst_n),
    .s_axi_awaddr    (axi_awaddr),
    .s_axi_awvalid   (axi_awvalid),
    .s_axi_awready   (axi_awready),
    .s_axi_wdata     (axi_wdata),
    .s_axi_wstrb     (axi_wstrb),
    .s_axi_wvalid    (axi_wvalid),
    .s_axi_wready    (axi_wready),
    .s_axi_bresp     (axi_bresp),
    .s_axi_bvalid    (axi_bvalid),
    .s_axi_bready    (axi_bready),
    .s_axi_araddr    (axi_araddr),
    .s_axi_arvalid   (axi_arvalid),
    .s_axi_arready   (axi_arready),
    .s_axi_rdata     (axi_rdata),
    .s_axi_rresp     (axi_rresp),
    .s_axi_rvalid    (axi_rvalid),
    .s_axi_rready    (axi_rready),
    // 状态输入
    .stat_cycles_missing     (14'd0),
    .stat_cycles_vsync_speed (16'd0),
    .stat_fb_frame_idx       (2'd0),
    .stat_physical_keys      (10'd0),
    .stat_debug_cpu_pc       (32'd0),
    .stat_debug_cpu_mixed    (32'd0),
    .stat_debug_irq          (32'd0),
    .stat_debug_dma          (32'd0),
    .stat_debug_mem          (32'd0),
    .stat_dbg_chain_flags    (32'd0),
    .stat_dbg_chain_counts0  (32'd0),
    .stat_dbg_chain_counts1  (32'd0),
    .stat_dbg_ch1_first_addr (32'd0),
    .stat_dbg_ch1_first_meta (32'd0),
    .stat_dbg_ch1_last_addr  (32'd0),
    .stat_dbg_ch1_last_meta  (32'd0),
    .stat_dbg_ddr_first_addr (32'd0),
    .stat_dbg_ddr_first_meta (32'd0),
    .stat_dbg_ddr_last_addr  (32'd0),
    .stat_dbg_ddr_last_meta  (32'd0),
    .stat_dbg_axi_ar_first_addr(32'd0),
    .stat_dbg_axi_ar_first_meta(32'd0),
    .stat_dbg_axi_ar_last_addr (32'd0),
    .stat_dbg_axi_ar_last_meta (32'd0),
    .stat_dbg_axi_r_first_addr (32'd0),
    .stat_dbg_axi_r_first_meta (32'd0),
    .stat_dbg_axi_r_last_addr  (32'd0),
    .stat_dbg_axi_r_last_meta  (32'd0),
    .stat_dbg_done_first_addr  (32'd0),
    .stat_dbg_done_first_meta  (32'd0),
    .stat_dbg_done_last_addr   (32'd0),
    .stat_dbg_done_last_meta   (32'd0),
    .sys_rom_loading           (1'b0),
    .sys_error_in              (32'd0),
    .irq_vsync_pulse           (1'b0),
    .irq_error_pulse           (1'b0),
    .irq_out                   (irq_out),
    .cfg_ctrl                  (cfg_ctrl),
    .cfg_keys                  (cfg_keys),
    .cfg_max_pak_addr          (cfg_max_pak_addr),
    .cfg_cycle_precalc         (cfg_cycle_precalc),
    .cfg_rtc_timestamp         (cfg_rtc_timestamp),
    .cfg_display_frame_idx     (cfg_display_frame_idx),
    .cfg_sw_reset              (cfg_sw_reset),
    .cfg_commit_toggle         (cfg_commit_toggle),
    .stat_fbcap_frame_seq      (fbcap_seq_axi),
    .stat_fbcap_frame_buf_idx  (fbcap_buf_idx_axi),
    .stat_save_status          (32'd0)
  );

  // -------------------------------------------------------
  // 二级同步器：frame_seq/frame_buf_idx → AXI 时钟域
  // （模拟 zynq_gba_top 中的 ASYNC_REG 同步）
  // -------------------------------------------------------
  logic [31:0] fbcap_seq_meta;
  logic        fbcap_buf_meta;
  always_ff @(posedge clk) begin
    if (!rst_n) begin
      fbcap_seq_meta   <= 32'd0;
      fbcap_seq_axi    <= 32'd0;
      fbcap_buf_meta   <= 1'b0;
      fbcap_buf_idx_axi <= 1'b0;
    end else begin
      fbcap_seq_meta    <= frame_seq;
      fbcap_seq_axi     <= fbcap_seq_meta;
      fbcap_buf_meta    <= frame_buf_idx;
      fbcap_buf_idx_axi <= fbcap_buf_meta;
    end
  end

  // -------------------------------------------------------
  // core_fb_newframe 逻辑（复制自 zynq_gba_top）
  // -------------------------------------------------------
  assign core_fb_newframe = unused_core_fb_req &&
                            (unused_core_fb_addr[19:0] == 20'd0);

  // -------------------------------------------------------
  // 辅助 task：AXI 读
  // -------------------------------------------------------
  task automatic axi_read(input logic [11:0] addr, output logic [31:0] data);
    // DUT 在 !rvalid && arvalid 时，同一拍同时置 arready=1, rvalid=1, rdata=结果。
    // 所以只需要等 rvalid 出现即可。
    @(posedge clk);
    axi_araddr  <= addr;
    axi_arvalid <= 1'b1;
    // 等 rvalid（最多 20 周期超时保护）
    begin
      int timeout = 20;
      do begin
        @(posedge clk);
        timeout--;
      end while (!axi_rvalid && timeout > 0);
      if (timeout <= 0) $display("[ERROR] axi_read timeout addr=0x%03x", addr);
    end
    data = axi_rdata;
    axi_arvalid <= 1'b0;
    @(posedge clk);  // 让 rvalid 清除
  endtask

  // -------------------------------------------------------
  // 辅助 task：AXI 写
  // -------------------------------------------------------
  task automatic axi_write(input logic [11:0] addr, input logic [31:0] data);
    // 阶段1：发送地址
    @(posedge clk);
    axi_awaddr  <= addr;
    axi_awvalid <= 1'b1;
    @(posedge clk);  // DUT 锁存 awaddr
    @(posedge clk);  // awready 可见
    axi_awvalid <= 1'b0;
    // 阶段2：发送数据
    axi_wdata   <= data;
    axi_wvalid  <= 1'b1;
    @(posedge clk);  // DUT 执行写入
    @(posedge clk);  // wready + bvalid 可见
    axi_wvalid  <= 1'b0;
    @(posedge clk);  // bvalid 清除
  endtask

  // -------------------------------------------------------
  // 辅助 task：向 BRAM 捕获模块写入一整帧像素
  // pixel_data = {1'b0, R[4:0], G[5:0], B[4:0], extra}
  // 这里简单用 addr 低 16 位作数据，便于验证
  // -------------------------------------------------------
  task automatic drive_one_frame(input int frame_num);
    int addr;
    $display("[TB] === 开始写入第 %0d 帧 ===", frame_num);
    for (addr = 0; addr < FRAME_PIXELS; addr++) begin
      @(posedge clk);
      pixel_addr <= addr[15:0];
      pixel_data <= {2'b00, addr[15:0]};  // 简单测试数据
      pixel_we   <= 1'b1;
    end
    @(posedge clk);
    pixel_we <= 1'b0;
    $display("[TB] === 第 %0d 帧写入完成, frame_seq=%0d, buf_idx=%0d ===",
             frame_num, frame_seq, frame_buf_idx);
  endtask

  // -------------------------------------------------------
  // 辅助 task：通过 port B 读 BRAM 一个字（模拟 PS 读取）
  // byte_addr 对应 PS 视角的地址偏移
  // -------------------------------------------------------
  task automatic bram_read_word(input logic [17:0] byte_addr,
                                 output logic [31:0] data);
    @(posedge clk);
    bram_en_b   <= 1'b1;
    bram_we_b   <= 4'b0;
    bram_addr_b <= byte_addr;
    @(posedge clk);
    bram_en_b   <= 1'b0;
    @(posedge clk);  // 1 周期读延迟
    data = bram_dout_b;
  endtask

  // -------------------------------------------------------
  // 辅助函数：复制 gba_frame_capture_bram 的 pack_pixel
  // -------------------------------------------------------
  function automatic logic [15:0] pack_pixel(input logic [17:0] pix);
    pack_pixel = {pix[17:13], pix[11:6], pix[5:1]};
  endfunction

  // -------------------------------------------------------
  // 断言辅助
  // -------------------------------------------------------
  task automatic check(input string name, input logic [31:0] actual,
                        input logic [31:0] expected);
    if (actual === expected) begin
      test_pass++;
      $display("[PASS] %s: 0x%08x == 0x%08x", name, actual, expected);
    end else begin
      test_fail++;
      $display("[FAIL] %s: actual=0x%08x, expected=0x%08x", name, actual, expected);
    end
  endtask

  // -------------------------------------------------------
  // 主测试流程
  // -------------------------------------------------------
  initial begin
    logic [31:0] rd_data;
    logic [31:0] expected_word;
    logic [15:0] packed_even, packed_odd;
    int buf_offset_bytes;

    $display("============================================================");
    $display(" tb_hdmi_black_screen — HDMI 黑屏根因定位仿真");
    $display("============================================================");

    // -- 复位
    pixel_addr = 0;
    pixel_data = 0;
    pixel_we   = 0;
    rst_n      = 0;
    repeat (20) @(posedge clk);
    rst_n = 1;
    repeat (5)  @(posedge clk);

    // =====================================================
    // 测试 1：初始状态检查
    // =====================================================
    $display("\n--- 测试 1：初始状态 ---");
    check("frame_seq 初始值", frame_seq, 32'd0);
    check("frame_buf_idx 初始值", {31'd0, frame_buf_idx}, 32'd0);

    // 通过 AXI 读取 FB_CAP_SEQ (偏移 0x0AC)
    repeat (5) @(posedge clk);  // 等同步器稳定
    axi_read(12'h0AC, rd_data);
    check("AXI FB_CAP_SEQ 初始值", rd_data, 32'd0);

    axi_read(12'h0A8, rd_data);
    check("AXI FB_CAP_STATUS 初始值", rd_data, 32'd0);

    // =====================================================
    // 测试 2：写入第一帧，验证 frame_seq 递增
    // =====================================================
    $display("\n--- 测试 2：写入第一帧 ---");
    drive_one_frame(1);

    check("frame_seq 第1帧后", frame_seq, 32'd1);
    check("frame_buf_idx 第1帧后", {31'd0, frame_buf_idx}, 32'd0);
    // 第1帧写入 buf 0，完成后 write_buf_idx 翻转到 1
    // frame_buf_idx 输出当前已完成帧的 buf idx = 0

    // 等同步器传播到 AXI 域（2 级同步 + AXI 读延迟）
    repeat (10) @(posedge clk);
    axi_read(12'h0AC, rd_data);
    check("AXI FB_CAP_SEQ 第1帧后", rd_data, 32'd1);

    axi_read(12'h0A8, rd_data);
    check("AXI FB_CAP_STATUS 第1帧后 (buf_idx=0)", rd_data, 32'd0);

    // =====================================================
    // 测试 3：通过 port B 读回 BRAM 数据（模拟 PS blit 读取）
    // =====================================================
    $display("\n--- 测试 3：BRAM port B 读回验证 ---");

    // 第1帧写入了 buffer 0（word 地址 0 ~ 19199）
    // PS 读 byte_addr = word_index * 4
    // 验证 word 0：包含 pixel[0] 和 pixel[1] 的打包数据
    // pixel[0] addr=0 (偶数), data = {2'b00, 16'h0000}
    // pixel[1] addr=1 (奇数), data = {2'b00, 16'h0001}
    packed_even = pack_pixel({2'b00, 16'h0000});
    packed_odd  = pack_pixel({2'b00, 16'h0001});
    expected_word = {packed_odd, packed_even};

    bram_read_word(18'd0, rd_data);
    check("BRAM buf0 word[0]", rd_data, expected_word);

    // 验证 word 5：包含 pixel[10] 和 pixel[11]
    packed_even = pack_pixel({2'b00, 16'h000A});
    packed_odd  = pack_pixel({2'b00, 16'h000B});
    expected_word = {packed_odd, packed_even};
    bram_read_word(18'd20, rd_data);  // word 5 => byte addr 20
    check("BRAM buf0 word[5]", rd_data, expected_word);

    // 验证最后一个 word (word 19199)：pixel[38398] 和 pixel[38399]
    packed_even = pack_pixel({2'b00, 16'h95FE});  // 38398
    packed_odd  = pack_pixel({2'b00, 16'h95FF});  // 38399
    expected_word = {packed_odd, packed_even};
    bram_read_word(18'd76796, rd_data);  // word 19199 => byte addr 76796
    check("BRAM buf0 word[19199] (最后)", rd_data, expected_word);

    // =====================================================
    // 测试 4：写入第二帧，验证双缓冲
    // =====================================================
    $display("\n--- 测试 4：写入第二帧（双缓冲） ---");
    drive_one_frame(2);

    check("frame_seq 第2帧后", frame_seq, 32'd2);
    check("frame_buf_idx 第2帧后", {31'd0, frame_buf_idx}, 32'd1);
    // 第2帧写入 buf 1

    // 读 buffer 1 的 word[0]（byte addr = 76800 = 19200*4）
    buf_offset_bytes = FRAME_WORDS * 4;  // 76800
    packed_even = pack_pixel({2'b00, 16'h0000});
    packed_odd  = pack_pixel({2'b00, 16'h0001});
    expected_word = {packed_odd, packed_even};
    bram_read_word(buf_offset_bytes[17:0], rd_data);
    check("BRAM buf1 word[0]", rd_data, expected_word);

    // 同时验证 buffer 0 的数据仍然完好（未被覆盖）
    packed_even = pack_pixel({2'b00, 16'h0000});
    packed_odd  = pack_pixel({2'b00, 16'h0001});
    expected_word = {packed_odd, packed_even};
    bram_read_word(18'd0, rd_data);
    check("BRAM buf0 word[0] 仍完好", rd_data, expected_word);

    // =====================================================
    // 测试 5：不完整帧（pixel_addr 不到 38399）
    // =====================================================
    $display("\n--- 测试 5：不完整帧，frame_seq 不应递增 ---");
    begin
      int addr;
      // 只写前 100 个像素
      for (addr = 0; addr < 100; addr++) begin
        @(posedge clk);
        pixel_addr <= addr[15:0];
        pixel_data <= 18'h1ABCD;
        pixel_we   <= 1'b1;
      end
      @(posedge clk);
      pixel_we <= 1'b0;
    end
    check("frame_seq 不完整帧后仍=2", frame_seq, 32'd2);

    // =====================================================
    // 测试 6：core_fb_newframe 信号测试
    // =====================================================
    $display("\n--- 测试 6：core_fb_newframe 信号 ---");

    // 当 addr[19:0] != 0 时，newframe 应为 0
    @(posedge clk);
    unused_core_fb_req  <= 1'b1;
    unused_core_fb_addr <= 26'h0000100;
    @(posedge clk);
    check("newframe addr!=0", {31'd0, core_fb_newframe}, 32'd0);

    // 当 addr[19:0] == 0 且 req=1 时，newframe 应为 1
    unused_core_fb_addr <= 26'h2000000;  // 高位非零，低20位=0
    @(posedge clk);
    check("newframe addr[19:0]==0 & req=1", {31'd0, core_fb_newframe}, 32'd1);

    // req=0 时，newframe 应为 0
    unused_core_fb_req <= 1'b0;
    @(posedge clk);
    check("newframe req=0", {31'd0, core_fb_newframe}, 32'd0);

    // =====================================================
    // 测试 7：模拟 largeimg SM 快速完成（largeimg_out_done=1）
    // 验证每帧 newframe 只触发一次
    // =====================================================
    $display("\n--- 测试 7：模拟 largeimg 快跑，检查 newframe 频率 ---");
    newframe_count = 0;
    fork
      // 监听 newframe 脉冲
      begin : monitor_newframe
        forever begin
          @(posedge clk);
          if (core_fb_newframe) newframe_count++;
        end
      end
      // 模拟一帧的 largeimg 输出：160行 × 240像素 × 2次写入
      begin : drive_largeimg
        int line, pixel, cnt;
        for (line = 0; line < FRAME_HEIGHT; line++) begin
          for (pixel = 0; pixel < FRAME_WIDTH; pixel++) begin
            for (cnt = 0; cnt < 2; cnt++) begin
              @(posedge clk);
              unused_core_fb_req <= 1'b1;
              // 地址公式（简化自 gba_top）：
              // line 0, pixel 0, cnt 0 → addr = line*1024 + frame*0x100000
              // 低20位 = (line*1024 + pixel*2 + cnt*512) & 0xFFFFF
              unused_core_fb_addr <= (line * 1024 + pixel * 2 +
                                      cnt * 512) & 26'h3FFFFFF;
            end
          end
        end
        @(posedge clk);
        unused_core_fb_req <= 1'b0;
        repeat (5) @(posedge clk);
        disable monitor_newframe;
      end
    join
    $display("[INFO] largeimg 一帧内 newframe 触发次数 = %0d (期望=1)", newframe_count);
    check("newframe 每帧仅触发一次", newframe_count, 1);

    // =====================================================
    // 测试 8：PS blit 地址映射验证
    // =====================================================
    $display("\n--- 测试 8：PS BRAM 地址映射完整性 ---");
    begin
      int word_idx;
      logic [31:0] rd_val;
      int errors = 0;
      // 抽样检查 buffer 0 的若干 word
      for (word_idx = 0; word_idx < FRAME_WORDS; word_idx += 1000) begin
        logic [17:0] byte_addr;
        logic [15:0] pe, po;
        logic [31:0] exp;
        byte_addr = word_idx * 4;
        pe = pack_pixel({2'b00, 16'(word_idx * 2)});
        po = pack_pixel({2'b00, 16'(word_idx * 2 + 1)});
        exp = {po, pe};
        bram_read_word(byte_addr, rd_val);
        if (rd_val !== exp) begin
          $display("[FAIL] buf0 word[%0d] addr=0x%05x: got=0x%08x exp=0x%08x",
                   word_idx, byte_addr, rd_val, exp);
          errors++;
        end
      end
      if (errors == 0) begin
        test_pass++;
        $display("[PASS] buf0 抽样 %0d 个 word 全部正确",
                 (FRAME_WORDS + 999) / 1000);
      end else begin
        test_fail++;
      end
    end

    // =====================================================
    // 测试 9：连续写入多帧后 AXI 读 FB_CAP_SEQ 持续递增
    // =====================================================
    $display("\n--- 测试 9：连续多帧 frame_seq 递增 ---");
    begin
      int f;
      for (f = 3; f <= 5; f++) begin
        drive_one_frame(f);
      end
      check("frame_seq 第5帧后", frame_seq, 32'd5);
      repeat (10) @(posedge clk);
      axi_read(12'h0AC, rd_data);
      check("AXI FB_CAP_SEQ 第5帧后", rd_data, 32'd5);
    end

    // =====================================================
    // 汇总
    // =====================================================
    $display("\n============================================================");
    $display(" 仿真完成: PASS=%0d  FAIL=%0d", test_pass, test_fail);
    $display("============================================================");

    if (test_fail > 0)
      $display("[RESULT] *** 存在失败用例，需进一步排查 ***");
    else
      $display("[RESULT] 全部通过 — BRAM 捕获 + AXI 读回 + 地址映射均正确");

    $display("\n[分析] 如果本 TB 全部 PASS，说明 PL 侧 BRAM 捕获链路无问题。");
    $display("[分析] 黑屏根因更可能在 PS 侧：");
    $display("[分析]   1. PsAppVideo_PresentCapturedFrameIfReady 未被调用");
    $display("[分析]   2. FB_CAP_SEQ 在 PS 看到的值始终 == fbcap_last_frame_seq");
    $display("[分析]   3. BRAM 控制器基地址不匹配 xparameters.h");
    $display("[分析]   4. BlitCapturedFrameToHdmi 的缩放/park 逻辑有 bug");

    $finish;
  end

  // -------------------------------------------------------
  // 超时保护
  // -------------------------------------------------------
  initial begin
    #(100_000_000);  // 100 ms
    $display("[TIMEOUT] 仿真超时退出！");
    $finish;
  end

endmodule
