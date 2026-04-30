// ddram_mux.sv: Zynq DDR multiplexer with short-burst read-ahead.
// Keeps the MiSTer RAM contract while using AXI-friendly sequential bursts.

module ddram_mux (
    input  logic         DDRAM_CLK,
    input  logic         DDRAM_BUSY,
    output logic [7:0]   DDRAM_BURSTCNT,
    output logic [28:0]  DDRAM_ADDR,
    input  logic [63:0]  DDRAM_DOUT,
    input  logic         DDRAM_DOUT_READY,
    output logic         DDRAM_RD,
    output logic [63:0]  DDRAM_DIN,
    output logic [7:0]   DDRAM_BE,
    output logic         DDRAM_WE,
    input  logic         CFG_ROM_DDR_SAFE,

    // Channel 1: Core Instruction/Data
    input  logic [27:1]  ch1_addr,
    output logic [63:0]  ch1_dout,
    input  logic [15:0]  ch1_din,
    input  logic         ch1_req,
    input  logic         ch1_rnw,
    output logic         ch1_ready,

    // Channel 2: Generic Bus
    input  logic [27:1]  ch2_addr,
    output logic [31:0]  ch2_dout,
    input  logic [31:0]  ch2_din,
    input  logic         ch2_req,
    input  logic         ch2_rnw,
    output logic         ch2_ready,

    // Channel 3: Unused/Reserved
    input  logic [25:1]  ch3_addr,
    output logic [15:0]  ch3_dout,
    input  logic [15:0]  ch3_din,
    input  logic         ch3_req,
    input  logic         ch3_rnw,
    output logic         ch3_ready,

    // Channel 4: Save State
    input  logic [27:1]  ch4_addr,
    output logic [63:0]  ch4_dout,
    input  logic [63:0]  ch4_din,
    input  logic         ch4_req,
    input  logic         ch4_rnw,
    input  logic [7:0]   ch4_be,
    output logic         ch4_ready,

    // Channel 5: Framebuffer
    input  logic [27:1]  ch5_addr,
    output logic [63:0]  ch5_dout,
    input  logic [63:0]  ch5_din,
    input  logic         ch5_req,
    input  logic         ch5_rnw,
    output logic         ch5_ready
);

    localparam logic [1:0] S_IDLE      = 2'd0;
    localparam logic [1:0] S_WAIT_READ = 2'd1;
    localparam logic [7:0] C_READ_BURST_MIN = 8'd1;
    localparam logic [7:0] C_READ_BURST_MID = 8'd2;
    localparam logic [7:0] C_READ_BURST_MAX = 8'd4;
    localparam logic [19:0] C_SDRAM_READ_TIMEOUT = 20'd250000;

    logic [7:0]  ram_burst = 8'd1;
    logic [63:0] ram_q[5:1];
    logic [27:1] resp_addr[5:1];
    logic [63:0] ram_data;
    logic [27:1] ram_address;
    logic        ram_read = 1'b0;
    logic        ram_write = 1'b0;
    logic [7:0]  ram_be;
    logic [5:1]  ready = '0;

    logic [27:1] cache_addr[2:1];
    logic [2:1]  cached = 2'd0;
    logic [27:1] seq_last_addr[2:1];
    logic [2:1]  seq_last_valid = 2'd0;
    logic [1:0]  seq_depth[2:1];
    logic        rom_ddr_safe_prev = 1'b0;

    // Three prefetched 64-bit lines per latency-sensitive channel.
    logic [63:0] pf_q[2:1][2:0];
    logic [27:1] pf_addr[2:1][2:0];
    logic        pf_valid[2:1][2:0];

    logic [1:0]  state = S_IDLE;
    logic [19:0] read_watch_ctr = 20'd0;
    logic [2:0]  active_ch = 3'd0;
    logic [27:1] active_addr;
    logic [7:0]  active_burst = 8'd1;
    logic [7:0]  read_beat = 8'd0;

    logic [5:1]  ch_rq = '0;
    logic [27:1] rq_addr[5:1];
    logic [63:0] rq_din[5:1];
    logic [7:0]  rq_be[5:1];
    logic [5:1]  rq_rnw;

    logic [2:0]  rr_last_grant = 3'd5;
    logic [5:1]  pending_req;
    logic [2:0]  grant_ch;
    logic        grant_valid;

    logic [27:1] cur_addr[5:1];
    logic [63:0] cur_din[5:1];
    logic [7:0]  cur_be[5:1];
    logic [5:1]  cur_rnw;

    assign DDRAM_BURSTCNT = ram_burst;
    assign DDRAM_BE       = ram_read ? 8'hFF : ram_be;
    assign DDRAM_ADDR     = {4'b0011, ram_address[27:3]};
    assign DDRAM_RD       = ram_read;
    assign DDRAM_DIN      = ram_data;
    assign DDRAM_WE       = ram_write;

    // Return data must be decoded with the completed request address, not the live bus address.
    assign ch1_dout  = resp_addr[1][2] ? {ram_q[1][31:0], ram_q[1][63:32]} : ram_q[1];
    assign ch2_dout  = resp_addr[2][2] ? ram_q[2][63:32] : ram_q[2][31:0];
    assign ch3_dout  = {ram_q[3][39:32], ram_q[3][7:0]};
    assign ch4_dout  = ram_q[4];
    assign ch5_dout  = ram_q[5];

    assign ch1_ready = ready[1];
    assign ch2_ready = ready[2];
    assign ch3_ready = ready[3];
    assign ch4_ready = ready[4];
    assign ch5_ready = ready[5];

    assign cur_addr[1] = ch_rq[1] ? rq_addr[1] : ch1_addr;
    assign cur_addr[2] = ch_rq[2] ? rq_addr[2] : ch2_addr;
    assign cur_addr[3] = ch_rq[3] ? rq_addr[3] : {ch3_addr, 2'b00};
    assign cur_addr[4] = ch_rq[4] ? rq_addr[4] : ch4_addr;
    assign cur_addr[5] = ch_rq[5] ? rq_addr[5] : ch5_addr;

    assign cur_din[1] = ch_rq[1] ? rq_din[1] : {4{ch1_din}};
    assign cur_din[2] = ch_rq[2] ? rq_din[2] : {2{ch2_din}};
    assign cur_din[3] = ch_rq[3] ? rq_din[3] : {24'd0, ch3_din[15:8], 24'd0, ch3_din[7:0]};
    assign cur_din[4] = ch_rq[4] ? rq_din[4] : ch4_din;
    assign cur_din[5] = ch_rq[5] ? rq_din[5] : ch5_din;

    assign cur_be[1]  = ch_rq[1] ? rq_be[1] : (8'h03 << {ch1_addr[2:1], 1'b0});
    assign cur_be[2]  = ch_rq[2] ? rq_be[2] : (ch2_addr[2] ? 8'hF0 : 8'h0F);
    assign cur_be[3]  = ch_rq[3] ? rq_be[3] : 8'hFF;
    assign cur_be[4]  = ch_rq[4] ? rq_be[4] : ch4_be;
    assign cur_be[5]  = ch_rq[5] ? rq_be[5] : 8'hFF;

    assign cur_rnw[1] = ch_rq[1] ? rq_rnw[1] : ch1_rnw;
    assign cur_rnw[2] = ch_rq[2] ? rq_rnw[2] : ch2_rnw;
    assign cur_rnw[3] = ch_rq[3] ? rq_rnw[3] : ch3_rnw;
    assign cur_rnw[4] = ch_rq[4] ? rq_rnw[4] : ch4_rnw;
    assign cur_rnw[5] = ch_rq[5] ? rq_rnw[5] : ch5_rnw;

    always_comb begin
        pending_req[1] = ch_rq[1] | ch1_req;
        pending_req[2] = ch_rq[2] | ch2_req;
        pending_req[3] = ch_rq[3] | ch3_req;
        pending_req[4] = ch_rq[4] | ch4_req;
        pending_req[5] = ch_rq[5] | ch5_req;

        grant_ch    = 3'd0;
        grant_valid = 1'b0;

        unique case (rr_last_grant)
            3'd1: begin
                if (pending_req[2]) begin grant_ch = 3'd2; grant_valid = 1'b1; end
                else if (pending_req[3]) begin grant_ch = 3'd3; grant_valid = 1'b1; end
                else if (pending_req[4]) begin grant_ch = 3'd4; grant_valid = 1'b1; end
                else if (pending_req[5]) begin grant_ch = 3'd5; grant_valid = 1'b1; end
                else if (pending_req[1]) begin grant_ch = 3'd1; grant_valid = 1'b1; end
            end
            3'd2: begin
                if (pending_req[3]) begin grant_ch = 3'd3; grant_valid = 1'b1; end
                else if (pending_req[4]) begin grant_ch = 3'd4; grant_valid = 1'b1; end
                else if (pending_req[5]) begin grant_ch = 3'd5; grant_valid = 1'b1; end
                else if (pending_req[1]) begin grant_ch = 3'd1; grant_valid = 1'b1; end
                else if (pending_req[2]) begin grant_ch = 3'd2; grant_valid = 1'b1; end
            end
            3'd3: begin
                if (pending_req[4]) begin grant_ch = 3'd4; grant_valid = 1'b1; end
                else if (pending_req[5]) begin grant_ch = 3'd5; grant_valid = 1'b1; end
                else if (pending_req[1]) begin grant_ch = 3'd1; grant_valid = 1'b1; end
                else if (pending_req[2]) begin grant_ch = 3'd2; grant_valid = 1'b1; end
                else if (pending_req[3]) begin grant_ch = 3'd3; grant_valid = 1'b1; end
            end
            3'd4: begin
                if (pending_req[5]) begin grant_ch = 3'd5; grant_valid = 1'b1; end
                else if (pending_req[1]) begin grant_ch = 3'd1; grant_valid = 1'b1; end
                else if (pending_req[2]) begin grant_ch = 3'd2; grant_valid = 1'b1; end
                else if (pending_req[3]) begin grant_ch = 3'd3; grant_valid = 1'b1; end
                else if (pending_req[4]) begin grant_ch = 3'd4; grant_valid = 1'b1; end
            end
            default: begin
                if (pending_req[1]) begin grant_ch = 3'd1; grant_valid = 1'b1; end
                else if (pending_req[2]) begin grant_ch = 3'd2; grant_valid = 1'b1; end
                else if (pending_req[3]) begin grant_ch = 3'd3; grant_valid = 1'b1; end
                else if (pending_req[4]) begin grant_ch = 3'd4; grant_valid = 1'b1; end
                else if (pending_req[5]) begin grant_ch = 3'd5; grant_valid = 1'b1; end
            end
        endcase
    end

    function automatic logic pf_match(
        input logic [27:1] a,
        input logic        valid,
        input logic [27:1] pfa
    );
    begin
        pf_match = valid && (pfa[27:3] == a[27:3]);
    end
    endfunction

    function automatic logic [7:0] select_burst(
        input logic       safe_mode,
        input logic       next_line,
        input logic [1:0] depth
    );
    begin
        if (safe_mode) begin
            select_burst = C_READ_BURST_MIN;
        end else if (!next_line) begin
            select_burst = C_READ_BURST_MIN;
        end else if (depth == 2'd0) begin
            select_burst = C_READ_BURST_MID;
        end else begin
            select_burst = C_READ_BURST_MAX;
        end
    end
    endfunction

    function automatic logic [7:0] clamp_burst_4k(
        input logic [27:1] addr,
        input logic [7:0]  burst
    );
        logic [9:0] addr_line;
        logic [9:0] last_line;
        logic [9:0] lines_fit;
    begin
        addr_line = {1'b0, addr[11:3]};
        last_line = addr_line + {2'b00, burst} - 10'd1;
        if (last_line <= 10'd511) begin
            clamp_burst_4k = burst;
        end else begin
            lines_fit = 10'd512 - addr_line;
            if (lines_fit == 10'd0) begin
                clamp_burst_4k = 8'd1;
            end else begin
                clamp_burst_4k = lines_fit[7:0];
            end
        end
        if (clamp_burst_4k == 8'd0) begin
            clamp_burst_4k = 8'd1;
        end
    end
    endfunction

    task automatic invalidate_prefetch(input int unsigned channel);
    begin
        pf_valid[channel][0] <= 1'b0;
        pf_valid[channel][1] <= 1'b0;
        pf_valid[channel][2] <= 1'b0;
    end
    endtask

    task automatic start_read(
        input logic [2:0]  channel,
        input logic [27:1] addr,
        input logic [7:0]  burst
    );
    begin
        read_watch_ctr <= 20'd0;
        active_ch    <= channel;
        active_addr  <= addr;
        active_burst <= burst;
        read_beat    <= 8'd0;
        ram_address  <= addr;
        ram_burst    <= burst;
        ram_read     <= 1'b1;
        state        <= S_WAIT_READ;
    end
    endtask

    always_ff @(posedge DDRAM_CLK) begin
        ready     <= 5'd0;
        ram_read  <= 1'b0;
        ram_write <= 1'b0;

        if (ch1_req && !ch_rq[1]) begin
            rq_addr[1] <= ch1_addr;
            rq_din[1]  <= {4{ch1_din}};
            rq_be[1]   <= 8'h03 << {ch1_addr[2:1], 1'b0};
            rq_rnw[1]  <= ch1_rnw;
        end
        if (ch2_req && !ch_rq[2]) begin
            rq_addr[2] <= ch2_addr;
            rq_din[2]  <= {2{ch2_din}};
            rq_be[2]   <= ch2_addr[2] ? 8'hF0 : 8'h0F;
            rq_rnw[2]  <= ch2_rnw;
        end
        if (ch3_req && !ch_rq[3]) begin
            rq_addr[3] <= {ch3_addr, 2'b00};
            rq_din[3]  <= {24'd0, ch3_din[15:8], 24'd0, ch3_din[7:0]};
            rq_be[3]   <= 8'hFF;
            rq_rnw[3]  <= ch3_rnw;
        end
        if (ch4_req && !ch_rq[4]) begin
            rq_addr[4] <= ch4_addr;
            rq_din[4]  <= ch4_din;
            rq_be[4]   <= ch4_be;
            rq_rnw[4]  <= ch4_rnw;
        end
        if (ch5_req && !ch_rq[5]) begin
            rq_addr[5] <= ch5_addr;
            rq_din[5]  <= ch5_din;
            rq_be[5]   <= 8'hFF;
            rq_rnw[5]  <= ch5_rnw;
        end
        ch_rq <= ch_rq | {ch5_req, ch4_req, ch3_req, ch2_req, ch1_req};

        if (CFG_ROM_DDR_SAFE != rom_ddr_safe_prev) begin
            rom_ddr_safe_prev <= CFG_ROM_DDR_SAFE;
            cached[1]         <= 1'b0;
            cached[2]         <= 1'b0;
            seq_last_valid[1] <= 1'b0;
            seq_last_valid[2] <= 1'b0;
            seq_depth[1]      <= 2'd0;
            seq_depth[2]      <= 2'd0;
            pf_valid[1][0]    <= 1'b0;
            pf_valid[1][1]    <= 1'b0;
            pf_valid[1][2]    <= 1'b0;
            pf_valid[2][0]    <= 1'b0;
            pf_valid[2][1]    <= 1'b0;
            pf_valid[2][2]    <= 1'b0;
        end

        if (state == S_WAIT_READ) begin
            if (DDRAM_DOUT_READY) begin
                if (active_ch <= 3'd2) begin
                    if (read_beat == 8'd0) begin
                        ram_q[active_ch]        <= DDRAM_DOUT;
                        resp_addr[active_ch]    <= active_addr;
                        cache_addr[active_ch]   <= active_addr;
                        cached[active_ch]       <= 1'b1;
                        ready[active_ch]        <= 1'b1;
                    end else if (read_beat <= 8'd3) begin
                        pf_q[active_ch][read_beat - 8'd1] <= DDRAM_DOUT;
                        // Tag prefetch by cache-line index, not raw addr + offset.
                        // Matching uses [27:3], so this avoids lane-dependent aliasing.
                        pf_addr[active_ch][read_beat - 8'd1] <= {
                            active_addr[27:3] + {{17{1'b0}}, read_beat},
                            2'b00
                        };
                        pf_valid[active_ch][read_beat - 8'd1] <= 1'b1;
                    end
                end else begin
                    ram_q[active_ch] <= DDRAM_DOUT;
                    resp_addr[active_ch] <= active_addr;
                    ready[active_ch] <= 1'b1;
                end

                read_watch_ctr <= 20'd0;

                if (read_beat >= (active_burst - 8'd1)) begin
                    state <= S_IDLE;
                end else begin
                    read_beat <= read_beat + 8'd1;
                end
            end else begin
                read_watch_ctr <= read_watch_ctr + 20'd1;
                if (read_watch_ctr >= C_SDRAM_READ_TIMEOUT) begin
                    state <= S_IDLE;
                end
            end
        end else if (!DDRAM_BUSY && grant_valid) begin
            rr_last_grant <= grant_ch;
            ch_rq[grant_ch] <= 1'b0;

            unique case (grant_ch)
                3'd1: begin
                    ram_data <= cur_din[1];
                    ram_be   <= cur_be[1];
                    if (!cur_rnw[1]) begin
                        ram_address <= cur_addr[1];
                        ram_burst   <= 8'd1;
                        ram_write   <= 1'b1;
                        cached[1]   <= 1'b0;
                        seq_last_valid[1] <= 1'b0;
                        seq_depth[1] <= 2'd0;
                        invalidate_prefetch(1);
                        ready[1]    <= 1'b1;
                    end else if (cached[1] && (cache_addr[1][27:3] == cur_addr[1][27:3])) begin
                        resp_addr[1] <= cur_addr[1];
                        seq_last_addr[1] <= cur_addr[1];
                        seq_last_valid[1] <= 1'b1;
                        ready[1] <= 1'b1;
                    end else if (!CFG_ROM_DDR_SAFE && pf_match(cur_addr[1], pf_valid[1][0], pf_addr[1][0])) begin
                        ram_q[1]       <= pf_q[1][0];
                        resp_addr[1]   <= cur_addr[1];
                        cache_addr[1]  <= cur_addr[1];
                        cached[1]      <= 1'b1;
                        pf_valid[1][0] <= 1'b0;
                        seq_last_addr[1] <= cur_addr[1];
                        seq_last_valid[1] <= 1'b1;
                        ready[1]       <= 1'b1;
                    end else if (!CFG_ROM_DDR_SAFE && pf_match(cur_addr[1], pf_valid[1][1], pf_addr[1][1])) begin
                        ram_q[1]       <= pf_q[1][1];
                        resp_addr[1]   <= cur_addr[1];
                        cache_addr[1]  <= cur_addr[1];
                        cached[1]      <= 1'b1;
                        pf_valid[1][1] <= 1'b0;
                        seq_last_addr[1] <= cur_addr[1];
                        seq_last_valid[1] <= 1'b1;
                        ready[1]       <= 1'b1;
                    end else if (!CFG_ROM_DDR_SAFE && pf_match(cur_addr[1], pf_valid[1][2], pf_addr[1][2])) begin
                        ram_q[1]       <= pf_q[1][2];
                        resp_addr[1]   <= cur_addr[1];
                        cache_addr[1]  <= cur_addr[1];
                        cached[1]      <= 1'b1;
                        pf_valid[1][2] <= 1'b0;
                        seq_last_addr[1] <= cur_addr[1];
                        seq_last_valid[1] <= 1'b1;
                        ready[1]       <= 1'b1;
                    end else begin
                        logic next_line;
                        next_line = seq_last_valid[1] &&
                                    ((seq_last_addr[1][27:3] + 25'd1) == cur_addr[1][27:3]);
                        cached[1] <= 1'b0;
                        seq_last_addr[1] <= cur_addr[1];
                        seq_last_valid[1] <= 1'b1;
                        if (next_line) begin
                            if (seq_depth[1] != 2'd3) begin
                                seq_depth[1] <= seq_depth[1] + 2'd1;
                            end
                        end else begin
                            seq_depth[1] <= 2'd0;
                        end
                        invalidate_prefetch(1);
                        start_read(3'd1,
                                   cur_addr[1],
                                   clamp_burst_4k(cur_addr[1],
                                                  select_burst(CFG_ROM_DDR_SAFE, next_line, seq_depth[1])));
                    end
                end

                3'd2: begin
                    ram_data <= cur_din[2];
                    ram_be   <= cur_be[2];
                    if (!cur_rnw[2]) begin
                        ram_address <= cur_addr[2];
                        ram_burst   <= 8'd1;
                        ram_write   <= 1'b1;
                        cached[2]   <= 1'b0;
                        seq_last_valid[2] <= 1'b0;
                        seq_depth[2] <= 2'd0;
                        invalidate_prefetch(2);
                        ready[2]    <= 1'b1;
                    end else if (cached[2] && (cache_addr[2][27:3] == cur_addr[2][27:3])) begin
                        resp_addr[2] <= cur_addr[2];
                        seq_last_addr[2] <= cur_addr[2];
                        seq_last_valid[2] <= 1'b1;
                        ready[2] <= 1'b1;
                    end else if (!CFG_ROM_DDR_SAFE && pf_match(cur_addr[2], pf_valid[2][0], pf_addr[2][0])) begin
                        ram_q[2]       <= pf_q[2][0];
                        resp_addr[2]   <= cur_addr[2];
                        cache_addr[2]  <= cur_addr[2];
                        cached[2]      <= 1'b1;
                        pf_valid[2][0] <= 1'b0;
                        seq_last_addr[2] <= cur_addr[2];
                        seq_last_valid[2] <= 1'b1;
                        ready[2]       <= 1'b1;
                    end else if (!CFG_ROM_DDR_SAFE && pf_match(cur_addr[2], pf_valid[2][1], pf_addr[2][1])) begin
                        ram_q[2]       <= pf_q[2][1];
                        resp_addr[2]   <= cur_addr[2];
                        cache_addr[2]  <= cur_addr[2];
                        cached[2]      <= 1'b1;
                        pf_valid[2][1] <= 1'b0;
                        seq_last_addr[2] <= cur_addr[2];
                        seq_last_valid[2] <= 1'b1;
                        ready[2]       <= 1'b1;
                    end else if (!CFG_ROM_DDR_SAFE && pf_match(cur_addr[2], pf_valid[2][2], pf_addr[2][2])) begin
                        ram_q[2]       <= pf_q[2][2];
                        resp_addr[2]   <= cur_addr[2];
                        cache_addr[2]  <= cur_addr[2];
                        cached[2]      <= 1'b1;
                        pf_valid[2][2] <= 1'b0;
                        seq_last_addr[2] <= cur_addr[2];
                        seq_last_valid[2] <= 1'b1;
                        ready[2]       <= 1'b1;
                    end else begin
                        logic next_line;
                        next_line = seq_last_valid[2] &&
                                    ((seq_last_addr[2][27:3] + 25'd1) == cur_addr[2][27:3]);
                        cached[2] <= 1'b0;
                        seq_last_addr[2] <= cur_addr[2];
                        seq_last_valid[2] <= 1'b1;
                        if (next_line) begin
                            if (seq_depth[2] != 2'd3) begin
                                seq_depth[2] <= seq_depth[2] + 2'd1;
                            end
                        end else begin
                            seq_depth[2] <= 2'd0;
                        end
                        invalidate_prefetch(2);
                        start_read(3'd2,
                                   cur_addr[2],
                                   clamp_burst_4k(cur_addr[2],
                                                  select_burst(CFG_ROM_DDR_SAFE, next_line, seq_depth[2])));
                    end
                end

                3'd3: begin
                    ram_data    <= cur_din[3];
                    ram_be      <= cur_be[3];
                    ram_address <= cur_addr[3];
                    ram_burst   <= 8'd1;
                    if (!cur_rnw[3]) begin
                        ram_write <= 1'b1;
                        cached[2] <= 1'b0;
                        seq_last_valid[2] <= 1'b0;
                        seq_depth[2] <= 2'd0;
                        invalidate_prefetch(2);
                        ready[3]  <= 1'b1;
                    end else begin
                        start_read(3'd3, cur_addr[3], 8'd1);
                    end
                end

                3'd4: begin
                    ram_data    <= cur_din[4];
                    ram_be      <= cur_be[4];
                    ram_address <= cur_addr[4];
                    ram_burst   <= 8'd1;
                    if (!cur_rnw[4]) begin
                        ram_write <= 1'b1;
                        ready[4]  <= 1'b1;
                    end else begin
                        start_read(3'd4, cur_addr[4], 8'd1);
                    end
                end

                default: begin
                    ram_data    <= cur_din[5];
                    ram_be      <= cur_be[5];
                    ram_address <= cur_addr[5];
                    ram_burst   <= 8'd1;
                    if (!cur_rnw[5]) begin
                        ram_write <= 1'b1;
                        ready[5]  <= 1'b1;
                    end else begin
                        start_read(3'd5, cur_addr[5], 8'd1);
                    end
                end
            endcase
        end
    end

endmodule
