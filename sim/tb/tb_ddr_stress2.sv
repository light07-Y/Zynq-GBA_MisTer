// tb_ddr_stress2.sv: 干净的高密度混合压力测试
// 纯 initial/fork 驱动（无 always_ff 自动驱动，避免双驱动冲突）
// 目标: 不同 AXI 延迟下 500+ 混合操作，找到第二个竞态
`timescale 1ns / 1ps

module tb_ddr_stress2;

    logic clk = 0;
    always #5 clk = ~clk;

    logic rst_n;

    // === ddram_mux <-> ddr_axi_backend 连线 ===
    logic        ddram_busy, ddram_dout_ready, ddram_rd, ddram_we;
    logic [7:0]  ddram_burstcnt, ddram_be;
    logic [28:0] ddram_addr;
    logic [63:0] ddram_dout, ddram_din;

    // === AXI4 连线 ===
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
    logic [31:0] err_vec;
    logic        err_pulse;

    // === 通道信号 ===
    logic [27:1] ch1_addr = '0;
    logic [15:0] ch1_din = '0;
    logic        ch1_req = 0, ch1_rnw = 1;
    logic [63:0] ch1_dout;
    logic        ch1_ready;

    logic [27:1] ch2_addr = '0;
    logic [31:0] ch2_din = '0;
    logic        ch2_req = 0, ch2_rnw = 1;
    logic [31:0] ch2_dout;
    logic        ch2_ready;

    logic [25:1] ch3_addr = '0;
    logic [15:0] ch3_din = '0;
    logic        ch3_req = 0, ch3_rnw = 1;
    logic [15:0] ch3_dout;
    logic        ch3_ready;

    logic [27:1] ch4_addr = '0;
    logic [63:0] ch4_din = '0;
    logic        ch4_req = 0, ch4_rnw = 1;
    logic [7:0]  ch4_be = '0;
    logic [63:0] ch4_dout;
    logic        ch4_ready;

    logic [27:1] ch5_addr = '0;
    logic [63:0] ch5_din = '0;
    logic        ch5_req = 0, ch5_rnw = 1;
    logic [63:0] ch5_dout;
    logic        ch5_ready;

    // === 监控统计 ===
    int rd_while_busy = 0, we_while_busy = 0;
    int total_pass = 0, total_fail = 0;

    // 监控: 检测 DDRAM_RD/WE 在 BUSY=1 时发出
    always @(posedge clk) begin
        if (rst_n) begin
            if (ddram_rd && ddram_busy) begin
                rd_while_busy++;
                $display("[BUG!] @%0t DDRAM_RD while BUSY! addr=0x%08X be_state=%0d mux_state=%0d",
                         $time, {ddram_addr, 3'b000}, u_backend.state, u_mux.state);
            end
            if (ddram_we && ddram_busy) begin
                we_while_busy++;
                $display("[BUG!] @%0t DDRAM_WE while BUSY! addr=0x%08X be_state=%0d mux_state=%0d",
                         $time, {ddram_addr, 3'b000}, u_backend.state, u_mux.state);
            end
        end
    end

    // === DUT 实例化 ===
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

    axi_mem_model_delayed #(
        .MEM_SIZE_BYTES(1048576),
        .ROM_HEX_FILE("../rom_data/rom_first_4k.hex"),
        .AR_ACCEPT_DELAY(2),
        .R_RESPONSE_DELAY(5),
        .AW_ACCEPT_DELAY(1),
        .W_ACCEPT_DELAY(0),
        .B_RESPONSE_DELAY(3)
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

    // === 辅助任务 ===
    // ch1 发读、等完成
    task automatic ch1_read_wait(input logic [27:1] addr, output logic [63:0] data, output int ok);
        int w;
        @(posedge clk);
        ch1_addr <= addr;
        ch1_rnw  <= 1;
        ch1_req  <= 1;
        @(posedge clk);
        ch1_req  <= 0;
        ok = 0;
        for (w = 0; w < 2000; w++) begin
            @(posedge clk);
            if (ch1_ready) begin
                data = ch1_dout;
                ok = 1;
                return;
            end
        end
    endtask

    // ch5 发写、等完成
    task automatic ch5_write_wait(input logic [27:1] addr, input logic [63:0] wdata, output int ok);
        int w;
        @(posedge clk);
        ch5_addr <= addr;
        ch5_din  <= wdata;
        ch5_rnw  <= 0;
        ch5_req  <= 1;
        @(posedge clk);
        ch5_req  <= 0;
        ok = 0;
        for (w = 0; w < 2000; w++) begin
            @(posedge clk);
            if (ch5_ready) begin
                ok = 1;
                return;
            end
        end
    endtask

    // ch2 发写、等完成
    task automatic ch2_write_wait(input logic [27:1] addr, input logic [31:0] wdata, output int ok);
        int w;
        @(posedge clk);
        ch2_addr <= addr;
        ch2_din  <= wdata;
        ch2_rnw  <= 0;
        ch2_req  <= 1;
        @(posedge clk);
        ch2_req  <= 0;
        ok = 0;
        for (w = 0; w < 2000; w++) begin
            @(posedge clk);
            if (ch2_ready) begin
                ok = 1;
                return;
            end
        end
    endtask

    // === 测试套件 ===
    logic [63:0] rd_data;
    int ok;
    int s_pass, s_fail;

    initial begin
        $display("=== tb_ddr_stress2: 干净高密度压力测试 ===");
        $display("AXI delays: AR=%0d R=%0d AW=%0d W=%0d B=%0d", 2, 5, 1, 0, 3);
        rst_n = 0;
        repeat (10) @(posedge clk);
        rst_n = 1;
        repeat (5) @(posedge clk);

        // =========================================================
        // S1: 500 纯 ch1 读，每次不同 cache line
        // =========================================================
        $display("\n--- S1: 500 纯 ch1 读 ---");
        s_pass = 0; s_fail = 0;
        for (int i = 0; i < 500; i++) begin
            ch1_read_wait(27'h060000 + i * 4, rd_data, ok);
            if (!ok) begin
                $display("[DEADLOCK] S1 ch1 read %0d timeout! be=%0d mux=%0d busy=%0b",
                         i, u_backend.state, u_mux.state, ddram_busy);
                s_fail++;
                break;
            end
            s_pass++;
        end
        if (s_fail == 0) begin
            $display("[PASS] S1: %0d reads OK, rd_busy=%0d", s_pass, rd_while_busy);
            total_pass++;
        end else begin
            $display("[FAIL] S1: stalled at read %0d", s_pass);
            total_fail++;
        end

        repeat (20) @(posedge clk);

        // =========================================================
        // S2: 300 ch1 读 + 300 ch5 写 严格交替 (写→读→写→读)
        // =========================================================
        $display("\n--- S2: 300 交替 ch5写→ch1读 ---");
        s_pass = 0; s_fail = 0;
        for (int i = 0; i < 300; i++) begin
            // ch5 写
            ch5_write_wait(27'h400000 + i * 4, {32'hFB000000 + i, 32'hFB000000 + i}, ok);
            if (!ok) begin
                $display("[DEADLOCK] S2 ch5 write %0d timeout! be=%0d mux=%0d busy=%0b",
                         i, u_backend.state, u_mux.state, ddram_busy);
                s_fail++;
                break;
            end
            // ch1 读
            ch1_read_wait(27'h060000 + i * 4, rd_data, ok);
            if (!ok) begin
                $display("[DEADLOCK] S2 ch1 read %0d timeout! be=%0d mux=%0d busy=%0b",
                         i, u_backend.state, u_mux.state, ddram_busy);
                s_fail++;
                break;
            end
            s_pass++;
        end
        if (s_fail == 0) begin
            $display("[PASS] S2: %0d wr+rd pairs OK, rd_busy=%0d we_busy=%0d",
                     s_pass, rd_while_busy, we_while_busy);
            total_pass++;
        end else begin
            $display("[FAIL] S2: stalled at pair %0d", s_pass);
            total_fail++;
        end

        repeat (20) @(posedge clk);

        // =========================================================
        // S3: 并发 ch1 读 + ch5 写 (fork/join)
        // 两个线程独立发射、独立等完成
        // =========================================================
        $display("\n--- S3: 并发 300 ch1读 + 300 ch5写 ---");
        s_pass = 0; s_fail = 0;
        fork
            // ch1 读线程
            begin : ch1_thread
                for (int i = 0; i < 300; i++) begin
                    int rok;
                    logic [63:0] rdata;
                    ch1_read_wait(27'h060000 + i * 4, rdata, rok);
                    if (!rok) begin
                        $display("[DEADLOCK] S3 ch1 read %0d timeout! be=%0d mux=%0d busy=%0b",
                                 i, u_backend.state, u_mux.state, ddram_busy);
                        s_fail++;
                        disable ch1_thread;
                    end
                end
            end
            // ch5 写线程
            begin : ch5_thread
                for (int i = 0; i < 300; i++) begin
                    int wok;
                    ch5_write_wait(27'h400000 + i * 4, 64'hDEAD0000 + i, wok);
                    if (!wok) begin
                        $display("[DEADLOCK] S3 ch5 write %0d timeout! be=%0d mux=%0d busy=%0b",
                                 i, u_backend.state, u_mux.state, ddram_busy);
                        s_fail++;
                        disable ch5_thread;
                    end
                end
            end
        join
        if (s_fail == 0) begin
            $display("[PASS] S3: 300+300 concurrent OK, rd_busy=%0d we_busy=%0d",
                     rd_while_busy, we_while_busy);
            total_pass++;
        end else begin
            $display("[FAIL] S3: concurrent deadlock");
            total_fail++;
        end

        repeat (20) @(posedge clk);

        // =========================================================
        // S4: 并发 ch1读 + ch2写 + ch5写 (三通道)
        // =========================================================
        $display("\n--- S4: 三通道并发 200 ch1读 + 100 ch2写 + 100 ch5写 ---");
        s_pass = 0; s_fail = 0;
        fork
            begin : s4_ch1
                for (int i = 0; i < 200; i++) begin
                    int rok;
                    logic [63:0] rdata;
                    ch1_read_wait(27'h060000 + i * 4, rdata, rok);
                    if (!rok) begin
                        $display("[DEADLOCK] S4 ch1 read %0d timeout!", i);
                        s_fail++; disable s4_ch1;
                    end
                end
            end
            begin : s4_ch2
                for (int i = 0; i < 100; i++) begin
                    int wok;
                    ch2_write_wait(27'h040000 + i * 2, 32'hCC000000 + i, wok);
                    if (!wok) begin
                        $display("[DEADLOCK] S4 ch2 write %0d timeout!", i);
                        s_fail++; disable s4_ch2;
                    end
                end
            end
            begin : s4_ch5
                for (int i = 0; i < 100; i++) begin
                    int wok;
                    ch5_write_wait(27'h400000 + i * 4, 64'hFBFB0000 + i, wok);
                    if (!wok) begin
                        $display("[DEADLOCK] S4 ch5 write %0d timeout!", i);
                        s_fail++; disable s4_ch5;
                    end
                end
            end
        join
        if (s_fail == 0) begin
            $display("[PASS] S4: 3-channel concurrent OK, rd_busy=%0d we_busy=%0d",
                     rd_while_busy, we_while_busy);
            total_pass++;
        end else begin
            $display("[FAIL] S4: 3-channel deadlock");
            total_fail++;
        end

        repeat (20) @(posedge clk);

        // =========================================================
        // S5: 快速写后立即读（最小间隔，复现写流水线竞态）
        // =========================================================
        $display("\n--- S5: 500 快速 ch5写→ch1读 (最小间隔) ---");
        s_pass = 0; s_fail = 0;
        for (int i = 0; i < 500; i++) begin
            // 同一周期发 ch5 写 + ch1 读请求
            @(posedge clk);
            ch5_addr <= 27'h400000 + i * 4;
            ch5_din  <= {32'h55550000 + i, 32'h55550000 + i};
            ch5_rnw  <= 0;
            ch5_req  <= 1;
            ch1_addr <= 27'h060000 + i * 4;
            ch1_rnw  <= 1;
            ch1_req  <= 1;
            @(posedge clk);
            ch5_req  <= 0;
            ch1_req  <= 0;
            // 等 ch5 完成
            begin
                int w;
                for (w = 0; w < 2000; w++) begin
                    @(posedge clk);
                    if (ch5_ready) break;
                end
                if (w >= 2000) begin
                    $display("[DEADLOCK] S5 ch5 write %0d timeout! be=%0d mux=%0d busy=%0b",
                             i, u_backend.state, u_mux.state, ddram_busy);
                    s_fail++; break;
                end
            end
            // 等 ch1 完成（可能已经完成）
            if (!ch1_ready) begin
                int w;
                for (w = 0; w < 2000; w++) begin
                    @(posedge clk);
                    if (ch1_ready) break;
                end
                if (w >= 2000) begin
                    $display("[DEADLOCK] S5 ch1 read %0d timeout! be=%0d mux=%0d busy=%0b",
                             i, u_backend.state, u_mux.state, ddram_busy);
                    s_fail++; break;
                end
            end
            s_pass++;
        end
        if (s_fail == 0) begin
            $display("[PASS] S5: %0d simultaneous wr+rd OK, rd_busy=%0d we_busy=%0d",
                     s_pass, rd_while_busy, we_while_busy);
            total_pass++;
        end else begin
            $display("[FAIL] S5: stalled at op %0d", s_pass);
            total_fail++;
        end

        // =========================================================
        // 汇总
        // =========================================================
        repeat (50) @(posedge clk);
        $display("\n========================================");
        $display("压力测试汇总: PASS=%0d FAIL=%0d", total_pass, total_fail);
        $display("总 RD while BUSY: %0d", rd_while_busy);
        $display("总 WE while BUSY: %0d", we_while_busy);
        if (total_fail > 0 || rd_while_busy > 0 || we_while_busy > 0)
            $display("*** 存在竞态 BUG！***");
        else
            $display("所有压力测试通过");
        $display("========================================");
        $finish;
    end

    // 超时保护
    initial begin
        #100000000; // 100ms
        $display("[TIMEOUT] 仿真超时! rd_busy=%0d we_busy=%0d", rd_while_busy, we_while_busy);
        $finish;
    end

endmodule
