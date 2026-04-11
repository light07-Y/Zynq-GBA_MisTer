// axi_mem_model_random.sv: 随机延迟 AXI4 从端模型
// 每次 AR/AW/B 响应使用 LFSR 伪随机延迟，暴露隐藏时序窗口
`timescale 1ns / 1ps

module axi_mem_model_random #(
    parameter MEM_SIZE_BYTES = 65536,
    parameter DATA_WIDTH     = 64,
    parameter ADDR_WIDTH     = 32,
    parameter ROM_HEX_FILE   = "",
    parameter MIN_AR_DELAY   = 0,
    parameter MAX_AR_DELAY   = 8,
    parameter MIN_R_DELAY    = 0,
    parameter MAX_R_DELAY    = 12,
    parameter MIN_AW_DELAY   = 0,
    parameter MAX_AW_DELAY   = 4,
    parameter MIN_B_DELAY    = 0,
    parameter MAX_B_DELAY    = 6,
    parameter SEED           = 32'hDEAD_BEEF
) (
    input  logic                    clk,
    input  logic                    rst_n,
    // AR
    input  logic [ADDR_WIDTH-1:0]   S_AXI_ARADDR,
    input  logic [7:0]              S_AXI_ARLEN,
    input  logic [2:0]              S_AXI_ARSIZE,
    input  logic [1:0]              S_AXI_ARBURST,
    input  logic                    S_AXI_ARVALID,
    output logic                    S_AXI_ARREADY,
    // R
    output logic [DATA_WIDTH-1:0]   S_AXI_RDATA,
    output logic [1:0]              S_AXI_RRESP,
    output logic                    S_AXI_RLAST,
    output logic                    S_AXI_RVALID,
    input  logic                    S_AXI_RREADY,
    // AW
    input  logic [ADDR_WIDTH-1:0]   S_AXI_AWADDR,
    input  logic [7:0]              S_AXI_AWLEN,
    input  logic [2:0]              S_AXI_AWSIZE,
    input  logic [1:0]              S_AXI_AWBURST,
    input  logic                    S_AXI_AWVALID,
    output logic                    S_AXI_AWREADY,
    // W
    input  logic [DATA_WIDTH-1:0]   S_AXI_WDATA,
    input  logic [7:0]              S_AXI_WSTRB,
    input  logic                    S_AXI_WLAST,
    input  logic                    S_AXI_WVALID,
    output logic                    S_AXI_WREADY,
    // B
    output logic [1:0]              S_AXI_BRESP,
    output logic                    S_AXI_BVALID,
    input  logic                    S_AXI_BREADY
);

    // --- 存储 ---
    localparam MEM_DWORDS = MEM_SIZE_BYTES / 4;
    logic [31:0] mem [0:MEM_DWORDS-1];
    localparam [31:0] DDR_BASE = 32'h1000_0000;
    localparam [31:0] ROM_BASE = 32'h100C_0000;
    localparam [31:0] ROM_OFF  = (ROM_BASE - DDR_BASE) >> 2;

    initial begin
        for (int i = 0; i < MEM_DWORDS; i++) mem[i] = 32'hDEAD_0000 + i[15:0];
        if (ROM_HEX_FILE != "") $readmemh(ROM_HEX_FILE, mem, ROM_OFF);
    end

    function automatic logic [31:0] idx(input logic [31:0] a);
        return (a >= DDR_BASE) ? ((a - DDR_BASE) >> 2) : 32'hFFFFFFFF;
    endfunction
    function automatic logic [63:0] rdq(input logic [31:0] a);
        logic [31:0] i; i = idx(a);
        return (i+1 < MEM_DWORDS) ? {mem[i+1], mem[i]} : 64'hDEAD_DEAD_DEAD_DEAD;
    endfunction

    // --- LFSR 伪随机 ---
    logic [31:0] lfsr = SEED;
    always_ff @(posedge clk)
        lfsr <= {lfsr[30:0], lfsr[31] ^ lfsr[21] ^ lfsr[1] ^ lfsr[0]};

    function automatic int rand_range(input int lo, input int hi, input int salt);
        if (hi <= lo) return lo;
        return lo + ((lfsr + salt) % (hi - lo + 1));
    endfunction

    // === 读通道 ===
    typedef enum logic [2:0] { R_IDLE, R_AR_WAIT, R_R_WAIT, R_DATA } rd_st_t;
    rd_st_t rst;
    logic [31:0] ra;
    logic [7:0]  rrem;
    int rd_cnt;

    always_ff @(posedge clk) begin
        if (!rst_n) begin
            rst <= R_IDLE; S_AXI_ARREADY <= 0; S_AXI_RVALID <= 0;
            S_AXI_RLAST <= 0; S_AXI_RDATA <= '0; S_AXI_RRESP <= 0;
        end else begin
            S_AXI_ARREADY <= 0;
            case (rst)
                R_IDLE: if (S_AXI_ARVALID) begin
                    ra <= {S_AXI_ARADDR[31:3], 3'b0};
                    rrem <= S_AXI_ARLEN;
                    rd_cnt <= rand_range(MIN_AR_DELAY, MAX_AR_DELAY, 0);
                    rst <= (rand_range(MIN_AR_DELAY, MAX_AR_DELAY, 0) == 0) ? R_R_WAIT : R_AR_WAIT;
                    if (rand_range(MIN_AR_DELAY, MAX_AR_DELAY, 0) == 0) begin
                        S_AXI_ARREADY <= 1;
                        rd_cnt <= rand_range(MIN_R_DELAY, MAX_R_DELAY, 1);
                        rst <= (rand_range(MIN_R_DELAY, MAX_R_DELAY, 1) == 0) ? R_DATA : R_R_WAIT;
                    end
                end
                R_AR_WAIT: begin
                    if (rd_cnt <= 1) begin
                        S_AXI_ARREADY <= 1;
                        rd_cnt <= rand_range(MIN_R_DELAY, MAX_R_DELAY, 2);
                        rst <= (rand_range(MIN_R_DELAY, MAX_R_DELAY, 2) == 0) ? R_DATA : R_R_WAIT;
                    end else rd_cnt <= rd_cnt - 1;
                end
                R_R_WAIT: begin
                    if (rd_cnt <= 1) rst <= R_DATA;
                    else rd_cnt <= rd_cnt - 1;
                end
                R_DATA: begin
                    if (!S_AXI_RVALID || S_AXI_RREADY) begin
                        S_AXI_RDATA <= rdq(ra);
                        S_AXI_RVALID <= 1;
                        S_AXI_RRESP <= 0;
                        S_AXI_RLAST <= (rrem == 0);
                        if (S_AXI_RREADY && S_AXI_RVALID) begin
                            if (rrem == 0) begin S_AXI_RVALID <= 0; rst <= R_IDLE; end
                            else begin rrem <= rrem - 1; ra <= ra + 32'd8; end
                        end
                    end
                end
            endcase
        end
    end

    // === 写通道 ===
    typedef enum logic [2:0] { W_IDLE, W_AW_WAIT, W_DATA, W_B_WAIT, W_RESP } wr_st_t;
    wr_st_t wst;
    logic [31:0] wa;
    int wr_cnt;

    always_ff @(posedge clk) begin
        if (!rst_n) begin
            wst <= W_IDLE; S_AXI_AWREADY <= 0; S_AXI_WREADY <= 0;
            S_AXI_BVALID <= 0; S_AXI_BRESP <= 0;
        end else begin
            S_AXI_AWREADY <= 0; S_AXI_WREADY <= 0;
            case (wst)
                W_IDLE: if (S_AXI_AWVALID) begin
                    wa <= S_AXI_AWADDR;
                    wr_cnt <= rand_range(MIN_AW_DELAY, MAX_AW_DELAY, 3);
                    if (rand_range(MIN_AW_DELAY, MAX_AW_DELAY, 3) == 0) begin
                        S_AXI_AWREADY <= 1; wst <= W_DATA;
                    end else wst <= W_AW_WAIT;
                end
                W_AW_WAIT: begin
                    if (wr_cnt <= 1) begin S_AXI_AWREADY <= 1; wst <= W_DATA; end
                    else wr_cnt <= wr_cnt - 1;
                end
                W_DATA: if (S_AXI_WVALID) begin
                    S_AXI_WREADY <= 1;
                    begin
                        logic [31:0] bi; bi = idx(wa);
                        if (bi < MEM_DWORDS)
                            for (int b=0;b<4;b++) if (S_AXI_WSTRB[b]) mem[bi][b*8+:8] <= S_AXI_WDATA[b*8+:8];
                        if (bi+1 < MEM_DWORDS)
                            for (int b=4;b<8;b++) if (S_AXI_WSTRB[b]) mem[bi+1][(b-4)*8+:8] <= S_AXI_WDATA[b*8+:8];
                    end
                    wr_cnt <= rand_range(MIN_B_DELAY, MAX_B_DELAY, 4);
                    if (rand_range(MIN_B_DELAY, MAX_B_DELAY, 4) == 0) wst <= W_RESP;
                    else wst <= W_B_WAIT;
                end
                W_B_WAIT: begin
                    if (wr_cnt <= 1) wst <= W_RESP;
                    else wr_cnt <= wr_cnt - 1;
                end
                W_RESP: begin
                    S_AXI_BVALID <= 1; S_AXI_BRESP <= 0;
                    if (S_AXI_BREADY && S_AXI_BVALID) begin
                        S_AXI_BVALID <= 0; wst <= W_IDLE;
                    end
                end
            endcase
        end
    end
endmodule
