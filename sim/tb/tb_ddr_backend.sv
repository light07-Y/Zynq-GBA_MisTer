// tb_ddr_backend.sv: ddram_mux + ddr_axi_backend 握手竞态仿真
// 目标：复现硬件 CHAIN 观测 — 第2个 DDR 读在 busy=1 时丢失
// 测试场景：
//   T1: 纯背靠背 ch1 读（控制组，应 PASS）
//   T2: ch5 写 → ch1 读 竞态
//   T3: ch2 写 → ch1 读 竞态
//   T4: 变延迟 AXI 响应下的背靠背读

`timescale 1ns / 1ps

module tb_ddr_backend;

    // === 时钟与复位 ===
    logic clk = 0;
    always #5 clk = ~clk; // 100 MHz

    logic rst_n;

    // === ddram_mux <-> ddr_axi_backend 连线 ===
    logic        ddram_busy;
    logic [7:0]  ddram_burstcnt;
    logic [28:0] ddram_addr;
    logic [63:0] ddram_dout;
    logic        ddram_dout_ready;
    logic        ddram_rd;
    logic [63:0] ddram_din;
    logic [7:0]  ddram_be;
    logic        ddram_we;

    // === AXI4 连线 ===
    logic [31:0] axi_awaddr, axi_araddr;
    logic [7:0]  axi_awlen, axi_arlen;
    logic [2:0]  axi_awsize, axi_arsize;
    logic [1:0]  axi_awburst, axi_arburst;
    logic        axi_awvalid, axi_awready;
    logic        axi_arvalid, axi_arready;
    logic [63:0] axi_wdata, axi_rdata;
    logic [7:0]  axi_wstrb;
    logic        axi_wlast, axi_wvalid, axi_wready;
    logic [1:0]  axi_bresp, axi_rresp;
    logic        axi_bvalid, axi_bready;
    logic        axi_rlast, axi_rvalid, axi_rready;
    logic [31:0] err_vec;
    logic        err_pulse;

    // === 通道驱动信号 ===
    logic [27:1] ch1_addr;
    logic [15:0] ch1_din;
    logic        ch1_req, ch1_rnw;
    logic [63:0] ch1_dout;
    logic        ch1_ready;

    logic [27:1] ch2_addr;
    logic [31:0] ch2_din;
    logic        ch2_req, ch2_rnw;
    logic [31:0] ch2_dout;
    logic        ch2_ready;

    logic [25:1] ch3_addr;
    logic [15:0] ch3_din;
    logic        ch3_req, ch3_rnw;
    logic [15:0] ch3_dout;
    logic        ch3_ready;

    logic [27:1] ch4_addr;
    logic [63:0] ch4_din;
    logic        ch4_req, ch4_rnw;
    logic [7:0]  ch4_be;
    logic [63:0] ch4_dout;
    logic        ch4_ready;

    logic [27:1] ch5_addr;
    logic [63:0] ch5_din;
    logic        ch5_req, ch5_rnw;
    logic [63:0] ch5_dout;
    logic        ch5_ready;

    // === 统计 ===
    int pass_count = 0;
    int fail_count = 0;
    int test_num   = 0;

    // === AXI 延迟控制 ===
    int axi_ar_delay = 0; // ARREADY 延迟周期
    int axi_r_delay  = 0; // RVALID 延迟周期

    // === 实例化 ddram_mux ===
    ddram_mux u_mux (
        .DDRAM_CLK       (clk),
        .DDRAM_BUSY      (ddram_busy),
        .DDRAM_BURSTCNT  (ddram_burstcnt),
        .DDRAM_ADDR      (ddram_addr),
        .DDRAM_DOUT      (ddram_dout),
        .DDRAM_DOUT_READY(ddram_dout_ready),
        .DDRAM_RD        (ddram_rd),
        .DDRAM_DIN       (ddram_din),
        .DDRAM_BE        (ddram_be),
        .DDRAM_WE        (ddram_we),
        .ch1_addr(ch1_addr), .ch1_dout(ch1_dout), .ch1_din(ch1_din),
        .ch1_req(ch1_req),   .ch1_rnw(ch1_rnw),   .ch1_ready(ch1_ready),
        .ch2_addr(ch2_addr), .ch2_dout(ch2_dout), .ch2_din(ch2_din),
        .ch2_req(ch2_req),   .ch2_rnw(ch2_rnw),   .ch2_ready(ch2_ready),
        .ch3_addr(ch3_addr), .ch3_dout(ch3_dout), .ch3_din(ch3_din),
        .ch3_req(ch3_req),   .ch3_rnw(ch3_rnw),   .ch3_ready(ch3_ready),
        .ch4_addr(ch4_addr), .ch4_dout(ch4_dout), .ch4_din(ch4_din),
        .ch4_req(ch4_req),   .ch4_rnw(ch4_rnw),   .ch4_be(ch4_be), .ch4_ready(ch4_ready),
        .ch5_addr(ch5_addr), .ch5_dout(ch5_dout), .ch5_din(ch5_din),
        .ch5_req(ch5_req),   .ch5_rnw(ch5_rnw),   .ch5_ready(ch5_ready)
    );

    // === 实例化 ddr_axi_backend ===
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

    // === 实例化 axi_mem_model ===
    axi_mem_model #(
        .MEM_SIZE_BYTES(1048576),  // 1MB
        .ROM_HEX_FILE("../rom_data/rom_first_4k.hex")
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

    // === 信号监控：记录关键握手事件 ===
    int ddram_rd_count = 0;
    int ddram_we_count = 0;
    int ddram_rd_while_busy = 0;
    int ddram_we_while_busy = 0;
    int ch1_ready_count = 0;

    always_ff @(posedge clk) begin
        if (rst_n) begin
            if (ddram_rd) begin
                ddram_rd_count <= ddram_rd_count + 1;
                if (ddram_busy)
                    ddram_rd_while_busy <= ddram_rd_while_busy + 1;
                $display("[MON] @%0t DDRAM_RD addr=0x%08X busy=%0b burst=%0d",
                         $time, {ddram_addr, 3'b000}, ddram_busy, ddram_burstcnt);
            end
            if (ddram_we) begin
                ddram_we_count <= ddram_we_count + 1;
                if (ddram_busy)
                    ddram_we_while_busy <= ddram_we_while_busy + 1;
                $display("[MON] @%0t DDRAM_WE addr=0x%08X busy=%0b be=0x%02X",
                         $time, {ddram_addr, 3'b000}, ddram_busy, ddram_be);
            end
            if (ch1_ready) begin
                ch1_ready_count <= ch1_ready_count + 1;
                $display("[MON] @%0t CH1_READY dout=0x%016X", $time, ch1_dout);
            end
            if (err_pulse) begin
                $display("[MON] @%0t ERR_PULSE vec=0x%08X", $time, err_vec);
            end
        end
    end

    // === 辅助：清空所有通道请求 ===
    task automatic clear_all_channels();
        ch1_addr <= '0; ch1_din <= '0; ch1_req <= 0; ch1_rnw <= 1;
        ch2_addr <= '0; ch2_din <= '0; ch2_req <= 0; ch2_rnw <= 1;
        ch3_addr <= '0; ch3_din <= '0; ch3_req <= 0; ch3_rnw <= 1;
        ch4_addr <= '0; ch4_din <= '0; ch4_req <= 0; ch4_rnw <= 1; ch4_be <= '0;
        ch5_addr <= '0; ch5_din <= '0; ch5_req <= 0; ch5_rnw <= 1;
    endtask

    // === 辅助：等待 ch1_ready，带超时 ===
    task automatic wait_ch1_ready(input int timeout_cycles, output int ok);
        int cnt;
        ok = 0;
        for (cnt = 0; cnt < timeout_cycles; cnt++) begin
            @(posedge clk);
            if (ch1_ready) begin
                ok = 1;
                return;
            end
        end
    endtask

    // === 辅助：等待 ch2_ready ===
    task automatic wait_ch2_ready(input int timeout_cycles, output int ok);
        int cnt;
        ok = 0;
        for (cnt = 0; cnt < timeout_cycles; cnt++) begin
            @(posedge clk);
            if (ch2_ready) begin
                ok = 1;
                return;
            end
        end
    endtask

    // === 辅助：等待 ch5_ready ===
    task automatic wait_ch5_ready(input int timeout_cycles, output int ok);
        int cnt;
        ok = 0;
        for (cnt = 0; cnt < timeout_cycles; cnt++) begin
            @(posedge clk);
            if (ch5_ready) begin
                ok = 1;
                return;
            end
        end
    endtask

    // === 辅助：等待 ddram 空闲 ===
    task automatic wait_idle(input int timeout_cycles);
        int cnt;
        for (cnt = 0; cnt < timeout_cycles; cnt++) begin
            @(posedge clk);
            if (!ddram_busy && !ddram_rd && !ddram_we)
                return;
        end
        $display("[WARN] @%0t wait_idle timeout", $time);
    endtask

    // === 辅助：重置统计 ===
    task automatic reset_counters();
        ddram_rd_count      <= 0;
        ddram_we_count      <= 0;
        ddram_rd_while_busy <= 0;
        ddram_we_while_busy <= 0;
        ch1_ready_count     <= 0;
        @(posedge clk); // 让寄存器更新
    endtask

    // === 辅助：检查并报告 ===
    task automatic check(input string name, input int condition);
        test_num++;
        if (condition) begin
            pass_count++;
            $display("[PASS] T%0d %s", test_num, name);
        end else begin
            fail_count++;
            $display("[FAIL] T%0d %s", test_num, name);
        end
    endtask

    // =========================================================================
    // 主测试流程
    // =========================================================================
    initial begin
        $display("=== tb_ddr_backend: ddram_mux + ddr_axi_backend 握手竞态仿真 ===");

        // 复位
        rst_n = 0;
        clear_all_channels();
        repeat (10) @(posedge clk);
        rst_n = 1;
        repeat (5) @(posedge clk);

        // =====================================================================
        // T1: 纯背靠背 ch1 读（控制组）
        // 地址 0x0C0000 和 0x0C0008 (不同 cache line)
        // =====================================================================
        $display("\n--- T1: 纯背靠背 ch1 读 ---");
        reset_counters();
        wait_idle(20);

        // 第一个读：ROM 起始
        @(posedge clk);
        ch1_addr <= 27'h060000; // 字地址 0x060000*2 = 0xC0000
        ch1_rnw  <= 1;
        ch1_req  <= 1;
        @(posedge clk);
        ch1_req  <= 0;

        begin
            int ok1;
            wait_ch1_ready(100, ok1);
            check("T1a_first_ch1_read_completes", ok1);
        end

        // 等待系统回到空闲
        wait_idle(20);

        // 第二个读：ROM 偏移 +8
        @(posedge clk);
        ch1_addr <= 27'h060004; // 不同 cache line
        ch1_rnw  <= 1;
        ch1_req  <= 1;
        @(posedge clk);
        ch1_req  <= 0;

        begin
            int ok2;
            wait_ch1_ready(100, ok2);
            check("T1b_second_ch1_read_completes", ok2);
        end

        wait_idle(20);
        @(posedge clk);
        check("T1c_no_rd_while_busy", ddram_rd_while_busy == 0);

        // =====================================================================
        // T2: ch5 写 → ch1 读 竞态（核心测试！）
        // ch5 先发出写请求，紧接着 ch1 发出读请求
        // =====================================================================
        $display("\n--- T2: ch5 写 → ch1 读 竞态 ---");
        reset_counters();
        wait_idle(20);

        // 同时发出 ch5 写 + ch1 读 — ch5 优先级低，但如果 ch5_req 先到一个周期...
        // 场景：ch5 写先到，ch1 读紧跟
        @(posedge clk);
        ch5_addr <= 27'h400000; // FB 区域
        ch5_din  <= 64'hAAAA_BBBB_CCCC_DDDD;
        ch5_rnw  <= 0; // 写
        ch5_req  <= 1;
        @(posedge clk);
        ch5_req  <= 0;

        // ch5 写发出后，立刻发 ch1 读
        @(posedge clk);
        ch1_addr <= 27'h060000;
        ch1_rnw  <= 1;
        ch1_req  <= 1;
        @(posedge clk);
        ch1_req  <= 0;

        begin
            int ok;
            wait_ch1_ready(200, ok);
            check("T2a_ch1_read_after_ch5_write", ok);
            if (!ok) $display("[DIAG] DEADLOCK: ch1 read lost after ch5 write! rd_busy=%0d",
                              ddram_rd_while_busy);
        end
        wait_idle(50);
        @(posedge clk);
        $display("[DIAG] T2 rd_count=%0d we_count=%0d rd_busy=%0d we_busy=%0d ch1_done=%0d",
                 ddram_rd_count, ddram_we_count, ddram_rd_while_busy, ddram_we_while_busy,
                 ch1_ready_count);

        // =====================================================================
        // T3: ch2 写 → ch1 读 竞态
        // =====================================================================
        $display("\n--- T3: ch2 写 → ch1 读 竞态 ---");
        reset_counters();
        wait_idle(20);

        @(posedge clk);
        ch2_addr <= 27'h040000; // WRAM 区域
        ch2_din  <= 32'h12345678;
        ch2_rnw  <= 0; // 写
        ch2_req  <= 1;
        @(posedge clk);
        ch2_req  <= 0;

        @(posedge clk);
        ch1_addr <= 27'h060004;
        ch1_rnw  <= 1;
        ch1_req  <= 1;
        @(posedge clk);
        ch1_req  <= 0;

        begin
            int ok;
            wait_ch1_ready(200, ok);
            check("T3a_ch1_read_after_ch2_write", ok);
            if (!ok) $display("[DIAG] DEADLOCK: ch1 read lost after ch2 write! rd_busy=%0d",
                              ddram_rd_while_busy);
        end
        wait_idle(50);
        @(posedge clk);
        $display("[DIAG] T3 rd_count=%0d we_count=%0d rd_busy=%0d we_busy=%0d ch1_done=%0d",
                 ddram_rd_count, ddram_we_count, ddram_rd_while_busy, ddram_we_while_busy,
                 ch1_ready_count);

        // =====================================================================
        // T4: 同周期 ch5 写 + ch1 读（同时到达）
        // =====================================================================
        $display("\n--- T4: 同周期 ch5 写 + ch1 读 ---");
        reset_counters();
        wait_idle(20);

        @(posedge clk);
        ch5_addr <= 27'h400008;
        ch5_din  <= 64'hDEAD_BEEF_CAFE_F00D;
        ch5_rnw  <= 0;
        ch5_req  <= 1;
        ch1_addr <= 27'h060008;
        ch1_rnw  <= 1;
        ch1_req  <= 1;
        @(posedge clk);
        ch5_req  <= 0;
        ch1_req  <= 0;

        // 公平仲裁下不再要求固定顺序，只要求两边都能在有限时间内完成。
        fork
            begin : t4_wait_ch1
                int ok;
                wait_ch1_ready(200, ok);
                check("T4a_simultaneous_ch5wr_ch1rd", ok);
            end
            begin : t4_wait_ch5
                int ok5;
                wait_ch5_ready(200, ok5);
                check("T4b_ch5_write_also_completes", ok5);
            end
        join
        wait_idle(50);

        // =====================================================================
        // T5: 连续 3 个 ch1 读（不同 cache line），验证无死锁
        // =====================================================================
        $display("\n--- T5: 连续 3 个 ch1 读 ---");
        reset_counters();
        wait_idle(20);

        for (int i = 0; i < 3; i++) begin
            @(posedge clk);
            ch1_addr <= 27'h060000 + i * 4; // 不同 cache line
            ch1_rnw  <= 1;
            ch1_req  <= 1;
            @(posedge clk);
            ch1_req  <= 0;

            begin
                int ok;
                wait_ch1_ready(100, ok);
                check($sformatf("T5_%0d_sequential_read", i), ok);
            end
        end
        wait_idle(20);

        // =====================================================================
        // T6: ch5 写 → ch1 读 → ch5 写 → ch1 读（交替）
        // =====================================================================
        $display("\n--- T6: 交替 ch5 写 / ch1 读 ---");
        reset_counters();
        wait_idle(20);

        for (int i = 0; i < 4; i++) begin
            if (i % 2 == 0) begin
                // ch5 写
                @(posedge clk);
                ch5_addr <= 27'h400000 + i * 4;
                ch5_din  <= {32'hA000_0000 + i, 32'hB000_0000 + i};
                ch5_rnw  <= 0;
                ch5_req  <= 1;
                @(posedge clk);
                ch5_req  <= 0;
                begin
                    int ok;
                    wait_ch5_ready(200, ok);
                    check($sformatf("T6_%0d_ch5_write", i), ok);
                end
            end else begin
                // ch1 读
                @(posedge clk);
                ch1_addr <= 27'h060000 + i * 4;
                ch1_rnw  <= 1;
                ch1_req  <= 1;
                @(posedge clk);
                ch1_req  <= 0;
                begin
                    int ok;
                    wait_ch1_ready(200, ok);
                    check($sformatf("T6_%0d_ch1_read", i), ok);
                end
            end
        end
        wait_idle(50);
        @(posedge clk);
        $display("[DIAG] T6 rd_count=%0d we_count=%0d rd_busy=%0d we_busy=%0d",
                 ddram_rd_count, ddram_we_count, ddram_rd_while_busy, ddram_we_while_busy);

        // =====================================================================
        // T7: 快速交替 — ch5 写后立即 ch1 读（不等 ch5 完成）
        // 这是最可能触发竞态的场景
        // =====================================================================
        $display("\n--- T7: 快速交替 ch5写+ch1读（不等 ch5 完成）---");
        reset_counters();
        wait_idle(20);

        // 发射 ch5 写
        @(posedge clk);
        ch5_addr <= 27'h400010;
        ch5_din  <= 64'hFFFF_FFFF_FFFF_FFFF;
        ch5_rnw  <= 0;
        ch5_req  <= 1;
        // 不等 ch5_ready，下一周期立刻发 ch1 读
        @(posedge clk);
        ch5_req  <= 0;
        ch1_addr <= 27'h060010;
        ch1_rnw  <= 1;
        ch1_req  <= 1;
        @(posedge clk);
        ch1_req  <= 0;

        begin
            int ok;
            wait_ch1_ready(200, ok);
            check("T7a_fast_ch5wr_then_ch1rd", ok);
            if (!ok) begin
                $display("[DIAG] *** T7 DEADLOCK DETECTED ***");
                $display("[DIAG] ddram_busy=%0b rd_count=%0d we_count=%0d rd_busy=%0d",
                         ddram_busy, ddram_rd_count, ddram_we_count, ddram_rd_while_busy);
                $display("[DIAG] backend state=%0d", u_backend.state);
            end
        end

        // =====================================================================
        // 汇总
        // =====================================================================
        repeat (20) @(posedge clk);
        $display("\n=== 汇总: %0d PASS, %0d FAIL (共 %0d) ===",
                 pass_count, fail_count, test_num);
        if (fail_count > 0)
            $display("*** 存在握手竞态 BUG！***");
        else
            $display("所有测试通过");
        $finish;
    end

    // 超时保护
    initial begin
        #500000;
        $display("[TIMEOUT] 仿真超时 500us");
        $display("汇总: %0d PASS, %0d FAIL", pass_count, fail_count);
        $finish;
    end

endmodule
