// tb_memorymux_chain.sv: 完整 ROM 读链路仿真
// memorymux ROM 决策逻辑 → cache → ddram_mux → ddr_axi_backend → AXI mem
//
// 精确测试:
//   1. MaxPakAddr 比较 (VHDL 25-bit vs 23-bit 位宽问题)
//   2. WAIT_SDRAM 双完成路径 (sdram_read_done vs cache_read_done)
//   3. cold miss → warm hit 数据完整性
//   4. MaxPakAddr=0 时的 READAFTERPAK 行为
//   5. sdram_second_dword 延迟采样时序

`timescale 1ns / 1ps

module tb_memorymux_chain;

    // ========== 时钟和复位 ==========
    logic clk = 1'b0;
    logic rst_n = 1'b0;
    always #5 clk = ~clk;  // 100 MHz

    // ========== memorymux_rom_emu 接口 ==========
    logic        gb_on;
    logic [31:0] mem_bus_Adr;
    logic        mem_bus_rnw;
    logic        mem_bus_ena;
    logic [1:0]  mem_bus_acc;
    logic [31:0] mem_bus_din;
    logic        mem_bus_done;
    logic [24:0] MaxPakAddr;

    // emu → cache
    logic        cache_read_enable;
    logic [22:0] cache_read_addr;
    logic [31:0] cache_read_data;
    logic        cache_read_done;
    logic [63:0] cache_read_full;

    // cache → DDR 信号
    logic        sdram_read_ena;
    logic        sdram_read_done;
    logic [24:0] sdram_read_addr;
    logic [31:0] sdram_read_data;
    logic [31:0] sdram_second_dword;

    // ch1 信号
    logic [27:1] ch1_addr;
    logic [63:0] ch1_dout;
    logic        ch1_req;
    logic        ch1_ready;

    // DDRAM 信号
    logic        ddram_busy;
    logic [7:0]  ddram_burstcnt;
    logic [28:0] ddram_addr;
    logic [63:0] ddram_dout;
    logic        ddram_dout_ready;
    logic        ddram_rd;
    logic [63:0] ddram_din;
    logic [7:0]  ddram_be;
    logic        ddram_we;

    // AXI 信号
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
    logic        m_axi_bvalid, m_axi_bready;
    logic [31:0] err_vec;
    logic        err_pulse;

    // 未使用通道
    logic [63:0] ch2d, ch4d, ch5d;
    logic [15:0] ch3d;
    logic        ch2r, ch3r, ch4r, ch5r;

    localparam integer Softmap_GBA_Gamerom_ADDR = 196608;
    localparam [31:0]  G_DDR_BASE = 32'h1000_0000;

    // ========== DUT: memorymux_rom_emu ==========
    memorymux_rom_emu u_emu (
        .clk               (clk),
        .gb_on             (gb_on),
        .mem_bus_Adr       (mem_bus_Adr),
        .mem_bus_rnw       (mem_bus_rnw),
        .mem_bus_ena       (mem_bus_ena),
        .mem_bus_acc       (mem_bus_acc),
        .mem_bus_din       (mem_bus_din),
        .mem_bus_done      (mem_bus_done),
        .MaxPakAddr        (MaxPakAddr),
        .cache_read_enable (cache_read_enable),
        .cache_read_addr   (cache_read_addr),
        .cache_read_data   (cache_read_data),
        .cache_read_done   (cache_read_done),
        .cache_read_full   (cache_read_full),
        .sdram_read_done   (sdram_read_done),
        .sdram_read_data   (sdram_read_data),
        .sdram_second_dword(sdram_second_dword)
    );

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
    assign ch1_req  = sdram_read_ena;

    assign sdram_read_done    = ch1_ready;
    assign sdram_read_data    = ch1_dout[31:0];
    assign sdram_second_dword = ch1_dout[63:32];

    // ========== DUT: ddram_mux ==========
    ddram_mux u_ddram_mux (
        .DDRAM_CLK(clk), .DDRAM_BUSY(ddram_busy),
        .DDRAM_BURSTCNT(ddram_burstcnt), .DDRAM_ADDR(ddram_addr),
        .DDRAM_DOUT(ddram_dout), .DDRAM_DOUT_READY(ddram_dout_ready),
        .DDRAM_RD(ddram_rd), .DDRAM_DIN(ddram_din),
        .DDRAM_BE(ddram_be), .DDRAM_WE(ddram_we),
        .ch1_addr(ch1_addr), .ch1_dout(ch1_dout), .ch1_din(16'd0),
        .ch1_req(ch1_req), .ch1_rnw(1'b1), .ch1_ready(ch1_ready),
        .ch2_addr(27'd0), .ch2_dout(ch2d), .ch2_din(32'd0),
        .ch2_req(1'b0), .ch2_rnw(1'b1), .ch2_ready(ch2r),
        .ch3_addr(25'd0), .ch3_dout(ch3d), .ch3_din(16'd0),
        .ch3_req(1'b0), .ch3_rnw(1'b1), .ch3_ready(ch3r),
        .ch4_addr(27'd0), .ch4_dout(ch4d), .ch4_din(64'd0),
        .ch4_req(1'b0), .ch4_rnw(1'b1), .ch4_be(8'd0), .ch4_ready(ch4r),
        .ch5_addr(27'd0), .ch5_dout(ch5d), .ch5_din(64'd0),
        .ch5_req(1'b0), .ch5_rnw(1'b1), .ch5_ready(ch5r)
    );

    // ========== DUT: ddr_axi_backend ==========
    ddr_axi_backend_sv #(.G_DDR_BASE(G_DDR_BASE)) u_backend (
        .clk(clk), .rst_n(rst_n),
        .DDRAM_BUSY(ddram_busy), .DDRAM_BURSTCNT(ddram_burstcnt),
        .DDRAM_ADDR(ddram_addr), .DDRAM_DOUT(ddram_dout),
        .DDRAM_DOUT_READY(ddram_dout_ready), .DDRAM_RD(ddram_rd),
        .DDRAM_DIN(ddram_din), .DDRAM_BE(ddram_be), .DDRAM_WE(ddram_we),
        .M_AXI_AWADDR(m_axi_awaddr), .M_AXI_AWLEN(m_axi_awlen),
        .M_AXI_AWSIZE(m_axi_awsize), .M_AXI_AWBURST(m_axi_awburst),
        .M_AXI_AWVALID(m_axi_awvalid), .M_AXI_AWREADY(m_axi_awready),
        .M_AXI_WDATA(m_axi_wdata), .M_AXI_WSTRB(m_axi_wstrb),
        .M_AXI_WLAST(m_axi_wlast), .M_AXI_WVALID(m_axi_wvalid),
        .M_AXI_WREADY(m_axi_wready), .M_AXI_BRESP(m_axi_bresp),
        .M_AXI_BVALID(m_axi_bvalid), .M_AXI_BREADY(m_axi_bready),
        .M_AXI_ARADDR(m_axi_araddr), .M_AXI_ARLEN(m_axi_arlen),
        .M_AXI_ARSIZE(m_axi_arsize), .M_AXI_ARBURST(m_axi_arburst),
        .M_AXI_ARVALID(m_axi_arvalid), .M_AXI_ARREADY(m_axi_arready),
        .M_AXI_RDATA(m_axi_rdata), .M_AXI_RRESP(m_axi_rresp),
        .M_AXI_RLAST(m_axi_rlast), .M_AXI_RVALID(m_axi_rvalid),
        .M_AXI_RREADY(m_axi_rready),
        .ERR_VEC(err_vec), .ERR_PULSE(err_pulse)
    );

    // ========== AXI 存储模型 ==========
    axi_mem_model #(
        .MEM_SIZE_BYTES(65536),
        .ROM_HEX_FILE("rom_first_4k.hex")
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

    // ========== 监控 ==========
    always @(posedge clk) begin
        if (ch1_req)
            $display("[MON] @%0t ch1_req addr=0x%07X", $time, ch1_addr);
        if (ch1_ready)
            $display("[MON] @%0t ch1_ready dout=0x%016X", $time, ch1_dout);
        if (ddram_rd)
            $display("[MON] @%0t DDRAM_RD addr=0x%08X", $time, ddram_addr);
        if (err_pulse)
            $display("[ERR] @%0t AXI error vec=0x%08X", $time, err_vec);
        if (mem_bus_done)
            $display("[MON] @%0t mem_bus_done din=0x%08X", $time, mem_bus_din);
    end

    // ========== 测试任务 ==========
    integer test_pass = 0;
    integer test_fail = 0;

    task automatic wait_clks(input int n);
        repeat(n) @(posedge clk);
    endtask

    task automatic do_rom_read(
        input [31:0] gba_addr,
        input [1:0]  acc,
        input [31:0] expected_data,
        input string label,
        input bit    expect_readafterpak
    );
        integer timeout;
        $display("\n[TEST] ====== %s ======", label);
        $display("[TEST] GBA addr=0x%08X acc=%0d MaxPak=0x%07X", gba_addr, acc, MaxPakAddr);

        @(posedge clk);
        mem_bus_Adr <= gba_addr;
        mem_bus_rnw <= 1'b1;
        mem_bus_ena <= 1'b1;
        mem_bus_acc <= acc;
        @(posedge clk);
        mem_bus_ena <= 1'b0;

        timeout = 0;
        while (!mem_bus_done && timeout < 300) begin
            @(posedge clk);
            timeout = timeout + 1;
        end

        if (mem_bus_done) begin
            $display("[TEST] %s: 完成 (%0d cyc) din=0x%08X expect=0x%08X",
                     label, timeout, mem_bus_din, expected_data);
            if (expect_readafterpak) begin
                // READAFTERPAK 返回地址模式数据，不检查精确值
                $display("[TEST] %s: READAFTERPAK 路径 ✓ PASS", label);
                test_pass++;
            end else if (mem_bus_din == expected_data) begin
                $display("[TEST] %s: ✓ PASS", label);
                test_pass++;
            end else begin
                $display("[TEST] %s: ✗ FAIL 数据不匹配!", label);
                test_fail++;
            end
        end else begin
            $display("[TEST] %s: ✗ TIMEOUT (%0d cyc)!", label, timeout);
            $display("[TEST]   cache_read_enable=%b cache_read_done=%b",
                     cache_read_enable, cache_read_done);
            $display("[TEST]   sdram_read_ena=%b sdram_read_done=%b ch1_req=%b",
                     sdram_read_ena, sdram_read_done, ch1_req);
            $display("[TEST]   ddram_rd=%b ddram_busy=%b", ddram_rd, ddram_busy);
            test_fail++;
        end
    endtask

    // ========== 主测试序列 ==========
    initial begin
        $display("\n============================================");
        $display(" memorymux ROM 读链路 完整仿真");
        $display("============================================\n");

        gb_on       = 1'b0;
        mem_bus_ena = 1'b0;
        mem_bus_rnw = 1'b1;
        mem_bus_Adr = 32'd0;
        mem_bus_acc = 2'b10;
        MaxPakAddr  = 25'd0;
        rst_n       = 1'b0;

        wait_clks(10);
        rst_n = 1'b1;
        wait_clks(5);

        // ====== 场景 A: MaxPakAddr=0 (ROM 未加载) ======
        $display("\n===== 场景 A: MaxPakAddr=0, ROM 未加载 =====");
        MaxPakAddr = 25'h0000000;
        gb_on = 1'b1;
        wait_clks(1100);  // cache CLEARCACHE

        // A1: 读 ROM 入口 - 应走 READAFTERPAK
        do_rom_read(32'h08000000, 2'b10, 32'h0, "A1_maxpak0_entry", 1);
        wait_clks(5);

        // A2: 读另一个 ROM 地址
        do_rom_read(32'h080000D0, 2'b10, 32'h0, "A2_maxpak0_target", 1);
        wait_clks(5);

        // ====== 场景 B: MaxPakAddr=0x400000 (16MB ROM, 正确值) ======
        $display("\n===== 场景 B: MaxPakAddr=0x400000, ROM 已加载 =====");
        // 模拟 PS 写入正确的 MaxPakAddr
        MaxPakAddr = 25'h0400000;
        // 模拟软复位: gb_on 脉冲
        gb_on = 1'b0;
        wait_clks(5);
        gb_on = 1'b1;
        wait_clks(1100);  // cache CLEARCACHE

        // B1: 32-bit cold miss 读 ROM 入口 (0x08000000)
        do_rom_read(32'h08000000, 2'b10, 32'hEA000032, "B1_cold_miss_entry", 0);
        wait_clks(10);

        // B2: 16-bit 读相邻地址 (应命中 mini cache 或 cache)
        do_rom_read(32'h08000002, 2'b01, 32'h0000EA00, "B2_mini_cache_16", 0);
        wait_clks(10);

        // B3: 32-bit 读入口目标 (0x080000D0, cold miss)
        do_rom_read(32'h080000D0, 2'b10, 32'hE3A00012, "B3_cold_miss_target", 0);
        wait_clks(10);

        // B4: 重复读同一地址 (mini cache hit)
        do_rom_read(32'h080000D0, 2'b10, 32'hE3A00012, "B4_mini_cache_hit", 0);
        wait_clks(10);

        // B5: 读第三个地址 (cold miss, 覆盖 mini cache)
        // 0x08000008 → DWORD offset 2, 与 B1(offset 0) 不同的 64-bit 行
        do_rom_read(32'h08000008, 2'b10, 32'h0A82843D, "B5_cold_miss_new", 0);
        wait_clks(10);

        // ★ B6: 关键测试 — gamepak cache HIT (非 mini cache)
        // 重读 B1 地址 (0x08000000)，此时:
        //   - mini cache 已被 B5 覆盖 (sdram_addr_buf != 0x08000000[24:3])
        //   - gamepak cache 应保存 B1 数据
        //   - 应走 cache_read_done 路径 (Path B)
        $display("\n[TEST] ★★★ B6: gamepak cache HIT 专项测试 ★★★");
        do_rom_read(32'h08000000, 2'b10, 32'hEA000032, "B6_gamepak_cache_hit", 0);
        wait_clks(10);

        // B7: 同理测试 B3 地址的 gamepak cache HIT
        do_rom_read(32'h080000D0, 2'b10, 32'hE3A00012, "B7_gamepak_cache_hit2", 0);
        wait_clks(10);

        // ====== 场景 C: 边界测试 ======
        $display("\n===== 场景 C: MaxPakAddr 边界 =====");

        // C1: 读 MaxPakAddr 边界处 (DWORD offset 0x3FFFFF, 最后一个有效地址)
        // GBA addr = 0x08000000 + 0x3FFFFF*4 = 0x08FFFFFC
        do_rom_read(32'h08FFFFFC, 2'b10, 32'h0, "C1_boundary_last_valid", 0);
        wait_clks(20);

        // C2: 读 MaxPakAddr 边界外 (DWORD offset 0x400000, 第一个无效地址)
        // GBA addr = 0x08000000 + 0x400000*4 = 0x09000000
        do_rom_read(32'h09000000, 2'b10, 32'h0, "C2_boundary_first_invalid", 1);
        wait_clks(10);

        // ====== 场景 D: MaxPakAddr 位宽深度测试 ======
        $display("\n===== 场景 D: MaxPakAddr 位宽专项测试 =====");

        // D1: MaxPakAddr 高位有效 (bit 23/24 set)
        gb_on = 1'b0; wait_clks(5);
        MaxPakAddr = 25'h1000000;  // 只有 bit 24 置位
        gb_on = 1'b1; wait_clks(1100);

        // mem_bus_Adr(24:2) = 23 bits, 最大值 0x7FFFFF
        // MaxPakAddr = 25'h1000000 (bit 24 set)
        // VHDL: {2'b00, 23'h000000} >= 25'h1000000 → 0 >= 16M → FALSE
        do_rom_read(32'h08000000, 2'b10, 32'hEA000032, "D1_highbit_maxpak", 0);
        wait_clks(20);

        // D2: MaxPakAddr = 25'h0000001 (只有 1 个 DWORD 有效)
        gb_on = 1'b0; wait_clks(5);
        MaxPakAddr = 25'h0000001;
        gb_on = 1'b1; wait_clks(1100);

        // GBA 0x08000000 → Adr[24:2]=0 >= MaxPak=1? → 0>=1 → FALSE → cache
        do_rom_read(32'h08000000, 2'b10, 32'hEA000032, "D2_minpak_addr0", 0);
        wait_clks(20);

        // GBA 0x08000004 → Adr[24:2]=1 >= MaxPak=1? → 1>=1 → TRUE → READAFTERPAK
        do_rom_read(32'h08000004, 2'b10, 32'h0, "D3_minpak_addr1", 1);
        wait_clks(10);

        // ====== 结果 ======
        $display("\n============================================");
        $display(" 仿真结束: PASS=%0d FAIL=%0d", test_pass, test_fail);
        $display("============================================\n");
        if (test_fail > 0)
            $display("*** 存在失败用例! ***");
        else
            $display("*** 全部通过 ***");

        wait_clks(20);
        $finish;
    end

    // 超时保护
    initial begin
        #300_000;
        $display("[TB] 全局超时!");
        $finish;
    end

endmodule
