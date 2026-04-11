`timescale 1ns / 1ps
// tb_fb_writer_overrun.sv
// 目标：在连续多行像素输入 + ch1 读干扰下，验证 fb_native_scale_writer
// 是否会因为 line buffer/pending 深度不足而丢行或覆写正在输出的行。

module tb_fb_writer_overrun;

  logic clk = 1'b0;
  logic rst_n = 1'b0;
  always #5 clk = ~clk;  // 100 MHz

  localparam int INPUT_LINES        = 6;
  localparam int PIXELS_PER_LINE    = 240;
  localparam int FRAME_BYTES        = 32'h0020_0000;
  localparam int FRAME2_BASE        = 32'h1840_0000;
  localparam int FB_BASE_OFFSET     = 32'h0003_2140; // 0x32000 + 0x140
  localparam int FB_LINE_BYTES      = 32'd2560;
  localparam int FB_FIELD_BYTES     = 32'd5120;
  localparam int MAX_EXPECTED_WRITES = INPUT_LINES * PIXELS_PER_LINE * 2;

  // fb writer input
  logic [1:0]  display_frame_idx;
  logic [15:0] pixel_addr;
  logic [17:0] pixel_data;
  logic        pixel_we;

  // fb writer -> arbiter
  logic [27:1] wr_addr;
  logic [63:0] wr_data;
  logic        wr_req;
  logic        wr_ack;

  // ddram_mux channel 1 (continuous ROM traffic)
  logic [27:1] ch1_addr;
  logic [63:0] ch1_dout;
  logic [15:0] ch1_din;
  logic        ch1_req;
  logic        ch1_rnw;
  logic        ch1_ready;

  // ddram_mux channel 5 (framebuffer writes)
  logic [27:1] ch5_addr;
  logic [63:0] ch5_dout;
  logic [63:0] ch5_din;
  logic        ch5_req;
  logic        ch5_rnw;
  logic        ch5_ready;

  // other ddram_mux channels (idle)
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

  // backend <-> mux
  logic        ddram_busy;
  logic [7:0]  ddram_burstcnt;
  logic [28:0] ddram_addr;
  logic [63:0] ddram_dout;
  logic        ddram_dout_ready;
  logic        ddram_rd;
  logic [63:0] ddram_din;
  logic [7:0]  ddram_be;
  logic        ddram_we;

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

  // scoreboard
  bit          seen[0:INPUT_LINES-1][0:1][0:PIXELS_PER_LINE-1];
  logic [63:0] observed_data[0:INPUT_LINES-1][0:1][0:PIXELS_PER_LINE-1];
  int          write_hits_per_line[0:INPUT_LINES-1][0:1];
  int          total_writes;
  int          total_fb_writes;
  int          total_ch1_reads;
  int          malformed_writes;
  int          duplicate_writes;

  // ch1 continuous traffic
  logic [24:0] ch1_word_addr;
  logic        ch1_stream_active;

  // simple AXI responder with deterministic latency
  localparam int AR_ACCEPT_DELAY = 1;
  localparam int R_DATA_DELAY    = 3;
  localparam int AW_ACCEPT_DELAY = 1;
  localparam int B_RESP_DELAY    = 4;

  logic        ar_pending;
  logic [31:0] ar_addr_captured;
  int          ar_delay_cnt;
  int          r_delay_cnt;

  logic        aw_pending;
  logic [31:0] aw_addr_captured;
  int          aw_delay_cnt;

  logic        have_aw;
  logic        have_w;
  logic [63:0] w_data_captured;
  logic [7:0]  w_strb_captured;
  logic        txn_valid;
  logic [31:0] txn_addr_captured;
  logic [63:0] txn_data_captured;
  logic [7:0]  txn_strb_captured;

  logic        bresp_pending;
  int          bresp_delay_cnt;

  function automatic logic [31:0] expand_pixel(input logic [17:0] pixel);
    expand_pixel = {
      8'h00,
      pixel[5:0],   pixel[5:4],
      pixel[11:6],  pixel[11:10],
      pixel[17:12], pixel[17:16]
    };
  endfunction

  function automatic logic [17:0] make_pixel(input int line_num, input int pixel_num);
    logic [5:0] red;
    logic [5:0] green;
    logic [5:0] blue;

    red   = ((line_num * 7) + pixel_num + 6'h03);
    green = ((line_num * 11) ^ (pixel_num + 6'h05));
    blue  = ((line_num * 13) + (pixel_num * 3) + 6'h01);

    make_pixel = {red, green, blue};
  endfunction

  task automatic feed_pixel(input int line_num, input int pixel_num, input int gap_cycles);
    logic [17:0] src_pixel;

    src_pixel = make_pixel(line_num, pixel_num);
    @(posedge clk);
    pixel_addr <= (line_num * PIXELS_PER_LINE) + pixel_num;
    pixel_data <= src_pixel;
    pixel_we   <= 1'b1;
    @(posedge clk);
    pixel_we   <= 1'b0;
    repeat (gap_cycles) @(posedge clk);
  endtask

  task automatic feed_line(input int line_num, input int gap_cycles);
    int pixel_num;

    for (pixel_num = 0; pixel_num < PIXELS_PER_LINE; pixel_num++) begin
      feed_pixel(line_num, pixel_num, gap_cycles);
    end
  endtask

  task automatic decode_and_score_write(input logic [31:0] addr, input logic [63:0] data);
    int frame_off;
    int field_off;
    int src_line;
    int stage;
    int x_idx;
    logic [31:0] expected_rgb;
    logic [63:0] expected_qword;
    logic [17:0] expected_src;

    total_writes++;

    if ((addr < FRAME2_BASE) || (addr >= (FRAME2_BASE + FRAME_BYTES))) begin
      return;
    end

    total_fb_writes++;
    frame_off = addr - FRAME2_BASE;

    if (frame_off < FB_BASE_OFFSET) begin
      malformed_writes++;
      $display("[TB] malformed fb write below active area addr=0x%08x", addr);
      return;
    end

    frame_off = frame_off - FB_BASE_OFFSET;
    src_line  = frame_off / FB_FIELD_BYTES;
    field_off = frame_off % FB_FIELD_BYTES;

    if (src_line < 0 || src_line >= INPUT_LINES) begin
      malformed_writes++;
      $display("[TB] malformed fb write outside expected line window addr=0x%08x src_line=%0d",
               addr, src_line);
      return;
    end

    if (field_off >= FB_LINE_BYTES) begin
      stage     = 1;
      field_off = field_off - FB_LINE_BYTES;
    end else begin
      stage = 0;
    end

    if ((field_off % 8) != 0) begin
      malformed_writes++;
      $display("[TB] malformed fb write non-qword-aligned line offset addr=0x%08x off=0x%0x",
               addr, field_off);
      return;
    end

    x_idx = field_off / 8;
    if (x_idx < 0 || x_idx >= PIXELS_PER_LINE) begin
      malformed_writes++;
      $display("[TB] malformed fb write x out of range addr=0x%08x x=%0d",
               addr, x_idx);
      return;
    end

    expected_src   = make_pixel(src_line, x_idx);
    expected_rgb   = expand_pixel(expected_src);
    expected_qword = {expected_rgb, expected_rgb};

    if (seen[src_line][stage][x_idx]) begin
      duplicate_writes++;
      $display("[TB] duplicate write line=%0d stage=%0d x=%0d addr=0x%08x data=0x%016x",
               src_line, stage, x_idx, addr, data);
    end

    seen[src_line][stage][x_idx]       = 1'b1;
    observed_data[src_line][stage][x_idx] = data;
    write_hits_per_line[src_line][stage]++;

    if (data !== expected_qword) begin
      malformed_writes++;
      $display("[TB] data mismatch line=%0d stage=%0d x=%0d addr=0x%08x data=0x%016x expect=0x%016x",
               src_line, stage, x_idx, addr, data, expected_qword);
    end
  endtask

  fb_native_scale_writer u_fb_writer (
    .clk              (clk),
    .rst_n            (rst_n),
    .display_frame_idx(display_frame_idx),
    .pixel_addr       (pixel_addr),
    .pixel_data       (pixel_data),
    .pixel_we         (pixel_we),
    .wr_addr          (wr_addr),
    .wr_data          (wr_data),
    .wr_req           (wr_req),
    .wr_ack           (wr_ack)
  );

  fb_ddr_arbiter u_fb_arb (
    .clk       (clk),
    .rst_n     (rst_n),
    .wr_addr   (wr_addr),
    .wr_data   (wr_data),
    .wr_req    (wr_req),
    .wr_ack    (wr_ack),
    .ch5_addr  (ch5_addr),
    .ch5_din   (ch5_din),
    .ch5_req   (ch5_req),
    .ch5_rnw   (ch5_rnw),
    .ch5_ready (ch5_ready)
  );

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

  assign ch1_addr = {1'b0, ch1_word_addr, 1'b0};
  assign ch1_din  = 16'd0;
  assign ch1_req  = ch1_stream_active;
  assign ch1_rnw  = 1'b1;

  assign m_axi_rdata  = {32'hA5A5_0000 | ch1_word_addr, 32'h5A5A_0000 | ch1_word_addr};
  assign m_axi_rresp  = 2'b00;
  assign m_axi_rlast  = m_axi_rvalid;

  // Continuous ch1 pressure: every ready immediately advances to the next line.
  always_ff @(posedge clk) begin
    if (!rst_n) begin
      ch1_word_addr   <= 25'h30000;
      total_ch1_reads <= 0;
    end else if (ch1_ready) begin
      ch1_word_addr   <= ch1_word_addr + 25'd4;
      total_ch1_reads <= total_ch1_reads + 1;
    end
  end

  // Simplified AXI slave timing model.
  always_ff @(posedge clk) begin
    if (!rst_n) begin
      ar_pending       <= 1'b0;
      ar_addr_captured <= 32'd0;
      ar_delay_cnt     <= 0;
      r_delay_cnt      <= 0;
      m_axi_arready    <= 1'b0;
      m_axi_rvalid     <= 1'b0;

      aw_pending       <= 1'b0;
      aw_addr_captured <= 32'd0;
      aw_delay_cnt     <= 0;
      m_axi_awready    <= 1'b0;

      have_aw         <= 1'b0;
      have_w          <= 1'b0;
      w_data_captured  <= 64'd0;
      w_strb_captured  <= 8'd0;
      txn_valid        <= 1'b0;
      txn_addr_captured <= 32'd0;
      txn_data_captured <= 64'd0;
      txn_strb_captured <= 8'd0;
      m_axi_wready     <= 1'b1;

      bresp_pending    <= 1'b0;
      bresp_delay_cnt  <= 0;
      m_axi_bvalid     <= 1'b0;
      m_axi_bresp      <= 2'b00;
    end else begin
      m_axi_arready <= 1'b0;
      m_axi_awready <= 1'b0;

      if (m_axi_rvalid && m_axi_rready) begin
        m_axi_rvalid <= 1'b0;
      end

      if (!ar_pending && m_axi_arvalid) begin
        ar_pending       <= 1'b1;
        ar_addr_captured <= m_axi_araddr;
        ar_delay_cnt     <= AR_ACCEPT_DELAY;
      end

      if (ar_pending) begin
        if (ar_delay_cnt > 0) begin
          ar_delay_cnt <= ar_delay_cnt - 1;
        end else begin
          m_axi_arready <= 1'b1;
          ar_pending    <= 1'b0;
          r_delay_cnt   <= R_DATA_DELAY;
        end
      end

      if (!m_axi_rvalid && (r_delay_cnt > 0)) begin
        r_delay_cnt <= r_delay_cnt - 1;
        if (r_delay_cnt == 1) begin
          m_axi_rvalid <= 1'b1;
        end
      end

      if (!aw_pending && !have_aw && m_axi_awvalid) begin
        aw_pending       <= 1'b1;
        aw_delay_cnt     <= AW_ACCEPT_DELAY;
      end

      if (aw_pending) begin
        if (aw_delay_cnt > 0) begin
          aw_delay_cnt <= aw_delay_cnt - 1;
        end else begin
          m_axi_awready <= 1'b1;
          if (m_axi_awvalid) begin
            aw_addr_captured <= m_axi_awaddr;
            have_aw          <= 1'b1;
            aw_pending       <= 1'b0;
          end
        end
      end

      if (!have_w && m_axi_wvalid && m_axi_wready) begin
        have_w          <= 1'b1;
        w_data_captured <= m_axi_wdata;
        w_strb_captured <= m_axi_wstrb;
      end

      if (!bresp_pending && !txn_valid && have_aw && have_w) begin
        txn_valid        <= 1'b1;
        txn_addr_captured <= aw_addr_captured;
        txn_data_captured <= w_data_captured;
        txn_strb_captured <= w_strb_captured;
        have_aw          <= 1'b0;
        have_w           <= 1'b0;
        bresp_pending   <= 1'b1;
        bresp_delay_cnt <= B_RESP_DELAY;
      end

      if (bresp_pending) begin
        if (bresp_delay_cnt > 0) begin
          bresp_delay_cnt <= bresp_delay_cnt - 1;
        end else begin
          m_axi_bvalid  <= 1'b1;
          m_axi_bresp   <= 2'b00;
          bresp_pending <= 1'b0;
        end
      end

      if (m_axi_bvalid && m_axi_bready) begin
        m_axi_bvalid <= 1'b0;
        if (txn_valid) begin
          // Score against the backend's own latched payload so the checker
          // cannot mis-pair AW/W beats under delayed handshakes.
          decode_and_score_write(u_backend.awaddr_reg, u_backend.wdata_reg);
        end
        txn_valid <= 1'b0;
      end
    end
  end

  task automatic report_line_status;
    int line_num;
    int stage;
    int first_missing;
    int first_bad;
    logic [63:0] expected_qword;
    logic [31:0] expected_rgb;

    for (line_num = 0; line_num < INPUT_LINES; line_num++) begin
      for (stage = 0; stage < 2; stage++) begin
        first_missing = -1;
        first_bad     = -1;
        for (int pixel_num = 0; pixel_num < PIXELS_PER_LINE; pixel_num++) begin
          expected_rgb   = expand_pixel(make_pixel(line_num, pixel_num));
          expected_qword = {expected_rgb, expected_rgb};
          if (!seen[line_num][stage][pixel_num] && (first_missing < 0)) begin
            first_missing = pixel_num;
          end
          if (seen[line_num][stage][pixel_num] &&
              observed_data[line_num][stage][pixel_num] !== expected_qword &&
              (first_bad < 0)) begin
            first_bad = pixel_num;
          end
        end

        $display("[TB] line=%0d stage=%0d hits=%0d first_missing=%0d first_bad=%0d",
                 line_num,
                 stage,
                 write_hits_per_line[line_num][stage],
                 first_missing,
                 first_bad);
      end
    end
  endtask

  initial begin
    int missing_writes;

    display_frame_idx = 2'd0;  // core should render into frame2
    pixel_addr        = '0;
    pixel_data        = '0;
    pixel_we          = 1'b0;
    ch1_stream_active = 1'b0;

    repeat (8) @(posedge clk);
    rst_n <= 1'b1;
    repeat (4) @(posedge clk);

    $display("==============================================================");
    $display("[TB] fb writer overrun stress");
    $display("[TB] INPUT_LINES=%0d PIXELS_PER_LINE=%0d", INPUT_LINES, PIXELS_PER_LINE);
    $display("[TB] Stress: gap=2 cycles, ch1 continuous traffic enabled");
    $display("==============================================================");

    ch1_stream_active <= 1'b1;

    fork
      begin : feed_lines
        for (int line_num = 0; line_num < INPUT_LINES; line_num++) begin
          $display("[TB] feeding line %0d", line_num);
          feed_line(line_num, 2);
        end
      end
    join

    // Wait long enough for all successfully queued writes to drain.
    repeat (MAX_EXPECTED_WRITES * 20) @(posedge clk);

    report_line_status();

    missing_writes = 0;
    for (int line_num = 0; line_num < INPUT_LINES; line_num++) begin
      for (int stage = 0; stage < 2; stage++) begin
        for (int pixel_num = 0; pixel_num < PIXELS_PER_LINE; pixel_num++) begin
          if (!seen[line_num][stage][pixel_num]) begin
            missing_writes++;
          end
        end
      end
    end

    $display("[TB] total_writes=%0d total_fb_writes=%0d total_ch1_reads=%0d malformed=%0d duplicate=%0d missing=%0d",
             total_writes, total_fb_writes, total_ch1_reads, malformed_writes, duplicate_writes, missing_writes);
    $display("[TB] writer state=%0d queued=%0d capture_buf_idx=%0d active_buf_idx=%0d write_x=%0d write_stage=%0b",
             u_fb_writer.wr_state,
             u_fb_writer.queued_line_count,
             u_fb_writer.capture_buf_idx,
             u_fb_writer.active_buf_idx,
             u_fb_writer.write_x,
             u_fb_writer.write_stage);

    if (missing_writes != 0) begin
      $fatal(1,
             "[FAIL] fb_native_scale_writer dropped %0d scaled pixels under sustained load",
             missing_writes);
    end

    if (malformed_writes != 0) begin
      $fatal(1,
             "[FAIL] fb_native_scale_writer produced %0d malformed/corrupted writes",
             malformed_writes);
    end

    if (total_fb_writes != MAX_EXPECTED_WRITES) begin
      $fatal(1,
             "[FAIL] expected %0d fb writes, observed %0d",
             MAX_EXPECTED_WRITES, total_fb_writes);
    end

    $display("[PASS] fb_native_scale_writer preserved every line under this stress profile.");
    $finish;
  end

  initial begin
    #40_000_000;
    $fatal(1, "[TIMEOUT] simulation exceeded 40 ms");
  end

endmodule
