`timescale 1ns / 1ps

module tb_fb_writer_resync;

  logic clk = 1'b0;
  logic rst_n = 1'b0;
  always #5 clk = ~clk;

  logic [1:0]  display_frame_idx;
  logic [15:0] pixel_addr;
  logic [17:0] pixel_data;
  logic        pixel_we;
  logic [27:1] wr_addr;
  logic [63:0] wr_data;
  logic        wr_req;
  logic        wr_ack;

  int write_count;

  function automatic logic [31:0] expand_pixel(input logic [17:0] pixel);
    expand_pixel = {
      8'h00,
      pixel[5:0],   pixel[5:4],
      pixel[11:6],  pixel[11:10],
      pixel[17:12], pixel[17:16]
    };
  endfunction

  function automatic logic [17:0] make_pixel(input int addr);
    logic [5:0] red;
    logic [5:0] green;
    logic [5:0] blue;
  begin
    red   = (addr + 6'h03);
    green = (addr ^ 6'h15);
    blue  = ((addr * 3) + 6'h01);
    make_pixel = {red, green, blue};
  end
  endfunction

  task automatic send_pixel(input int addr);
  begin
    @(posedge clk);
    pixel_addr <= addr[15:0];
    pixel_data <= make_pixel(addr);
    pixel_we   <= 1'b1;
    @(posedge clk);
    pixel_we   <= 1'b0;
  end
  endtask

  fb_native_scale_writer dut (
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

  always_ff @(posedge clk) begin
    if (!rst_n) begin
      wr_ack <= 1'b0;
      write_count <= 0;
    end else begin
      wr_ack <= wr_req;
      if (wr_req) begin
        write_count <= write_count + 1;
      end
    end
  end

  initial begin
    int idx;

    display_frame_idx = 2'd0;
    pixel_addr = '0;
    pixel_data = '0;
    pixel_we = 1'b0;
    repeat (4) @(posedge clk);
    rst_n = 1'b1;

    // Feed a partial frame tail without ever providing pixel_addr==0.
    // A robust writer must ignore this until a real frame start arrives.
    for (idx = 0; idx < 260; idx++) begin
      send_pixel(120 + idx);
    end

    repeat (80) @(posedge clk);
    if (write_count != 0) begin
      $fatal(1, "[TB] writer emitted %0d writes before frame sync", write_count);
    end

    // Now provide a clean frame start and one full line.
    for (idx = 0; idx < 240; idx++) begin
      send_pixel(idx);
    end

    wait (write_count == 480);

    if (wr_addr !== 28'h0) begin
      // no-op, keeps linter quiet about wr_addr visibility
    end

    $display("tb_fb_writer_resync PASS");
    $finish;
  end

endmodule
