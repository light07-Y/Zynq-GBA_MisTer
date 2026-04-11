// ddram_mux_noguard.sv: 移除 !ram_write 守卫的旧版本，用于验证硬件是否部署了修复
// 唯一区别：state 0 条件从 `!DDRAM_BUSY && !ram_write` 变为 `!DDRAM_BUSY`

module ddram_mux_noguard (
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
    input  logic [27:1]  ch1_addr, output logic [63:0] ch1_dout, input  logic [15:0] ch1_din,
    input  logic         ch1_req,  input  logic        ch1_rnw,  output logic         ch1_ready,
    input  logic [27:1]  ch2_addr, output logic [31:0] ch2_dout, input  logic [31:0] ch2_din,
    input  logic         ch2_req,  input  logic        ch2_rnw,  output logic         ch2_ready,
    input  logic [25:1]  ch3_addr, output logic [15:0] ch3_dout, input  logic [15:0] ch3_din,
    input  logic         ch3_req,  input  logic        ch3_rnw,  output logic         ch3_ready,
    input  logic [27:1]  ch4_addr, output logic [63:0] ch4_dout, input  logic [63:0] ch4_din,
    input  logic         ch4_req,  input  logic        ch4_rnw,  input  logic [7:0] ch4_be, output logic ch4_ready,
    input  logic [27:1]  ch5_addr, output logic [63:0] ch5_dout, input  logic [63:0] ch5_din,
    input  logic         ch5_req,  input  logic        ch5_rnw,  output logic         ch5_ready
);
    logic [7:0]  ram_burst;
    logic [63:0] ram_q[5:1];
    logic [63:0] ram_data;
    logic [27:1] ram_address;
    logic        ram_read = 1'b0;
    logic        ram_write = 1'b0;
    logic [7:0]  ram_be;
    logic [5:1]  ready = '0;
    assign DDRAM_BURSTCNT = ram_burst;
    assign DDRAM_BE       = ram_read ? 8'hFF : ram_be;
    assign DDRAM_ADDR     = {4'b0011, ram_address[27:3]};
    assign DDRAM_RD       = ram_read;
    assign DDRAM_DIN      = ram_data;
    assign DDRAM_WE       = ram_write;
    assign ch1_dout  = ch1_addr[2] ? {ram_q[1][31:0], ram_q[1][63:32]} : ram_q[1];
    assign ch2_dout  = ch2_addr[2] ? ram_q[2][63:32] : ram_q[2][31:0];
    assign ch3_dout  = {ram_q[3][39:32], ram_q[3][7:0]};
    assign ch4_dout  = ram_q[4];
    assign ch5_dout  = ram_q[5];
    assign ch1_ready = ready[1]; assign ch2_ready = ready[2];
    assign ch3_ready = ready[3]; assign ch4_ready = ready[4]; assign ch5_ready = ready[5];

    logic [27:1] cache_addr[2:1];
    logic [1:0]  state  = 2'd0;
    logic [2:1]  cached = 2'd0;
    logic [2:0]  active_ch = 3'd0;
    logic [5:1]  ch_rq = '0;

    always_ff @(posedge DDRAM_CLK) begin
        ch_rq <= ch_rq | {ch5_req, ch4_req, ch3_req, ch2_req, ch1_req};
        ready <= 5'd0;
        ram_write <= 1'b0;
        ram_read  <= 1'b0;
        case (state)
            2'd0: begin
                // ★ 旧代码：没有 !ram_write 守卫 ★
                if (!DDRAM_BUSY) begin
                    if (ch_rq[1] || ch1_req) begin
                        ch_rq[1] <= 1'b0; active_ch <= 3'd1;
                        ram_data <= {4{ch1_din}}; ram_be <= 8'h03 << {ch1_addr[2:1], 1'b0};
                        if (~ch1_rnw) begin
                            ram_address <= ch1_addr; ram_write <= 1'b1; ram_burst <= 8'd1;
                            cached[1] <= 1'b0; ready[1] <= 1'b1;
                        end else if (cached[1] && cache_addr[1][27:3] == ch1_addr[27:3]) begin
                            ready[1] <= 1'b1;
                        end else begin
                            ram_address <= ch1_addr; cache_addr[1] <= ch1_addr;
                            ram_read <= 1'b1; ram_burst <= 8'd1; cached[1] <= 1'b1; state <= 2'd1;
                        end
                    end else if (ch_rq[2] || ch2_req) begin
                        ch_rq[2] <= 1'b0; active_ch <= 3'd2;
                        ram_data <= {2{ch2_din}}; ram_be <= ch2_addr[2] ? 8'hF0 : 8'h0F;
                        if (~ch2_rnw) begin
                            ram_address <= ch2_addr; ram_write <= 1'b1; ram_burst <= 8'd1;
                            cached[2] <= 1'b0; ready[2] <= 1'b1;
                        end else if (cached[2] && cache_addr[2][27:3] == ch2_addr[27:3]) begin
                            ready[2] <= 1'b1;
                        end else begin
                            ram_address <= ch2_addr; cache_addr[2] <= ch2_addr;
                            ram_read <= 1'b1; ram_burst <= 8'd1; cached[2] <= 1'b1; state <= 2'd1;
                        end
                    end else if (ch_rq[5] || ch5_req) begin
                        ch_rq[5] <= 1'b0; active_ch <= 3'd5;
                        ram_data <= ch5_din; ram_be <= 8'hFF; ram_address <= ch5_addr; ram_burst <= 8'd1;
                        if (~ch5_rnw) begin ram_write <= 1'b1; ready[5] <= 1'b1;
                        end else begin ram_read <= 1'b1; state <= 2'd1; end
                    end
                end
            end
            2'd1: begin
                if (DDRAM_DOUT_READY) begin
                    ram_q[active_ch] <= DDRAM_DOUT;
                    ready[active_ch] <= 1'b1;
                    state <= 2'd0;
                end
            end
            default: state <= 2'd0;
        endcase
    end
endmodule
