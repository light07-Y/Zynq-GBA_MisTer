// tb_ddr_stress.sv: 高密度混合 read+write 压力测试
// 目标：用变延迟 AXI 模型复现硬件第二个竞态 (255+事务后卡死)
// 策略：
//   - 交替发射 ch1 读 + ch5 写，模拟 GBA 运行时 ROM读+FB写并发
//   - 每轮测试不同 AXI 延迟配置
//   - 监控 DDRAM_RD/WE 在 BUSY=1 时发出的次数

`timescale 1ns / 1ps

module tb_ddr_stress;

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
    int total_rd_issued = 0;
    int total_we_issued = 0;
    int rd_while_busy   = 0;
    int we_while_busy   = 0;
    int ch1_completions = 0;
    int ch5_completions = 0;
    int deadlock_count  = 0;

    // === 实例化 ddram_mux ===
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

    // === 实例化带延迟的 AXI 内存模型 ===
    // 延迟参数通过 defparam 在每轮测试前配置
    axi_mem_model_delayed #(
        .MEM_SIZE_BYTES(1048576),
        .ROM_HEX_FILE("../rom_data/rom_first_4k.hex"),
        .AR_ACCEPT_DELAY(2),     // ARREADY 延迟 2 周期
        .R_RESPONSE_DELAY(5),    // 读数据延迟 5 周期
        .AW_ACCEPT_DELAY(1),     // AWREADY 延迟 1 周期
        .W_ACCEPT_DELAY(0),      // WREADY 即时
        .B_RESPONSE_DELAY(3)     // 写响应延迟 3 周期
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

    // === 信号监控 ===
    always_ff @(posedge clk) begin
        if (rst_n) begin
            if (ddram_rd) begin
                total_rd_issued <= total_rd_issued + 1;
                if (ddram_busy) begin
                    rd_while_busy <= rd_while_busy + 1;
                    $display("[BUG!] @%0t DDRAM_RD while BUSY addr=0x%08X backend_state=%0d",
                             $time, {ddram_addr, 3'b000}, u_backend.state);
                end
            end
            if (ddram_we) begin
                total_we_issued <= total_we_issued + 1;
                if (ddram_busy) begin
                    we_while_busy <= we_while_busy + 1;
                    $display("[BUG!] @%0t DDRAM_WE while BUSY addr=0x%08X backend_state=%0d",
                             $time, {ddram_addr, 3'b000}, u_backend.state);
                end
            end
            if (ch1_ready) ch1_completions <= ch1_completions + 1;
            if (ch5_ready) ch5_completions <= ch5_completions + 1;
        end
    end

    // === 辅助任务 ===
    task automatic clear_all();
        ch1_addr <= '0; ch1_din <= '0; ch1_req <= 0; ch1_rnw <= 1;
        ch2_addr <= '0; ch2_din <= '0; ch2_req <= 0; ch2_rnw <= 1;
        ch3_addr <= '0; ch3_din <= '0; ch3_req <= 0; ch3_rnw <= 1;
        ch4_addr <= '0; ch4_din <= '0; ch4_req <= 0; ch4_rnw <= 1; ch4_be <= '0;
        ch5_addr <= '0; ch5_din <= '0; ch5_req <= 0; ch5_rnw <= 1;
    endtask

    // === 并发驱动器: ch1 连续读 ROM ===
    int ch1_target_ops;
    int ch1_ops_done;
    logic ch1_driver_active;

    task automatic run_ch1_reader(input int num_ops);
        ch1_target_ops = num_ops;
        ch1_ops_done = 0;
        ch1_driver_active = 1;
        // 发射第一个请求
        @(posedge clk);
        ch1_addr <= 27'h060000; // ROM 起始
        ch1_rnw  <= 1;
        ch1_req  <= 1;
        @(posedge clk);
        ch1_req  <= 0;
    endtask

    // ch1 自动连续读驱动: 收到 ready 后发下一个
    always_ff @(posedge clk) begin
        if (ch1_driver_active && ch1_ready) begin
            ch1_ops_done <= ch1_ops_done + 1;
            if (ch1_ops_done + 1 < ch1_target_ops) begin
                // 发射下一个读（不同 cache line）
                ch1_addr <= 27'h060000 + ((ch1_ops_done + 1) * 4);
                ch1_rnw  <= 1;
                ch1_req  <= 1;
            end else begin
                ch1_driver_active <= 0;
                ch1_req <= 0;
            end
        end else if (ch1_driver_active) begin
            ch1_req <= 0; // 只脉冲 1 周期
        end
    end

    // === 并发驱动器: ch5 连续写 FB ===
    int ch5_target_ops;
    int ch5_ops_done;
    logic ch5_driver_active;

    task automatic run_ch5_writer(input int num_ops);
        ch5_target_ops = num_ops;
        ch5_ops_done = 0;
        ch5_driver_active = 1;
        // 发射第一个请求
        @(posedge clk);
        ch5_addr <= 27'h400000;
        ch5_din  <= 64'hAAAA_BBBB_0000_0000;
        ch5_rnw  <= 0;
        ch5_req  <= 1;
        @(posedge clk);
        ch5_req  <= 0;
    endtask

    // ch5 自动连续写驱动: 收到 ready 后发下一个
    always_ff @(posedge clk) begin
        if (ch5_driver_active && ch5_ready) begin
            ch5_ops_done <= ch5_ops_done + 1;
            if (ch5_ops_done + 1 < ch5_target_ops) begin
                ch5_addr <= 27'h400000 + ((ch5_ops_done + 1) * 4);
                ch5_din  <= 64'hAAAA_BBBB_0000_0000 + (ch5_ops_done + 1);
                ch5_rnw  <= 0;
                ch5_req  <= 1;
            end else begin
                ch5_driver_active <= 0;
                ch5_req <= 0;
            end
        end else if (ch5_driver_active) begin
            ch5_req <= 0;
        end
    end

    // === 主测试流程 ===
    initial begin
        $display("=== tb_ddr_stress: 高密度混合压力测试 (变延迟 AXI) ===");
        $display("AXI 延迟: AR=%0d R=%0d AW=%0d W=%0d B=%0d",
                 2, 5, 1, 0, 3);

        rst_n = 0;
        clear_all();
        ch1_driver_active = 0;
        ch5_driver_active = 0;
        repeat (10) @(posedge clk);
        rst_n = 1;
        repeat (5) @(posedge clk);

        // =====================================================================
        // 测试 S1: 500 个 ch1 读（纯读，无并发写）
        // =====================================================================
        $display("\n--- S1: 500 个纯 ch1 读 (变延迟 AXI) ---");
        total_rd_issued = 0; total_we_issued = 0;
        rd_while_busy = 0; we_while_busy = 0;
        ch1_completions = 0; ch5_completions = 0;

        run_ch1_reader(500);

        // 等待完成或超时
        begin
            int timeout;
            for (timeout = 0; timeout < 100000; timeout++) begin
                @(posedge clk);
                if (!ch1_driver_active) break;
            end
            if (ch1_driver_active) begin
                $display("[DEADLOCK] S1 ch1 stalled after %0d ops, rd_busy=%0d",
                         ch1_ops_done, rd_while_busy);
                deadlock_count++;
                ch1_driver_active = 0;
            end else begin
                $display("[OK] S1 completed: %0d ch1 reads, rd_busy=%0d",
                         ch1_completions, rd_while_busy);
            end
        end

        // 等待系统空闲
        repeat (100) @(posedge clk);

        // =====================================================================
        // 测试 S2: 300 ch1 读 + 300 ch5 写 并发
        // 这是最关键的测试: 模拟 GBA 运行时 ROM读 + FB写
        // =====================================================================
        $display("\n--- S2: 300 ch1 读 + 300 ch5 写 并发 ---");
        total_rd_issued = 0; total_we_issued = 0;
        rd_while_busy = 0; we_while_busy = 0;
        ch1_completions = 0; ch5_completions = 0;

        // 同时启动两个驱动器
        fork
            run_ch1_reader(300);
            run_ch5_writer(300);
        join

        // 等待两个都完成
        begin
            int timeout;
            for (timeout = 0; timeout < 200000; timeout++) begin
                @(posedge clk);
                if (!ch1_driver_active && !ch5_driver_active) break;
            end
            if (ch1_driver_active || ch5_driver_active) begin
                $display("[DEADLOCK] S2 stalled! ch1_done=%0d/%0d ch5_done=%0d/%0d",
                         ch1_ops_done, 300, ch5_ops_done, 300);
                $display("[DEADLOCK] rd_busy=%0d we_busy=%0d backend=%0d busy=%0b",
                         rd_while_busy, we_while_busy, u_backend.state, ddram_busy);
                $display("[DEADLOCK] mux_state=%0d ram_read=%0b ram_write=%0b",
                         u_mux.state, u_mux.ram_read, u_mux.ram_write);
                deadlock_count++;
                ch1_driver_active = 0;
                ch5_driver_active = 0;
            end else begin
                $display("[OK] S2 completed: ch1=%0d ch5=%0d rd_busy=%0d we_busy=%0d",
                         ch1_completions, ch5_completions, rd_while_busy, we_while_busy);
            end
        end

        repeat (100) @(posedge clk);

        // =====================================================================
        // 测试 S3: 更激进 — ch1 读每 2 周期发一个 + ch5 写每 3 周期发一个
        // 使用直接脉冲驱动（不等 ready）
        // =====================================================================
        $display("\n--- S3: 激进交叉 ch1读+ch5写 (不等完成) ---");
        total_rd_issued = 0; total_we_issued = 0;
        rd_while_busy = 0; we_while_busy = 0;
        ch1_completions = 0; ch5_completions = 0;
        ch1_driver_active = 0;
        ch5_driver_active = 0;

        fork
            // ch1 读: 每隔 3 周期发一个请求
            begin
                for (int i = 0; i < 200; i++) begin
                    @(posedge clk);
                    ch1_addr <= 27'h060000 + (i * 4);
                    ch1_rnw  <= 1;
                    ch1_req  <= 1;
                    @(posedge clk);
                    ch1_req  <= 0;
                    @(posedge clk); // 间隔
                end
            end
            // ch5 写: 每隔 4 周期发一个请求
            begin
                for (int i = 0; i < 150; i++) begin
                    @(posedge clk);
                    ch5_addr <= 27'h400000 + (i * 4);
                    ch5_din  <= {32'hFB000000 + i, 32'hFB000000 + i};
                    ch5_rnw  <= 0;
                    ch5_req  <= 1;
                    @(posedge clk);
                    ch5_req  <= 0;
                    repeat (2) @(posedge clk); // 间隔
                end
            end
        join

        // 等待所有操作排空
        begin
            int timeout;
            for (timeout = 0; timeout < 50000; timeout++) begin
                @(posedge clk);
                if (!ddram_busy && !ddram_rd && !ddram_we &&
                    u_mux.state == 2'd0 && u_mux.ch_rq == 5'd0)
                    break;
            end
            if (timeout >= 50000) begin
                $display("[DEADLOCK] S3 drain timeout! ch1=%0d ch5=%0d rd_busy=%0d we_busy=%0d",
                         ch1_completions, ch5_completions, rd_while_busy, we_while_busy);
                $display("[DEADLOCK] backend=%0d mux_state=%0d ch_rq=0x%02X busy=%0b",
                         u_backend.state, u_mux.state, u_mux.ch_rq, ddram_busy);
                deadlock_count++;
            end else begin
                $display("[OK] S3 completed: ch1=%0d ch5=%0d rd_busy=%0d we_busy=%0d",
                         ch1_completions, ch5_completions, rd_while_busy, we_while_busy);
            end
        end

        repeat (100) @(posedge clk);

        // =====================================================================
        // 测试 S4: ch2 写 + ch1 读 并发 (WRAM 写 + ROM 读)
        // =====================================================================
        $display("\n--- S4: 200 ch2 写 + 200 ch1 读 并发 ---");
        total_rd_issued = 0; total_we_issued = 0;
        rd_while_busy = 0; we_while_busy = 0;
        ch1_completions = 0; ch5_completions = 0;

        fork
            // ch1 读
            begin
                for (int i = 0; i < 200; i++) begin
                    @(posedge clk);
                    ch1_addr <= 27'h060000 + (i * 4);
                    ch1_rnw  <= 1;
                    ch1_req  <= 1;
                    @(posedge clk);
                    ch1_req  <= 0;
                    // 等 ch1_ready
                    begin
                        int w;
                        for (w = 0; w < 500; w++) begin
                            @(posedge clk);
                            if (ch1_ready) break;
                        end
                        if (w >= 500) begin
                            $display("[DEADLOCK] S4 ch1 read %0d timeout! backend=%0d mux=%0d busy=%0b",
                                     i, u_backend.state, u_mux.state, ddram_busy);
                            deadlock_count++;
                            disable fork;
                        end
                    end
                end
            end
            // ch2 写
            begin
                for (int i = 0; i < 200; i++) begin
                    @(posedge clk);
                    ch2_addr <= 27'h040000 + (i * 2);
                    ch2_din  <= 32'hCC000000 + i;
                    ch2_rnw  <= 0;
                    ch2_req  <= 1;
                    @(posedge clk);
                    ch2_req  <= 0;
                    // 等 ch2_ready
                    begin
                        int w;
                        for (w = 0; w < 500; w++) begin
                            @(posedge clk);
                            if (ch2_ready) break;
                        end
                        if (w >= 500) begin
                            $display("[DEADLOCK] S4 ch2 write %0d timeout!", i);
                            deadlock_count++;
                            disable fork;
                        end
                    end
                end
            end
        join

        $display("[INFO] S4 rd_busy=%0d we_busy=%0d ch1=%0d",
                 rd_while_busy, we_while_busy, ch1_completions);

        // =====================================================================
        // 汇总
        // =====================================================================
        repeat (50) @(posedge clk);
        $display("\n=== 压力测试汇总 ===");
        $display("总 RD while BUSY: %0d", rd_while_busy);
        $display("总 WE while BUSY: %0d", we_while_busy);
        $display("死锁次数: %0d", deadlock_count);
        if (deadlock_count > 0 || rd_while_busy > 0 || we_while_busy > 0)
            $display("*** 仍存在竞态 BUG！***");
        else
            $display("所有压力测试通过");
        $finish;
    end

    // 超时保护
    initial begin
        #50000000; // 50ms
        $display("[TIMEOUT] 仿真超时");
        $display("rd_busy=%0d we_busy=%0d deadlocks=%0d",
                 rd_while_busy, we_while_busy, deadlock_count);
        $finish;
    end

endmodule
