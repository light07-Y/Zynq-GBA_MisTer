// axi_mem_model_delayed.sv: 带可配置延迟的 AXI4 从端模型
// 模拟真实 Zynq HP 端口的多周期响应延迟
`timescale 1ns / 1ps

module axi_mem_model_delayed #(
    parameter MEM_SIZE_BYTES   = 65536,
    parameter DATA_WIDTH       = 64,
    parameter ADDR_WIDTH       = 32,
    parameter ROM_HEX_FILE     = "",
    parameter AR_ACCEPT_DELAY  = 0,   // ARREADY 延迟周期
    parameter R_RESPONSE_DELAY = 0,   // 读数据响应延迟
    parameter AW_ACCEPT_DELAY  = 0,   // AWREADY 延迟周期
    parameter W_ACCEPT_DELAY   = 0,   // WREADY 延迟周期
    parameter B_RESPONSE_DELAY = 0    // 写响应延迟
) (
    input  logic                    clk,
    input  logic                    rst_n,
    input  logic [ADDR_WIDTH-1:0]   S_AXI_ARADDR,
    input  logic [7:0]              S_AXI_ARLEN,
    input  logic [2:0]              S_AXI_ARSIZE,
    input  logic [1:0]              S_AXI_ARBURST,
    input  logic                    S_AXI_ARVALID,
    output logic                    S_AXI_ARREADY,
    output logic [DATA_WIDTH-1:0]   S_AXI_RDATA,
    output logic [1:0]              S_AXI_RRESP,
    output logic                    S_AXI_RLAST,
    output logic                    S_AXI_RVALID,
    input  logic                    S_AXI_RREADY,
    input  logic [ADDR_WIDTH-1:0]   S_AXI_AWADDR,
    input  logic [7:0]              S_AXI_AWLEN,
    input  logic [2:0]              S_AXI_AWSIZE,
    input  logic [1:0]              S_AXI_AWBURST,
    input  logic                    S_AXI_AWVALID,
    output logic                    S_AXI_AWREADY,
    input  logic [DATA_WIDTH-1:0]   S_AXI_WDATA,
    input  logic [7:0]              S_AXI_WSTRB,
    input  logic                    S_AXI_WLAST,
    input  logic                    S_AXI_WVALID,
    output logic                    S_AXI_WREADY,
    output logic [1:0]              S_AXI_BRESP,
    output logic                    S_AXI_BVALID,
    input  logic                    S_AXI_BREADY
);

    localparam MEM_DWORDS = MEM_SIZE_BYTES / 4;
    logic [31:0] mem [0:MEM_DWORDS-1];

    localparam [31:0] DDR_BASE_ADDR = 32'h1000_0000;
    localparam [31:0] ROM_BASE_ADDR = 32'h100C_0000;
    localparam [31:0] ROM_DWORD_OFFSET = (ROM_BASE_ADDR - DDR_BASE_ADDR) >> 2;

    initial begin
        for (int i = 0; i < MEM_DWORDS; i++) mem[i] = 32'hDEAD_BEEF;
        if (ROM_HEX_FILE != "") begin
            $readmemh(ROM_HEX_FILE, mem, ROM_DWORD_OFFSET);
        end
    end

    function automatic logic [31:0] addr_to_dword_idx(input logic [31:0] addr);
        if (addr >= DDR_BASE_ADDR)
            return (addr - DDR_BASE_ADDR) >> 2;
        else
            return 32'hFFFF_FFFF;
    endfunction

    function automatic logic [63:0] read_qword(input logic [31:0] addr);
        logic [31:0] idx;
        idx = addr_to_dword_idx(addr);
        if (idx + 1 < MEM_DWORDS)
            return {mem[idx+1], mem[idx]};
        else
            return 64'hDEAD_DEAD_DEAD_DEAD;
    endfunction

    // === 读通道: 带延迟 ===
    typedef enum logic [2:0] { R_IDLE, R_AR_DELAY, R_DATA_DELAY, R_DATA } rd_state_t;
    rd_state_t rd_state;
    logic [31:0] rd_addr;
    logic [7:0]  rd_remaining;
    int rd_delay_cnt;

    always_ff @(posedge clk) begin
        if (!rst_n) begin
            rd_state <= R_IDLE;
            S_AXI_ARREADY <= 1'b0;
            S_AXI_RVALID <= 1'b0;
            S_AXI_RLAST <= 1'b0;
            S_AXI_RDATA <= '0;
            S_AXI_RRESP <= 2'b00;
        end else begin
            S_AXI_ARREADY <= 1'b0;

            case (rd_state)
                R_IDLE: begin
                    if (S_AXI_ARVALID) begin
                        rd_addr <= {S_AXI_ARADDR[31:3], 3'b000};
                        rd_remaining <= S_AXI_ARLEN;
                        if (AR_ACCEPT_DELAY == 0) begin
                            S_AXI_ARREADY <= 1'b1;
                            rd_delay_cnt <= R_RESPONSE_DELAY;
                            rd_state <= (R_RESPONSE_DELAY > 0) ? R_DATA_DELAY : R_DATA;
                        end else begin
                            rd_delay_cnt <= AR_ACCEPT_DELAY - 1;
                            rd_state <= R_AR_DELAY;
                        end
                    end
                end

                R_AR_DELAY: begin
                    if (rd_delay_cnt == 0) begin
                        S_AXI_ARREADY <= 1'b1;
                        rd_delay_cnt <= R_RESPONSE_DELAY;
                        rd_state <= (R_RESPONSE_DELAY > 0) ? R_DATA_DELAY : R_DATA;
                    end else begin
                        rd_delay_cnt <= rd_delay_cnt - 1;
                    end
                end

                R_DATA_DELAY: begin
                    if (rd_delay_cnt == 0) begin
                        rd_state <= R_DATA;
                    end else begin
                        rd_delay_cnt <= rd_delay_cnt - 1;
                    end
                end

                R_DATA: begin
                    if (!S_AXI_RVALID || S_AXI_RREADY) begin
                        S_AXI_RDATA <= read_qword(rd_addr);
                        S_AXI_RVALID <= 1'b1;
                        S_AXI_RRESP <= 2'b00;
                        S_AXI_RLAST <= (rd_remaining == 8'd0);
                        if (S_AXI_RREADY && S_AXI_RVALID) begin
                            if (rd_remaining == 8'd0) begin
                                S_AXI_RVALID <= 1'b0;
                                rd_state <= R_IDLE;
                            end else begin
                                rd_remaining <= rd_remaining - 8'd1;
                                rd_addr <= rd_addr + 32'd8;
                            end
                        end
                    end
                end
            endcase
        end
    end

    // === 写通道: 带延迟 ===
    typedef enum logic [2:0] { W_IDLE, W_AW_DELAY, W_DATA, W_B_DELAY, W_RESP } wr_state_t;
    wr_state_t wr_state;
    logic [31:0] wr_addr;
    int wr_delay_cnt;

    always_ff @(posedge clk) begin
        if (!rst_n) begin
            wr_state <= W_IDLE;
            S_AXI_AWREADY <= 1'b0;
            S_AXI_WREADY <= 1'b0;
            S_AXI_BVALID <= 1'b0;
            S_AXI_BRESP <= 2'b00;
        end else begin
            S_AXI_AWREADY <= 1'b0;
            S_AXI_WREADY <= 1'b0;

            case (wr_state)
                W_IDLE: begin
                    if (S_AXI_AWVALID) begin
                        wr_addr <= S_AXI_AWADDR;
                        if (AW_ACCEPT_DELAY == 0) begin
                            S_AXI_AWREADY <= 1'b1;
                            wr_state <= W_DATA;
                        end else begin
                            wr_delay_cnt <= AW_ACCEPT_DELAY - 1;
                            wr_state <= W_AW_DELAY;
                        end
                    end
                end

                W_AW_DELAY: begin
                    if (wr_delay_cnt == 0) begin
                        S_AXI_AWREADY <= 1'b1;
                        wr_state <= W_DATA;
                    end else begin
                        wr_delay_cnt <= wr_delay_cnt - 1;
                    end
                end

                W_DATA: begin
                    if (S_AXI_WVALID) begin
                        S_AXI_WREADY <= 1'b1;
                        // 写入存储
                        begin
                            logic [31:0] base_idx;
                            base_idx = addr_to_dword_idx(wr_addr);
                            if (base_idx < MEM_DWORDS) begin
                                for (int b = 0; b < 4; b++)
                                    if (S_AXI_WSTRB[b]) mem[base_idx][b*8 +: 8] <= S_AXI_WDATA[b*8 +: 8];
                            end
                            if ((base_idx + 1) < MEM_DWORDS) begin
                                for (int b = 4; b < 8; b++)
                                    if (S_AXI_WSTRB[b]) mem[base_idx+1][(b-4)*8 +: 8] <= S_AXI_WDATA[b*8 +: 8];
                            end
                        end
                        if (S_AXI_WLAST) begin
                            if (B_RESPONSE_DELAY == 0) begin
                                wr_state <= W_RESP;
                            end else begin
                                wr_delay_cnt <= B_RESPONSE_DELAY - 1;
                                wr_state <= W_B_DELAY;
                            end
                        end
                    end
                end

                W_B_DELAY: begin
                    if (wr_delay_cnt == 0) begin
                        wr_state <= W_RESP;
                    end else begin
                        wr_delay_cnt <= wr_delay_cnt - 1;
                    end
                end

                W_RESP: begin
                    S_AXI_BVALID <= 1'b1;
                    S_AXI_BRESP <= 2'b00;
                    if (S_AXI_BREADY && S_AXI_BVALID) begin
                        S_AXI_BVALID <= 1'b0;
                        wr_state <= W_IDLE;
                    end
                end
            endcase
        end
    end

endmodule
