// ddram_mux.sv: 现代化存储多路复用器 (SystemVerilog 2012+ Version)
// 适配 Vivado 2025.2.1 高级综合范式

module ddram_mux (
    input  logic         DDRAM_CLK,
    input  logic         DDRAM_BUSY,
    output logic [7:0]   DDRAM_BURSTCNT,
    output logic [28:0]  DDRAM_ADDR,      // 修改为 28:0 统一标准
    input  logic [63:0]  DDRAM_DOUT,
    input  logic         DDRAM_DOUT_READY,
    output logic         DDRAM_RD,
    output logic [63:0]  DDRAM_DIN,
    output logic [7:0]   DDRAM_BE,
    output logic         DDRAM_WE,
    
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

    // --- 寄存器定义与类型现代化 ---
    logic [7:0]  ram_burst;
    logic [63:0] ram_q[5:1];
    logic [63:0] ram_data;
    logic [27:1] ram_address;
    logic        ram_read = 1'b0;
    logic        ram_write = 1'b0;
    logic [7:0]  ram_be;
    logic [5:1]  ready = '0;
    // --- 组合逻辑分配 ---
    assign DDRAM_BURSTCNT = ram_burst;
    assign DDRAM_BE       = ram_read ? 8'hFF : ram_be;
    // 保持与 MiSTer 原始 ddram 地址语义一致：
    // DDRAM_ADDR 表示 0x3000_0000 基址上的 64-bit 对齐地址，
    // 后端再把这段地址空间映射到 PS DDR。
    assign DDRAM_ADDR     = {4'b0011, ram_address[27:3]};
    assign DDRAM_RD       = ram_read;
    assign DDRAM_DIN      = ram_data;
    assign DDRAM_WE       = ram_write;

    // 输出映射
    // Channel 1 keeps MiSTer's data layout contract:
    // - ch1_dout low/high DWORD ordering follows ch1_addr[2]
    // - only the current 64-bit line is cached; adjacent-line prefetch is
    //   intentionally disabled here to prioritize correctness on AXI DDR
    assign ch1_dout  = ch1_addr[2] ? {ram_q[1][31:0], ram_q[1][63:32]} : ram_q[1];
    assign ch2_dout  = ch2_addr[2] ? ram_q[2][63:32] : ram_q[2][31:0];
    assign ch3_dout  = {ram_q[3][39:32], ram_q[3][7:0]};
    assign ch4_dout  = ram_q[4];
    assign ch5_dout  = ram_q[5];
    
    assign ch1_ready = ready[1];
    assign ch2_ready = ready[2];
    assign ch3_ready = ready[3];
    assign ch4_ready = ready[4];
    assign ch5_ready = ready[5];

    // --- 状态机与缓存逻辑 ---
    logic [27:1] cache_addr[2:1];
    logic [1:0]  state  = 2'd0;
    logic [2:1]  cached = 2'd0;
    logic [2:0]  active_ch = 3'd0;
    logic [5:1]  ch_rq = '0;
    logic [2:0]  rr_last_grant = 3'd5;
    logic [5:1]  pending_req;
    logic [2:0]  grant_ch;
    logic        grant_valid;

    // 固定优先级会在 ch1 持续高电平请求时长期饿死 ch5(framebuffer)。
    // 这里改为简单轮转仲裁：从上次服务通道的下一个通道开始找，
    // 保证已挂起请求在有限时间内一定能被发到 backend。
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

    always_ff @(posedge DDRAM_CLK) begin
        // 请求锁存（始终执行，不受 BUSY 影响）
        ch_rq <= ch_rq | {ch5_req, ch4_req, ch3_req, ch2_req, ch1_req};
        ready <= 5'd0;

        // ---- MiSTer 风格：整个状态机在 !BUSY 守卫内 ----
        // ram_read/ram_write 在 BUSY 期间保持（不被清零），
        // 确保 backend 从 BUSY→空闲 时能捕获挂起的请求。
        if (!DDRAM_BUSY) begin
            ram_write <= 1'b0;
            ram_read  <= 1'b0;

            case (state)
                2'd0: begin // Idle
                    if (grant_valid) begin
                        rr_last_grant <= grant_ch;

                        unique case (grant_ch)
                            3'd1: begin
                                ch_rq[1]         <= 1'b0;
                                active_ch        <= 3'd1;
                                ram_data         <= {4{ch1_din}};
                                ram_be           <= 8'h03 << {ch1_addr[2:1], 1'b0};
                                if (~ch1_rnw) begin
                                    ram_address  <= ch1_addr;
                                    ram_write    <= 1'b1;
                                    ram_burst    <= 8'd1;
                                    cached[1]    <= 1'b0;
                                    ready[1]     <= 1'b1;
                                end else if (cached[1] && cache_addr[1][27:3] == ch1_addr[27:3]) begin
                                    ready[1]     <= 1'b1;
                                end else begin
                                    ram_address   <= ch1_addr;
                                    cache_addr[1] <= ch1_addr;
                                    ram_read      <= 1'b1;
                                    ram_burst     <= 8'd1;
                                    cached[1]     <= 1'b1;
                                    state         <= 2'd1;
                                end
                            end

                            3'd2: begin
                                ch_rq[2]         <= 1'b0;
                                active_ch        <= 3'd2;
                                ram_data         <= {2{ch2_din}};
                                ram_be           <= ch2_addr[2] ? 8'hF0 : 8'h0F;
                                if (~ch2_rnw) begin
                                    ram_address  <= ch2_addr;
                                    ram_write    <= 1'b1;
                                    ram_burst    <= 8'd1;
                                    cached[2]    <= 1'b0;
                                    ready[2]     <= 1'b1;
                                end else if (cached[2] && cache_addr[2][27:3] == ch2_addr[27:3]) begin
                                    ready[2]     <= 1'b1;
                                end else begin
                                    ram_address   <= ch2_addr;
                                    cache_addr[2] <= ch2_addr;
                                    ram_read      <= 1'b1;
                                    ram_burst     <= 8'd1;
                                    cached[2]     <= 1'b1;
                                    state         <= 2'd1;
                                end
                            end

                            3'd3: begin
                                ch_rq[3]         <= 1'b0;
                                active_ch        <= 3'd3;
                                ram_address      <= {ch3_addr, 2'b00};
                                ram_data         <= {24'd0, ch3_din[15:8], 24'd0, ch3_din[7:0]};
                                ram_be           <= 8'hFF;
                                ram_burst        <= 8'd1;
                                if (~ch3_rnw) begin
                                    ram_write    <= 1'b1;
                                    cached[2]    <= 1'b0;
                                    ready[3]     <= 1'b1;
                                end else begin
                                    ram_read     <= 1'b1;
                                    state        <= 2'd1;
                                end
                            end

                            3'd4: begin
                                ch_rq[4]         <= 1'b0;
                                active_ch        <= 3'd4;
                                ram_data         <= ch4_din;
                                ram_be           <= ch4_be;
                                ram_address      <= ch4_addr;
                                ram_burst        <= 8'd1;
                                if (~ch4_rnw) begin
                                    ram_write    <= 1'b1;
                                    ready[4]     <= 1'b1;
                                end else begin
                                    ram_read     <= 1'b1;
                                    state        <= 2'd1;
                                end
                            end

                            default: begin
                                ch_rq[5]         <= 1'b0;
                                active_ch        <= 3'd5;
                                ram_data         <= ch5_din;
                                ram_be           <= 8'hFF;
                                ram_address      <= ch5_addr;
                                ram_burst        <= 8'd1;
                                if (~ch5_rnw) begin
                                    ram_write    <= 1'b1;
                                    ready[5]     <= 1'b1;
                                end else begin
                                    ram_read     <= 1'b1;
                                    state        <= 2'd1;
                                end
                            end
                        endcase
                    end
                end

                2'd1: begin // Wait Data
                    if (DDRAM_DOUT_READY) begin
                        ram_q[active_ch] <= DDRAM_DOUT;
                        ready[active_ch] <= 1'b1;
                        state            <= 2'd0;
                    end
                end

                default: state <= 2'd0;
            endcase
        end // if (!DDRAM_BUSY)
    end

endmodule
