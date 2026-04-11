// tb_ddr_noguard.sv: 验证无 !ram_write 守卫的旧代码是否精确复现硬件 rd=1+busy=1
// 场景：ch5 写 → ch1 读交替（模拟硬件 framebuffer 写 + ROM 读混合）
`timescale 1ns / 1ps

module tb_ddr_noguard;
    logic clk = 0;
    always #5 clk = ~clk;
    logic rst_n;

    logic        ddram_busy, ddram_dout_ready, ddram_rd, ddram_we;
    logic [7:0]  ddram_burstcnt, ddram_be;
    logic [28:0] ddram_addr;
    logic [63:0] ddram_dout, ddram_din;
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
    logic [31:0] err_vec; logic err_pulse;

    logic [27:1] ch1_addr = '0; logic [15:0] ch1_din = '0;
    logic        ch1_req = 0, ch1_rnw = 1;
    logic [63:0] ch1_dout; logic ch1_ready;
    logic [27:1] ch2_addr = '0; logic [31:0] ch2_din = '0;
    logic        ch2_req = 0, ch2_rnw = 1;
    logic [31:0] ch2_dout; logic ch2_ready;
    logic [25:1] ch3_addr = '0; logic [15:0] ch3_din = '0;
    logic        ch3_req = 0, ch3_rnw = 1;
    logic [15:0] ch3_dout; logic ch3_ready;
    logic [27:1] ch4_addr = '0; logic [63:0] ch4_din = '0;
    logic        ch4_req = 0, ch4_rnw = 1; logic [7:0] ch4_be = '0;
    logic [63:0] ch4_dout; logic ch4_ready;
    logic [27:1] ch5_addr = '0; logic [63:0] ch5_din = '0;
    logic        ch5_req = 0, ch5_rnw = 0;
    logic [63:0] ch5_dout; logic ch5_ready;

    // 检测 rd+busy 和 we+busy
    int a1_cnt = 0, a2_cnt = 0, deadlock_cnt = 0;
    always_ff @(posedge clk) if (rst_n && ddram_rd && ddram_busy) begin
        a1_cnt++;
        if (a1_cnt <= 5)
            $display("[BUG RD+BUSY] @%0t #%0d be=%0d mux=%0d rw=%b rr=%b ch_rq=%05b",
                     $time, a1_cnt, u_backend.state, u_mux.state,
                     u_mux.ram_write, u_mux.ram_read, u_mux.ch_rq);
    end
    always_ff @(posedge clk) if (rst_n && ddram_we && ddram_busy) begin
        a2_cnt++;
        if (a2_cnt <= 5)
            $display("[BUG WE+BUSY] @%0t #%0d be=%0d mux=%0d",
                     $time, a2_cnt, u_backend.state, u_mux.state);
    end

    // DUT: 使用无守卫版本
    ddram_mux_noguard u_mux (
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
        .MIN_AR_DELAY(1), .MAX_AR_DELAY(5),
        .MIN_R_DELAY(2),  .MAX_R_DELAY(10),
        .MIN_AW_DELAY(1), .MAX_AW_DELAY(3),
        .MIN_B_DELAY(1),  .MAX_B_DELAY(5),
        .SEED(32'hFACE_CAFE)
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

    task automatic pulse_ch1_read(input logic [27:1] addr, output int ok);
        @(posedge clk); ch1_addr <= addr; ch1_rnw <= 1; ch1_req <= 1;
        @(posedge clk); ch1_req <= 0;
        ok = 0;
        for (int w = 0; w < 5000; w++) begin
            @(posedge clk);
            if (ch1_ready) begin ok = 1; return; end
        end
    endtask
    task automatic pulse_ch5_write(input logic [27:1] addr, input logic [63:0] wdata, output int ok);
        @(posedge clk); ch5_addr <= addr; ch5_din <= wdata; ch5_rnw <= 0; ch5_req <= 1;
        @(posedge clk); ch5_req <= 0;
        ok = 0;
        for (int w = 0; w < 5000; w++) begin
            @(posedge clk);
            if (ch5_ready) begin ok = 1; return; end
        end
    endtask

    int ok;
    initial begin
        $display("=== tb_ddr_noguard: 无 !ram_write 守卫的旧代码 ===");
        $display("=== 目标：复现 rd=1+busy=1 硬件现象 ===");
        rst_n = 0; repeat (10) @(posedge clk); rst_n = 1; repeat (5) @(posedge clk);

        // S1: 100 纯读（应该没问题）
        $display("\n--- S1: 100 纯 ch1 读 ---");
        for (int i = 0; i < 100; i++) begin
            pulse_ch1_read(27'h060000 + i*4, ok);
            if (!ok) begin $display("[DEADLOCK] S1 #%0d", i); deadlock_cnt++; break; end
        end
        $display("S1 done: a1=%0d deadlock=%0d", a1_cnt, deadlock_cnt);

        // S2: 100 ch5写→ch1读交替（这是触发 write→read 竞态的场景！）
        $display("\n--- S2: 100 ch5写→ch1读 交替 (关键场景) ---");
        for (int i = 0; i < 100; i++) begin
            pulse_ch5_write(27'h400000 + i*4, 64'hFB000000 + i, ok);
            if (!ok) begin $display("[DEADLOCK] S2 ch5w #%0d", i); deadlock_cnt++; break; end
            pulse_ch1_read(27'h060000 + (i+200)*4, ok);
            if (!ok) begin
                $display("[DEADLOCK] S2 ch1r #%0d @%0t be=%0d mux=%0d busy=%b rw=%b rr=%b",
                         i, $time, u_backend.state, u_mux.state, ddram_busy,
                         u_mux.ram_write, u_mux.ram_read);
                deadlock_cnt++; break;
            end
        end
        $display("S2 done: a1=%0d a2=%0d deadlock=%0d", a1_cnt, a2_cnt, deadlock_cnt);

        // S3: 并发 ch1读 + ch5写
        if (deadlock_cnt == 0) begin
            $display("\n--- S3: 并发 ch1(200读) + ch5(200写) ---");
            fork
                begin : s3_ch1
                    for (int i = 0; i < 200; i++) begin
                        int rok;
                        pulse_ch1_read(27'h060000 + (i+500)*4, rok);
                        if (!rok) begin
                            $display("[DEADLOCK] S3 ch1 #%0d @%0t", i, $time);
                            deadlock_cnt++; disable s3_ch1;
                        end
                    end
                end
                begin : s3_ch5
                    for (int i = 0; i < 200; i++) begin
                        int wok;
                        pulse_ch5_write(27'h400000 + (i+200)*4, 64'hAB000000+i, wok);
                        if (!wok) begin
                            $display("[DEADLOCK] S3 ch5 #%0d @%0t", i, $time);
                            deadlock_cnt++; disable s3_ch5;
                        end
                    end
                end
            join
            $display("S3 done: a1=%0d a2=%0d deadlock=%0d", a1_cnt, a2_cnt, deadlock_cnt);
        end

        repeat (50) @(posedge clk);
        $display("\n=== 无守卫旧代码汇总 ===");
        $display("RD+BUSY 违规: %0d", a1_cnt);
        $display("WE+BUSY 违规: %0d", a2_cnt);
        $display("死锁: %0d", deadlock_cnt);
        if (a1_cnt > 0 || deadlock_cnt > 0)
            $display("*** 确认：无 !ram_write 守卫导致 rd+busy 竞态！***");
        else
            $display("*** 无守卫版也通过 — 问题在别处 ***");
        $finish;
    end

    initial begin #50000000; $display("[TIMEOUT]"); $finish; end
endmodule
