// tb_memorymux_real.sv
// 用真实 gba_memorymux.vhd (通过 VHDL wrapper) + ddram_mux + ddr_axi_backend + axi_mem_model
// 端到端验证 ROM 读路径，不使用任何行为模型替代

`timescale 1ns / 1ps

module tb_memorymux_real;

    logic clk = 0;
    always #5 clk = ~clk;  // 100MHz
    logic rst_n = 0;

    // === memorymux wrapper 端口 ===
    logic        gb_on = 0;
    logic [31:0] mem_bus_Adr = 0;
    logic        mem_bus_rnw = 0;
    logic        mem_bus_ena = 0;
    logic [1:0]  mem_bus_acc = 0;
    logic [31:0] mem_bus_dout = 0;
    logic [31:0] mem_bus_din;
    logic        mem_bus_done;

    logic        sdram_read_ena;
    logic        sdram_read_done;
    logic [24:0] sdram_read_addr;
    logic [31:0] sdram_read_data;
    logic [31:0] sdram_second_dword;

    logic [24:0] MaxPakAddr = 25'h0400000;  // 8MB ROM
    logic        memory_remap = 0;
    logic        SramFlashEnable = 0;

    // === ddram_mux 端口 ===
    logic        ddram_busy;
    logic [7:0]  ddram_burstcnt;
    logic [28:0] ddram_addr;
    logic [63:0] ddram_dout;
    logic        ddram_dout_ready;
    logic        ddram_rd;
    logic [63:0] ddram_din;
    logic [7:0]  ddram_be;
    logic        ddram_we;

    logic [27:1] ch1_addr;
    logic [63:0] ch1_dout, ch1_din;
    logic        ch1_ready;
    logic        ch1_req, ch1_rnw;

    // === AXI 信号 ===
    logic [31:0] m_axi_araddr;
    logic [7:0]  m_axi_arlen;
    logic [2:0]  m_axi_arsize;
    logic [1:0]  m_axi_arburst;
    logic        m_axi_arvalid, m_axi_arready;
    logic [63:0] m_axi_rdata;
    logic [1:0]  m_axi_rresp;
    logic        m_axi_rlast, m_axi_rvalid, m_axi_rready;
    logic [31:0] m_axi_awaddr;
    logic [7:0]  m_axi_awlen;
    logic [2:0]  m_axi_awsize;
    logic [1:0]  m_axi_awburst;
    logic        m_axi_awvalid, m_axi_awready;
    logic [63:0] m_axi_wdata;
    logic [7:0]  m_axi_wstrb;
    logic        m_axi_wlast, m_axi_wvalid, m_axi_wready;
    logic [1:0]  m_axi_bresp;
    logic        m_axi_bvalid, m_axi_bready;

    // ====================================================
    // 实例化真实 memorymux (VHDL wrapper)
    // ====================================================
    memorymux_real_wrapper u_mux_real (
        .clk100             (clk),
        .gb_on              (gb_on),
        .mem_bus_Adr        (mem_bus_Adr),
        .mem_bus_rnw        (mem_bus_rnw),
        .mem_bus_ena        (mem_bus_ena),
        .mem_bus_acc        (mem_bus_acc),
        .mem_bus_dout       (mem_bus_dout),
        .mem_bus_din        (mem_bus_din),
        .mem_bus_done       (mem_bus_done),
        .sdram_read_ena     (sdram_read_ena),
        .sdram_read_done    (sdram_read_done),
        .sdram_read_addr    (sdram_read_addr),
        .sdram_read_data    (sdram_read_data),
        .sdram_second_dword (sdram_second_dword),
        .MaxPakAddr         (MaxPakAddr),
        .memory_remap       (memory_remap),
        .SramFlashEnable    (SramFlashEnable),
        .bus_out_Din       (),
        .bus_out_Dout      (32'd0),
        .bus_out_Adr       (),
        .bus_out_rnw       (),
        .bus_out_ena       (),
        .bus_out_done      (1'b0)
    );

    // ====================================================
    // 真实 ddram_mux (只用 ch1，其余 tie-off)
    // ====================================================
    assign ch1_addr = {1'b0, sdram_read_addr, 1'b0};
    assign ch1_din  = 16'd0;
    assign ch1_req  = sdram_read_ena;
    assign ch1_rnw  = 1'b1;

    assign sdram_read_done    = ch1_ready;
    assign sdram_read_data    = ch1_dout[31:0];
    assign sdram_second_dword = ch1_dout[63:32];

    ddram_mux u_ddram_mux (
        .DDRAM_CLK    (clk),
        // DDRAM 接口
        .DDRAM_BUSY   (ddram_busy),
        .DDRAM_BURSTCNT(ddram_burstcnt),
        .DDRAM_ADDR   (ddram_addr),
        .DDRAM_DOUT   (ddram_dout),
        .DDRAM_DOUT_READY(ddram_dout_ready),
        .DDRAM_RD     (ddram_rd),
        .DDRAM_DIN    (ddram_din),
        .DDRAM_BE     (ddram_be),
        .DDRAM_WE     (ddram_we),
        // ch1: ROM 读
        .ch1_addr     (ch1_addr),
        .ch1_dout     (ch1_dout),
        .ch1_din      (ch1_din),
        .ch1_req      (ch1_req),
        .ch1_rnw      (ch1_rnw),
        .ch1_ready    (ch1_ready),
        // ch2-ch5: 未使用
        .ch2_addr     (27'd0), .ch2_din(32'd0), .ch2_req(1'b0), .ch2_rnw(1'b1),
        .ch2_dout     (), .ch2_ready(),
        .ch3_addr     (25'd0), .ch3_din(16'd0), .ch3_req(1'b0), .ch3_rnw(1'b1),
        .ch3_dout     (), .ch3_ready(),
        .ch4_addr     (27'd0), .ch4_din(64'd0), .ch4_req(1'b0), .ch4_rnw(1'b1), .ch4_be(8'd0),
        .ch4_dout     (), .ch4_ready(),
        .ch5_addr     (27'd0), .ch5_din(64'd0), .ch5_req(1'b0), .ch5_rnw(1'b1),
        .ch5_dout     (), .ch5_ready()
    );

    // ====================================================
    // 真实 ddr_axi_backend
    // ====================================================
    ddr_axi_backend_sv #(
        .G_DDR_BASE(32'h1000_0000)
    ) u_backend (
        .clk           (clk),
        .rst_n         (rst_n),
        .DDRAM_BUSY    (ddram_busy),
        .DDRAM_BURSTCNT(ddram_burstcnt),
        .DDRAM_ADDR    (ddram_addr),
        .DDRAM_DOUT    (ddram_dout),
        .DDRAM_DOUT_READY(ddram_dout_ready),
        .DDRAM_RD      (ddram_rd),
        .DDRAM_DIN     (ddram_din),
        .DDRAM_BE      (ddram_be),
        .DDRAM_WE      (ddram_we),
        .M_AXI_ARADDR  (m_axi_araddr),
        .M_AXI_ARLEN   (m_axi_arlen),
        .M_AXI_ARSIZE  (m_axi_arsize),
        .M_AXI_ARBURST (m_axi_arburst),
        .M_AXI_ARVALID (m_axi_arvalid),
        .M_AXI_ARREADY (m_axi_arready),
        .M_AXI_RDATA   (m_axi_rdata),
        .M_AXI_RRESP   (m_axi_rresp),
        .M_AXI_RLAST   (m_axi_rlast),
        .M_AXI_RVALID  (m_axi_rvalid),
        .M_AXI_RREADY  (m_axi_rready),
        .M_AXI_AWADDR  (m_axi_awaddr),
        .M_AXI_AWLEN   (m_axi_awlen),
        .M_AXI_AWSIZE  (m_axi_awsize),
        .M_AXI_AWBURST (m_axi_awburst),
        .M_AXI_AWVALID (m_axi_awvalid),
        .M_AXI_AWREADY (m_axi_awready),
        .M_AXI_WDATA   (m_axi_wdata),
        .M_AXI_WSTRB   (m_axi_wstrb),
        .M_AXI_WLAST   (m_axi_wlast),
        .M_AXI_WVALID  (m_axi_wvalid),
        .M_AXI_WREADY  (m_axi_wready),
        .M_AXI_BRESP   (m_axi_bresp),
        .M_AXI_BVALID  (m_axi_bvalid),
        .M_AXI_BREADY  (m_axi_bready)
    );

    // ====================================================
    // AXI 存储模型 (预加载真实 ROM hex)
    // ====================================================
    axi_mem_model #(
        .MEM_SIZE_BYTES(1048576),  // 1MB: 覆盖 DDR_BASE+0xC0000 ROM 偏移
        .ROM_HEX_FILE("../rom_data/rom_first_4k.hex")
    ) u_axi_mem (
        .clk(clk), .rst_n(rst_n),
        .S_AXI_ARADDR (m_axi_araddr),  .S_AXI_ARLEN(m_axi_arlen),
        .S_AXI_ARSIZE (m_axi_arsize),  .S_AXI_ARBURST(m_axi_arburst),
        .S_AXI_ARVALID(m_axi_arvalid), .S_AXI_ARREADY(m_axi_arready),
        .S_AXI_RDATA  (m_axi_rdata),   .S_AXI_RRESP(m_axi_rresp),
        .S_AXI_RLAST  (m_axi_rlast),   .S_AXI_RVALID(m_axi_rvalid),
        .S_AXI_RREADY (m_axi_rready),
        .S_AXI_AWADDR (m_axi_awaddr),  .S_AXI_AWLEN(m_axi_awlen),
        .S_AXI_AWSIZE (m_axi_awsize),  .S_AXI_AWBURST(m_axi_awburst),
        .S_AXI_AWVALID(m_axi_awvalid), .S_AXI_AWREADY(m_axi_awready),
        .S_AXI_WDATA  (m_axi_wdata),   .S_AXI_WSTRB(m_axi_wstrb),
        .S_AXI_WLAST  (m_axi_wlast),   .S_AXI_WVALID(m_axi_wvalid),
        .S_AXI_WREADY (m_axi_wready),
        .S_AXI_BRESP  (m_axi_bresp),   .S_AXI_BVALID(m_axi_bvalid),
        .S_AXI_BREADY (m_axi_bready)
    );

    // ====================================================
    // 测试逻辑
    // ====================================================
    localparam [1:0] ACCESS_16BIT = 2'b01;
    localparam [1:0] ACCESS_32BIT = 2'b10;

    integer total_pass = 0;
    integer total_fail = 0;

    task automatic wait_clks(input int n);
        repeat(n) @(posedge clk);
    endtask

    // ROM 读: 驱动 mem_bus → 等待 mem_bus_done
    task automatic do_rom_read(
        input [31:0] addr,
        input [1:0]  acc,
        input [31:0] expected,
        input string label,
        input int    max_wait
    );
        integer cnt;
        $display("\n[TEST] === %s === addr=0x%08X acc=%0d expect=0x%08X", label, addr, acc, expected);

        @(posedge clk);
        mem_bus_Adr  <= addr;
        mem_bus_rnw  <= 1'b1;
        mem_bus_ena  <= 1'b1;
        mem_bus_acc  <= acc;
        mem_bus_dout <= 32'd0;
        @(posedge clk);
        mem_bus_ena  <= 1'b0;

        cnt = 0;
        while (!mem_bus_done && cnt < max_wait) begin
            @(posedge clk);
            cnt++;
        end

        if (mem_bus_done) begin
            $display("[TEST] %s: din=0x%08X (%0d cyc)", label, mem_bus_din, cnt);
            if (mem_bus_din === expected) begin
                $display("[TEST] %s: PASS", label);
                total_pass++;
            end else if ($isunknown(mem_bus_din)) begin
                $display("[TEST] %s: FAIL (contains X!)", label);
                total_fail++;
            end else begin
                $display("[TEST] %s: FAIL (got 0x%08X, expected 0x%08X)", label, mem_bus_din, expected);
                total_fail++;
            end
        end else begin
            $display("[TEST] %s: FAIL (TIMEOUT %0d cyc)", label, max_wait);
            total_fail++;
        end
    endtask

    // ROM 原始数据 (PokemonSapphire-E.gba, little-endian DWORDs):
    // Byte  0-3:  0xEA000032
    // Byte  4-7:  0x51AEFF24
    // Byte  8-11: 0x21A29A69
    // Byte 12-15: 0x0A82843D

    initial begin
        $display("\n=============================================");
        $display(" 真实 gba_memorymux 端到端 ROM 读仿真");
        $display("=============================================\n");

        rst_n = 0;
        wait_clks(10);
        rst_n = 1;
        wait_clks(5);
        gb_on = 1;
        wait_clks(1200);  // 等 cache CLEARCACHE 完成

        // ----- 冷未命中测试 (sdram_read_done 路径) -----

        // R1: GBA addr 0x08000000, 32-bit, 偶 DWORD (ch1_addr[2]=0)
        do_rom_read(32'h08000000, ACCESS_32BIT, 32'hEA000032, "R1_cold_even_32", 100);
        wait_clks(5);

        // R2: GBA addr 0x08000004, 32-bit, 奇 DWORD (ch1_addr[2]=1)
        do_rom_read(32'h08000004, ACCESS_32BIT, 32'h51AEFF24, "R2_cold_odd_32", 100);
        wait_clks(5);

        // R3: GBA addr 0x08000008, 32-bit
        do_rom_read(32'h08000008, ACCESS_32BIT, 32'h21A29A69, "R3_cold_0x08", 100);
        wait_clks(5);

        // R4: GBA addr 0x0800000C, 32-bit
        do_rom_read(32'h0800000C, ACCESS_32BIT, 32'h0A82843D, "R4_cold_0x0C", 100);
        wait_clks(5);

        // ----- cache HIT 测试 (cache_read_done 路径) -----

        // R5: 再读 0x08000000 — 应 cache hit
        do_rom_read(32'h08000000, ACCESS_32BIT, 32'hEA000032, "R5_hit_0x00", 100);
        wait_clks(5);

        // R6: 再读 0x08000004 — cache hit
        do_rom_read(32'h08000004, ACCESS_32BIT, 32'h51AEFF24, "R6_hit_0x04", 100);
        wait_clks(5);

        // R7: 再读 0x0800000C — cache hit
        do_rom_read(32'h0800000C, ACCESS_32BIT, 32'h0A82843D, "R7_hit_0x0C", 100);
        wait_clks(5);

        // ----- mini cache 测试 -----

        // R8: 16-bit 读 0x08000000 (mini cache hit, 同 DWORD)
        do_rom_read(32'h08000000, ACCESS_16BIT, 32'h00000032, "R8_mini16_0x00", 100);
        wait_clks(5);

        // R9: 16-bit 读 0x08000002
        do_rom_read(32'h08000002, ACCESS_16BIT, 32'h0000EA00, "R9_mini16_0x02", 100);
        wait_clks(5);

        // ----- 奇 DWORD 冷未命中后 cache HIT 交叉验证 -----
        // (用一个新的、未缓存的地址)

        // R10: 冷未命中 0x08000014 (奇 DWORD: Adr[2]=1)
        // Byte 20-23: 需要检查 ROM
        // R11: cache HIT 0x08000010 (偶 DWORD, 同 cache line)
        // 这会验证奇 DWORD miss 后偶 DWORD hit 是否正确
        // (先跳过精确期望值，只检查是否有 X)

        do_rom_read(32'h08000014, ACCESS_32BIT, 32'h988B2411, "R10_cold_odd_0x14", 100);
        wait_clks(5);

        do_rom_read(32'h08000010, ACCESS_32BIT, 32'hAD09E484, "R11_hit_even_after_odd", 100);
        wait_clks(5);

        // ----- READAFTERPAK 测试 -----

        // R12: 超出 MaxPakAddr 的地址
        do_rom_read(32'h09000000, ACCESS_32BIT, 32'h00010000, "R12_readafterpak", 100);
        wait_clks(5);

        // =============================================================
        // memory_remap=1 测试: 验证地址截断效果
        // 当 memory_remap=1 时, cache_read_addr = "00000" & Adr(19:2)
        // 这意味着只有低 18 位有效 → 1MB 内地址空间
        // 超出 1MB 的地址会 alias 到低地址
        // =============================================================
        $display("\n===== memory_remap=1 地址截断测试 =====");
        memory_remap <= 1'b1;
        wait_clks(5);

        // M1: 读 0x08000000 (低地址, 不受截断影响)
        // 数据应与 R1 相同
        do_rom_read(32'h08000000, ACCESS_32BIT, 32'hEA000032, "M1_remap_low", 100);
        wait_clks(5);

        // M2: 读 0x08100000 (超过 1MB, Adr[24:20]≠0)
        // memory_remap=1 → cache_read_addr = "00000" & Adr(19:2) = 0
        // 所以应该 alias 到地址 0 → 与 0x08000000 相同的 cache line
        // 如果返回 0xEA000032, 说明地址确实被截断了
        do_rom_read(32'h08100000, ACCESS_32BIT, 32'hEA000032, "M2_remap_alias_1MB", 100);
        wait_clks(5);

        // M3: 读 0x08200000 (2MB 处)
        // 同理, alias 到地址 0
        do_rom_read(32'h08200000, ACCESS_32BIT, 32'hEA000032, "M3_remap_alias_2MB", 100);
        wait_clks(5);

        // 恢复 memory_remap=0
        memory_remap <= 1'b0;
        wait_clks(5);

        // M4: 恢复后读 0x08000000, 确认正常
        do_rom_read(32'h08000000, ACCESS_32BIT, 32'hEA000032, "M4_remap_off_verify", 100);
        wait_clks(5);

        // ----- 结果汇总 -----
        $display("\n=============================================");
        $display(" PASS=%0d  FAIL=%0d", total_pass, total_fail);
        $display("=============================================");
        if (total_fail > 0)
            $display(" *** 存在失败! ***");
        else
            $display(" 全部通过!");

        wait_clks(20);
        $finish;
    end

    // 超时保护
    initial begin #500_000; $display("GLOBAL TIMEOUT"); $finish; end

    // 关键信号追踪
    always @(posedge clk) begin
        if (sdram_read_ena)
            $display("[TRACE] @%0t sdram_read_ena addr=0x%07X", $time, sdram_read_addr);
        if (sdram_read_done)
            $display("[TRACE] @%0t sdram_read_done data=0x%08X data2=0x%08X",
                     $time, sdram_read_data, sdram_second_dword);
        if (mem_bus_done)
            $display("[TRACE] @%0t mem_bus_done din=0x%08X", $time, mem_bus_din);
    end

endmodule
