module frame_tick_480p (
  input  logic      clk,
  input  logic      rst_n,

  output logic      frame_pulse,
  output logic [1:0] frame_idx
);

  localparam int H_TOTAL  = 800;
  localparam int H_ACTIVE = 640;
  localparam int V_TOTAL  = 525;
  localparam int V_ACTIVE = 480;

  logic [1:0] pix_div;
  logic       pix_ce;

  logic [9:0] h_cnt;
  logic [9:0] v_cnt;

  always_ff @(posedge clk) begin
    if (!rst_n) begin
      pix_div     <= 2'd0;
      pix_ce      <= 1'b0;
      h_cnt       <= 10'd0;
      v_cnt       <= 10'd0;
      frame_pulse <= 1'b0;
      frame_idx   <= 2'd0;
    end else begin
      frame_pulse <= 1'b0;

      // Generate a 25 MHz timing tick from the 100 MHz fabric clock.
      pix_div <= pix_div + 2'd1;
      pix_ce  <= (pix_div == 2'd3);

      if (pix_ce) begin
        if (h_cnt == H_TOTAL - 1) begin
          h_cnt <= 10'd0;
          if (v_cnt == V_TOTAL - 1) begin
            v_cnt       <= 10'd0;
            frame_pulse <= 1'b1;
            frame_idx   <= frame_idx + 2'd1;
          end else begin
            v_cnt <= v_cnt + 10'd1;
          end
        end else begin
          h_cnt <= h_cnt + 10'd1;
        end
      end
    end
  end

endmodule
