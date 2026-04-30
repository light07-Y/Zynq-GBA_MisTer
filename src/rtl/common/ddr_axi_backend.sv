// ddr_axi_backend.sv: 现代化 AXI4 DDR 后端适配器
// 适配 Vivado 2025.2.1 高级总线范式与 UltraFast 设计方法学

module ddr_axi_backend_sv #(
    parameter logic [31:0] G_DDR_BASE = 32'h1000_0000
) (
    input  logic         clk,
    input  logic         rst_n,

    // 存储多路复用器接口 (Internal Core Interface)
    output logic         DDRAM_BUSY,
    input  logic [7:0]   DDRAM_BURSTCNT,
    input  logic [28:0]  DDRAM_ADDR,      // 修改为 28:0 统一标准
    output logic [63:0]  DDRAM_DOUT,
    output logic         DDRAM_DOUT_READY,
    input  logic         DDRAM_RD,
    input  logic [63:0]  DDRAM_DIN,
    input  logic [7:0]   DDRAM_BE,
    input  logic         DDRAM_WE,

    // AXI4 Master 接口 (64-bit Data Width)
    output logic [31:0]  M_AXI_AWADDR,
    output logic [7:0]   M_AXI_AWLEN,
    output logic [2:0]   M_AXI_AWSIZE,
    output logic [1:0]   M_AXI_AWBURST,
    output logic         M_AXI_AWVALID,
    input  logic         M_AXI_AWREADY,

    output logic [63:0]  M_AXI_WDATA,
    output logic [7:0]   M_AXI_WSTRB,
    output logic         M_AXI_WLAST,
    output logic         M_AXI_WVALID,
    input  logic         M_AXI_WREADY,

    input  logic [1:0]   M_AXI_BRESP,
    input  logic         M_AXI_BVALID,
    output logic         M_AXI_BREADY,

    output logic [31:0]  M_AXI_ARADDR,
    output logic [7:0]   M_AXI_ARLEN,
    output logic [2:0]   M_AXI_ARSIZE,
    output logic [1:0]   M_AXI_ARBURST,
    output logic         M_AXI_ARVALID,
    input  logic         M_AXI_ARREADY,

    input  logic [63:0]  M_AXI_RDATA,
    input  logic [1:0]   M_AXI_RRESP,
    input  logic         M_AXI_RLAST,
    input  logic         M_AXI_RVALID,
    output logic         M_AXI_RREADY,

    // Error Reporting
    output logic [31:0]  ERR_VEC,
    output logic         ERR_PULSE
);

    // --- 常量与类型定义 ---
    localparam logic [31:0] C_MISTER_BASE = 32'h3000_0000;
    localparam logic [19:0] C_WATCHDOG_LIMIT = 20'd100000;

    typedef enum logic [2:0] {
        S_IDLE,
        S_RD_ADDR,
        S_RD_DATA,
        S_WR_ADDR_DATA,
        S_WR_RESP
    } state_t;

    state_t state;

    // --- 内部寄存器 ---
    logic [31:0] awaddr_reg;
    logic [7:0]  awlen_reg;
    logic        awvalid_reg;
    logic [63:0] wdata_reg;
    logic [7:0]  wstrb_reg;
    logic        wvalid_reg;
    logic        bready_reg;

    logic [31:0] araddr_reg;
    logic [7:0]  arlen_reg;
    logic        arvalid_reg;
    logic        rready_reg;

    logic [63:0] dout_reg;
    logic        dout_ready_reg;

    logic [7:0]  rd_remaining;
    logic        aw_done;
    logic        w_done;
    logic [19:0] watchdog_ctr;

    // --- 增强型地址映射逻辑 ---
    // 确保 2025.2 综合器不会因位宽推导产生警告
    function automatic logic [31:0] map_addr(input logic [28:0] in_addr);
        logic [31:0] full_addr;
    begin
        full_addr = {in_addr, 3'b000}; 
        if (full_addr >= C_MISTER_BASE) begin
            map_addr = G_DDR_BASE + (full_addr - C_MISTER_BASE);
        end else begin
            map_addr = G_DDR_BASE + full_addr; // 默认偏移
        end
    end
    endfunction

    task automatic latch_error(input logic [3:0] err_code, input logic [27:0] err_addr);
    begin
        ERR_VEC   <= {err_code, err_addr};
        ERR_PULSE <= 1'b1;
    end
    endtask

    task automatic abort_axi(input logic is_read, input logic [3:0] err_code);
    begin
        awvalid_reg    <= 1'b0;
        wvalid_reg     <= 1'b0;
        bready_reg     <= 1'b0;
        arvalid_reg    <= 1'b0;
        rready_reg     <= 1'b0;
        aw_done        <= 1'b0;
        w_done         <= 1'b0;
        rd_remaining   <= 8'd0;
        watchdog_ctr   <= 20'd0;
        state          <= S_IDLE;

        if (is_read) begin
            dout_reg       <= 64'hFFFF_FFFF_FFFF_FFFF;
            dout_ready_reg <= 1'b1;
            latch_error(err_code, araddr_reg[27:0]);
        end else begin
            latch_error(err_code, awaddr_reg[27:0]);
        end
    end
    endtask

    // --- 接口驱动 (组合逻辑) ---
    assign DDRAM_BUSY       = (state != S_IDLE);
    assign DDRAM_DOUT       = dout_reg;
    assign DDRAM_DOUT_READY = dout_ready_reg;

    assign M_AXI_AWADDR   = awaddr_reg;
    assign M_AXI_AWLEN    = awlen_reg;
    assign M_AXI_AWSIZE   = 3'b011; // 64-bit / 8-byte
    assign M_AXI_AWBURST  = 2'b01;  // INCR
    assign M_AXI_AWVALID  = awvalid_reg;

    assign M_AXI_WDATA    = wdata_reg;
    assign M_AXI_WSTRB    = wstrb_reg;
    assign M_AXI_WLAST    = wvalid_reg && (awlen_reg == 8'd0); // 改进的 WLAST 逻辑
    assign M_AXI_WVALID   = wvalid_reg;

    assign M_AXI_BREADY   = bready_reg;

    assign M_AXI_ARADDR   = araddr_reg;
    assign M_AXI_ARLEN    = arlen_reg;
    assign M_AXI_ARSIZE   = 3'b011; // 64-bit
    assign M_AXI_ARBURST  = 2'b01;  // INCR
    assign M_AXI_ARVALID  = arvalid_reg;

    assign M_AXI_RREADY   = rready_reg;

    // --- 状态机逻辑 ---
    always_ff @(posedge clk) begin
        if (!rst_n) begin
            state          <= S_IDLE;
            {awvalid_reg, wvalid_reg, bready_reg} <= 3'b000;
            {arvalid_reg, rready_reg} <= 2'b00;
            dout_ready_reg <= 1'b0;
            aw_done <= 1'b0;
            w_done  <= 1'b0;
            rd_remaining <= 8'd0;
            watchdog_ctr <= 20'd0;
            dout_reg <= 64'd0;
            awaddr_reg <= 32'd0;
            awlen_reg <= 8'd0;
            wdata_reg <= 64'd0;
            wstrb_reg <= 8'd0;
            araddr_reg <= 32'd0;
            arlen_reg <= 8'd0;
            ERR_VEC <= 32'h0;
            ERR_PULSE <= 1'b0;
        end else begin
            dout_ready_reg <= 1'b0;
            ERR_PULSE <= 1'b0;
            watchdog_ctr <= (state == S_IDLE) ? 20'd0 : (watchdog_ctr + 20'd1);

            case (state)
                S_IDLE: begin
                    if (DDRAM_RD) begin
                        araddr_reg  <= map_addr(DDRAM_ADDR);
                        arlen_reg   <= (DDRAM_BURSTCNT == 8'd0) ? 8'd0 : DDRAM_BURSTCNT - 8'd1;
                        rd_remaining <= (DDRAM_BURSTCNT == 8'd0) ? 8'd0 : DDRAM_BURSTCNT - 8'd1;
                        arvalid_reg <= 1'b1;
                        watchdog_ctr <= 20'd0;
                        state       <= S_RD_ADDR;
                    end else if (DDRAM_WE) begin
                        awaddr_reg  <= map_addr(DDRAM_ADDR);
                        awlen_reg   <= 8'd0; // 目前 Core 仅执行单次写
                        awvalid_reg <= 1'b1;
                        wdata_reg   <= DDRAM_DIN;
                        wstrb_reg   <= DDRAM_BE;
                        wvalid_reg  <= 1'b1;
                        bready_reg  <= 1'b0;
                        aw_done     <= 1'b0;
                        w_done      <= 1'b0;
                        watchdog_ctr <= 20'd0;
                        state       <= S_WR_ADDR_DATA;
                    end
                end

                S_RD_ADDR: begin
                    if (M_AXI_ARREADY) begin
                        arvalid_reg <= 1'b0;
                        rready_reg  <= 1'b1;
                        watchdog_ctr <= 20'd0;
                        state       <= S_RD_DATA;
                    end
                end

                S_RD_DATA: begin
                    if (M_AXI_RVALID) begin
                        watchdog_ctr   <= 20'd0;
                        dout_reg       <= (M_AXI_RRESP == 2'b00) ? M_AXI_RDATA : 64'hFFFF_FFFF_FFFF_FFFF;
                        dout_ready_reg <= 1'b1;
                        if (M_AXI_RRESP != 2'b00) begin
                            latch_error({2'b01, M_AXI_RRESP}, araddr_reg[27:0]);
                        end
                        if (M_AXI_RLAST || rd_remaining == 8'd0) begin
                            rready_reg <= 1'b0;
                            rd_remaining <= 8'd0;
                            state      <= S_IDLE;
                        end else begin
                            rd_remaining <= rd_remaining - 8'd1;
                        end
                    end
                end

                S_WR_ADDR_DATA: begin
                    if (M_AXI_AWREADY) begin
                        awvalid_reg <= 1'b0;
                        aw_done     <= 1'b1;
                        watchdog_ctr <= 20'd0;
                    end
                    if (M_AXI_WREADY) begin
                        wvalid_reg  <= 1'b0;
                        w_done      <= 1'b1;
                        watchdog_ctr <= 20'd0;
                    end
                    // 当地址和数据都握手成功后进入响应阶段
                    if ((aw_done || M_AXI_AWREADY) && (w_done || M_AXI_WREADY)) begin
                        bready_reg <= 1'b1;
                        state      <= S_WR_RESP;
                    end
                end

                S_WR_RESP: begin
                    if (M_AXI_BVALID) begin
                        watchdog_ctr <= 20'd0;
                        if (M_AXI_BRESP != 2'b00) begin
                            latch_error({2'b10, M_AXI_BRESP}, awaddr_reg[27:0]);
                        end
                        bready_reg <= 1'b0;
                        state      <= S_IDLE;
                    end
                end

                default: state <= S_IDLE;
            endcase

            if ((state != S_IDLE) && (watchdog_ctr >= C_WATCHDOG_LIMIT)) begin
                abort_axi((state == S_RD_ADDR) || (state == S_RD_DATA), 4'hD);
            end
        end
    end

endmodule
