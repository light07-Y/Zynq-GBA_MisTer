// tb_ddr_definitive.sv: 定位 rd=1+busy=1 竞态的决定性仿真
// 策略:
//   1. 随机 AXI 延迟（暴露隐藏时序窗口）
//   2. SVA 断言精确到周期
//   3. 数据正确性校验（检测数据损坏→CPU死循环假说）
//   4. 多通道并发读（ch1+ch2 同时读，模拟硬件纯读场景）
//   5. 缓存命中/未命中交替模式
//   6. 写+读混合（验证 ram_write 守卫在随机延迟下是否稳健）
//   7. 10000+ 操作
`timescale 1ns / 1ps

module tb_ddr_definitive;

    logic clk = 0;
    always #5 clk = ~clk;
    logic rst_n;

    // === ddram_mux ↔ backend 连线 ===
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

    // === 通道信号（所有初始化为安静状态）===
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
    logic        ch5_req = 0, ch5_rnw = 1;
    logic [63:0] ch5_dout; logic ch5_ready;

    // =========================================================================
    // SVA 断言: 核心协议检查
    // =========================================================================
    // A1: DDRAM_RD 不应在 DDRAM_BUSY=1 时有效
    property p_rd_not_while_busy;
        @(posedge clk) disable iff (!rst_n)
        ddram_rd |-> !ddram_busy;
    endproperty
    assert property (p_rd_not_while_busy) else begin
        $display("[SVA FAIL] @%0t DDRAM_RD=1 while BUSY=1! backend=%0d mux=%0d",
                 $time, u_backend.state, u_mux.state);
        $display("  ram_read=%b ram_write=%b ch_rq=0x%02X active=%0d",
                 u_mux.ram_read, u_mux.ram_write, u_mux.ch_rq, u_mux.active_ch);
    end

    // A2: DDRAM_WE 不应在 DDRAM_BUSY=1 时有效
    property p_we_not_while_busy;
        @(posedge clk) disable iff (!rst_n)
        ddram_we |-> !ddram_busy;
    endproperty
    assert property (p_we_not_while_busy) else begin
        $display("[SVA FAIL] @%0t DDRAM_WE=1 while BUSY=1!", $time);
    end

    // A3: DDRAM_RD 和 DDRAM_WE 不应同时有效
    property p_rd_we_mutex;
        @(posedge clk) disable iff (!rst_n)
        !(ddram_rd && ddram_we);
    endproperty
    assert property (p_rd_we_mutex) else
        $display("[SVA FAIL] @%0t RD and WE both active!", $time);

    // =========================================================================
    // 统计与 CHAIN 模拟
    // =========================================================================
    int total_ops = 0, total_rd = 0, total_we = 0;
    int a1_fails = 0, a2_fails = 0, a3_fails = 0;
    int data_errors = 0;

    // 模拟硬件 CHAIN 捕获
    logic [31:0] chain_ddr_first_addr, chain_ddr_first_meta;
    logic [31:0] chain_ddr_last_addr,  chain_ddr_last_meta;
    logic        chain_ddr_seen = 0;
    logic [7:0]  chain_ddr_count = 0;

    always_ff @(posedge clk) begin
        if (rst_n && (ddram_rd || ddram_we)) begin
            chain_ddr_last_addr <= {ddram_addr, 3'b000};
            chain_ddr_last_meta <= {13'd0, ddram_busy, ddram_we, ddram_rd,
                                    ddram_burstcnt, chain_ddr_count};
            if (!chain_ddr_seen) begin
                chain_ddr_first_addr <= {ddram_addr, 3'b000};
                chain_ddr_first_meta <= {13'd0, ddram_busy, ddram_we, ddram_rd,
                                         ddram_burstcnt, chain_ddr_count};
                chain_ddr_seen <= 1;
            end
            if (chain_ddr_count < 8'hFF) chain_ddr_count <= chain_ddr_count + 1;

            // 统计
            if (ddram_rd) total_rd <= total_rd + 1;
            if (ddram_we) total_we <= total_we + 1;
            total_ops <= total_ops + 1;

            // SVA 失败计数（冗余检查）
            if (ddram_rd && ddram_busy) a1_fails <= a1_fails + 1;
            if (ddram_we && ddram_busy) a2_fails <= a2_fails + 1;
        end
    end

    // =========================================================================
    // DUT 实例化
    // =========================================================================
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

    axi_mem_model_random #(
        .MEM_SIZE_BYTES(1048576),
        .ROM_HEX_FILE("../rom_data/rom_first_4k.hex"),
        .MIN_AR_DELAY(0), .MAX_AR_DELAY(8),
        .MIN_R_DELAY(0),  .MAX_R_DELAY(15),
        .MIN_AW_DELAY(0), .MAX_AW_DELAY(4),
        .MIN_B_DELAY(0),  .MAX_B_DELAY(6),
        .SEED(32'hCAFE_BABE)
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

    // =========================================================================
    // 辅助 task
    // =========================================================================
    task automatic ch1_read(input logic [27:1] addr, output logic [63:0] data, output int ok);
        @(posedge clk); ch1_addr <= addr; ch1_rnw <= 1; ch1_req <= 1;
        @(posedge clk); ch1_req <= 0;
        ok = 0;
        for (int w = 0; w < 5000; w++) begin
            @(posedge clk);
            if (ch1_ready) begin data = ch1_dout; ok = 1; return; end
        end
    endtask

    task automatic ch2_read(input logic [27:1] addr, output logic [31:0] data, output int ok);
        @(posedge clk); ch2_addr <= addr; ch2_rnw <= 1; ch2_req <= 1;
        @(posedge clk); ch2_req <= 0;
        ok = 0;
        for (int w = 0; w < 5000; w++) begin
            @(posedge clk);
            if (ch2_ready) begin data = ch2_dout; ok = 1; return; end
        end
    endtask

    task automatic ch5_write(input logic [27:1] addr, input logic [63:0] wdata, output int ok);
        @(posedge clk); ch5_addr <= addr; ch5_din <= wdata; ch5_rnw <= 0; ch5_req <= 1;
        @(posedge clk); ch5_req <= 0;
        ok = 0;
        for (int w = 0; w < 5000; w++) begin
            @(posedge clk);
            if (ch5_ready) begin ok = 1; return; end
        end
    endtask

    // =========================================================================
    // 计算期望数据（与 AXI 内存模型一致）
    // =========================================================================
    function automatic logic [63:0] expected_qword(input logic [27:1] ch1_a);
        // ch1_addr 是 halfword 地址，DDR byte addr = {4'b0011, ch1_a[27:3], 3'b0}
        // 后端映射: AXI addr = DDR_BASE + (full_addr - MISTER_BASE)
        //         = 0x10000000 + ({4'b0011, ch1_a[27:3], 3'b0} - 0x30000000)
        //         = 0x10000000 + {ch1_a[27:3], 3'b0}
        logic [31:0] axi_addr;
        logic [31:0] dw_idx;
        axi_addr = 32'h1000_0000 + {5'd0, ch1_a[27:3], 3'b0};
        dw_idx = (axi_addr - 32'h1000_0000) >> 2;
        return {u_axi_mem.mem[dw_idx+1], u_axi_mem.mem[dw_idx]};
    endfunction

    // =========================================================================
    // 主测试
    // =========================================================================
    int ok, s_pass, s_fail, s_derr;
    logic [63:0] rd64;
    logic [31:0] rd32;
    int total_pass = 0, total_fail = 0;

    initial begin
        $display("================================================================");
        $display("tb_ddr_definitive: 随机AXI延迟 + SVA断言 + 数据校验");
        $display("AXI delay ranges: AR[0..8] R[0..15] AW[0..4] B[0..6]");
        $display("================================================================");
        rst_n = 0; repeat (10) @(posedge clk); rst_n = 1; repeat (5) @(posedge clk);

        // =================================================================
        // T1: 1000 纯 ch1 读（顺序地址，全 cache miss）
        // =================================================================
        $display("\n--- T1: 1000 ch1 顺序读 (全 cache miss) ---");
        s_pass = 0; s_fail = 0; s_derr = 0;
        for (int i = 0; i < 1000; i++) begin
            logic [27:1] addr;
            addr = 27'h060000 + i * 4; // 每次不同 cache line
            ch1_read(addr, rd64, ok);
            if (!ok) begin
                $display("[DEADLOCK] T1 #%0d timeout be=%0d mux=%0d busy=%b",
                         i, u_backend.state, u_mux.state, ddram_busy);
                s_fail++; break;
            end
            // 数据校验
            if (rd64 !== expected_qword(addr)) begin
                if (s_derr < 5)
                    $display("[DATA ERR] T1 #%0d addr=%07X got=%016X exp=%016X",
                             i, addr, rd64, expected_qword(addr));
                s_derr++;
            end
            s_pass++;
        end
        $display("T1: %0d/%0d pass, data_err=%0d, SVA_a1=%0d", s_pass, 1000, s_derr, a1_fails);
        if (s_fail == 0 && s_derr == 0) total_pass++; else total_fail++;

        repeat (20) @(posedge clk);

        // =================================================================
        // T2: 1000 ch1 读（随机地址，缓存命中+未命中混合）
        // =================================================================
        $display("\n--- T2: 1000 ch1 随机地址读 (命中+未命中) ---");
        s_pass = 0; s_fail = 0; s_derr = 0;
        begin
            logic [27:1] addr_pool [0:31]; // 32 个地址池
            for (int j = 0; j < 32; j++)
                addr_pool[j] = 27'h060000 + j * 4;
            for (int i = 0; i < 1000; i++) begin
                logic [27:1] addr;
                // 70% 概率复用上一次地址（缓存命中）
                if (i > 0 && (i % 10) < 7)
                    addr = addr_pool[(i * 7) % 32]; // 伪随机但重复
                else
                    addr = 27'h060000 + (i + 1000) * 4; // 新地址
                ch1_read(addr, rd64, ok);
                if (!ok) begin
                    $display("[DEADLOCK] T2 #%0d timeout", i);
                    s_fail++; break;
                end
                if (rd64 !== expected_qword(addr)) s_derr++;
                s_pass++;
            end
        end
        $display("T2: %0d/%0d pass, data_err=%0d, SVA_a1=%0d", s_pass, 1000, s_derr, a1_fails);
        if (s_fail == 0 && s_derr == 0) total_pass++; else total_fail++;

        repeat (20) @(posedge clk);

        // =================================================================
        // T3: 500 交替 ch5写 → ch1读 (随机AXI延迟下写→读保护)
        // =================================================================
        $display("\n--- T3: 500 交替 ch5写→ch1读 ---");
        s_pass = 0; s_fail = 0; s_derr = 0;
        for (int i = 0; i < 500; i++) begin
            ch5_write(27'h400000 + i*4, {32'hFB000000+i, 32'hFB000000+i}, ok);
            if (!ok) begin $display("[DEADLOCK] T3 ch5 write #%0d", i); s_fail++; break; end
            ch1_read(27'h060000 + (i+2000)*4, rd64, ok);
            if (!ok) begin $display("[DEADLOCK] T3 ch1 read #%0d", i); s_fail++; break; end
            if (rd64 !== expected_qword(27'h060000 + (i+2000)*4)) s_derr++;
            s_pass++;
        end
        $display("T3: %0d/%0d pass, data_err=%0d, SVA_a1=%0d a2=%0d",
                 s_pass, 500, s_derr, a1_fails, a2_fails);
        if (s_fail == 0 && s_derr == 0) total_pass++; else total_fail++;

        repeat (20) @(posedge clk);

        // =================================================================
        // T4: 并发 ch1读 + ch2读 (纯读多通道，模拟硬件场景)
        // =================================================================
        $display("\n--- T4: 并发 500 ch1读 + 500 ch2读 ---");
        s_pass = 0; s_fail = 0; s_derr = 0;
        fork
            begin : t4_ch1
                for (int i = 0; i < 500; i++) begin
                    int rok; logic [63:0] d;
                    ch1_read(27'h060000 + (i+3000)*4, d, rok);
                    if (!rok) begin
                        $display("[DEADLOCK] T4 ch1 #%0d be=%0d mux=%0d busy=%b",
                                 i, u_backend.state, u_mux.state, ddram_busy);
                        s_fail++; disable t4_ch1;
                    end
                end
            end
            begin : t4_ch2
                for (int i = 0; i < 500; i++) begin
                    int rok; logic [31:0] d;
                    ch2_read(27'h040000 + i*2, d, rok);
                    if (!rok) begin
                        $display("[DEADLOCK] T4 ch2 #%0d", i);
                        s_fail++; disable t4_ch2;
                    end
                end
            end
        join
        $display("T4: fail=%0d, SVA_a1=%0d total_ddr_ops=%0d", s_fail, a1_fails, total_ops);
        if (s_fail == 0) total_pass++; else total_fail++;

        repeat (20) @(posedge clk);

        // =================================================================
        // T5: 三通道混合: ch1读 + ch2读 + ch5写 (最大压力)
        // =================================================================
        $display("\n--- T5: 三通道 300 ch1读 + 200 ch2读 + 200 ch5写 ---");
        s_pass = 0; s_fail = 0;
        fork
            begin : t5_ch1
                for (int i = 0; i < 300; i++) begin
                    int rok; logic [63:0] d;
                    ch1_read(27'h060000 + (i+5000)*4, d, rok);
                    if (!rok) begin
                        $display("[DEADLOCK] T5 ch1 #%0d", i); s_fail++; disable t5_ch1;
                    end
                end
            end
            begin : t5_ch2
                for (int i = 0; i < 200; i++) begin
                    int rok; logic [31:0] d;
                    ch2_read(27'h040000 + (i+500)*2, d, rok);
                    if (!rok) begin
                        $display("[DEADLOCK] T5 ch2 #%0d", i); s_fail++; disable t5_ch2;
                    end
                end
            end
            begin : t5_ch5
                for (int i = 0; i < 200; i++) begin
                    int wok;
                    ch5_write(27'h400000 + (i+500)*4, 64'hABCD0000 + i, wok);
                    if (!wok) begin
                        $display("[DEADLOCK] T5 ch5 #%0d", i); s_fail++; disable t5_ch5;
                    end
                end
            end
        join
        $display("T5: fail=%0d, SVA_a1=%0d a2=%0d total_ops=%0d",
                 s_fail, a1_fails, a2_fails, total_ops);
        if (s_fail == 0) total_pass++; else total_fail++;

        // =================================================================
        // 汇总
        // =================================================================
        repeat (50) @(posedge clk);
        $display("\n================================================================");
        $display("决定性仿真汇总: PASS=%0d FAIL=%0d", total_pass, total_fail);
        $display("总 DDR 操作: %0d (rd=%0d we=%0d)", total_ops, total_rd, total_we);
        $display("SVA 失败: A1(rd+busy)=%0d A2(we+busy)=%0d A3(rd&we)=%0d",
                 a1_fails, a2_fails, a3_fails);
        $display("数据错误: %0d", data_errors);
        $display("CHAIN 模拟: first=0x%08X meta=0x%08X last=0x%08X meta=0x%08X",
                 chain_ddr_first_addr, chain_ddr_first_meta,
                 chain_ddr_last_addr, chain_ddr_last_meta);
        if (total_fail == 0 && a1_fails == 0 && a2_fails == 0)
            $display("*** 所有测试通过 — ddram_mux+backend 协议正确 ***");
        else
            $display("*** 存在问题！***");
        $display("================================================================");
        $finish;
    end

    // 超时保护
    initial begin
        #500000000; // 500ms
        $display("[TIMEOUT] ops=%0d a1=%0d a2=%0d", total_ops, a1_fails, a2_fails);
        $finish;
    end

endmodule
