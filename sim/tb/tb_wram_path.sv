// tb_wram_path.sv
// 验证 WRAM (ch2) 读写路径:
//   CPU mem_bus → memorymux → bus_out → ch2 → ddram_mux → ddr_axi_backend → AXI
// 同时也验证 ROM (ch1) 路径在 WRAM 操作混合时的正确性
`timescale 1ns / 1ps

module tb_wram_path;

    logic clk = 0;
    always #5 clk = ~clk;
    logic rst_n = 0;

    // memorymux wrapper 端口
    logic        gb_on = 0;
    logic [31:0] mem_bus_Adr = 0;
    logic        mem_bus_rnw = 0;
    logic        mem_bus_ena = 0;
    logic [1:0]  mem_bus_acc = 0;
    logic [31:0] mem_bus_dout = 0;
    logic [31:0] mem_bus_din;
    logic        mem_bus_done;

    // sdram (ch1 ROM)
    logic        sdram_read_ena;
    logic        sdram_read_done;
    logic [24:0] sdram_read_addr;
    logic [31:0] sdram_read_data;
    logic [31:0] sdram_second_dword;

    // bus_out (ch2 WRAM)
    logic [31:0] bus_out_din;   // memorymux → DDR (写数据)
    logic [31:0] bus_out_dout;  // DDR → memorymux (读数据)
    logic [25:0] bus_out_adr;
    logic        bus_out_rnw_sig;
    logic        bus_out_ena_sig;
    logic        bus_out_done_sig;

    // 配置
    logic [24:0] MaxPakAddr = 25'h0400000;
    logic        memory_remap = 0;
    logic        SramFlashEnable = 0;

    // ddram_mux
    logic        ddram_busy;
    logic [7:0]  ddram_burstcnt;
    logic [28:0] ddram_addr;
    logic [63:0] ddram_dout, ddram_din;
    logic        ddram_dout_ready, ddram_rd, ddram_we;
    logic [7:0]  ddram_be;

    // ch1 信号
    logic [27:1] ch1_addr;
    logic [63:0] ch1_dout;
    logic        ch1_ready;

    // ch2 信号
    logic [27:1] ch2_addr;
    logic [31:0] ch2_dout;
    logic [31:0] ch2_din_sig;
    logic        ch2_req, ch2_rnw_sig, ch2_ready;

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

    // memorymux wrapper (暴露 bus_out)
    memorymux_real_wrapper u_mux_real (
        .clk100(clk), .gb_on(gb_on),
        .mem_bus_Adr(mem_bus_Adr), .mem_bus_rnw(mem_bus_rnw),
        .mem_bus_ena(mem_bus_ena), .mem_bus_acc(mem_bus_acc),
        .mem_bus_dout(mem_bus_dout), .mem_bus_din(mem_bus_din),
        .mem_bus_done(mem_bus_done),
        .sdram_read_ena(sdram_read_ena), .sdram_read_done(sdram_read_done),
        .sdram_read_addr(sdram_read_addr), .sdram_read_data(sdram_read_data),
        .sdram_second_dword(sdram_second_dword),
        .bus_out_Din(bus_out_din), .bus_out_Dout(bus_out_dout),
        .bus_out_Adr(bus_out_adr), .bus_out_rnw(bus_out_rnw_sig),
        .bus_out_ena(bus_out_ena_sig), .bus_out_done(bus_out_done_sig),
        .MaxPakAddr(MaxPakAddr), .memory_remap(memory_remap),
        .SramFlashEnable(SramFlashEnable)
    );

    // ch1: ROM 读 (memorymux → ddram_mux)
    assign ch1_addr = {1'b0, sdram_read_addr, 1'b0};
    assign sdram_read_done    = ch1_ready;
    assign sdram_read_data    = ch1_dout[31:0];
    assign sdram_second_dword = ch1_dout[63:32];

    // ch2: WRAM (memorymux bus_out → ddram_mux)
    // 与 zynq_gba_top.v 一致: ch2_addr = {bus_out_adr, 1'b0}
    assign ch2_addr    = {bus_out_adr, 1'b0};
    assign ch2_din_sig = bus_out_din;
    assign ch2_req     = bus_out_ena_sig;
    assign ch2_rnw_sig = bus_out_rnw_sig;
    assign bus_out_dout     = ch2_dout;
    assign bus_out_done_sig = ch2_ready;

    ddram_mux u_ddram_mux (
        .DDRAM_CLK(clk),
        .DDRAM_BUSY(ddram_busy), .DDRAM_BURSTCNT(ddram_burstcnt),
        .DDRAM_ADDR(ddram_addr), .DDRAM_DOUT(ddram_dout),
        .DDRAM_DOUT_READY(ddram_dout_ready),
        .DDRAM_RD(ddram_rd), .DDRAM_DIN(ddram_din),
        .DDRAM_BE(ddram_be), .DDRAM_WE(ddram_we),
        .ch1_addr(ch1_addr), .ch1_dout(ch1_dout), .ch1_din(16'd0),
        .ch1_req(sdram_read_ena), .ch1_rnw(1'b1), .ch1_ready(ch1_ready),
        .ch2_addr(ch2_addr), .ch2_dout(ch2_dout), .ch2_din(ch2_din_sig),
        .ch2_req(ch2_req), .ch2_rnw(ch2_rnw_sig), .ch2_ready(ch2_ready),
        .ch3_addr(25'd0), .ch3_din(16'd0), .ch3_req(1'b0), .ch3_rnw(1'b1),
        .ch3_dout(), .ch3_ready(),
        .ch4_addr(27'd0), .ch4_din(64'd0), .ch4_req(1'b0), .ch4_rnw(1'b1),
        .ch4_be(8'd0), .ch4_dout(), .ch4_ready(),
        .ch5_addr(27'd0), .ch5_din(64'd0), .ch5_req(1'b0), .ch5_rnw(1'b1),
        .ch5_dout(), .ch5_ready()
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
        .MEM_SIZE_BYTES(1048576),  // 1MB 足够 WRAM 256KB
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

    // 测试计数
    localparam [1:0] ACCESS_16BIT = 2'b01;
    localparam [1:0] ACCESS_32BIT = 2'b10;
    integer total_pass = 0, total_fail = 0;

    task automatic wait_clks(input int n);
        repeat(n) @(posedge clk);
    endtask

    // CPU 写 (通过 mem_bus)
    task automatic cpu_write(input [31:0] addr, input [1:0] acc, input [31:0] data, input string label, input int max_wait);
        integer cnt;
        $display("\n[TEST] === %s === WR addr=0x%08X data=0x%08X acc=%0d", label, addr, data, acc);
        @(posedge clk);
        mem_bus_Adr  <= addr;
        mem_bus_rnw  <= 1'b0;
        mem_bus_ena  <= 1'b1;
        mem_bus_acc  <= acc;
        mem_bus_dout <= data;
        @(posedge clk);
        mem_bus_ena  <= 1'b0;
        cnt = 0;
        while (!mem_bus_done && cnt < max_wait) begin
            @(posedge clk); cnt++;
        end
        if (mem_bus_done) begin
            $display("[TEST] %s: WR done (%0d cyc)", label, cnt);
            total_pass++;
        end else begin
            $display("[TEST] %s: WR TIMEOUT!", label);
            total_fail++;
        end
    endtask

    // CPU 读 (通过 mem_bus)
    task automatic cpu_read(input [31:0] addr, input [1:0] acc, input [31:0] expected, input string label, input int max_wait);
        integer cnt;
        $display("\n[TEST] === %s === RD addr=0x%08X expect=0x%08X", label, addr, expected);
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
            @(posedge clk); cnt++;
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
                $display("[TEST] %s: FAIL (got 0x%08X)", label, mem_bus_din);
                total_fail++;
            end
        end else begin
            $display("[TEST] %s: TIMEOUT!", label);
            total_fail++;
        end
    endtask

    initial begin
        $display("\n=============================================");
        $display(" WRAM (ch2) + ROM (ch1) 端到端仿真");
        $display("=============================================\n");

        rst_n = 0;
        wait_clks(10);
        rst_n = 1;
        wait_clks(5);
        gb_on = 1;
        wait_clks(1200);  // cache CLEARCACHE 完成

        // === WRAM 写测试 ===
        // GBA WRAM 地址: 0x02000000 - 0x0203FFFF (256KB)

        // W1: 32-bit 写 WRAM
        cpu_write(32'h02000000, ACCESS_32BIT, 32'hDEAD_BEEF, "W1_wram_wr32", 100);
        wait_clks(5);

        // W2: 32-bit 写 WRAM 偏移 4
        cpu_write(32'h02000004, ACCESS_32BIT, 32'h1234_5678, "W2_wram_wr32_off4", 100);
        wait_clks(5);

        // W3: 16-bit 写 WRAM 偏移 8
        cpu_write(32'h02000008, ACCESS_16BIT, 32'h0000_ABCD, "W3_wram_wr16", 100);
        wait_clks(5);

        // === WRAM 读回测试 ===

        // R1: 读回 W1
        cpu_read(32'h02000000, ACCESS_32BIT, 32'hDEAD_BEEF, "R1_wram_rd32", 100);
        wait_clks(5);

        // R2: 读回 W2
        cpu_read(32'h02000004, ACCESS_32BIT, 32'h1234_5678, "R2_wram_rd32_off4", 100);
        wait_clks(5);

        // R3: 读回 W3 (16-bit)
        cpu_read(32'h02000008, ACCESS_16BIT, 32'h0000_ABCD, "R3_wram_rd16", 100);
        wait_clks(5);

        // === 混合: WRAM 操作后 ROM 读仍正确 ===

        // R4: ROM 读 (确认 ch1 不受 ch2 影响)
        cpu_read(32'h08000000, ACCESS_32BIT, 32'hEA00_0032, "R4_rom_after_wram", 100);
        wait_clks(5);

        // R5: 再读 WRAM (确认 WRAM 不受 ROM 读影响)
        cpu_read(32'h02000000, ACCESS_32BIT, 32'hDEAD_BEEF, "R5_wram_after_rom", 100);
        wait_clks(5);

        // === IWRAM (内部 32KB, 0x03000000) 测试 ===
        $display("\n===== IWRAM 测试 (内部 SyncRamDual) =====");

        // IW1: 32-bit 写 IWRAM
        cpu_write(32'h03000000, ACCESS_32BIT, 32'hCAFE_BABE, "IW1_iwram_wr32", 100);
        wait_clks(5);

        // IW2: 32-bit 写 IWRAM 偏移 4
        cpu_write(32'h03000004, ACCESS_32BIT, 32'h1111_2222, "IW2_iwram_wr32_off4", 100);
        wait_clks(5);

        // IW3: 16-bit 写 IWRAM 偏移 8
        cpu_write(32'h03000008, ACCESS_16BIT, 32'h0000_5678, "IW3_iwram_wr16", 100);
        wait_clks(5);

        // IW4: 8-bit 写 IWRAM 偏移 0x0C
        cpu_write(32'h0300000C, 2'b00, 32'h000000AA, "IW4_iwram_wr8", 100);
        wait_clks(5);

        // 读回验证
        cpu_read(32'h03000000, ACCESS_32BIT, 32'hCAFE_BABE, "IW5_iwram_rd32", 20);
        wait_clks(5);

        cpu_read(32'h03000004, ACCESS_32BIT, 32'h1111_2222, "IW6_iwram_rd32_off4", 20);
        wait_clks(5);

        cpu_read(32'h03000008, ACCESS_16BIT, 32'h0000_5678, "IW7_iwram_rd16", 20);
        wait_clks(5);

        // IW8: 8-bit 读回
        cpu_read(32'h0300000C, 2'b00, 32'h000000AA, "IW8_iwram_rd8", 20);
        wait_clks(5);

        // IW9: 确认 IWRAM 和 WRAM 不互相干扰
        cpu_read(32'h02000000, ACCESS_32BIT, 32'hDEAD_BEEF, "IW9_wram_after_iwram", 100);
        wait_clks(5);

        // === 结果 ===
        $display("\n=============================================");
        $display(" PASS=%0d  FAIL=%0d", total_pass, total_fail);
        $display("=============================================");
        if (total_fail > 0) $display(" *** 存在失败! ***");
        else $display(" 全部通过!");

        wait_clks(20);
        $finish;
    end

    initial begin #500_000; $display("TIMEOUT"); $finish; end

    // TRACE
    always @(posedge clk) begin
        if (bus_out_ena_sig)
            $display("[TRACE] @%0t bus_out_ena adr=0x%07X rnw=%b din=0x%08X",
                     $time, bus_out_adr, bus_out_rnw_sig, bus_out_din);
        if (bus_out_done_sig)
            $display("[TRACE] @%0t bus_out_done dout=0x%08X", $time, bus_out_dout);
    end

endmodule
