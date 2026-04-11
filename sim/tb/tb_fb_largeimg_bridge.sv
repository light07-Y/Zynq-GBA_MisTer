`timescale 1ns/1ps

module tb_fb_largeimg_bridge;

  localparam logic [25:0] FB_REGION_BASE  = 26'h2000000;
  localparam logic [25:0] FB_FRAME_STRIDE = 26'h0080000;
  localparam logic [25:0] FB_LINE_STRIDE  = 26'd640;
  localparam logic [25:0] FB_X_OFFSET     = 26'd80;
  localparam logic [25:0] FB_Y_OFFSET     = 26'd51200;

  logic        clk = 1'b0;
  logic        rst_n = 1'b0;
  logic [1:0]  display_frame_idx;
  logic [25:0] largeimg_addr;
  logic [63:0] largeimg_data;
  logic        largeimg_req;
  logic [27:1] wr_addr;
  logic [63:0] wr_data;
  logic        wr_req;

  fb_largeimg_bridge dut (
    .clk(clk),
    .rst_n(rst_n),
    .display_frame_idx(display_frame_idx),
    .largeimg_addr(largeimg_addr),
    .largeimg_data(largeimg_data),
    .largeimg_req(largeimg_req),
    .wr_addr(wr_addr),
    .wr_data(wr_data),
    .wr_req(wr_req)
  );

  always #5 clk = ~clk;

  function automatic logic [1:0] backbuffer_frame_idx(input logic [1:0] frame_idx);
    case (frame_idx)
      2'd0:    backbuffer_frame_idx = 2'd2;
      2'd1:    backbuffer_frame_idx = 2'd0;
      default: backbuffer_frame_idx = 2'd1;
    endcase
  endfunction

  function automatic logic [25:0] frame_offset(input logic [1:0] frame_idx);
    case (frame_idx)
      2'd0:    frame_offset = 26'h0000000;
      2'd1:    frame_offset = FB_FRAME_STRIDE;
      default: frame_offset = (FB_FRAME_STRIDE << 1);
    endcase
  endfunction

  function automatic logic [27:1] expected_wr_addr(
    input logic [1:0] frame_idx,
    input int unsigned row_idx,
    input int unsigned x_idx
  );
    logic [25:0] write_addr_core;
  begin
    write_addr_core =
      FB_REGION_BASE
      + frame_offset(frame_idx)
      + FB_Y_OFFSET
      + (row_idx * FB_LINE_STRIDE)
      + FB_X_OFFSET
      + x_idx;
    expected_wr_addr = {write_addr_core, 1'b0};
  end
  endfunction

  task automatic pulse_and_check(
    input logic [1:0] disp_idx,
    input logic [25:0] in_addr,
    input logic [63:0] in_data,
    input logic [1:0] expected_frame,
    input int unsigned expected_row,
    input int unsigned expected_x,
    input string label
  );
    logic [27:1] expected_addr;
  begin
    expected_addr = expected_wr_addr(expected_frame, expected_row, expected_x);

    display_frame_idx = disp_idx;
    largeimg_addr = in_addr;
    largeimg_data = in_data;
    largeimg_req = 1'b1;
    #1;

    if (wr_req !== 1'b1) begin
      $fatal(1, "[%s] wr_req deasserted unexpectedly", label);
    end
    if (wr_addr !== expected_addr) begin
      $fatal(1, "[%s] wr_addr mismatch exp=0x%08x got=0x%08x",
             label, expected_addr, wr_addr);
    end
    if (wr_data !== in_data) begin
      $fatal(1, "[%s] wr_data mismatch exp=0x%016x got=0x%016x",
             label, in_data, wr_data);
    end

    @(posedge clk);
    #1;
    largeimg_req = 1'b0;
    largeimg_addr = '0;
    largeimg_data = '0;
    @(posedge clk);
  end
  endtask

  initial begin
    display_frame_idx = 2'd0;
    largeimg_addr = '0;
    largeimg_data = '0;
    largeimg_req = 1'b0;

    repeat (3) @(posedge clk);
    rst_n = 1'b1;
    @(posedge clk);

    pulse_and_check(
      2'd0,
      {1'b1, 5'd2, 20'd0},
      64'h1122_3344_5566_7788,
      backbuffer_frame_idx(2'd0),
      0,
      0,
      "frame0-start-display0"
    );

    pulse_and_check(
      2'd1,
      {1'b1, 5'd2, 20'd2},
      64'h0102_0304_0506_0708,
      backbuffer_frame_idx(2'd0),
      0,
      2,
      "frame0-mid-display-change"
    );

    pulse_and_check(
      2'd1,
      {1'b1, 5'd2, 20'd512},
      64'h8899_AABB_CCDD_EEFF,
      backbuffer_frame_idx(2'd0),
      1,
      0,
      "frame0-next-row"
    );

    pulse_and_check(
      2'd1,
      {1'b1, 5'd0, 20'd0},
      64'hDEAD_BEEF_0123_4567,
      backbuffer_frame_idx(2'd1),
      0,
      0,
      "frame1-start-display1"
    );

    pulse_and_check(
      2'd2,
      {1'b1, 5'd1, 20'd514},
      64'h1357_9BDF_2468_ACE0,
      backbuffer_frame_idx(2'd1),
      1,
      2,
      "frame1-mid-display-change"
    );

    pulse_and_check(
      2'd2,
      {1'b1, 5'd1, 20'd0},
      64'hFACE_CAFE_0BAD_F00D,
      backbuffer_frame_idx(2'd2),
      0,
      0,
      "frame2-start-display2"
    );

    $display("tb_fb_largeimg_bridge PASS");
    $finish;
  end

endmodule
