// tb_fb_write_path.sv
// 验证 FB 写路径: fb_native_scale_writer -> fb_ddr_arbiter -> ch5 -> ddram_mux -> DDR
`timescale 1ns / 1ps

module tb_fb_write_path;

    logic clk = 0;
    always #5 clk = ~clk;
    logic rst_n = 0;

    // fb_native_scale_writer 输入
    logic [1:0]  display_frame_idx = 2'd0;
    logic [15:0] pixel_addr = 0;
    logic [17:0] pixel_data = 0;
    logic        pixel_we = 0;

    // fb_native_scale_writer -> fb_ddr_arbiter
    logic [27:1] fb_wr_addr;
    logic [63:0] fb_wr_data;
    logic        fb_wr_req, fb_wr_ack;

    // fb_ddr_arbiter -> ddram_mux ch5
    logic [27:1] ch5_addr;
    logic [63:0] ch5_din;
    logic        ch5_req, ch5_rnw;
    logic        ch5_ready;

    // ddram_mux -> ddr_axi_backend
    logic        ddram_busy;
    logic [7:0]  ddram_burstcnt;
    logic [28:0] ddram_addr;
    logic [63:0] ddram_dout, ddram_din;
    logic        ddram_dout_ready, ddram_rd, ddram_we;
    logic [7:0]  ddram_be;

    // AXI 信号
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

    // 实例化
    fb_native_scale_writer u_fb_writer (
        .clk(clk), .rst_n(rst_n),
        .display_frame_idx(display_frame_idx),
        .pixel_addr(pixel_addr), .pixel_data(pixel_data), .pixel_we(pixel_we),
        .wr_addr(fb_wr_addr), .wr_data(fb_wr_data),
        .wr_req(fb_wr_req), .wr_ack(fb_wr_ack)
    );

    fb_ddr_arbiter u_fb_arb (
        .clk(clk), .rst_n(rst_n),
        .wr_addr(fb_wr_addr), .wr_data(fb_wr_data),
        .wr_req(fb_wr_req), .wr_ack(fb_wr_ack),
        .ch5_addr(ch5_addr), .ch5_din(ch5_din),
        .ch5_req(ch5_req), .ch5_rnw(ch5_rnw), .ch5_ready(ch5_ready)
    );

    ddram_mux u_ddram_mux (
        .DDRAM_CLK(clk),
        .DDRAM_BUSY(ddram_busy), .DDRAM_BURSTCNT(ddram_burstcnt),
        .DDRAM_ADDR(ddram_addr), .DDRAM_DOUT(ddram_dout),
        .DDRAM_DOUT_READY(ddram_dout_ready),
        .DDRAM_RD(ddram_rd), .DDRAM_DIN(ddram_din),
        .DDRAM_BE(ddram_be), .DDRAM_WE(ddram_we),
        .ch1_addr(27'd0), .ch1_din(16'd0), .ch1_req(1'b0), .ch1_rnw(1'b1),
        .ch1_dout(), .ch1_ready(),
        .ch2_addr(27'd0), .ch2_din(32'd0), .ch2_req(1'b0), .ch2_rnw(1'b1),
        .ch2_dout(), .ch2_ready(),
        .ch3_addr(25'd0), .ch3_din(16'd0), .ch3_req(1'b0), .ch3_rnw(1'b1),
        .ch3_dout(), .ch3_ready(),
        .ch4_addr(27'd0), .ch4_din(64'd0), .ch4_req(1'b0), .ch4_rnw(1'b1),
        .ch4_be(8'd0), .ch4_dout(), .ch4_ready(),
        .ch5_addr(ch5_addr), .ch5_din(ch5_din),
        .ch5_req(ch5_req), .ch5_rnw(ch5_rnw), .ch5_ready(ch5_ready),
        .ch5_dout()
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

    axi_mem_model #(.MEM_SIZE_BYTES(65536)) u_axi_mem (
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
    integer wr_count = 0;
    integer total_pass = 0;
    integer total_fail = 0;

    // 监控 AXI 写: 验证地址在 FB 区域内
    localparam [31:0] FB_DDR_BASE = 32'h1800_0000;
    localparam [31:0] FB_DDR_END  = 32'h1860_0000; // 3 frames * 2MB

    always @(posedge clk) begin
        if (m_axi_awvalid && m_axi_awready) begin
            wr_count++;
            if (m_axi_awaddr >= FB_DDR_BASE && m_axi_awaddr < FB_DDR_END) begin
                // 地址在 FB 区域内 - OK
            end else begin
                $display("[FB_ERR] @%0t WR addr=0x%08X 超出 FB 区域!", $time, m_axi_awaddr);
                total_fail++;
            end
        end
    end

    // 喂像素的 task
    task automatic feed_pixel(input [15:0] addr, input [17:0] data);
        @(posedge clk);
        pixel_addr <= addr;
        pixel_data <= data;
        pixel_we   <= 1'b1;
        @(posedge clk);
        pixel_we   <= 1'b0;
    endtask

    // 喂一整行 (240 像素)
    task automatic feed_line(input [7:0] y, input [17:0] base_color);
        for (int x = 0; x < 240; x++) begin
            feed_pixel(y * 240 + x, base_color + x[5:0]);
        end
    endtask

    task automatic wait_clks(input int n);
        repeat(n) @(posedge clk);
    endtask

    initial begin
        $display("\n=============================================");
        $display(" FB 写路径端到端仿真");
        $display("=============================================\n");

        rst_n = 0;
        wait_clks(10);
        rst_n = 1;
        wait_clks(5);

        // 喂第一行 (y=0), 红色为主
        $display("[TEST] 喂第 0 行像素 (240 pixels)...");
        feed_line(8'd0, 18'h00F00);
        wait_clks(50);

        // 等待 DDR 写完成
        $display("[TEST] 等待 DDR 写完成...");
        wait_clks(3000);

        // 喂第二行 (y=1), 绿色为主
        $display("[TEST] 喂第 1 行像素...");
        feed_line(8'd1, 18'h3C000);
        wait_clks(3000);

        // 喂第三行 (y=2), 蓝色为主
        $display("[TEST] 喂第 2 行像素...");
        feed_line(8'd2, 18'h0003F);
        wait_clks(3000);

        $display("\n=============================================");
        $display(" FB 写路径: AXI 写事务数=%0d  地址错误=%0d", wr_count, total_fail);
        if (wr_count > 0 && total_fail == 0)
            $display(" FB 写路径: 全部写入地址正确!");
        else if (wr_count == 0)
            $display(" FB 写路径: *** 未检测到任何 AXI 写! ***");
        $display("=============================================");

        wait_clks(20);
        $finish;
    end

    initial begin #200_000; $display("TIMEOUT"); $finish; end

endmodule
