// tb_ddr_arbitration.sv
// 验证 ddram_mux 多通道并发: ch1(ROM读) + ch5(FB写) 同时活动
// 检测: 数据正确性、饥饿、死锁
`timescale 1ns / 1ps

module tb_ddr_arbitration;

    logic clk = 0;
    always #5 clk = ~clk;
    logic rst_n = 0;

    // ddram_mux <-> ddr_axi_backend
    logic        ddram_busy;
    logic [7:0]  ddram_burstcnt;
    logic [28:0] ddram_addr;
    logic [63:0] ddram_dout, ddram_din;
    logic        ddram_dout_ready, ddram_rd, ddram_we;
    logic [7:0]  ddram_be;

    // ch1: ROM 读
    logic [27:1] ch1_addr;
    logic [63:0] ch1_dout;
    logic [15:0] ch1_din;
    logic        ch1_req, ch1_rnw, ch1_ready;

    // ch5: FB 写
    logic [27:1] ch5_addr;
    logic [63:0] ch5_dout, ch5_din;
    logic        ch5_req, ch5_rnw, ch5_ready;

    // AXI
    logic [31:0] m_axi_araddr, m_axi_awaddr;
    logic [7:0]  m_axi_arlen, m_axi_awlen;
    logic [2:0]  m_axi_arsize, m_axi_awsize;
    logic [1:0]  m_axi_arburst, m_axi_awburst;
    logic        m_axi_arvalid, m_axi_arready;
    logic        m_axi_awvalid, m_axi_awready;
    logic [63:0] m_axi_rdata, m_axi_wdata;
    logic [1:0]  m_axi_rresp, m_axi_bresp;
    logic        m_axi_rlast, m_axi_rvalid, m_axi_rready;
    logic [7:0]  m_axi_wstrb;
    logic        m_axi_wlast, m_axi_wvalid, m_axi_wready;
    logic        m_axi_bvalid, m_axi_bready;

    ddram_mux u_ddram_mux (
        .DDRAM_CLK(clk),
        .DDRAM_BUSY(ddram_busy), .DDRAM_BURSTCNT(ddram_burstcnt),
        .DDRAM_ADDR(ddram_addr), .DDRAM_DOUT(ddram_dout),
        .DDRAM_DOUT_READY(ddram_dout_ready),
        .DDRAM_RD(ddram_rd), .DDRAM_DIN(ddram_din),
        .DDRAM_BE(ddram_be), .DDRAM_WE(ddram_we),
        .ch1_addr(ch1_addr), .ch1_dout(ch1_dout), .ch1_din(ch1_din),
        .ch1_req(ch1_req), .ch1_rnw(ch1_rnw), .ch1_ready(ch1_ready),
        .ch2_addr(27'd0), .ch2_din(32'd0), .ch2_req(1'b0), .ch2_rnw(1'b1),
        .ch2_dout(), .ch2_ready(),
        .ch3_addr(25'd0), .ch3_din(16'd0), .ch3_req(1'b0), .ch3_rnw(1'b1),
        .ch3_dout(), .ch3_ready(),
        .ch4_addr(27'd0), .ch4_din(64'd0), .ch4_req(1'b0), .ch4_rnw(1'b1),
        .ch4_be(8'd0), .ch4_dout(), .ch4_ready(),
        .ch5_addr(ch5_addr), .ch5_din(ch5_din),
        .ch5_req(ch5_req), .ch5_rnw(ch5_rnw), .ch5_ready(ch5_ready),
        .ch5_dout(ch5_dout)
    );

    ddr_axi_backend_sv #(.G_DDR_BASE(32'h1000_0000)) u_backend (
        .clk(clk), .rst_n(rst_n),
        .DDRAM_BUSY(ddram_busy), .DDRAM_BURSTCNT(ddram_burstcnt),
        .DDRAM_ADDR(ddram_addr), .DDRAM_DOUT(ddram_dout),
        .DDRAM_DOUT_READY(ddram_dout_ready),
        .DDRAM_RD(ddram_rd), .DDRAM_DIN(ddram_din),
        .DDRAM_BE(ddram_be), .DDRAM_WE(ddram_we),
        .M_AXI_ARADDR(m_axi_araddr), .M_AXI_ARLEN(m_axi_arlen),
        .M_AXI_ARSIZE(m_axi_arsize), .M_AXI_ARBURST(m_axi_arburst),
        .M_AXI_ARVALID(m_axi_arvalid), .M_AXI_ARREADY(m_axi_arready),
        .M_AXI_RDATA(m_axi_rdata), .M_AXI_RRESP(m_axi_rresp),
        .M_AXI_RLAST(m_axi_rlast), .M_AXI_RVALID(m_axi_rvalid),
        .M_AXI_RREADY(m_axi_rready),
        .M_AXI_AWADDR(m_axi_awaddr), .M_AXI_AWLEN(m_axi_awlen),
        .M_AXI_AWSIZE(m_axi_awsize), .M_AXI_AWBURST(m_axi_awburst),
        .M_AXI_AWVALID(m_axi_awvalid), .M_AXI_AWREADY(m_axi_awready),
        .M_AXI_WDATA(m_axi_wdata), .M_AXI_WSTRB(m_axi_wstrb),
        .M_AXI_WLAST(m_axi_wlast), .M_AXI_WVALID(m_axi_wvalid),
        .M_AXI_WREADY(m_axi_wready),
        .M_AXI_BRESP(m_axi_bresp), .M_AXI_BVALID(m_axi_bvalid),
        .M_AXI_BREADY(m_axi_bready)
    );

    axi_mem_model #(
        .MEM_SIZE_BYTES(65536),
        .ROM_HEX_FILE("../rom_data/rom_first_4k.hex")
    ) u_axi_mem (
        .clk(clk), .rst_n(rst_n),
        .S_AXI_ARADDR(m_axi_araddr), .S_AXI_ARLEN(m_axi_arlen),
        .S_AXI_ARSIZE(m_axi_arsize), .S_AXI_ARBURST(m_axi_arburst),
        .S_AXI_ARVALID(m_axi_arvalid), .S_AXI_ARREADY(m_axi_arready),
        .S_AXI_RDATA(m_axi_rdata), .S_AXI_RRESP(m_axi_rresp),
        .S_AXI_RLAST(m_axi_rlast), .S_AXI_RVALID(m_axi_rvalid),
        .S_AXI_RREADY(m_axi_rready),
        .S_AXI_AWADDR(m_axi_awaddr), .S_AXI_AWLEN(m_axi_awlen),
        .S_AXI_AWSIZE(m_axi_awsize), .S_AXI_AWBURST(m_axi_awburst),
        .S_AXI_AWVALID(m_axi_awvalid), .S_AXI_AWREADY(m_axi_awready),
        .S_AXI_WDATA(m_axi_wdata), .S_AXI_WSTRB(m_axi_wstrb),
        .S_AXI_WLAST(m_axi_wlast), .S_AXI_WVALID(m_axi_wvalid),
        .S_AXI_WREADY(m_axi_wready),
        .S_AXI_BRESP(m_axi_bresp), .S_AXI_BVALID(m_axi_bvalid),
        .S_AXI_BREADY(m_axi_bready)
    );

    // 统计
    integer ch1_req_cnt = 0, ch1_done_cnt = 0;
    integer ch5_req_cnt = 0, ch5_done_cnt = 0;
    integer ch1_max_latency = 0, ch5_max_latency = 0;
    integer ch1_lat_start, ch5_lat_start;
    integer total_pass = 0, total_fail = 0;

    // ch1 延迟追踪
    always @(posedge clk) begin
        if (ch1_req && !ch1_ready) ch1_lat_start <= $time;
        if (ch1_ready) begin
            integer lat;
            lat = ($time - ch1_lat_start) / 10;
            if (lat > ch1_max_latency) ch1_max_latency = lat;
        end
    end

    // ch5 延迟追踪
    always @(posedge clk) begin
        if (ch5_req && !ch5_ready) ch5_lat_start <= $time;
        if (ch5_ready) begin
            integer lat;
            lat = ($time - ch5_lat_start) / 10;
            if (lat > ch5_max_latency) ch5_max_latency = lat;
        end
    end

    task automatic wait_clks(input int n);
        repeat(n) @(posedge clk);
    endtask

    // === ch1 ROM 读驱动 (并发进程) ===
    logic        ch1_test_active = 0;
    logic [24:0] ch1_test_addr;
    logic [63:0] ch1_test_got;
    logic        ch1_test_done_flag;

    always_comb begin
        ch1_addr = {1'b0, ch1_test_addr, 1'b0};
        ch1_din  = 16'd0;
        ch1_rnw  = 1'b1;
        ch1_req  = ch1_test_active;
    end

    // === ch5 FB 写驱动 (并发进程) ===
    logic        ch5_test_active = 0;
    logic [27:1] ch5_test_addr;
    logic [63:0] ch5_test_data;

    always_comb begin
        ch5_addr = ch5_test_addr;
        ch5_din  = ch5_test_data;
        ch5_rnw  = 1'b0;
        ch5_req  = ch5_test_active;
    end

    // ch1 读取: 发请求并等 ready
    task automatic ch1_read(input [24:0] addr, output [63:0] data);
        @(posedge clk);
        ch1_test_addr <= addr;
        ch1_test_active <= 1'b1;
        ch1_req_cnt++;
        @(posedge clk);
        while (!ch1_ready) @(posedge clk);
        data = ch1_dout;
        ch1_test_active <= 1'b0;
        ch1_done_cnt++;
        @(posedge clk);
    endtask

    // ch5 写入: 发请求并等 ready
    task automatic ch5_write(input [27:1] addr, input [63:0] data);
        @(posedge clk);
        ch5_test_addr <= addr;
        ch5_test_data <= data;
        ch5_test_active <= 1'b1;
        ch5_req_cnt++;
        @(posedge clk);
        while (!ch5_ready) @(posedge clk);
        ch5_test_active <= 1'b0;
        ch5_done_cnt++;
        @(posedge clk);
    endtask

    // === 并发测试 ===
    logic ch1_burst_done = 0;
    logic ch5_burst_done = 0;
    logic [63:0] ch1_results [0:7];

    // ch1 连续读 8 个 ROM 地址
    initial begin : ch1_burst_proc
        wait (rst_n === 1'b1);
        wait_clks(20);

        // 等待同步起跑
        @(posedge clk);

        $display("[CH1] 开始连续 8 次 ROM 读...");
        for (int i = 0; i < 8; i++) begin
            logic [63:0] d;
            ch1_read(25'h30000 + i*2, d);  // 每次读 8 字节 (2 DWORD)
            ch1_results[i] = d;
            $display("[CH1] read[%0d] addr=0x%07X dout=0x%016X", i, 25'h30000 + i*2, d);
        end
        $display("[CH1] 完成");
        ch1_burst_done = 1;
    end

    // ch5 连续写 8 个 FB 地址 (并发!)
    initial begin : ch5_burst_proc
        wait (rst_n === 1'b1);
        wait_clks(20);

        // 同时开始
        @(posedge clk);

        $display("[CH5] 开始连续 8 次 FB 写...");
        for (int i = 0; i < 8; i++) begin
            // FB 地址区域 (对应 DDR 0x18xxxxxx)
            ch5_write({1'b0, 22'h200000 + i[21:0], 4'b0000}, {32'hFBDA7A00, 16'h0000, 16'(i)});
            $display("[CH5] write[%0d] done", i);
        end
        $display("[CH5] 完成");
        ch5_burst_done = 1;
    end

    // === 主控制 ===
    initial begin
        $display("\n=============================================");
        $display(" DDR 多通道仲裁并发仿真 (ch1 ROM读 + ch5 FB写)");
        $display("=============================================\n");

        rst_n = 0;
        wait_clks(10);
        rst_n = 1;

        // 等待两个 burst 都完成
        wait (ch1_burst_done && ch5_burst_done);
        wait_clks(50);

        // 验证 ch1 读取数据
        $display("\n----- ch1 数据验证 -----");

        // ROM DWORD 0,1 → 0xEA000032, 0x51AEFF24
        if (ch1_results[0][31:0] === 32'hEA000032) begin
            $display("[CHECK] ch1_results[0] LOW = 0x%08X PASS", ch1_results[0][31:0]);
            total_pass++;
        end else begin
            $display("[CHECK] ch1_results[0] LOW = 0x%08X FAIL (expect 0xEA000032)", ch1_results[0][31:0]);
            total_fail++;
        end

        if (ch1_results[0][63:32] === 32'h51AEFF24) begin
            $display("[CHECK] ch1_results[0] HIGH = 0x%08X PASS", ch1_results[0][63:32]);
            total_pass++;
        end else begin
            $display("[CHECK] ch1_results[0] HIGH = 0x%08X FAIL (expect 0x51AEFF24)", ch1_results[0][63:32]);
            total_fail++;
        end

        // ROM DWORD 2,3 → 0x21A29A69, 0x0A82843D
        if (ch1_results[1][31:0] === 32'h21A29A69) begin
            $display("[CHECK] ch1_results[1] LOW = 0x%08X PASS", ch1_results[1][31:0]);
            total_pass++;
        end else begin
            $display("[CHECK] ch1_results[1] LOW = 0x%08X FAIL (expect 0x21A29A69)", ch1_results[1][31:0]);
            total_fail++;
        end

        if (ch1_results[1][63:32] === 32'h0A82843D) begin
            $display("[CHECK] ch1_results[1] HIGH = 0x%08X PASS", ch1_results[1][63:32]);
            total_pass++;
        end else begin
            $display("[CHECK] ch1_results[1] HIGH = 0x%08X FAIL (expect 0x0A82843D)", ch1_results[1][63:32]);
            total_fail++;
        end

        $display("\n=============================================");
        $display(" ch1: req=%0d done=%0d max_lat=%0d cyc", ch1_req_cnt, ch1_done_cnt, ch1_max_latency);
        $display(" ch5: req=%0d done=%0d max_lat=%0d cyc", ch5_req_cnt, ch5_done_cnt, ch5_max_latency);
        $display(" PASS=%0d FAIL=%0d", total_pass, total_fail);
        if (ch1_done_cnt == ch1_req_cnt && ch5_done_cnt == ch5_req_cnt && total_fail == 0)
            $display(" 全部通道完成, 无饥饿/死锁, 数据正确!");
        else
            $display(" *** 存在问题! ***");
        $display("=============================================");

        wait_clks(10);
        $finish;
    end

    initial begin #300_000; $display("TIMEOUT - 可能死锁!"); $finish; end

endmodule
