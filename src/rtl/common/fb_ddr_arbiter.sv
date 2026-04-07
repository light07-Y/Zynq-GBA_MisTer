module fb_ddr_arbiter (
  input  logic         clk,
  input  logic         rst_n,

  input  logic [27:1]  wr_addr,
  input  logic [63:0]  wr_data,
  input  logic         wr_req,
  output logic         wr_ack,

  output logic [27:1]  ch5_addr,
  output logic [63:0]  ch5_din,
  output logic         ch5_req,
  output logic         ch5_rnw,
  input  logic         ch5_ready
);

  typedef enum logic [1:0] {
    IDLE,
    WAIT_DONE
  } state_t;

  state_t state;

  always_ff @(posedge clk) begin
    if (!rst_n) begin
      state            <= IDLE;
      ch5_addr         <= '0;
      ch5_din          <= '0;
      ch5_rnw          <= 1'b0;
      ch5_req          <= 1'b0;
      wr_ack           <= 1'b0;
    end else begin
      ch5_req <= 1'b0;
      wr_ack  <= 1'b0;

      case (state)
        IDLE: begin
          if (wr_req) begin
            ch5_addr        <= wr_addr;
            ch5_din         <= wr_data;
            ch5_rnw         <= 1'b0;
            ch5_req         <= 1'b1;
            state           <= WAIT_DONE;
          end
        end

        WAIT_DONE: begin
          if (ch5_ready) begin
            wr_ack <= 1'b1;
            state <= IDLE;
          end
        end

        default: begin
          state <= IDLE;
        end
      endcase
    end
  end

endmodule

module fb_native_scale_writer (
  input  logic         clk,
  input  logic         rst_n,
  input  logic [15:0]  pixel_addr,
  input  logic [17:0]  pixel_data,
  input  logic         pixel_we,
  output logic [27:1]  wr_addr,
  output logic [63:0]  wr_data,
  output logic         wr_req,
  input  logic         wr_ack
);

  localparam logic [25:0] FB_REGION_BASE  = 26'h2000000;
  localparam logic [25:0] FB_FRAME_STRIDE = 26'h0100000;

  typedef enum logic [1:0] {
    W_IDLE,
    W_SEND,
    W_WAIT_ACK
  } wr_state_t;

  (* ram_style = "distributed" *) logic [17:0] linebuf0 [0:239];
  (* ram_style = "distributed" *) logic [17:0] linebuf1 [0:239];

  wr_state_t wr_state;

  logic        capture_buf;
  logic [7:0]  capture_x;
  logic [7:0]  capture_y;
  logic [1:0]  capture_frame;

  logic        pending_valid;
  logic        pending_buf;
  logic [7:0]  pending_y;
  logic [1:0]  pending_frame;

  logic        active_buf;
  logic [7:0]  active_y;
  logic [1:0]  active_frame;
  logic [7:0]  write_x;
  logic        write_stage;

  logic [17:0] write_pixel;
  logic [31:0] write_pixel_rgb;
  logic [25:0] write_addr_core;

  function automatic logic [1:0] next_frame_idx(input logic [1:0] frame_idx);
    case (frame_idx)
      2'd0:    next_frame_idx = 2'd1;
      2'd1:    next_frame_idx = 2'd2;
      default: next_frame_idx = 2'd0;
    endcase
  endfunction

  function automatic logic [25:0] frame_offset(input logic [1:0] frame_idx);
    case (frame_idx)
      2'd0:    frame_offset = 26'h0000000;
      2'd1:    frame_offset = 26'h0100000;
      default: frame_offset = 26'h0200000;
    endcase
  endfunction

  function automatic logic [31:0] expand_pixel(input logic [17:0] pixel);
    expand_pixel = {
      8'h00,
      pixel[5:0],   pixel[5:4],
      pixel[11:6],  pixel[11:10],
      pixel[17:12], pixel[17:16]
    };
  endfunction

  always_comb begin
    write_pixel = active_buf ? linebuf1[write_x] : linebuf0[write_x];
    write_pixel_rgb = expand_pixel(write_pixel);
    write_addr_core =
      FB_REGION_BASE
      + frame_offset(active_frame)
      + {8'd0, active_y, 10'd0}
      + (write_stage ? 26'd512 : 26'd0)
      + {17'd0, write_x, 1'b0};

    wr_addr = {write_addr_core, 1'b0};
    wr_data = {write_pixel_rgb, write_pixel_rgb};
  end

  always_ff @(posedge clk) begin
    logic line_done;
    logic line_buf;
    logic [7:0] line_y;
    logic [1:0] line_frame;
    logic launch_line;
    logic launch_pending;

    if (!rst_n) begin
      wr_state       <= W_IDLE;
      wr_req         <= 1'b0;
      capture_buf    <= 1'b0;
      capture_x      <= 8'd0;
      capture_y      <= 8'd0;
      capture_frame  <= 2'd0;
      pending_valid  <= 1'b0;
      pending_buf    <= 1'b0;
      pending_y      <= 8'd0;
      pending_frame  <= 2'd0;
      active_buf     <= 1'b0;
      active_y       <= 8'd0;
      active_frame   <= 2'd0;
      write_x        <= 8'd0;
      write_stage    <= 1'b0;
    end else begin
      wr_req         <= 1'b0;
      line_done      = 1'b0;
      line_buf       = 1'b0;
      line_y         = 8'd0;
      line_frame     = 2'd0;
      launch_line    = 1'b0;
      launch_pending = 1'b0;

      if (pixel_we) begin
        if (capture_buf) begin
          linebuf1[capture_x] <= pixel_data;
        end else begin
          linebuf0[capture_x] <= pixel_data;
        end

        if ((pixel_addr == 16'd0) && ((capture_x != 8'd0) || (capture_y != 8'd0))) begin
          capture_x <= 8'd1;
          capture_y <= 8'd0;
          if (capture_buf) begin
            linebuf1[8'd0] <= pixel_data;
          end else begin
            linebuf0[8'd0] <= pixel_data;
          end
        end else if (capture_x == 8'd239) begin
          line_done  = 1'b1;
          line_buf   = capture_buf;
          line_y     = capture_y;
          line_frame = capture_frame;

          capture_buf <= ~capture_buf;
          capture_x   <= 8'd0;
          if (capture_y == 8'd159) begin
            capture_y     <= 8'd0;
            capture_frame <= next_frame_idx(capture_frame);
          end else begin
            capture_y <= capture_y + 8'd1;
          end
        end else begin
          capture_x <= capture_x + 8'd1;
        end
      end

      case (wr_state)
        W_IDLE: begin
          if (pending_valid) begin
            launch_pending = 1'b1;
            pending_valid  <= 1'b0;
          end else if (line_done) begin
            launch_line = 1'b1;
          end

          if (launch_pending) begin
            active_buf   <= pending_buf;
            active_y     <= pending_y;
            active_frame <= pending_frame;
            write_x      <= 8'd0;
            write_stage  <= 1'b0;
            wr_state     <= W_SEND;
          end else if (launch_line) begin
            active_buf   <= line_buf;
            active_y     <= line_y;
            active_frame <= line_frame;
            write_x      <= 8'd0;
            write_stage  <= 1'b0;
            wr_state     <= W_SEND;
          end
        end

        W_SEND: begin
          wr_req    <= 1'b1;
          wr_state  <= W_WAIT_ACK;
        end

        W_WAIT_ACK: begin
          if (wr_ack) begin
            if (!write_stage) begin
              write_stage <= 1'b1;
              wr_state    <= W_SEND;
            end else if (write_x == 8'd239) begin
              if (pending_valid) begin
                active_buf   <= pending_buf;
                active_y     <= pending_y;
                active_frame <= pending_frame;
                write_x      <= 8'd0;
                write_stage  <= 1'b0;
                pending_valid <= 1'b0;
                wr_state     <= W_SEND;
              end else if (line_done) begin
                active_buf   <= line_buf;
                active_y     <= line_y;
                active_frame <= line_frame;
                write_x      <= 8'd0;
                write_stage  <= 1'b0;
                wr_state     <= W_SEND;
              end else begin
                wr_state <= W_IDLE;
              end
            end else begin
              write_x     <= write_x + 8'd1;
              write_stage <= 1'b0;
              wr_state    <= W_SEND;
            end
          end
        end

        default: begin
          wr_state <= W_IDLE;
        end
      endcase

      if (line_done && !launch_line) begin
        if (!pending_valid) begin
          pending_valid <= 1'b1;
          pending_buf   <= line_buf;
          pending_y     <= line_y;
          pending_frame <= line_frame;
        end
      end
    end
  end

endmodule
