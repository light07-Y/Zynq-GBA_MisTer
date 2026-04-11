// tb_ddr_level_req.sv: 电平式 ch_req（保持高直到 ready）+ 极端 AXI 延迟
// 精确模拟 GBA 核心的真实请求协议
`timescale 1ns / 1ps

module tb_ddr_level_req;
    logic clk = 0;
    always #5 clk = ~clk;
    logic rst_n;

    // ddram_mux ↔ backend
    logic        ddram_busy, ddram_dout_ready, ddram_rd, ddram_we;
    logic [7:0]  ddram_burstcnt, ddram_be;
    logic [28:0] ddram_addr;
    logic [63:0] ddram_dout, ddram_din;
    // AXI
    logic [31:0] axi_awaddr, axi_araddr;
    logic [7:0]  axi_awlen, axi_arlen;
    logic [2:0]  axi_awsize, axi_arsize;
    logic [1:0]  axi_awburst, axi_arburst;
    logic        axi_awvalid, axi_awready, axi_arvalid, axi_arready;
    logic [63:0] axi_wdata, axi_rdata;
    logic [7:0]  axi_wstrb;
    logic        axi_wlast, axi_wvalid, axi_wready;
    logic [1:0]  axi_bresp, axi_rresp;
    logic        axi_bvalid, axi_bready, axi_rlast, axi_rvalid, axi_rready;
    logic [31:0] err_vec; logic err_pulse;

    // 通道信号
    logic [27:1] ch1_addr = '0;  logic [15:0] ch1_din = '0;
    logic        ch1_req = 0, ch1_rnw = 1;
    logic [63:0] ch1_dout; logic ch1_ready;
    logic [27:1] ch2_addr = '0;  logic [31:0] ch2_din = '0;
    logic        ch2_req = 0, ch2_rnw = 1;
    logic [31:0] ch2_dout; logic ch2_ready;
    logic [25:1] ch3_addr = '0;  logic [15:0] ch3_din = '0;
    logic        ch3_req = 0, ch3_rnw = 1;
    logic [15:0] ch3_dout; logic ch3_ready;
    logic [27:1] ch4_addr = '0;  logic [63:0] ch4_din = '0;
    logic        ch4_req = 0, ch4_rnw = 1; logic [7:0] ch4_be = '0;
    logic [63:0] ch4_dout; logic ch4_ready;
    logic [27:1] ch5_addr = '0;  logic [63:0] ch5_din = '0;
    logic        ch5_req = 0, ch5_rnw = 0;
    logic [63:0] ch5_dout; logic ch5_ready;

    // SVA
    int a1_fail = 0;
    always_ff @(posedge clk) if (rst_n && ddram_rd && ddram_busy) begin
        a1_fail++;
        if (a1_fail <= 3)
            $display("[BUG] @%0t RD=1 BUSY=1! be_st=%0d mux_st=%0d rw=%b rr=%b ch_rq=%05b",
                     $time, u_backend.state, u_mux.state,
                     u_mux.ram_write, u_mux.ram_read, u_mux.ch_rq);
    end
    int a2_fail = 0;
    always_ff @(posedge clk) if (rst_n && ddram_we && ddram_busy) begin
        a2_fail++;
        if (a2_fail <= 3)
            $display("[BUG] @%0t WE=1 BUSY=1!", $time);
    end

    // DUT
    ddram_mux u_mux (
        .DDRAM_CLK(clk), .DDRAM_BUSY(ddram_busy),
        .DDRAM_BURSTCNT(ddram_burstcnt), .DDRAM_ADDR(ddram_addr),
        .DDRAM_DOUT(ddram_dout), .DDRAM_DOUT_READY(ddram_dout_ready),
        .DDRAM_RD(ddram_rd), .DDRAM_DIN(ddram_din),
        .DDRAM_BE(ddram_be), .DDRAM_WE(ddram_we),
        .ch1_addr(ch1_addr), .ch1_dout(ch1_dout), .ch1_din(ch1_din),
        .ch1_req(ch1_req), .ch1_rnw(ch1_rnw), .ch1_ready(ch1_ready),
        .ch2_addr(ch2_addr), .ch2_dout(ch2_dout), .ch2_din(ch2_din),
        .ch2_req(ch2_req), .ch2_rnw(ch2_rnw), .ch2_ready(ch2_ready),
        .ch3_addr(ch3_addr), .ch3_dout(ch3_dout), .ch3_din(ch3_din),
        .ch3_req(ch3_req), .ch3_rnw(ch3_rnw), .ch3_ready(ch3_ready),
        .ch4_addr(ch4_addr), .ch4_dout(ch4_dout), .ch4_din(ch4_din),
        .ch4_req(ch4_req), .ch4_rnw(ch4_rnw), .ch4_be(ch4_be), .ch4_ready(ch4_ready),
        .ch5_addr(ch5_addr), .ch5_dout(ch5_dout), .ch5_din(ch5_din),
        .ch5_req(ch5_req), .ch5_rnw(ch5_rnw), .ch5_ready(ch5_ready)
    );
    ddr_axi_backend_sv #(.G_DDR_BASE(32'h1000_0000)) u_backend (
        .clk(clk), .rst_n(rst_n),
        .DDRAM_BUSY(ddram_busy), .DDRAM_BURSTCNT(ddram_burstcnt),
        .DDRAM_ADDR(ddram_addr), .DDRAM_DOUT(ddram_dout),
        .DDRAM_DOUT_READY(ddram_dout_ready),
        .DDRAM_RD(ddram_rd), .DDRAM_DIN(ddram_din),
        .DDRAM_BE(ddram_be), .DDRAM_WE(ddram_we),
        .M_AXI_AWADDR(axi_awaddr), .M_AXI_AWLEN(axi_awlen),
        .M_AXI_AWSIZE(axi_awsize), .M_AXI_AWBURST(axi_awburst),
        .M_AXI_AWVALID(axi_awvalid), .M_AXI_AWREADY(axi_awready),
        .M_AXI_WDATA(axi_wdata), .M_AXI_WSTRB(axi_wstrb),
        .M_AXI_WLAST(axi_wlast), .M_AXI_WVALID(axi_wvalid), .M_AXI_WREADY(axi_wready),
        .M_AXI_BRESP(axi_bresp), .M_AXI_BVALID(axi_bvalid), .M_AXI_BREADY(axi_bready),
        .M_AXI_ARADDR(axi_araddr), .M_AXI_ARLEN(axi_arlen),
        .M_AXI_ARSIZE(axi_arsize), .M_AXI_ARBURST(axi_arburst),
        .M_AXI_ARVALID(axi_arvalid), .M_AXI_ARREADY(axi_arready),
        .M_AXI_RDATA(axi_rdata), .M_AXI_RRESP(axi_rresp),
        .M_AXI_RLAST(axi_rlast), .M_AXI_RVALID(axi_rvalid), .M_AXI_RREADY(axi_rready),
        .ERR_VEC(err_vec), .ERR_PULSE(err_pulse)
    );
    // 极端延迟 AXI 模型: AR 0~50, R 0~80 周期
    axi_mem_model_random #(
        .MEM_SIZE_BYTES(1048576),
        .MIN_AR_DELAY(0), .MAX_AR_DELAY(50),
        .MIN_R_DELAY(0),  .MAX_R_DELAY(80),
        .MIN_AW_DELAY(0), .MAX_AW_DELAY(20),
        .MIN_B_DELAY(0),  .MAX_B_DELAY(30),
        .SEED(32'hBAAD_F00D)
    ) u_axi_mem (
        .clk(clk), .rst_n(rst_n),
        .S_AXI_ARADDR(axi_araddr), .S_AXI_ARLEN(axi_arlen),
        .S_AXI_ARSIZE(axi_arsize), .S_AXI_ARBURST(axi_arburst),
        .S_AXI_ARVALID(axi_arvalid), .S_AXI_ARREADY(axi_arready),
        .S_AXI_RDATA(axi_rdata), .S_AXI_RRESP(axi_rresp),
        .S_AXI_RLAST(axi_rlast), .S_AXI_RVALID(axi_rvalid), .S_AXI_RREADY(axi_rready),
        .S_AXI_AWADDR(axi_awaddr), .S_AXI_AWLEN(axi_awlen),
        .S_AXI_AWSIZE(axi_awsize), .S_AXI_AWBURST(axi_awburst),
        .S_AXI_AWVALID(axi_awvalid), .S_AXI_AWREADY(axi_awready),
        .S_AXI_WDATA(axi_wdata), .S_AXI_WSTRB(axi_wstrb),
        .S_AXI_WLAST(axi_wlast), .S_AXI_WVALID(axi_wvalid), .S_AXI_WREADY(axi_wready),
        .S_AXI_BRESP(axi_bresp), .S_AXI_BVALID(axi_bvalid), .S_AXI_BREADY(axi_bready)
    );

    int total_ops = 0;

    // =========================================================================
    // T1: 电平式 ch1_req — 保持高直到 ready（模拟真实 GBA 核心）
    // 1000 次顺序 ROM 读，每次保持 ch1_req 直到 ch1_ready
    // =========================================================================
    initial begin
        $display("=== tb_ddr_level_req: 电平ch_req + 极端AXI延迟(AR≤50 R≤80) ===");
        rst_n = 0; repeat (10) @(posedge clk); rst_n = 1; repeat (5) @(posedge clk);

        // --- T1: 1000 电平式 ch1 纯读 ---
        $display("\n--- T1: 1000 电平式 ch1 读 (保持 req 直到 ready) ---");
        for (int i = 0; i < 1000; i++) begin
            ch1_addr <= 27'h060000 + i * 4;
            ch1_rnw  <= 1;
            ch1_req  <= 1;  // 持续保持高！
            // 等待 ready
            begin : wait_t1
                for (int w = 0; w < 50000; w++) begin
                    @(posedge clk);
                    if (ch1_ready) begin
                        ch1_req <= 0;  // ready 后才拉低
                        disable wait_t1;
                    end
                end
                $display("[DEADLOCK] T1 #%0d @%0t be=%0d mux=%0d busy=%b",
                         i, $time, u_backend.state, u_mux.state, ddram_busy);
                ch1_req <= 0;
            end
            total_ops++;
            @(posedge clk); // 1 周期间隔
        end
        $display("T1: 1000 done, a1_fail=%0d total_ops=%0d", a1_fail, total_ops);

        repeat (20) @(posedge clk);

        // --- T2: 电平式 ch1 读 + ch5 写交替（ch5_req 也保持到 ready）---
        $display("\n--- T2: 300 电平 ch5写→ch1读 交替 ---");
        for (int i = 0; i < 300; i++) begin
            // ch5 写
            ch5_addr <= 27'h400000 + i*4;
            ch5_din  <= {32'hFB00_0000+i, 32'hFB00_0000+i};
            ch5_rnw  <= 0;
            ch5_req  <= 1;
            begin : wait_t2w
                for (int w = 0; w < 50000; w++) begin
                    @(posedge clk);
                    if (ch5_ready) begin ch5_req <= 0; disable wait_t2w; end
                end
                $display("[DEADLOCK] T2 ch5w #%0d @%0t", i, $time);
                ch5_req <= 0;
            end
            total_ops++;
            // ch1 读
            ch1_addr <= 27'h060000 + (i+2000)*4;
            ch1_rnw  <= 1;
            ch1_req  <= 1;
            begin : wait_t2r
                for (int w = 0; w < 50000; w++) begin
                    @(posedge clk);
                    if (ch1_ready) begin ch1_req <= 0; disable wait_t2r; end
                end
                $display("[DEADLOCK] T2 ch1r #%0d @%0t", i, $time);
                ch1_req <= 0;
            end
            total_ops++;
        end
        $display("T2: 300 pairs done, a1=%0d a2=%0d", a1_fail, a2_fail);

        repeat (20) @(posedge clk);

        // --- T3: 并发电平 ch1 + ch2 读 ---
        $display("\n--- T3: 并发电平 ch1(500)+ch2(500) 读 ---");
        fork
            begin : t3_ch1
                for (int i = 0; i < 500; i++) begin
                    ch1_addr <= 27'h060000 + (i+3000)*4;
                    ch1_rnw  <= 1;
                    ch1_req  <= 1;
                    begin : wt3c1
                        for (int w = 0; w < 50000; w++) begin
                            @(posedge clk);
                            if (ch1_ready) begin ch1_req <= 0; disable wt3c1; end
                        end
                        $display("[DEADLOCK] T3 ch1 #%0d @%0t", i, $time);
                        ch1_req <= 0;
                    end
                    total_ops++;
                    @(posedge clk);
                end
            end
            begin : t3_ch2
                for (int i = 0; i < 500; i++) begin
                    ch2_addr <= 27'h040000 + i*2;
                    ch2_rnw  <= 1;
                    ch2_req  <= 1;
                    begin : wt3c2
                        for (int w = 0; w < 50000; w++) begin
                            @(posedge clk);
                            if (ch2_ready) begin ch2_req <= 0; disable wt3c2; end
                        end
                        $display("[DEADLOCK] T3 ch2 #%0d @%0t", i, $time);
                        ch2_req <= 0;
                    end
                    total_ops++;
                    @(posedge clk);
                end
            end
        join
        $display("T3: done, a1=%0d a2=%0d", a1_fail, a2_fail);

        repeat (20) @(posedge clk);

        // --- T4: 三通道并发电平 (最大压力) ---
        $display("\n--- T4: 三通道并发电平 ch1(200)+ch2(200)+ch5(200) ---");
        fork
            begin : t4_ch1
                for (int i = 0; i < 200; i++) begin
                    ch1_addr <= 27'h060000 + (i+5000)*4;
                    ch1_rnw <= 1; ch1_req <= 1;
                    begin : wt4c1
                        for (int w = 0; w < 50000; w++) begin
                            @(posedge clk);
                            if (ch1_ready) begin ch1_req <= 0; disable wt4c1; end
                        end
                        $display("[DL] T4 ch1 #%0d", i); ch1_req <= 0;
                    end
                    total_ops++;
                end
            end
            begin : t4_ch2
                for (int i = 0; i < 200; i++) begin
                    ch2_addr <= 27'h040000 + (i+1000)*2;
                    ch2_rnw <= 1; ch2_req <= 1;
                    begin : wt4c2
                        for (int w = 0; w < 50000; w++) begin
                            @(posedge clk);
                            if (ch2_ready) begin ch2_req <= 0; disable wt4c2; end
                        end
                        $display("[DL] T4 ch2 #%0d", i); ch2_req <= 0;
                    end
                    total_ops++;
                end
            end
            begin : t4_ch5
                for (int i = 0; i < 200; i++) begin
                    ch5_addr <= 27'h400000 + (i+1000)*4;
                    ch5_din <= 64'hDEAD_0000 + i;
                    ch5_rnw <= 0; ch5_req <= 1;
                    begin : wt4c5
                        for (int w = 0; w < 50000; w++) begin
                            @(posedge clk);
                            if (ch5_ready) begin ch5_req <= 0; disable wt4c5; end
                        end
                        $display("[DL] T4 ch5 #%0d", i); ch5_req <= 0;
                    end
                    total_ops++;
                end
            end
        join
        $display("T4: done, a1=%0d a2=%0d", a1_fail, a2_fail);

        // 汇总
        repeat (50) @(posedge clk);
        $display("\n=== 电平式仿真汇总: total_ops=%0d a1(rd+busy)=%0d a2(we+busy)=%0d ===",
                 total_ops, a1_fail, a2_fail);
        if (a1_fail == 0 && a2_fail == 0)
            $display("*** PASS: 电平式 ch_req + 极端延迟无竞态 ***");
        else
            $display("*** FAIL: 发现竞态！***");
        $finish;
    end

    initial begin #500000000; $display("[TIMEOUT]"); $finish; end
endmodule
