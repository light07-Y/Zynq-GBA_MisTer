module audio_validation_tone (
    input               clk_100,
    input               rst_n,
    input               enable,
    input               sample_ce,
    output      [15:0]  audio_l,
    output      [15:0]  audio_r,
    output              active
);

    `include "audio_validation_tone_meta.vh"

    localparam integer OUTPUT_SAMPLE_RATE = 48000;
    localparam [15:0] SAMPLE_COUNT = `AUDIO_VALIDATION_SAMPLE_COUNT;
    localparam [15:0] SAMPLE_RATE  = `AUDIO_VALIDATION_SAMPLE_RATE;

    (* rom_style = "block" *) reg [7:0] sample_rom [0:SAMPLE_COUNT-1];

    reg        enable_d = 1'b0;
    reg        playing = 1'b0;
    reg [15:0] sample_idx = 16'd0;
    reg [15:0] phase_accum = 16'd0;
    reg [15:0] rom_addr = 16'd0;
    reg        rom_fetch_pending = 1'b0;
    reg signed [7:0] rom_q = 8'sd0;
    reg signed [15:0] current_sample = 16'sd0;

    initial begin
        $readmemh("audio_validation_tone.mem", sample_rom);
    end

    always @(posedge clk_100) begin
        rom_q <= sample_rom[rom_addr];

        if (!rst_n) begin
            enable_d          <= 1'b0;
            playing           <= 1'b0;
            sample_idx        <= 16'd0;
            phase_accum       <= 16'd0;
            rom_addr          <= 16'd0;
            rom_fetch_pending <= 1'b0;
            rom_q             <= 8'sd0;
            current_sample    <= 16'sd0;
        end else if (!enable) begin
            enable_d          <= 1'b0;
            playing           <= 1'b0;
            sample_idx        <= 16'd0;
            phase_accum       <= 16'd0;
            rom_addr          <= 16'd0;
            rom_fetch_pending <= 1'b0;
            current_sample    <= 16'sd0;
        end else begin
            enable_d <= enable;

            if (rom_fetch_pending) begin
                current_sample    <= {rom_q, 8'd0};
                rom_fetch_pending <= 1'b0;
            end

            if (!enable_d) begin
                playing           <= 1'b1;
                sample_idx        <= 16'd0;
                phase_accum       <= 16'd0;
                rom_addr          <= 16'd0;
                rom_fetch_pending <= 1'b1;
                current_sample    <= 16'sd0;
            end else if (playing && sample_ce) begin
                if ((phase_accum + SAMPLE_RATE) >= OUTPUT_SAMPLE_RATE) begin
                    phase_accum <= phase_accum + SAMPLE_RATE - OUTPUT_SAMPLE_RATE;

                    if ((sample_idx + 16'd1) < SAMPLE_COUNT) begin
                        sample_idx        <= sample_idx + 16'd1;
                        rom_addr          <= sample_idx + 16'd1;
                        rom_fetch_pending <= 1'b1;
                    end else begin
                        playing           <= 1'b0;
                        sample_idx        <= 16'd0;
                        phase_accum       <= 16'd0;
                        rom_addr          <= 16'd0;
                        rom_fetch_pending <= 1'b0;
                        current_sample    <= 16'sd0;
                    end
                end else begin
                    phase_accum <= phase_accum + SAMPLE_RATE;
                end
            end
        end
    end

    assign active = playing;
    assign audio_l = playing ? current_sample[15:0] : 16'd0;
    assign audio_r = playing ? current_sample[15:0] : 16'd0;

endmodule
