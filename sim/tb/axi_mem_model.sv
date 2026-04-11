// axi_mem_model.sv: 简易 AXI4 从端存储模型（仿真专用）
// 预加载 ROM 数据，响应读请求，打印详细日志
module axi_mem_model #(
    parameter MEM_SIZE_BYTES = 65536,  // 64KB 足够仿真前几个 ROM 读
    parameter DATA_WIDTH     = 64,
    parameter ADDR_WIDTH     = 32,
    parameter ROM_HEX_FILE   = ""      // $readmemh 文件路径
) (
    input  logic                    clk,
    input  logic                    rst_n,

    // AXI4 Slave - 读地址通道
    input  logic [ADDR_WIDTH-1:0]   S_AXI_ARADDR,
    input  logic [7:0]              S_AXI_ARLEN,
    input  logic [2:0]              S_AXI_ARSIZE,
    input  logic [1:0]              S_AXI_ARBURST,
    input  logic                    S_AXI_ARVALID,
    output logic                    S_AXI_ARREADY,

    // AXI4 Slave - 读数据通道
    output logic [DATA_WIDTH-1:0]   S_AXI_RDATA,
    output logic [1:0]              S_AXI_RRESP,
    output logic                    S_AXI_RLAST,
    output logic                    S_AXI_RVALID,
    input  logic                    S_AXI_RREADY,

    // AXI4 Slave - 写地址通道
    input  logic [ADDR_WIDTH-1:0]   S_AXI_AWADDR,
    input  logic [7:0]              S_AXI_AWLEN,
    input  logic [2:0]              S_AXI_AWSIZE,
    input  logic [1:0]              S_AXI_AWBURST,
    input  logic                    S_AXI_AWVALID,
    output logic                    S_AXI_AWREADY,

    // AXI4 Slave - 写数据通道
    input  logic [DATA_WIDTH-1:0]   S_AXI_WDATA,
    input  logic [7:0]              S_AXI_WSTRB,
    input  logic                    S_AXI_WLAST,
    input  logic                    S_AXI_WVALID,
    output logic                    S_AXI_WREADY,

    // AXI4 Slave - 写响应通道
    output logic [1:0]              S_AXI_BRESP,
    output logic                    S_AXI_BVALID,
    input  logic                    S_AXI_BREADY
);

    // 内部存储 - 按 32-bit DWORD 组织
    localparam MEM_DWORDS = MEM_SIZE_BYTES / 4;
    logic [31:0] mem [0:MEM_DWORDS-1];

    // DDR 基地址 (所有 AXI 地址减去此值得到内存索引)
    localparam [31:0] DDR_BASE_ADDR = 32'h1000_0000;
    // ROM 预加载偏移 (0x100C0000 - 0x10000000 = 0xC0000, /4 = 0x30000 DWORDs)
    localparam [31:0] ROM_BASE_ADDR = 32'h100C_0000;
    localparam [31:0] ROM_DWORD_OFFSET = (ROM_BASE_ADDR - DDR_BASE_ADDR) >> 2;

    // 读状态机
    typedef enum logic [1:0] { R_IDLE, R_DATA } rd_state_t;
    rd_state_t rd_state;
    logic [31:0] rd_addr;
    logic [7:0]  rd_remaining;

    // 写状态机
    typedef enum logic [1:0] { W_IDLE, W_DATA, W_RESP } wr_state_t;
    wr_state_t wr_state;
    logic [31:0] wr_addr;

    // 初始化存储
    initial begin
        // 先全部清零
        for (int i = 0; i < MEM_DWORDS; i++) begin
            mem[i] = 32'hDEAD_BEEF;  // 未初始化标记
        end
        // 加载 ROM hex 文件
        if (ROM_HEX_FILE != "") begin
            $readmemh(ROM_HEX_FILE, mem, ROM_DWORD_OFFSET);
            $display("[AXI_MEM] 已加载 ROM hex: %s (offset=0x%06X)", ROM_HEX_FILE, ROM_DWORD_OFFSET);
            $display("[AXI_MEM] mem[0]=0x%08X mem[1]=0x%08X mem[2]=0x%08X mem[3]=0x%08X",
                     mem[0], mem[1], mem[2], mem[3]);
        end else begin
            $display("[AXI_MEM] 警告: 未指定 ROM hex 文件");
        end
    end

    // 读取辅助函数：将 AXI 地址映射到 mem 数组索引
    function automatic logic [31:0] addr_to_dword_idx(input logic [31:0] addr);
        logic [31:0] offset;
        if (addr >= DDR_BASE_ADDR) begin
            offset = addr - DDR_BASE_ADDR;
        end else begin
            offset = 32'hFFFF_FFFF;  // 超出范围
        end
        return offset >> 2;  // 转为 DWORD 索引
    endfunction

    // 从存储中读取 64-bit
    function automatic logic [63:0] read_qword(input logic [31:0] addr);
        logic [31:0] idx;
        idx = addr_to_dword_idx(addr);
        if (idx + 1 < MEM_DWORDS) begin
            return {mem[idx+1], mem[idx]};  // 小端: 低 DWORD 在低位
        end else if (idx < MEM_DWORDS) begin
            return {32'hBAD0_BAD0, mem[idx]};
        end else begin
            return 64'hDEAD_DEAD_DEAD_DEAD;
        end
    endfunction

    // === 读通道状态机 ===
    always_ff @(posedge clk) begin
        if (!rst_n) begin
            rd_state     <= R_IDLE;
            S_AXI_ARREADY <= 1'b0;
            S_AXI_RVALID  <= 1'b0;
            S_AXI_RLAST   <= 1'b0;
            S_AXI_RDATA   <= '0;
            S_AXI_RRESP   <= 2'b00;
        end else begin
            S_AXI_ARREADY <= 1'b0;

            case (rd_state)
                R_IDLE: begin
                    if (S_AXI_ARVALID) begin
                        S_AXI_ARREADY <= 1'b1;
                        rd_addr       <= {S_AXI_ARADDR[31:3], 3'b000};  // 64-bit 对齐
                        rd_remaining  <= S_AXI_ARLEN;
                        rd_state      <= R_DATA;
                        $display("[AXI_MEM] @%0t RD_ADDR addr=0x%08X len=%0d size=%0d",
                                 $time, S_AXI_ARADDR, S_AXI_ARLEN, S_AXI_ARSIZE);
                    end
                end

                R_DATA: begin
                    if (!S_AXI_RVALID || S_AXI_RREADY) begin
                        S_AXI_RDATA  <= read_qword(rd_addr);
                        S_AXI_RVALID <= 1'b1;
                        S_AXI_RRESP  <= 2'b00;  // OKAY
                        S_AXI_RLAST  <= (rd_remaining == 8'd0);
                        $display("[AXI_MEM] @%0t RD_DATA addr=0x%08X data=0x%016X last=%0b",
                                 $time, rd_addr, read_qword(rd_addr), (rd_remaining == 8'd0));
                        if (rd_remaining == 8'd0) begin
                            if (S_AXI_RREADY || !S_AXI_RVALID) begin
                                // 等待 RREADY 后回 IDLE
                            end
                        end
                        if (S_AXI_RREADY && S_AXI_RVALID) begin
                            if (rd_remaining == 8'd0) begin
                                S_AXI_RVALID <= 1'b0;
                                rd_state     <= R_IDLE;
                            end else begin
                                rd_remaining <= rd_remaining - 8'd1;
                                rd_addr      <= rd_addr + 32'd8;
                            end
                        end
                    end
                end
            endcase
        end
    end

    // === 写通道状态机（简单应答即可）===
    always_ff @(posedge clk) begin
        if (!rst_n) begin
            wr_state      <= W_IDLE;
            S_AXI_AWREADY <= 1'b0;
            S_AXI_WREADY  <= 1'b0;
            S_AXI_BVALID  <= 1'b0;
            S_AXI_BRESP   <= 2'b00;
        end else begin
            S_AXI_AWREADY <= 1'b0;
            S_AXI_WREADY  <= 1'b0;

            case (wr_state)
                W_IDLE: begin
                    if (S_AXI_AWVALID) begin
                        S_AXI_AWREADY <= 1'b1;
                        wr_addr       <= S_AXI_AWADDR;
                        wr_state      <= W_DATA;
                        $display("[AXI_MEM] @%0t WR_ADDR addr=0x%08X", $time, S_AXI_AWADDR);
                    end
                end

                W_DATA: begin
                    if (S_AXI_WVALID) begin
                        S_AXI_WREADY <= 1'b1;
                        $display("[AXI_MEM] @%0t WR_DATA addr=0x%08X data=0x%016X strb=0x%02X",
                                 $time, wr_addr, S_AXI_WDATA, S_AXI_WSTRB);
                        // 写入存储 (按字节粒度)
                        begin
                            logic [31:0] base_idx;
                            base_idx = addr_to_dword_idx(wr_addr);
                            if (base_idx < MEM_DWORDS) begin
                                for (int b = 0; b < 4; b++) begin
                                    if (S_AXI_WSTRB[b])
                                        mem[base_idx][b*8 +: 8] <= S_AXI_WDATA[b*8 +: 8];
                                end
                            end
                            if ((base_idx + 1) < MEM_DWORDS) begin
                                for (int b = 4; b < 8; b++) begin
                                    if (S_AXI_WSTRB[b])
                                        mem[base_idx+1][(b-4)*8 +: 8] <= S_AXI_WDATA[b*8 +: 8];
                                end
                            end
                        end
                        if (S_AXI_WLAST) begin
                            wr_state <= W_RESP;
                        end else begin
                            wr_addr <= wr_addr + 32'd8;
                        end
                    end
                end

                W_RESP: begin
                    S_AXI_BVALID <= 1'b1;
                    S_AXI_BRESP  <= 2'b00;
                    if (S_AXI_BREADY && S_AXI_BVALID) begin
                        S_AXI_BVALID <= 1'b0;
                        wr_state     <= W_IDLE;
                    end
                end
            endcase
        end
    end

endmodule
