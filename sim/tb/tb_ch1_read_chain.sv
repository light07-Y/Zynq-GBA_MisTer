// tb_ch1_read_chain.sv: ch1 ROM 读链路专项仿真
// 目标：精确定位 ch1_req 为何不触发
//
// 仿真层次：
//   TB 驱动 cache.read_enable / read_addr
//     → cache (mem_read_ena = ch1_req?)
//       → ddram_mux (ch1 → DDRAM_RD?)
//         → ddr_axi_backend (AXI AR?)
//           → axi_mem_model (返回 ROM 数据)
//
// 关键观测点：
//   1. cache 状态机是否从 CLEARCACHE 正确过渡到 IDLE
//   2. read_enable 脉冲后 mem_read_ena 是否拉高
//   3. ddram_mux 是否将 ch1_req 转为 DDRAM_RD
//   4. ddr_axi_backend 是否发出 AXI AR 请求
//   5. AXI 返回数据是否正确传回 cache

`timescale 1ns / 1ps

module tb_ch1_read_chain;

    // ========== 时钟和复位 ==========
    logic clk = 1'b0;
    logic rst_n = 1'b0;
    always #5 clk = ~clk;  // 100 MHz

    // ========== cache 接口信号 ==========
    logic        gb_on;
    logic        cache_read_enable;
    logic [22:0] cache_read_addr;      // SIZEBASEBITS=23
    logic [31:0] cache_read_data;
    logic        cache_read_done;
    logic [63:0] cache_read_full;

    // cache → (mem_read_ena = sdram_read_ena = ch1_req)
    logic        sdram_read_ena;       // = mem_read_ena from cache
    logic        sdram_read_done;      // = mem_read_done to cache
    logic [24:0] sdram_read_addr;      // = mem_read_addr from cache
    logic [31:0] sdram_read_data;      // = mem_read_data to cache
    logic [31:0] sdram_second_dword;   // = mem_read_data2 to cache

    // ========== ddram_mux 接口 ==========
    // ch1 信号
    logic [27:1] ch1_addr;
    logic [63:0] ch1_dout;
    logic [15:0] ch1_din;
    logic        ch1_req;
    logic        ch1_rnw;
    logic        ch1_ready;

    // DDRAM 总线 (mux → backend)
    logic        ddram_busy;
    logic [7:0]  ddram_burstcnt;
    logic [28:0] ddram_addr;
    logic [63:0] ddram_dout;
    logic        ddram_dout_ready;
    logic        ddram_rd;
    logic [63:0] ddram_din;
    logic [7:0]  ddram_be;
    logic        ddram_we;

    // AXI 总线 (backend → mem model)
    logic [31:0] m_axi_awaddr, m_axi_araddr;
    logic [7:0]  m_axi_awlen, m_axi_arlen;
    logic [2:0]  m_axi_awsize, m_axi_arsize;
    logic [1:0]  m_axi_awburst, m_axi_arburst;
    logic        m_axi_awvalid, m_axi_arvalid;
    logic        m_axi_awready, m_axi_arready;
    logic [63:0] m_axi_wdata, m_axi_rdata;
    logic [7:0]  m_axi_wstrb;
    logic        m_axi_wlast, m_axi_rlast;
    logic        m_axi_wvalid, m_axi_rvalid;
    logic        m_axi_wready, m_axi_rready;
    logic [1:0]  m_axi_bresp, m_axi_rresp;
    logic        m_axi_bvalid;
    logic        m_axi_bready;
    logic [31:0] err_vec;
    logic        err_pulse;

    // 未使用通道信号
    logic [63:0] ch2_dout_unused, ch4_dout_unused, ch5_dout_unused;
    logic [15:0] ch3_dout_unused;
    logic        ch2_ready_unused, ch3_ready_unused, ch4_ready_unused, ch5_ready_unused;

    // ========== 参数 ==========
    localparam integer Softmap_GBA_Gamerom_ADDR = 196608;  // 0x30000
    localparam [31:0]  G_DDR_BASE = 32'h1000_0000;

    // ========== DUT: cache ==========
    cache #(
        .SIZE                     (1024),
        .SIZEBASEBITS             (23),
        .BITWIDTH                 (32),
        .Softmap_GBA_Gamerom_ADDR (Softmap_GBA_Gamerom_ADDR)
    ) u_cache (
        .clk            (clk),
        .gb_on          (gb_on),
        .read_enable    (cache_read_enable),
        .read_addr      (cache_read_addr),
        .read_data      (cache_read_data),
        .read_done      (cache_read_done),
        .read_full      (cache_read_full),
        .mem_read_ena   (sdram_read_ena),
        .mem_read_done  (sdram_read_done),
        .mem_read_addr  (sdram_read_addr),
        .mem_read_data  (sdram_read_data),
        .mem_read_data2 (sdram_second_dword)
    );

    // ========== 连线: cache → ch1 → ddram_mux ==========
    assign ch1_addr = {1'b0, sdram_read_addr, 1'b0};
    assign ch1_din  = 16'd0;
    assign ch1_req  = sdram_read_ena;
    assign ch1_rnw  = 1'b1;

    assign sdram_read_done    = ch1_ready;
    assign sdram_read_data    = ch1_dout[31:0];
    assign sdram_second_dword = ch1_dout[63:32];

    // ========== DUT: ddram_mux ==========
    ddram_mux u_ddram_mux (
        .DDRAM_CLK        (clk),
        .DDRAM_BUSY       (ddram_busy),
        .DDRAM_BURSTCNT   (ddram_burstcnt),
        .DDRAM_ADDR       (ddram_addr),
        .DDRAM_DOUT       (ddram_dout),
        .DDRAM_DOUT_READY (ddram_dout_ready),
        .DDRAM_RD         (ddram_rd),
        .DDRAM_DIN        (ddram_din),
        .DDRAM_BE         (ddram_be),
        .DDRAM_WE         (ddram_we),
        // ch1: ROM 读
        .ch1_addr  (ch1_addr),
        .ch1_dout  (ch1_dout),
        .ch1_din   (ch1_din),
        .ch1_req   (ch1_req),
        .ch1_rnw   (ch1_rnw),
        .ch1_ready (ch1_ready),
        // ch2-ch5: 全部空闲
        .ch2_addr  (27'd0), .ch2_dout  (ch2_dout_unused),
        .ch2_din   (32'd0), .ch2_req   (1'b0),
        .ch2_rnw   (1'b1),  .ch2_ready (ch2_ready_unused),
        .ch3_addr  (25'd0), .ch3_dout  (ch3_dout_unused),
        .ch3_din   (16'd0), .ch3_req   (1'b0),
        .ch3_rnw   (1'b1),  .ch3_ready (ch3_ready_unused),
        .ch4_addr  (27'd0), .ch4_dout  (ch4_dout_unused),
        .ch4_din   (64'd0), .ch4_req   (1'b0),
        .ch4_rnw   (1'b1),  .ch4_be    (8'd0),
        .ch4_ready (ch4_ready_unused),
        .ch5_addr  (27'd0), .ch5_dout  (ch5_dout_unused),
        .ch5_din   (64'd0), .ch5_req   (1'b0),
        .ch5_rnw   (1'b1),  .ch5_ready (ch5_ready_unused)
    );

    // ========== DUT: ddr_axi_backend ==========
    ddr_axi_backend_sv #(
        .G_DDR_BASE (G_DDR_BASE)
    ) u_backend (
        .clk              (clk),
        .rst_n            (rst_n),
        .DDRAM_BUSY       (ddram_busy),
        .DDRAM_BURSTCNT   (ddram_burstcnt),
        .DDRAM_ADDR       (ddram_addr),
        .DDRAM_DOUT       (ddram_dout),
        .DDRAM_DOUT_READY (ddram_dout_ready),
        .DDRAM_RD         (ddram_rd),
        .DDRAM_DIN        (ddram_din),
        .DDRAM_BE         (ddram_be),
        .DDRAM_WE         (ddram_we),
        .M_AXI_AWADDR    (m_axi_awaddr),
        .M_AXI_AWLEN     (m_axi_awlen),
        .M_AXI_AWSIZE    (m_axi_awsize),
        .M_AXI_AWBURST   (m_axi_awburst),
        .M_AXI_AWVALID   (m_axi_awvalid),
        .M_AXI_AWREADY   (m_axi_awready),
        .M_AXI_WDATA     (m_axi_wdata),
        .M_AXI_WSTRB     (m_axi_wstrb),
        .M_AXI_WLAST     (m_axi_wlast),
        .M_AXI_WVALID    (m_axi_wvalid),
        .M_AXI_WREADY    (m_axi_wready),
        .M_AXI_BRESP     (m_axi_bresp),
        .M_AXI_BVALID    (m_axi_bvalid),
        .M_AXI_BREADY    (m_axi_bready),
        .M_AXI_ARADDR    (m_axi_araddr),
        .M_AXI_ARLEN     (m_axi_arlen),
        .M_AXI_ARSIZE    (m_axi_arsize),
        .M_AXI_ARBURST   (m_axi_arburst),
        .M_AXI_ARVALID   (m_axi_arvalid),
        .M_AXI_ARREADY   (m_axi_arready),
        .M_AXI_RDATA     (m_axi_rdata),
        .M_AXI_RRESP     (m_axi_rresp),
        .M_AXI_RLAST     (m_axi_rlast),
        .M_AXI_RVALID    (m_axi_rvalid),
        .M_AXI_RREADY    (m_axi_rready),
        .ERR_VEC          (err_vec),
        .ERR_PULSE        (err_pulse)
    );

    // ========== AXI 存储模型 ==========
    axi_mem_model #(
        .MEM_SIZE_BYTES (65536),
        .ROM_HEX_FILE   ("rom_first_4k.hex")
    ) u_axi_mem (
        .clk            (clk),
        .rst_n          (rst_n),
        .S_AXI_ARADDR   (m_axi_araddr),
        .S_AXI_ARLEN    (m_axi_arlen),
        .S_AXI_ARSIZE   (m_axi_arsize),
        .S_AXI_ARBURST  (m_axi_arburst),
        .S_AXI_ARVALID  (m_axi_arvalid),
        .S_AXI_ARREADY  (m_axi_arready),
        .S_AXI_RDATA    (m_axi_rdata),
        .S_AXI_RRESP    (m_axi_rresp),
        .S_AXI_RLAST    (m_axi_rlast),
        .S_AXI_RVALID   (m_axi_rvalid),
        .S_AXI_RREADY   (m_axi_rready),
        .S_AXI_AWADDR   (m_axi_awaddr),
        .S_AXI_AWLEN    (m_axi_awlen),
        .S_AXI_AWSIZE   (m_axi_awsize),
        .S_AXI_AWBURST  (m_axi_awburst),
        .S_AXI_AWVALID  (m_axi_awvalid),
        .S_AXI_AWREADY  (m_axi_awready),
        .S_AXI_WDATA    (m_axi_wdata),
        .S_AXI_WSTRB    (m_axi_wstrb),
        .S_AXI_WLAST    (m_axi_wlast),
        .S_AXI_WVALID   (m_axi_wvalid),
        .S_AXI_WREADY   (m_axi_wready),
        .S_AXI_BRESP    (m_axi_bresp),
        .S_AXI_BVALID   (m_axi_bvalid),
        .S_AXI_BREADY   (m_axi_bready)
    );

    // ========== 监控信号（详细日志）==========
    // cache 内部状态探测（利用层次引用）
    // 注意：Vivado xsim 支持跨层次引用 VHDL 信号

    always @(posedge clk) begin
        // 监控 ch1_req
        if (ch1_req) begin
            $display("[CH1_MON] @%0t ch1_req=1 ch1_addr=0x%07X sdram_addr=0x%07X",
                     $time, ch1_addr, sdram_read_addr);
        end
        // 监控 ch1_ready
        if (ch1_ready) begin
            $display("[CH1_MON] @%0t ch1_ready=1 ch1_dout=0x%016X (lo=0x%08X hi=0x%08X)",
                     $time, ch1_dout, ch1_dout[31:0], ch1_dout[63:32]);
        end
        // 监控 cache_read_done
        if (cache_read_done) begin
            $display("[CACHE_MON] @%0t cache_read_done=1 data=0x%08X full=0x%016X",
                     $time, cache_read_data, cache_read_full);
        end
        // 监控 DDRAM_RD
        if (ddram_rd) begin
            $display("[DDRAM_MON] @%0t DDRAM_RD=1 ADDR=0x%08X BURST=%0d",
                     $time, ddram_addr, ddram_burstcnt);
        end
        // 监控 DDRAM_DOUT_READY
        if (ddram_dout_ready) begin
            $display("[DDRAM_MON] @%0t DDRAM_DOUT_READY=1 DOUT=0x%016X",
                     $time, ddram_dout);
        end
        // 监控 AXI 错误
        if (err_pulse) begin
            $display("[ERROR] @%0t AXI error! ERR_VEC=0x%08X", $time, err_vec);
        end
    end

    // ========== 测试序列 ==========
    integer test_pass = 0;
    integer test_fail = 0;

    task automatic wait_clks(input int n);
        repeat(n) @(posedge clk);
    endtask

    task automatic do_cache_read(
        input [22:0] addr,
        input [31:0] expected_data,
        input string label
    );
        integer timeout;
        $display("\n[TEST] === %s: cache read addr=0x%06X (GBA 0x%08X) ===",
                 label, addr, 32'h08000000 + {addr, 2'b00});

        @(posedge clk);
        cache_read_enable <= 1'b1;
        cache_read_addr   <= addr;
        @(posedge clk);
        cache_read_enable <= 1'b0;

        // 等待 cache_read_done
        timeout = 0;
        while (!cache_read_done && timeout < 200) begin
            @(posedge clk);
            timeout = timeout + 1;
        end

        if (cache_read_done) begin
            $display("[TEST] %s: 完成! data=0x%08X (期望=0x%08X) 延迟=%0d 周期",
                     label, cache_read_data, expected_data, timeout);
            if (cache_read_data == expected_data) begin
                $display("[TEST] %s: ✓ PASS", label);
                test_pass = test_pass + 1;
            end else begin
                $display("[TEST] %s: ✗ FAIL 数据不匹配!", label);
                test_fail = test_fail + 1;
            end
        end else begin
            $display("[TEST] %s: ✗ TIMEOUT (%0d 周期)! cache_read_done 未触发", label, timeout);
            $display("[TEST] %s: 检查 sdram_read_ena=%b ch1_req=%b ddram_rd=%b ddram_busy=%b",
                     label, sdram_read_ena, ch1_req, ddram_rd, ddram_busy);
            test_fail = test_fail + 1;
        end
    endtask

    initial begin
        $display("\n========================================");
        $display(" ch1 ROM 读链路仿真 - 开始");
        $display("========================================");
        $display(" Softmap_GBA_Gamerom_ADDR = 0x%06X", Softmap_GBA_Gamerom_ADDR);
        $display(" G_DDR_BASE              = 0x%08X", G_DDR_BASE);
        $display("========================================\n");

        // 初始化
        gb_on             = 1'b0;
        cache_read_enable = 1'b0;
        cache_read_addr   = 23'd0;
        rst_n             = 1'b0;

        // 复位阶段
        wait_clks(10);
        rst_n = 1'b1;
        $display("[TB] @%0t rst_n 释放", $time);
        wait_clks(5);

        // ====== 测试 1: gb_on 从 0→1, 观察 cache CLEARCACHE ======
        $display("\n[TEST] === 阶段 1: 启动 gb_on, 等待 cache 清空 ===");
        gb_on = 1'b1;
        $display("[TB] @%0t gb_on=1", $time);

        // cache CLEARCACHE 需要 SIZE=1024 个周期
        wait_clks(1100);  // 留些余量
        $display("[TB] @%0t cache 应已完成 CLEARCACHE", $time);

        // ====== 测试 2: 首次 ROM 读 (cold miss) ======
        // GBA 地址 0x08000000 → cache_read_addr = 0x000000
        // 期望数据: ROM offset 0 = 0xEA000032 (入口指令)
        do_cache_read(23'h000000, 32'hEA000032, "T2_ROM_entry");
        wait_clks(10);

        // ====== 测试 3: 相邻地址读 (同 cache line 的另一 DWORD) ======
        // GBA 地址 0x08000004 → cache_read_addr = 0x000001
        // 期望数据: ROM offset 4 = 0x51AEFF24
        do_cache_read(23'h000001, 32'h51AEFF24, "T3_ROM_w1");
        wait_clks(10);

        // ====== 测试 4: 重复读同一地址 (cache hit) ======
        do_cache_read(23'h000000, 32'hEA000032, "T4_cache_hit");
        wait_clks(10);

        // ====== 测试 5: 读入口目标地址 ======
        // 0x080000D0 → ROM offset 0xD0 → DWORD offset 0x34
        // 期望数据: 0xE3A00012
        do_cache_read(23'h000034, 32'hE3A00012, "T5_entry_target");
        wait_clks(10);

        // ====== 测试 6: gb_on 脉冲复位后重新读 ======
        $display("\n[TEST] === 阶段 6: 模拟 sw_reset (gb_on 脉冲) ===");
        gb_on = 1'b0;
        wait_clks(5);
        gb_on = 1'b1;
        $display("[TB] @%0t gb_on 重新置 1", $time);
        wait_clks(1100);
        do_cache_read(23'h000000, 32'hEA000032, "T6_after_reset");
        wait_clks(10);

        // ====== 结果汇总 ======
        $display("\n========================================");
        $display(" 仿真结束: PASS=%0d FAIL=%0d", test_pass, test_fail);
        $display("========================================\n");

        if (test_fail > 0) begin
            $display("*** 存在失败用例 - 请检查波形! ***");
        end else begin
            $display("*** 所有用例通过 ***");
        end

        wait_clks(20);
        $finish;
    end

    // 超时保护
    initial begin
        #100_000;
        $display("[TB] 全局超时 100us! 强制结束");
        $finish;
    end

endmodule
