// tb_cache_only.sv: 最小化 cache 模块仿真
// 重点测试: 奇 DWORD 冷未命中后 cache 填充顺序是否正确
// 模拟 ddram_mux 的 ch1_dout DWORD 交换效应

`timescale 1ns / 1ps

module tb_cache_only;

    logic clk = 0;
    always #5 clk = ~clk;

    logic        gb_on = 0;
    logic        read_enable = 0;
    logic [22:0] read_addr = 0;
    logic [31:0] read_data;
    logic        read_done;
    logic [63:0] read_full;

    logic        mem_read_ena;
    logic        mem_read_done = 0;
    logic [24:0] mem_read_addr;
    logic [31:0] mem_read_data = 0;
    logic [31:0] mem_read_data2 = 0;

    localparam integer Softmap = 196608;  // 0x30000

    cache #(
        .SIZE(1024), .SIZEBASEBITS(23), .BITWIDTH(32),
        .Softmap_GBA_Gamerom_ADDR(Softmap)
    ) u_cache (
        .clk(clk), .gb_on(gb_on),
        .read_enable(read_enable), .read_addr(read_addr),
        .read_data(read_data), .read_done(read_done), .read_full(read_full),
        .mem_read_ena(mem_read_ena), .mem_read_done(mem_read_done),
        .mem_read_addr(mem_read_addr),
        .mem_read_data(mem_read_data), .mem_read_data2(mem_read_data2)
    );

    // =====================================================
    // DDR 响应模型 — 精确模拟 ddram_mux 的 DWORD 交换
    // =====================================================
    // 假设 DDR 中存储的 ROM 数据（自然顺序）:
    //   64-bit cache line N: { HIGH_DWORD, LOW_DWORD }
    //   LOW_DWORD  (偶 read_addr) = 0xEEEE_0000 | (N*2)
    //   HIGH_DWORD (奇 read_addr) = 0xODDD_0000 | (N*2+1)
    //
    // ddram_mux 的交换逻辑:
    //   ch1_addr[2] = sdram_read_addr[0] = mem_read_addr[0]
    //   若 mem_read_addr[0]=0 (偶): 无交换
    //     mem_read_data  = LOW_DWORD  (= ch1_dout[31:0])
    //     mem_read_data2 = HIGH_DWORD (= ch1_dout[63:32])
    //   若 mem_read_addr[0]=1 (奇): 交换
    //     mem_read_data  = HIGH_DWORD (= ch1_dout[31:0] 交换后)
    //     mem_read_data2 = LOW_DWORD  (= ch1_dout[63:32] 交换后)

    logic [31:0] ddr_resp_data;
    logic [31:0] ddr_resp_data2;
    logic [2:0]  ddr_delay_cnt;
    logic        ddr_pending;

    // 根据 mem_read_addr 计算 ROM 数据（自然顺序）
    function automatic logic [31:0] rom_low_dword(input logic [24:0] addr);
        // cache line index = addr 去掉 bit0 → addr[24:1]
        // LOW_DWORD 标记模式
        return 32'hEEEE_0000 | {16'h0, addr[15:1], 1'b0};
    endfunction

    function automatic logic [31:0] rom_high_dword(input logic [24:0] addr);
        return 32'h0DDD_0000 | {16'h0, addr[15:1], 1'b1};
    endfunction

    always_ff @(posedge clk) begin
        mem_read_done <= 1'b0;
        if (mem_read_ena) begin
            ddr_pending <= 1'b1;
            ddr_delay_cnt <= 3'd3;
            // 模拟 ddram_mux DWORD 交换
            if (mem_read_addr[0] == 1'b0) begin
                // 偶地址: 无交换
                ddr_resp_data  <= rom_low_dword(mem_read_addr);
                ddr_resp_data2 <= rom_high_dword(mem_read_addr);
                $display("[DDR] @%0t 偶地址 addr=0x%07X: data=LOW(0x%08X) data2=HIGH(0x%08X)",
                         $time, mem_read_addr,
                         rom_low_dword(mem_read_addr), rom_high_dword(mem_read_addr));
            end else begin
                // 奇地址: 交换 (模拟 ch1_addr[2]=1)
                ddr_resp_data  <= rom_high_dword(mem_read_addr);
                ddr_resp_data2 <= rom_low_dword(mem_read_addr);
                $display("[DDR] @%0t 奇地址 addr=0x%07X: data=HIGH(0x%08X) data2=LOW(0x%08X) [SWAPPED]",
                         $time, mem_read_addr,
                         rom_high_dword(mem_read_addr), rom_low_dword(mem_read_addr));
            end
        end else if (ddr_pending) begin
            if (ddr_delay_cnt == 0) begin
                mem_read_done  <= 1'b1;
                mem_read_data  <= ddr_resp_data;
                mem_read_data2 <= ddr_resp_data2;
                ddr_pending    <= 1'b0;
            end else begin
                ddr_delay_cnt <= ddr_delay_cnt - 1;
            end
        end
    end

    // 追踪日志
    always @(posedge clk) begin
        if (mem_read_ena)
            $display("[CACHE] @%0t mem_read_ena=1 addr=0x%07X", $time, mem_read_addr);
        if (mem_read_done)
            $display("[CACHE] @%0t mem_read_done=1 data=0x%08X data2=0x%08X",
                     $time, mem_read_data, mem_read_data2);
        if (read_done)
            $display("[CACHE] @%0t read_done=1 data=0x%08X full=0x%016X",
                     $time, read_data, read_full);
    end

    task automatic wait_clks(input int n);
        repeat(n) @(posedge clk);
    endtask

    // 冷未命中 — 等 mem_read_done
    task automatic cold_miss(input [22:0] addr, input string label);
        integer cnt;
        $display("\n--- %s: cold miss (addr=0x%06X) ---", label, addr);
        @(posedge clk);
        read_enable <= 1'b1;
        read_addr   <= addr;
        @(posedge clk);
        read_enable <= 1'b0;
        cnt = 0;
        while (!mem_read_done && cnt < 50) begin
            @(posedge clk);
            cnt++;
        end
        $display("[%s] DDR returned after %0d cycles", label, cnt);
        wait_clks(5);  // 让 cache 完成 READCACHE_SECOND + RAM 写入
    endtask

    // 缓存命中测试
    integer total_pass = 0;
    integer total_fail = 0;

    task automatic cache_hit_test(
        input [22:0] addr,
        input [31:0] expected,
        input string label
    );
        integer timeout;
        $display("\n[TEST] === %s === addr=0x%06X expect=0x%08X", label, addr, expected);

        @(posedge clk);
        read_enable <= 1'b1;
        read_addr   <= addr;
        @(posedge clk);
        read_enable <= 1'b0;

        timeout = 0;
        while (!read_done && timeout < 50) begin
            @(posedge clk);
            timeout++;
        end

        if (read_done) begin
            $display("[TEST] %s: data=0x%08X full=0x%016X (%0d cyc)",
                     label, read_data, read_full, timeout);
            if (read_data === expected) begin
                $display("[TEST] %s: PASS", label);
                total_pass++;
            end else if ($isunknown(read_data)) begin
                $display("[TEST] %s: FAIL (data contains X!)", label);
                total_fail++;
            end else begin
                $display("[TEST] %s: FAIL (got 0x%08X, expected 0x%08X)", label, read_data, expected);
                total_fail++;
            end
        end else begin
            $display("[TEST] %s: FAIL (TIMEOUT)", label);
            total_fail++;
        end
    endtask

    initial begin
        $display("\n========================================");
        $display(" cache DWORD 交换专项仿真");
        $display("========================================\n");
        ddr_pending = 0;
        ddr_delay_cnt = 0;

        wait_clks(5);
        gb_on = 1'b1;
        wait_clks(1100);  // CLEARCACHE

        // =====================================================
        // 场景 A: 偶 DWORD 冷未命中 → 偶/奇 cache HIT
        // 预期: cache 正确填充, 命中返回正确数据
        // =====================================================
        $display("\n===== 场景 A: 偶 DWORD 先冷未命中 =====");

        // A1: cold miss addr=0 (偶, mem_read_addr=0x30000, bit0=0, 无交换)
        // DDR: data=LOW(0xEEEE0000) data2=HIGH(0xODDD0001)
        // cache 存: datain={data2, data}={HIGH, LOW}={0xODDD0001, 0xEEEE0000}
        cold_miss(23'h000000, "A1");

        // A2: cache HIT addr=0 (up_low=0 → dataout[31:0])
        // 期望: 0xEEEE0000 (LOW_DWORD)
        cache_hit_test(23'h000000, 32'hEEEE0000, "A2_even_hit");
        wait_clks(3);

        // A3: cache HIT addr=1 (up_low=1 → dataout[63:32])
        // 期望: 0xODDD0001 (HIGH_DWORD)
        cache_hit_test(23'h000001, 32'h0DDD0001, "A3_odd_hit");
        wait_clks(3);

        // =====================================================
        // 场景 B: 奇 DWORD 先冷未命中 → 奇/偶 cache HIT
        // ★★★ 关键测试: 验证 DWORD 交换是否破坏 cache 填充
        // =====================================================
        $display("\n===== 场景 B: 奇 DWORD 先冷未命中 (关键!) =====");

        // B1: cold miss addr=5 (奇, mem_read_addr=0x30005, bit0=1, 有交换!)
        // DDR 自然顺序: LOW=0xEEEE0004 HIGH=0xODDD0005
        // 交换后: data=HIGH(0x0DDD0005) data2=LOW(0xEEEE0004)
        // cache 存: datain={data2, data}={LOW, HIGH}={0xEEEE0004, 0x0DDD0005}
        // ★ 但 cache 期望: datain={HIGH, LOW}={0x0DDD0005, 0xEEEE0004}!
        cold_miss(23'h000005, "B1_odd_miss");

        // B2: cache HIT addr=5 (up_low=1 → dataout[63:32])
        // 如果 cache 填充正确: dataout[63:32] = HIGH = 0x0DDD0005
        // 如果 cache 填充错误: dataout[63:32] = LOW  = 0xEEEE0004
        cache_hit_test(23'h000005, 32'h0DDD0005, "B2_odd_hit_after_odd_miss");
        wait_clks(3);

        // B3: cache HIT addr=4 (up_low=0 → dataout[31:0])
        // 如果 cache 填充正确: dataout[31:0] = LOW = 0xEEEE0004
        // 如果 cache 填充错误: dataout[31:0] = HIGH = 0x0DDD0005
        cache_hit_test(23'h000004, 32'hEEEE0004, "B3_even_hit_after_odd_miss");
        wait_clks(3);

        // =====================================================
        // 场景 C: 另一个奇 DWORD 冷未命中 (不同 cache line)
        // =====================================================
        $display("\n===== 场景 C: 第二组奇 DWORD 冷未命中 =====");

        // C1: cold miss addr=0x000003 (奇, cache line 1)
        cold_miss(23'h000003, "C1_odd_miss_2");

        // C2: cache HIT addr=2 (偶, 同 cache line)
        cache_hit_test(23'h000002, 32'hEEEE0002, "C2_even_hit_after_odd_miss_2");
        wait_clks(3);

        // C3: cache HIT addr=3 (奇, 同 cache line)
        cache_hit_test(23'h000003, 32'h0DDD0003, "C3_odd_hit_after_odd_miss_2");
        wait_clks(3);

        // =====================================================
        // 结果汇总
        // =====================================================
        $display("\n========================================");
        $display(" 结果: PASS=%0d  FAIL=%0d", total_pass, total_fail);
        $display("========================================");
        if (total_fail > 0)
            $display(" *** 存在失败! DWORD 交换可能破坏 cache 填充 ***");
        else
            $display(" 全部通过");

        wait_clks(10);
        $finish;
    end

    initial begin #200_000; $display("TIMEOUT"); $finish; end

endmodule
