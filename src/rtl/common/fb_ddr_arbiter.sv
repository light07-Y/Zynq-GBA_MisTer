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

module gba_frame_capture_bram (
  input  logic         wr_clk,
  input  logic         wr_rst_n,
  input  logic [15:0]  pixel_addr,
  input  logic [17:0]  pixel_data,
  input  logic         pixel_we,
  output logic [31:0]  frame_seq,
  output logic         frame_buf_idx,
  input  logic         bram_clk_b,
  input  logic         bram_rst_b,
  input  logic         bram_en_b,
  input  logic [3:0]   bram_we_b,
  input  logic [17:0]  bram_addr_b,
  input  logic [31:0]  bram_din_b,
  output logic [31:0]  bram_dout_b
);

  localparam int unsigned FRAME_PIXELS = 240 * 160;
  localparam int unsigned FRAME_WORDS  = FRAME_PIXELS / 2;
  localparam int unsigned MEM_WORDS    = FRAME_WORDS * 2;
  localparam int unsigned MEM_ADDR_W   = 16;

  logic        write_buf_idx;
  logic [15:0] even_pixel_latched;
  logic [MEM_ADDR_W-1:0] mem_addra;
  logic [MEM_ADDR_W-1:0] mem_addrb;
  logic [31:0] mem_dina;
  logic        mem_wea;

  function automatic logic [15:0] pack_pixel(input logic [17:0] pixel);
    pack_pixel = {pixel[17:13], pixel[11:6], pixel[5:1]};
  endfunction

  assign mem_addrb = bram_addr_b[17:2];

  xpm_memory_tdpram #(
    .ADDR_WIDTH_A            (MEM_ADDR_W),
    .ADDR_WIDTH_B            (MEM_ADDR_W),
    .AUTO_SLEEP_TIME         (0),
    .BYTE_WRITE_WIDTH_A      (32),
    .BYTE_WRITE_WIDTH_B      (8),
    .CASCADE_HEIGHT          (0),
    .CLOCKING_MODE           ("independent_clock"),
    .ECC_MODE                ("no_ecc"),
    .MEMORY_INIT_FILE        ("none"),
    .MEMORY_INIT_PARAM       (""),
    .MEMORY_OPTIMIZATION     ("true"),
    .MEMORY_PRIMITIVE        ("block"),
    .MEMORY_SIZE             (MEM_WORDS * 32),
    .MESSAGE_CONTROL         (0),
    .READ_DATA_WIDTH_A       (32),
    .READ_DATA_WIDTH_B       (32),
    .READ_LATENCY_A          (1),
    .READ_LATENCY_B          (1),
    .READ_RESET_VALUE_A      ("0"),
    .READ_RESET_VALUE_B      ("0"),
    .RST_MODE_A              ("SYNC"),
    .RST_MODE_B              ("SYNC"),
    .SIM_ASSERT_CHK          (0),
    .USE_EMBEDDED_CONSTRAINT (0),
    .USE_MEM_INIT            (0),
    .WAKEUP_TIME             ("disable_sleep"),
    .WRITE_DATA_WIDTH_A      (32),
    .WRITE_DATA_WIDTH_B      (32),
    .WRITE_MODE_A            ("write_first"),
    .WRITE_MODE_B            ("read_first")
  ) i_framebuf_ram (
    .addra          (mem_addra),
    .addrb          (mem_addrb),
    .clka           (wr_clk),
    .clkb           (bram_clk_b),
    .dina           (mem_dina),
    .dinb           (bram_din_b),
    .douta          (),
    .doutb          (bram_dout_b),
    .ena            (mem_wea),
    .enb            (bram_en_b),
    .injectdbiterra (1'b0),
    .injectdbiterrb (1'b0),
    .injectsbiterra (1'b0),
    .injectsbiterrb (1'b0),
    .regcea         (1'b1),
    .regceb         (1'b1),
    .rsta           (1'b0),
    .rstb           (bram_rst_b),
    .sleep          (1'b0),
    .wea            (mem_wea),
    .web            (bram_we_b),
    .dbiterra       (),
    .dbiterrb       (),
    .sbiterra       (),
    .sbiterrb       ()
  );

  always_ff @(posedge wr_clk) begin
    if (!wr_rst_n) begin
      write_buf_idx       <= 1'b0;
      even_pixel_latched  <= 16'd0;
      mem_addra           <= '0;
      mem_dina            <= 32'd0;
      mem_wea             <= 1'b0;
      frame_seq           <= 32'd0;
      frame_buf_idx       <= 1'b0;
    end else begin
      mem_wea <= 1'b0;

      if (pixel_we) begin
        if (!pixel_addr[0]) begin
          even_pixel_latched <= pack_pixel(pixel_data);
        end else if (pixel_addr < FRAME_PIXELS) begin
          mem_addra <= (write_buf_idx ? MEM_ADDR_W'(FRAME_WORDS) : MEM_ADDR_W'(0))
                     + {{(MEM_ADDR_W-15){1'b0}}, pixel_addr[15:1]};
          mem_dina  <= {pack_pixel(pixel_data), even_pixel_latched};
          mem_wea   <= 1'b1;

          if (pixel_addr == 16'd38399) begin
            frame_buf_idx <= write_buf_idx;
            frame_seq     <= frame_seq + 32'd1;
            write_buf_idx <= ~write_buf_idx;
          end
        end
      end
    end
  end

endmodule

module fb_largeimg_bridge (
  input  logic         clk,
  input  logic         rst_n,
  input  logic [1:0]   display_frame_idx,
  input  logic [25:0]  largeimg_addr,
  input  logic [63:0]  largeimg_data,
  input  logic         largeimg_req,
  output logic [27:1]  wr_addr,
  output logic [63:0]  wr_data,
  output logic         wr_req
);

  localparam logic [25:0] FB_REGION_BASE  = 26'h2000000;
  localparam logic [25:0] FB_FRAME_STRIDE = 26'h0080000;
  localparam logic [25:0] FB_LINE_STRIDE  = 26'd640;
  localparam logic [25:0] FB_X_OFFSET     = 26'd80;
  localparam logic [25:0] FB_Y_OFFSET     = 26'd51200;

  logic [1:0] active_frame_idx;
  logic [1:0] target_frame_idx;
  logic       frame_start;
  logic [10:0] src_row;
  logic [8:0]  src_x;
  logic [25:0] write_addr_core;

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

  assign frame_start = largeimg_req && (largeimg_addr[19:0] == 20'd0);
  assign src_row = largeimg_addr[19:9];
  assign src_x = largeimg_addr[8:0];
  assign target_frame_idx = frame_start ? backbuffer_frame_idx(display_frame_idx)
                                        : active_frame_idx;
  assign write_addr_core =
    FB_REGION_BASE
    + frame_offset(target_frame_idx)
    + FB_Y_OFFSET
    + ({15'd0, src_row} * FB_LINE_STRIDE)
    + FB_X_OFFSET
    + {17'd0, src_x};

  assign wr_addr = {write_addr_core, 1'b0};
  assign wr_data = largeimg_data;
  assign wr_req  = largeimg_req;

  always_ff @(posedge clk) begin
    if (!rst_n) begin
      active_frame_idx <= 2'd2;
    end else if (frame_start) begin
      active_frame_idx <= backbuffer_frame_idx(display_frame_idx);
    end
  end

endmodule

module fb_native_scale_writer (
  input  logic         clk,
  input  logic         rst_n,
  input  logic [1:0]   display_frame_idx,
  input  logic [15:0]  pixel_addr,
  input  logic [17:0]  pixel_data,
  input  logic         pixel_we,
  output logic [27:1]  wr_addr,
  output logic [63:0]  wr_data,
  output logic         wr_req,
  input  logic         wr_ack
);

  localparam logic [25:0] FB_REGION_BASE  = 26'h2000000;
  localparam logic [25:0] FB_FRAME_STRIDE = 26'h0080000;
  localparam logic [25:0] FB_LINE_STRIDE  = 26'd640;
  localparam logic [25:0] FB_FIELD_STRIDE = 26'd1280;
  localparam logic [25:0] FB_X_OFFSET     = 26'd80;
  localparam logic [25:0] FB_Y_OFFSET     = 26'd51200;
  localparam int unsigned LINE_PIXELS     = 240;
  localparam int unsigned NUM_LINE_BUFS   = 8;
  localparam int unsigned BUF_IDX_W       = $clog2(NUM_LINE_BUFS);
  localparam logic [BUF_IDX_W-1:0] LAST_BUF_IDX = NUM_LINE_BUFS - 1;
  localparam int unsigned LINEBUF_DEPTH   = NUM_LINE_BUFS * LINE_PIXELS;
  localparam int unsigned LINEBUF_ADDR_W  = $clog2(LINEBUF_DEPTH);

  typedef enum logic [1:0] {
    W_IDLE,
    W_FETCH,
    W_SEND,
    W_WAIT_ACK
  } wr_state_t;

  // Each completed source line expands into 480 single-beat DDR writes.
  // A 2-line ping-pong buffer is not enough once ch1 traffic and AXI response
  // latency are present, because capture can wrap back and overwrite the line
  // that the writer is still draining. Keep a small FIFO of completed lines so
  // the GPU's bursty line output is decoupled from the DDR write latency.
  logic [7:0]  queued_y     [0:NUM_LINE_BUFS-1];
  logic [1:0]  queued_frame [0:NUM_LINE_BUFS-1];
  logic [LINEBUF_ADDR_W-1:0] linebuf_wr_addr;
  logic [LINEBUF_ADDR_W-1:0] linebuf_rd_addr;
  logic [17:0] linebuf_rd_data;
  logic        frame_start;
  logic        capture_accept;
  logic        capture_seq_ok;

  wr_state_t wr_state;

  logic [BUF_IDX_W-1:0] capture_buf_idx;
  logic [7:0]  capture_x;
  logic [7:0]  capture_y;
  logic [1:0]  capture_frame;
  logic        capture_synced;
  logic [15:0] last_pixel_addr;

  logic [BUF_IDX_W-1:0] fifo_head_idx;
  logic [BUF_IDX_W:0]   queued_line_count;
  logic [BUF_IDX_W-1:0] active_buf_idx;
  logic [7:0]  active_y;
  logic [1:0]  active_frame;
  logic [7:0]  write_x;
  logic        write_stage;

  logic [17:0] write_pixel;
  logic [31:0] write_pixel_rgb;
  logic [25:0] write_addr_core;

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

  function automatic logic [31:0] expand_pixel(input logic [17:0] pixel);
    expand_pixel = {
      8'h00,
      pixel[5:0],   pixel[5:4],
      pixel[11:6],  pixel[11:10],
      pixel[17:12], pixel[17:16]
    };
  endfunction

  function automatic logic [BUF_IDX_W-1:0] next_buf_idx(input logic [BUF_IDX_W-1:0] idx);
    if (idx == LAST_BUF_IDX) begin
      next_buf_idx = '0;
    end else begin
      next_buf_idx = idx + {{BUF_IDX_W-1{1'b0}}, 1'b1};
    end
  endfunction

  function automatic logic [LINEBUF_ADDR_W-1:0] linebuf_base_addr(input logic [BUF_IDX_W-1:0] idx);
    case (idx)
      3'd0:    linebuf_base_addr = LINEBUF_ADDR_W'(0);
      3'd1:    linebuf_base_addr = LINEBUF_ADDR_W'(240);
      3'd2:    linebuf_base_addr = LINEBUF_ADDR_W'(480);
      3'd3:    linebuf_base_addr = LINEBUF_ADDR_W'(720);
      3'd4:    linebuf_base_addr = LINEBUF_ADDR_W'(960);
      3'd5:    linebuf_base_addr = LINEBUF_ADDR_W'(1200);
      3'd6:    linebuf_base_addr = LINEBUF_ADDR_W'(1440);
      default: linebuf_base_addr = LINEBUF_ADDR_W'(1680);
    endcase
  endfunction

  function automatic logic [LINEBUF_ADDR_W-1:0] linebuf_addr(
    input logic [BUF_IDX_W-1:0] buf_idx,
    input logic [7:0]           pixel_x
  );
    linebuf_addr = linebuf_base_addr(buf_idx) + {{(LINEBUF_ADDR_W-8){1'b0}}, pixel_x};
  endfunction

  xpm_memory_tdpram #(
    .ADDR_WIDTH_A            (LINEBUF_ADDR_W),
    .ADDR_WIDTH_B            (LINEBUF_ADDR_W),
    .AUTO_SLEEP_TIME         (0),
    .BYTE_WRITE_WIDTH_A      (18),
    .BYTE_WRITE_WIDTH_B      (18),
    .CASCADE_HEIGHT          (0),
    .CLOCKING_MODE           ("common_clock"),
    .ECC_MODE                ("no_ecc"),
    .MEMORY_INIT_FILE        ("none"),
    .MEMORY_INIT_PARAM       (""),
    .MEMORY_OPTIMIZATION     ("true"),
    .MEMORY_PRIMITIVE        ("block"),
    .MEMORY_SIZE             (LINEBUF_DEPTH * 18),
    .MESSAGE_CONTROL         (0),
    .READ_DATA_WIDTH_A       (18),
    .READ_DATA_WIDTH_B       (18),
    .READ_LATENCY_A          (1),
    .READ_LATENCY_B          (1),
    .READ_RESET_VALUE_A      ("0"),
    .READ_RESET_VALUE_B      ("0"),
    .RST_MODE_A              ("SYNC"),
    .RST_MODE_B              ("SYNC"),
    .SIM_ASSERT_CHK          (0),
    .USE_EMBEDDED_CONSTRAINT (0),
    .USE_MEM_INIT            (0),
    .WAKEUP_TIME             ("disable_sleep"),
    .WRITE_DATA_WIDTH_A      (18),
    .WRITE_DATA_WIDTH_B      (18),
    .WRITE_MODE_A            ("write_first"),
    .WRITE_MODE_B            ("read_first")
  ) i_linebuf_ram (
    .addra          (linebuf_wr_addr),
    .addrb          (linebuf_rd_addr),
    .clka           (clk),
    .clkb           (clk),
    .dina           (pixel_data),
    .dinb           (18'd0),
    .douta          (),
    .doutb          (linebuf_rd_data),
    .ena            (capture_accept),
    .enb            (1'b1),
    .injectdbiterra (1'b0),
    .injectdbiterrb (1'b0),
    .injectsbiterra (1'b0),
    .injectsbiterrb (1'b0),
    .regcea         (1'b1),
    .regceb         (1'b1),
    .rsta           (1'b0),
    .rstb           (1'b0),
    .sleep          (1'b0),
    .wea            (capture_accept),
    .web            (1'b0),
    .dbiterra       (),
    .dbiterrb       (),
    .sbiterra       (),
    .sbiterrb       ()
  );

  always_comb begin
    frame_start = pixel_we && (pixel_addr == 16'd0);
    capture_seq_ok = pixel_we && capture_synced && (pixel_addr == (last_pixel_addr + 16'd1));
    capture_accept = frame_start || capture_seq_ok;
    linebuf_wr_addr = frame_start ? linebuf_addr(capture_buf_idx, 8'd0)
                                  : linebuf_addr(capture_buf_idx, capture_x);
    write_pixel = linebuf_rd_data;
    write_pixel_rgb = expand_pixel(write_pixel);
    write_addr_core =
      FB_REGION_BASE
      + frame_offset(active_frame)
      + FB_Y_OFFSET
      + ({18'd0, active_y} * FB_FIELD_STRIDE)
      + (write_stage ? FB_LINE_STRIDE : 26'd0)
      + FB_X_OFFSET
      + {17'd0, write_x, 1'b0};

    wr_addr = {write_addr_core, 1'b0};
    wr_data = {write_pixel_rgb, write_pixel_rgb};
  end

  always_ff @(posedge clk) begin
    logic line_done;
    logic [7:0] line_y;
    logic [1:0] line_frame;
    logic [BUF_IDX_W-1:0] line_buf_idx;
    logic queue_push;
    logic queue_pop;
    logic push_allowed;
    logic [BUF_IDX_W:0] queue_count_after_pop;

    if (!rst_n) begin
      wr_state       <= W_IDLE;
      wr_req         <= 1'b0;
      capture_buf_idx <= '0;
      capture_x      <= 8'd0;
      capture_y      <= 8'd0;
      capture_frame  <= 2'd2;
      capture_synced <= 1'b0;
      last_pixel_addr <= 16'd0;
      fifo_head_idx  <= '0;
      queued_line_count <= '0;
      active_buf_idx <= '0;
      active_y       <= 8'd0;
      active_frame   <= 2'd2;
      write_x        <= 8'd0;
      write_stage    <= 1'b0;
      linebuf_rd_addr <= '0;
    end else begin
      wr_req         <= 1'b0;
      line_done      = 1'b0;
      line_y         = 8'd0;
      line_frame     = 2'd0;
      line_buf_idx   = capture_buf_idx;
      queue_push     = 1'b0;
      queue_pop      = 1'b0;
      queue_count_after_pop = queued_line_count;

      if (pixel_we) begin
        if (frame_start) begin
          capture_synced <= 1'b1;
          last_pixel_addr <= 16'd0;
          capture_x <= 8'd1;
          capture_y <= 8'd0;
          capture_frame <= backbuffer_frame_idx(display_frame_idx);
        end else if (!capture_synced || !capture_seq_ok) begin
          capture_synced <= 1'b0;
          capture_x <= 8'd0;
          capture_y <= 8'd0;
        end else if (capture_x == 8'd239) begin
          last_pixel_addr <= pixel_addr;
          line_done  = 1'b1;
          line_y     = capture_y;
          line_frame = capture_frame;
          line_buf_idx = capture_buf_idx;
          queue_push = 1'b1;

          capture_x <= 8'd0;
          if (capture_y == 8'd159) begin
            capture_y     <= 8'd0;
            capture_frame <= backbuffer_frame_idx(display_frame_idx);
          end else begin
            capture_y <= capture_y + 8'd1;
          end
        end else begin
          last_pixel_addr <= pixel_addr;
          capture_x <= capture_x + 8'd1;
        end
      end

      case (wr_state)
        W_IDLE: begin
          if (queued_line_count != 0) begin
            active_buf_idx <= fifo_head_idx;
            active_y       <= queued_y[fifo_head_idx];
            active_frame   <= queued_frame[fifo_head_idx];
            write_x      <= 8'd0;
            write_stage  <= 1'b0;
            linebuf_rd_addr <= linebuf_addr(fifo_head_idx, 8'd0);
            wr_state     <= W_FETCH;
          end
        end

        W_FETCH: begin
          wr_state <= W_SEND;
        end

        W_SEND: begin
          wr_req   <= 1'b1;
          wr_state <= W_WAIT_ACK;
        end

        W_WAIT_ACK: begin
          if (wr_ack) begin
            if (!write_stage) begin
              write_stage <= 1'b1;
              wr_state    <= W_SEND;
            end else if (write_x == 8'd239) begin
              queue_pop = 1'b1;
              wr_state  <= W_IDLE;
            end else begin
              write_x        <= write_x + 8'd1;
              write_stage    <= 1'b0;
              linebuf_rd_addr <= linebuf_addr(active_buf_idx, write_x + 8'd1);
              wr_state       <= W_FETCH;
            end
          end
        end

        default: begin
          wr_state <= W_IDLE;
        end
      endcase

      if (queue_pop) begin
        fifo_head_idx <= next_buf_idx(fifo_head_idx);
        queue_count_after_pop = queued_line_count - {{BUF_IDX_W{1'b0}}, 1'b1};
      end

      push_allowed = (queue_count_after_pop < (NUM_LINE_BUFS - 1));
      if (queue_push && push_allowed) begin
        queued_y[line_buf_idx]     <= line_y;
        queued_frame[line_buf_idx] <= line_frame;
        capture_buf_idx            <= next_buf_idx(capture_buf_idx);

        if (!queue_pop && (queued_line_count == 0)) begin
          fifo_head_idx <= line_buf_idx;
        end

        queued_line_count <= queue_count_after_pop + {{BUF_IDX_W{1'b0}}, 1'b1};
      end else begin
        queued_line_count <= queue_count_after_pop;
      end
    end
  end

endmodule
