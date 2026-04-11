// tb_ddr_race_trigger.sv: 精确触发 write→read 竞态
// 策略：ch1_req 持续保持高（模拟 CPU 不断取指），ch5 周期性写入
// 在无守卫版本中，当 ch5 写完成→mux 回到 state 0→处理 pending ch1 读时，
// 如果紧接着又有 ch5 写请求，mux 会先处理 ch1（高优先级）读。
// 但 ch5 写回到 state 0 后，ch1 可能在 ram_write 生效周期趁 BUSY=0 发起读。
// 分别测试：无守卫版（应触发 bug）vs 有守卫版（应安全）
`timescale 1ns / 1ps

module tb_ddr_race_trigger;
    logic clk = 0;
    always #5 clk = ~clk;
    logic rst_n;

    // === 两套信号：分别给 noguard 和 guarded mux ===
    // --- noguard ---
    logic        ng_busy, ng_dout_ready, ng_rd, ng_we;
    logic [7:0]  ng_burstcnt, ng_be;
    logic [28:0] ng_addr;
    logic [63:0] ng_dout, ng_din;
    // --- guarded ---
    logic        gd_busy, gd_dout_ready, gd_rd, gd_we;
    logic [7:0]  gd_burstcnt, gd_be;
    logic [28:0] gd_addr;
    logic [63:0] gd_dout, gd_din;
    // AXI (两套)
    logic [31:0] ng_axi_awaddr, ng_axi_araddr, gd_axi_awaddr, gd_axi_araddr;
    logic [7:0]  ng_axi_awlen, ng_axi_arlen, gd_axi_awlen, gd_axi_arlen;
    logic [2:0]  ng_axi_awsize, ng_axi_arsize, gd_axi_awsize, gd_axi_arsize;
    logic [1:0]  ng_axi_awburst, ng_axi_arburst, gd_axi_awburst, gd_axi_arburst;
    logic        ng_axi_awvalid, ng_axi_awready, ng_axi_arvalid, ng_axi_arready;
    logic        gd_axi_awvalid, gd_axi_awready, gd_axi_arvalid, gd_axi_arready;
    logic [63:0] ng_axi_wdata, ng_axi_rdata, gd_axi_wdata, gd_axi_rdata;
    logic [7:0]  ng_axi_wstrb, gd_axi_wstrb;
    logic        ng_axi_wlast, ng_axi_wvalid, ng_axi_wready;
    logic        gd_axi_wlast, gd_axi_wvalid, gd_axi_wready;
    logic [1:0]  ng_axi_bresp, ng_axi_rresp, gd_axi_bresp, gd_axi_rresp;
    logic        ng_axi_bvalid, ng_axi_bready, ng_axi_rlast, ng_axi_rvalid, ng_axi_rready;
    logic        gd_axi_bvalid, gd_axi_bready, gd_axi_rlast, gd_axi_rvalid, gd_axi_rready;
    logic [31:0] ng_err, gd_err; logic ng_errp, gd_errp;

    // 共享通道信号
    logic [27:1] ch1_addr;
    logic        ch1_req, ch1_rnw;
    logic [27:1] ch5_addr;
    logic [63:0] ch5_din;
    logic        ch5_req, ch5_rnw;

    logic [63:0] ng_ch1_dout, gd_ch1_dout;
    logic        ng_ch1_ready, gd_ch1_ready;
    logic        ng_ch5_ready, gd_ch5_ready;

    // 未用通道固定值
    logic [27:1] z27 = '0; logic [25:1] z25 = '0;
    logic [15:0] z16 = '0; logic [31:0] z32 = '0; logic [63:0] z64 = '0;
    logic [7:0]  z8 = '0;

    // 检测器
    int ng_rd_busy = 0, gd_rd_busy = 0;
    int ng_we_busy = 0, gd_we_busy = 0;
    always_ff @(posedge clk) if (rst_n) begin
        if (ng_rd && ng_busy) begin
            ng_rd_busy++;
            if (ng_rd_busy <= 3)
                $display("[NG BUG] @%0t RD+BUSY! be=%0d mux=%0d rw=%b rr=%b",
                         $time, ng_be_inst.state, ng_mux.state, ng_mux.ram_write, ng_mux.ram_read);
        end
        if (gd_rd && gd_busy) begin
            gd_rd_busy++;
            if (gd_rd_busy <= 3)
                $display("[GD BUG] @%0t RD+BUSY!", $time);
        end
    end

    // 死锁检测：如果 mux 在 state=1 超过 10000 周期
    int ng_stuck_cnt = 0, gd_stuck_cnt = 0;
    logic ng_deadlock = 0, gd_deadlock = 0;
    always_ff @(posedge clk) if (rst_n) begin
        if (ng_mux.state == 2'd1) ng_stuck_cnt <= ng_stuck_cnt + 1;
        else ng_stuck_cnt <= 0;
        if (ng_stuck_cnt > 10000 && !ng_deadlock) begin
            ng_deadlock <= 1;
            $display("[NG DEADLOCK] @%0t mux stuck state=1, be=%0d busy=%b",
                     $time, ng_be_inst.state, ng_busy);
        end
        if (gd_mux.state == 2'd1) gd_stuck_cnt <= gd_stuck_cnt + 1;
        else gd_stuck_cnt <= 0;
        if (gd_stuck_cnt > 10000 && !gd_deadlock) begin
            gd_deadlock <= 1;
            $display("[GD DEADLOCK] @%0t mux stuck state=1", $time);
        end
    end

    // ========== 无守卫 MUX + Backend + AXI ==========
    ddram_mux_noguard ng_mux (
        .DDRAM_CLK(clk), .DDRAM_BUSY(ng_busy),
        .DDRAM_BURSTCNT(ng_burstcnt), .DDRAM_ADDR(ng_addr),
        .DDRAM_DOUT(ng_dout), .DDRAM_DOUT_READY(ng_dout_ready),
        .DDRAM_RD(ng_rd), .DDRAM_DIN(ng_din), .DDRAM_BE(ng_be), .DDRAM_WE(ng_we),
        .ch1_addr(ch1_addr), .ch1_dout(ng_ch1_dout), .ch1_din(z16),
        .ch1_req(ch1_req), .ch1_rnw(ch1_rnw), .ch1_ready(ng_ch1_ready),
        .ch2_addr(z27), .ch2_dout(), .ch2_din(z32), .ch2_req(1'b0), .ch2_rnw(1'b1), .ch2_ready(),
        .ch3_addr(z25), .ch3_dout(), .ch3_din(z16), .ch3_req(1'b0), .ch3_rnw(1'b1), .ch3_ready(),
        .ch4_addr(z27), .ch4_dout(), .ch4_din(z64), .ch4_req(1'b0), .ch4_rnw(1'b1), .ch4_be(z8), .ch4_ready(),
        .ch5_addr(ch5_addr), .ch5_dout(), .ch5_din(ch5_din),
        .ch5_req(ch5_req), .ch5_rnw(ch5_rnw), .ch5_ready(ng_ch5_ready)
    );
    ddr_axi_backend_sv #(.G_DDR_BASE(32'h1000_0000)) ng_be_inst (
        .clk(clk), .rst_n(rst_n),
        .DDRAM_BUSY(ng_busy), .DDRAM_BURSTCNT(ng_burstcnt), .DDRAM_ADDR(ng_addr),
        .DDRAM_DOUT(ng_dout), .DDRAM_DOUT_READY(ng_dout_ready),
        .DDRAM_RD(ng_rd), .DDRAM_DIN(ng_din), .DDRAM_BE(ng_be), .DDRAM_WE(ng_we),
        .M_AXI_AWADDR(ng_axi_awaddr), .M_AXI_AWLEN(ng_axi_awlen),
        .M_AXI_AWSIZE(ng_axi_awsize), .M_AXI_AWBURST(ng_axi_awburst),
        .M_AXI_AWVALID(ng_axi_awvalid), .M_AXI_AWREADY(ng_axi_awready),
        .M_AXI_WDATA(ng_axi_wdata), .M_AXI_WSTRB(ng_axi_wstrb),
        .M_AXI_WLAST(ng_axi_wlast), .M_AXI_WVALID(ng_axi_wvalid), .M_AXI_WREADY(ng_axi_wready),
        .M_AXI_BRESP(ng_axi_bresp), .M_AXI_BVALID(ng_axi_bvalid), .M_AXI_BREADY(ng_axi_bready),
        .M_AXI_ARADDR(ng_axi_araddr), .M_AXI_ARLEN(ng_axi_arlen),
        .M_AXI_ARSIZE(ng_axi_arsize), .M_AXI_ARBURST(ng_axi_arburst),
        .M_AXI_ARVALID(ng_axi_arvalid), .M_AXI_ARREADY(ng_axi_arready),
        .M_AXI_RDATA(ng_axi_rdata), .M_AXI_RRESP(ng_axi_rresp),
        .M_AXI_RLAST(ng_axi_rlast), .M_AXI_RVALID(ng_axi_rvalid), .M_AXI_RREADY(ng_axi_rready),
        .ERR_VEC(ng_err), .ERR_PULSE(ng_errp)
    );
    axi_mem_model_random #(
        .MEM_SIZE_BYTES(1048576), .SEED(32'hAAAA_5555),
        .MIN_AR_DELAY(1), .MAX_AR_DELAY(3), .MIN_R_DELAY(2), .MAX_R_DELAY(8),
        .MIN_AW_DELAY(1), .MAX_AW_DELAY(2), .MIN_B_DELAY(1), .MAX_B_DELAY(4)
    ) ng_axi (
        .clk(clk), .rst_n(rst_n),
        .S_AXI_ARADDR(ng_axi_araddr), .S_AXI_ARLEN(ng_axi_arlen),
        .S_AXI_ARSIZE(ng_axi_arsize), .S_AXI_ARBURST(ng_axi_arburst),
        .S_AXI_ARVALID(ng_axi_arvalid), .S_AXI_ARREADY(ng_axi_arready),
        .S_AXI_RDATA(ng_axi_rdata), .S_AXI_RRESP(ng_axi_rresp),
        .S_AXI_RLAST(ng_axi_rlast), .S_AXI_RVALID(ng_axi_rvalid), .S_AXI_RREADY(ng_axi_rready),
        .S_AXI_AWADDR(ng_axi_awaddr), .S_AXI_AWLEN(ng_axi_awlen),
        .S_AXI_AWSIZE(ng_axi_awsize), .S_AXI_AWBURST(ng_axi_awburst),
        .S_AXI_AWVALID(ng_axi_awvalid), .S_AXI_AWREADY(ng_axi_awready),
        .S_AXI_WDATA(ng_axi_wdata), .S_AXI_WSTRB(ng_axi_wstrb),
        .S_AXI_WLAST(ng_axi_wlast), .S_AXI_WVALID(ng_axi_wvalid), .S_AXI_WREADY(ng_axi_wready),
        .S_AXI_BRESP(ng_axi_bresp), .S_AXI_BVALID(ng_axi_bvalid), .S_AXI_BREADY(ng_axi_bready)
    );

    // ========== 有守卫 MUX + Backend + AXI ==========
    ddram_mux gd_mux (
        .DDRAM_CLK(clk), .DDRAM_BUSY(gd_busy),
        .DDRAM_BURSTCNT(gd_burstcnt), .DDRAM_ADDR(gd_addr),
        .DDRAM_DOUT(gd_dout), .DDRAM_DOUT_READY(gd_dout_ready),
        .DDRAM_RD(gd_rd), .DDRAM_DIN(gd_din), .DDRAM_BE(gd_be), .DDRAM_WE(gd_we),
        .ch1_addr(ch1_addr), .ch1_dout(gd_ch1_dout), .ch1_din(z16),
        .ch1_req(ch1_req), .ch1_rnw(ch1_rnw), .ch1_ready(gd_ch1_ready),
        .ch2_addr(z27), .ch2_dout(), .ch2_din(z32), .ch2_req(1'b0), .ch2_rnw(1'b1), .ch2_ready(),
        .ch3_addr(z25), .ch3_dout(), .ch3_din(z16), .ch3_req(1'b0), .ch3_rnw(1'b1), .ch3_ready(),
        .ch4_addr(z27), .ch4_dout(), .ch4_din(z64), .ch4_req(1'b0), .ch4_rnw(1'b1), .ch4_be(z8), .ch4_ready(),
        .ch5_addr(ch5_addr), .ch5_dout(), .ch5_din(ch5_din),
        .ch5_req(ch5_req), .ch5_rnw(ch5_rnw), .ch5_ready(gd_ch5_ready)
    );
    ddr_axi_backend_sv #(.G_DDR_BASE(32'h1000_0000)) gd_be_inst (
        .clk(clk), .rst_n(rst_n),
        .DDRAM_BUSY(gd_busy), .DDRAM_BURSTCNT(gd_burstcnt), .DDRAM_ADDR(gd_addr),
        .DDRAM_DOUT(gd_dout), .DDRAM_DOUT_READY(gd_dout_ready),
        .DDRAM_RD(gd_rd), .DDRAM_DIN(gd_din), .DDRAM_BE(gd_be), .DDRAM_WE(gd_we),
        .M_AXI_AWADDR(gd_axi_awaddr), .M_AXI_AWLEN(gd_axi_awlen),
        .M_AXI_AWSIZE(gd_axi_awsize), .M_AXI_AWBURST(gd_axi_awburst),
        .M_AXI_AWVALID(gd_axi_awvalid), .M_AXI_AWREADY(gd_axi_awready),
        .M_AXI_WDATA(gd_axi_wdata), .M_AXI_WSTRB(gd_axi_wstrb),
        .M_AXI_WLAST(gd_axi_wlast), .M_AXI_WVALID(gd_axi_wvalid), .M_AXI_WREADY(gd_axi_wready),
        .M_AXI_BRESP(gd_axi_bresp), .M_AXI_BVALID(gd_axi_bvalid), .M_AXI_BREADY(gd_axi_bready),
        .M_AXI_ARADDR(gd_axi_araddr), .M_AXI_ARLEN(gd_axi_arlen),
        .M_AXI_ARSIZE(gd_axi_arsize), .M_AXI_ARBURST(gd_axi_arburst),
        .M_AXI_ARVALID(gd_axi_arvalid), .M_AXI_ARREADY(gd_axi_arready),
        .M_AXI_RDATA(gd_axi_rdata), .M_AXI_RRESP(gd_axi_rresp),
        .M_AXI_RLAST(gd_axi_rlast), .M_AXI_RVALID(gd_axi_rvalid), .M_AXI_RREADY(gd_axi_rready),
        .ERR_VEC(gd_err), .ERR_PULSE(gd_errp)
    );
    axi_mem_model_random #(
        .MEM_SIZE_BYTES(1048576), .SEED(32'hAAAA_5555),
        .MIN_AR_DELAY(1), .MAX_AR_DELAY(3), .MIN_R_DELAY(2), .MAX_R_DELAY(8),
        .MIN_AW_DELAY(1), .MAX_AW_DELAY(2), .MIN_B_DELAY(1), .MAX_B_DELAY(4)
    ) gd_axi (
        .clk(clk), .rst_n(rst_n),
        .S_AXI_ARADDR(gd_axi_araddr), .S_AXI_ARLEN(gd_axi_arlen),
        .S_AXI_ARSIZE(gd_axi_arsize), .S_AXI_ARBURST(gd_axi_arburst),
        .S_AXI_ARVALID(gd_axi_arvalid), .S_AXI_ARREADY(gd_axi_arready),
        .S_AXI_RDATA(gd_axi_rdata), .S_AXI_RRESP(gd_axi_rresp),
        .S_AXI_RLAST(gd_axi_rlast), .S_AXI_RVALID(gd_axi_rvalid), .S_AXI_RREADY(gd_axi_rready),
        .S_AXI_AWADDR(gd_axi_awaddr), .S_AXI_AWLEN(gd_axi_awlen),
        .S_AXI_AWSIZE(gd_axi_awsize), .S_AXI_AWBURST(gd_axi_awburst),
        .S_AXI_AWVALID(gd_axi_awvalid), .S_AXI_AWREADY(gd_axi_awready),
        .S_AXI_WDATA(gd_axi_wdata), .S_AXI_WSTRB(gd_axi_wstrb),
        .S_AXI_WLAST(gd_axi_wlast), .S_AXI_WVALID(gd_axi_wvalid), .S_AXI_WREADY(gd_axi_wready),
        .S_AXI_BRESP(gd_axi_bresp), .S_AXI_BVALID(gd_axi_bvalid), .S_AXI_BREADY(gd_axi_bready)
    );

    // ========== 测试激励：ch1 持续请求 + ch5 周期性写 ==========
    int ch1_rd_cnt = 0;
    initial begin
        $display("=== tb_ddr_race_trigger: ch1持续读 + ch5周期写 ===");
        $display("=== 对比 noguard vs guarded ===");
        rst_n = 0;
        ch1_req = 0; ch1_rnw = 1; ch1_addr = '0;
        ch5_req = 0; ch5_rnw = 0; ch5_addr = '0; ch5_din = '0;
        repeat (10) @(posedge clk);
        rst_n = 1;
        repeat (5) @(posedge clk);

        // ch1: 持续请求不同地址的读（模拟 CPU 连续取指）
        // ch5: 每隔几个 ch1 读就插入一次写（模拟 framebuffer 写）
        fork
            // ch1 驱动：持续发脉冲读，一完成就发下一个
            begin : drv_ch1
                for (int i = 0; i < 2000; i++) begin
                    ch1_addr <= 27'h060000 + i * 4; // 每次不同 cache line
                    ch1_rnw  <= 1;
                    ch1_req  <= 1;
                    @(posedge clk);
                    ch1_req  <= 0;
                    // 等待任意一个 mux 的 ready（两个 mux 接收相同输入）
                    begin : wch1
                        for (int w = 0; w < 20000; w++) begin
                            @(posedge clk);
                            if (ng_ch1_ready || gd_ch1_ready) begin
                                ch1_rd_cnt++;
                                disable wch1;
                            end
                            if (ng_deadlock || gd_deadlock) disable wch1;
                        end
                    end
                    if (ng_deadlock || gd_deadlock) disable drv_ch1;
                end
            end
            // ch5 驱动：每 3 个 ch1 读后插一次写
            begin : drv_ch5
                int wr_cnt = 0;
                forever begin
                    // 等待 3 个 ch1 读完成
                    wait(ch1_rd_cnt >= (wr_cnt + 1) * 3 || ng_deadlock || gd_deadlock);
                    if (ng_deadlock || gd_deadlock) disable drv_ch5;
                    ch5_addr <= 27'h400000 + wr_cnt * 4;
                    ch5_din  <= {32'hFB000000 + wr_cnt, 32'hFB000000 + wr_cnt};
                    ch5_rnw  <= 0;
                    ch5_req  <= 1;
                    @(posedge clk);
                    ch5_req  <= 0;
                    // 等待任意 mux 的 ch5_ready
                    begin : wch5
                        for (int w = 0; w < 20000; w++) begin
                            @(posedge clk);
                            if (ng_ch5_ready || gd_ch5_ready) disable wch5;
                            if (ng_deadlock || gd_deadlock) disable wch5;
                        end
                    end
                    wr_cnt++;
                    if (wr_cnt >= 500 || ng_deadlock || gd_deadlock) disable drv_ch5;
                end
            end
        join_any
        disable fork;

        repeat (100) @(posedge clk);
        $display("\n=== 结果 ===");
        $display("ch1 完成读: %0d", ch1_rd_cnt);
        $display("NOGUARD: rd+busy=%0d we+busy=%0d deadlock=%0b", ng_rd_busy, ng_we_busy, ng_deadlock);
        $display("GUARDED: rd+busy=%0d we+busy=%0d deadlock=%0b", gd_rd_busy, gd_we_busy, gd_deadlock);
        if (ng_deadlock && !gd_deadlock)
            $display("*** 确认：!ram_write 守卫是必须的！无守卫版死锁！***");
        else if (!ng_deadlock && !gd_deadlock)
            $display("*** 两版都没死锁 — 竞态窗口可能未命中 ***");
        else
            $display("*** 意外结果 ***");
        $finish;
    end

    initial begin #100000000; $display("[TIMEOUT] ng_dl=%b gd_dl=%b", ng_deadlock, gd_deadlock); $finish; end
endmodule
