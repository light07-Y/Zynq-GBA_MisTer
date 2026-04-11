// tb_config_mgr_timing.sv: gba_config_mgr 配置同步时序验证
// 精确模拟 PS 端的 apply_shadow_config + set_sw_reset 序列
// 验证 MaxPakAddr 在 GBA_on 上升沿之前是否已到达核心域

`timescale 1ns / 1ps

module tb_config_mgr_timing;

    logic clk = 0;
    logic rst_n = 0;
    always #5 clk = ~clk;  // 100 MHz

    // AXI 域信号（模拟 PS 写入）
    logic [31:0] cfg_ctrl_axi;
    logic [9:0]  cfg_keys_axi;
    logic [24:0] cfg_max_pak_addr_axi;
    logic [15:0] cfg_cycle_precalc_axi;
    logic [31:0] cfg_rtc_timestamp_axi;
    logic        cfg_commit_toggle_axi;
    logic        sw_reset_axi;

    // 核心域输出
    logic [31:0] cfg_ctrl_core;
    logic [9:0]  cfg_keys_core;
    logic [24:0] cfg_max_pak_addr_core;
    logic [15:0] cfg_cycle_precalc_core;
    logic [31:0] cfg_rtc_timestamp_core;

    // sw_reset CDC
    logic sw_reset_meta, sw_reset_sync;
    wire  sw_reset_core = sw_reset_sync;

    // GBA_on 推导
    wire GBA_on = cfg_ctrl_core[0] & ~sw_reset_core;

    // DUT
    gba_config_mgr u_dut (
        .clk_core              (clk),
        .rst_n                 (rst_n),
        .cfg_ctrl_axi          (cfg_ctrl_axi),
        .cfg_keys_axi          (cfg_keys_axi),
        .cfg_max_pak_addr_axi  (cfg_max_pak_addr_axi),
        .cfg_cycle_precalc_axi (cfg_cycle_precalc_axi),
        .cfg_rtc_timestamp_axi (cfg_rtc_timestamp_axi),
        .cfg_commit_toggle_axi (cfg_commit_toggle_axi),
        .btns                  (4'd0),
        .sws                   (4'd0),
        .cfg_ctrl_core         (cfg_ctrl_core),
        .cfg_keys_core         (cfg_keys_core),
        .cfg_max_pak_addr_core (cfg_max_pak_addr_core),
        .cfg_cycle_precalc_core(cfg_cycle_precalc_core),
        .cfg_rtc_timestamp_core(cfg_rtc_timestamp_core)
    );

    // sw_reset CDC（复刻 zynq_gba_top.v 行 435-436）
    always @(posedge clk) begin
        if (!rst_n) begin
            sw_reset_meta <= 1'b0;
            sw_reset_sync <= 1'b0;
        end else begin
            sw_reset_meta <= sw_reset_axi;
            sw_reset_sync <= sw_reset_meta;
        end
    end

    // ========== 辅助任务 ==========
    task automatic wait_clks(input int n);
        repeat(n) @(posedge clk);
    endtask

    // 模拟 PS apply_shadow_config
    task automatic apply_shadow_config();
        $display("[PS] @%0t apply_shadow_config: toggle commit", $time);
        cfg_commit_toggle_axi <= ~cfg_commit_toggle_axi;
    endtask

    // 模拟 PS set_sw_reset
    task automatic set_sw_reset(input logic val);
        $display("[PS] @%0t set_sw_reset(%0b)", $time, val);
        sw_reset_axi <= val;
    endtask

    // 状态快照
    task automatic snapshot(input string tag);
        $display("[SNAP] @%0t %s: ctrl_core=0x%08X maxpak_core=0x%07X sw_reset_core=%b GBA_on=%b",
                 $time, tag, cfg_ctrl_core, cfg_max_pak_addr_core, sw_reset_core, GBA_on);
    endtask

    // ========== 监控 GBA_on 上升沿 ==========
    logic GBA_on_prev = 0;
    always @(posedge clk) begin
        GBA_on_prev <= GBA_on;
        if (GBA_on && !GBA_on_prev) begin
            $display("[CRITICAL] @%0t ★ GBA_on 上升沿! maxpak_core=0x%07X ctrl_core=0x%08X",
                     $time, cfg_max_pak_addr_core, cfg_ctrl_core);
            if (cfg_max_pak_addr_core == 25'd0) begin
                $display("[CRITICAL] ★★★ BUG! MaxPakAddr 在 GBA_on 上升沿时仍为 0! ★★★");
            end else begin
                $display("[CRITICAL] ✓ MaxPakAddr 在 GBA_on 前已就绪");
            end
        end
    end

    // ========== 主测试序列（精确复刻 main.c load_rom_from_sd 序列）==========
    integer test_pass = 0;
    integer test_fail = 0;

    initial begin
        $display("\n============================================");
        $display(" gba_config_mgr 配置同步时序仿真");
        $display("============================================\n");

        // 初始化所有 AXI 域信号
        cfg_ctrl_axi          = 32'h0000_1612;  // 复位默认值
        cfg_keys_axi          = 10'd0;
        cfg_max_pak_addr_axi  = 25'd0;
        cfg_cycle_precalc_axi = 16'd100;
        cfg_rtc_timestamp_axi = 32'd0;
        cfg_commit_toggle_axi = 1'b0;
        sw_reset_axi          = 1'b0;

        wait_clks(10);
        rst_n = 1'b1;
        $display("[TB] @%0t rst_n 释放", $time);
        wait_clks(5);

        snapshot("initial");

        // ====== 模拟 INIT 1: 初始配置（不设 CORE_ON）======
        $display("\n--- INIT 1: 初始配置 ---");
        cfg_ctrl_axi = (32'h0000_1612 | 32'h52) & ~32'h01;  // | BOOT_REQ & ~CORE_ON
        apply_shadow_config();
        wait_clks(20);
        snapshot("after_init1");

        // ====== 模拟 load_rom_from_sd 中的 set_sw_reset(1) ======
        $display("\n--- ROM 加载开始: sw_reset=1 ---");
        set_sw_reset(1);
        wait_clks(10);
        snapshot("sw_reset_asserted");

        // ====== 模拟 ROM 加载完成后的配置（行 688-691）======
        $display("\n--- ROM 加载完成: 写入 MaxPakAddr + CORE_ON ---");
        cfg_max_pak_addr_axi = 25'h0400000;  // 16MB ROM
        cfg_ctrl_axi = cfg_ctrl_axi & ~32'h100;  // 清除 ROM_LOADING
        cfg_ctrl_axi = cfg_ctrl_axi | 32'h53;     // 设置 CORE_ON | BOOT_REQUIRED
        $display("[PS] @%0t cfg_max_pak_addr_axi=0x%07X cfg_ctrl_axi=0x%08X",
                 $time, cfg_max_pak_addr_axi, cfg_ctrl_axi);
        apply_shadow_config();  // 行 691

        // 等待 config_mgr 同步（模拟 PS 后续操作耗时）
        wait_clks(20);
        snapshot("after_rom_config");

        // ====== 模拟 usleep(2000) + set_sw_reset(0)（行 697-698）======
        $display("\n--- usleep(2000) + set_sw_reset(0) ---");
        // 2ms = 200,000 cycles at 100MHz, 我们用 100 cycles 模拟
        wait_clks(100);
        snapshot("before_sw_reset_release");

        set_sw_reset(0);  // 行 698: GBA_on 应在此后上升

        // 监控 GBA_on 上升沿后的 MaxPakAddr
        wait_clks(10);
        snapshot("after_sw_reset_release");

        // ====== 模拟第二次 apply_shadow_config（行 701）======
        $display("\n--- 第二次 apply_shadow_config ---");
        wait_clks(100);  // 模拟 usleep(2000)
        apply_shadow_config();  // 行 701
        wait_clks(20);
        snapshot("after_second_commit");

        // ====== 验证最终状态 ======
        $display("\n--- 最终验证 ---");
        if (cfg_max_pak_addr_core == 25'h0400000) begin
            $display("[TEST] ✓ PASS: MaxPakAddr = 0x%07X (正确)", cfg_max_pak_addr_core);
            test_pass++;
        end else begin
            $display("[TEST] ✗ FAIL: MaxPakAddr = 0x%07X (应为 0x0400000)", cfg_max_pak_addr_core);
            test_fail++;
        end

        if (GBA_on) begin
            $display("[TEST] ✓ PASS: GBA_on = 1", );
            test_pass++;
        end else begin
            $display("[TEST] ✗ FAIL: GBA_on = 0", );
            test_fail++;
        end

        // ====== 场景 2: 如果 sw_reset 释放后再提交会怎样 ======
        $display("\n===== 场景 2: sw_reset 释放后 MaxPakAddr 变更 =====");
        cfg_max_pak_addr_axi = 25'h0000000;  // 清零
        apply_shadow_config();
        wait_clks(20);
        snapshot("after_clear_maxpak");

        if (cfg_max_pak_addr_core == 25'd0) begin
            $display("[TEST] ✓ MaxPakAddr 被正确清零: 0x%07X", cfg_max_pak_addr_core);
            test_pass++;
        end else begin
            $display("[TEST] ✗ MaxPakAddr 未清零: 0x%07X", cfg_max_pak_addr_core);
            test_fail++;
        end

        $display("\n============================================");
        $display(" 结束: PASS=%0d FAIL=%0d", test_pass, test_fail);
        $display("============================================\n");

        wait_clks(10);
        $finish;
    end

    initial begin #50_000; $display("[TB] 超时"); $finish; end

endmodule
