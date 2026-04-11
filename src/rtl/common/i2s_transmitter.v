// i2s_transmitter.v: 48 kHz / 16-bit I2S transmitter for the Zybo SSM2603.
// We cannot derive 12.288 MHz from 100 MHz with a plain integer divider, so
// the clocks are generated from a common fractional-N accumulator. This keeps
// MCLK/BCLK/LRCLK in the correct 256fs/32fs/fs relationship and removes the
// audible pitch/speed error caused by the old 12.5 MHz / 48.8 kHz approximation.

module i2s_transmitter (
    input         clk_100,
    input         rst_n,
    input  [15:0] audio_l,
    input  [15:0] audio_r,

    output        ac_mclk,
    output        ac_bclk,
    output        ac_pblrc,
    output        ac_pbdat,
    output        ac_muten,
    output        sample_ce
);

    localparam integer CLK_RATE_HZ            = 100000000;
    localparam integer AUDIO_RATE_HZ          = 48000;
    localparam integer AUDIO_DW               = 16;
    localparam integer MCLK_EDGE_RATE_HZ      = AUDIO_RATE_HZ * 512; // 24.576 MHz edge rate for 12.288 MHz square wave
    localparam integer MCLK_EDGES_PER_BCLK    = 8;                  // 24.576 MHz / 8 -> 3.072 MHz BCLK edge rate

    reg [31:0] mclk_accum = 32'd0;
    reg        mclk_edge_ce = 1'b0;
    reg [2:0]  mclk_to_bclk_div = 3'd0;
    reg        i2s_ce = 1'b0;

    reg        mclk_reg = 1'b0;
    reg        sclk_reg = 1'b1;
    reg        lrclk_reg = 1'b1;
    reg        sdata_reg = 1'b0;
    reg        msclk = 1'b1;
    reg        sample_ce_reg = 1'b0;
    reg [7:0]  bit_cnt = 8'd1;
    reg [15:0] left_latched = 16'd0;
    reg [15:0] right_latched = 16'd0;

    always @(posedge clk_100) begin
        if (!rst_n) begin
            mclk_accum       <= 32'd0;
            mclk_edge_ce     <= 1'b0;
            mclk_to_bclk_div <= 3'd0;
            i2s_ce           <= 1'b0;
            mclk_reg         <= 1'b0;
            sclk_reg         <= 1'b1;
            lrclk_reg        <= 1'b1;
            sdata_reg        <= 1'b0;
            msclk            <= 1'b1;
            sample_ce_reg    <= 1'b0;
            bit_cnt          <= 8'd1;
            left_latched     <= 16'd0;
            right_latched    <= 16'd0;
        end else begin
            mclk_edge_ce <= 1'b0;
            i2s_ce       <= 1'b0;
            sample_ce_reg <= 1'b0;

            if ((mclk_accum + MCLK_EDGE_RATE_HZ) >= CLK_RATE_HZ) begin
                mclk_accum   <= mclk_accum + MCLK_EDGE_RATE_HZ - CLK_RATE_HZ;
                mclk_edge_ce <= 1'b1;
            end else begin
                mclk_accum <= mclk_accum + MCLK_EDGE_RATE_HZ;
            end

            if (mclk_edge_ce) begin
                mclk_reg <= ~mclk_reg;

                if (mclk_to_bclk_div == (MCLK_EDGES_PER_BCLK - 1)) begin
                    mclk_to_bclk_div <= 3'd0;
                    i2s_ce           <= 1'b1;
                end else begin
                    mclk_to_bclk_div <= mclk_to_bclk_div + 3'd1;
                end
            end

            // Reuse the proven upstream I2S serializer sequencing so channel
            // switching and MSB alignment stay compatible with 16-bit I2S mode.
            if (i2s_ce) begin
                sclk_reg <= msclk;
                msclk    <= ~msclk;

                if (msclk) begin
                    if (bit_cnt >= AUDIO_DW) begin
                        bit_cnt   <= 8'd1;
                        lrclk_reg <= ~lrclk_reg;

                        if (lrclk_reg) begin
                            left_latched  <= audio_l;
                            right_latched <= audio_r;
                            sample_ce_reg <= 1'b1;
                        end
                    end else begin
                        bit_cnt <= bit_cnt + 8'd1;
                    end

                    sdata_reg <= lrclk_reg ? right_latched[AUDIO_DW - bit_cnt] : left_latched[AUDIO_DW - bit_cnt];
                end
            end
        end
    end

    assign ac_mclk  = mclk_reg;
    assign ac_bclk  = sclk_reg;
    assign ac_pblrc = lrclk_reg;
    assign ac_pbdat = sdata_reg;
    assign ac_muten = 1'b1;
    assign sample_ce = sample_ce_reg;

endmodule
