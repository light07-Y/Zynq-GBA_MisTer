// ============================================================================
// tb_gpu_timing_cycle.sv
// 行为级仿真：验证 GBA GPU timing + cycling 机制
// 
// 目的：在 lockspeed=1 配置下，用行为模型复现 gba_top.vhd 的 cycling 进程
//       和 gba_gpu_timing.vhd 的状态机，验证 drawline 是否能正确产生。
//
// 被测假设：
//   1) cycling 进程正确产生 gba_step
//   2) CPU 在 gba_step=1 时产生 new_cycles_valid_cpu
//   3) GPU timing 状态机收到 cycles 后推进 VISIBLE→HBLANK→...
//   4) drawline 脉冲按时产生，linecounter 递增
//
// 如果仿真通过 → 硬件问题在信号连接层或综合层
// 如果仿真失败 → 找到时序逻辑 bug
// ============================================================================

`timescale 1ns / 1ps

module tb_gpu_timing_cycle;

  // ---------- 参数（与硬件一致） ----------
  localparam SPEEDDIV       = 6;      // gba_top.vhd constant
  localparam CYCLE_PRECALC  = 100;    // PS 端默认值
  localparam LOCKSPEED      = 1;
  localparam CPUTURBO       = 0;

  // GBA GPU timing 常量
  localparam VISIBLE_CYCLES = 1008;
  localparam HBLANK_CYCLES  = 224;
  localparam SCANLINE_TOTAL = VISIBLE_CYCLES + HBLANK_CYCLES; // 1232
  localparam VISIBLE_LINES  = 160;
  localparam VBLANK_LINES   = 68;     // lines 160-227
  localparam TOTAL_LINES    = 228;

  // GPU 状态编码
  typedef enum logic [1:0] {
    GPU_VISIBLE      = 2'd0,
    GPU_HBLANK       = 2'd1,
    GPU_VBLANK       = 2'd2,
    GPU_VBLANKHBLANK = 2'd3
  } gpu_state_t;

  // ---------- 信号 ----------
  logic clk100 = 0;
  logic rst_n  = 0;

  // cycling 进程信号（复现 gba_top.vhd）
  int   cycles_ahead    = 0;
  int   cycles_16_100   = 0;
  logic new_missing     = 0;
  logic new_exact_cycle = 0;
  logic gba_step        = 0;

  // CPU 模型信号
  logic [7:0] new_cycles_cpu       = 0;
  logic       new_cycles_valid_cpu = 0;
  int         cpu_latency_cnt      = 0;
  localparam  CPU_LATENCY          = 3;  // 模拟每条指令3个clk100延迟

  // new_cycles / new_cycles_valid（顶层mux输出）
  logic [7:0] new_cycles;
  logic       new_cycles_valid;

  // GPU timing 信号（复现 gba_gpu_timing.vhd）
  gpu_state_t gpustate   = GPU_VISIBLE;
  logic [11:0] gpu_cycles = 0;
  logic [7:0]  linecounter = 0;
  logic        drawsoon   = 0;
  logic        drawline   = 0;
  logic        hblank_trigger = 0;
  logic        vblank_trigger = 0;

  // 统计计数器
  int drawline_count    = 0;
  int frame_count       = 0;
  int total_clk         = 0;
  int first_drawline_clk = -1;

  // ---------- 时钟 ----------
  always #5 clk100 = ~clk100;  // 100 MHz

  // ---------- 顶层 mux（复现 gba_top.vhd line 1004-1005）----------
  assign new_cycles       = (CPUTURBO) ? 8'd1 : new_cycles_cpu;
  assign new_cycles_valid = (CPUTURBO) ? new_exact_cycle : new_cycles_valid_cpu;

  // ---------- cycling 进程（复现 gba_top.vhd line 1144-1178）----------
  always @(posedge clk100) begin
    if (!rst_n) begin
      cycles_ahead    <= 0;
      cycles_16_100   <= 0;
      new_missing     <= 0;
      new_exact_cycle <= 0;
      gba_step        <= 0;
    end else begin
      int new_cycles_ahead_v;
      new_missing     <= 0;
      new_exact_cycle <= 0;

      new_cycles_ahead_v = cycles_ahead;
      if (new_cycles_valid)
        new_cycles_ahead_v = new_cycles_ahead_v + new_cycles;

      if (cycles_16_100 < (SPEEDDIV - 1)) begin
        cycles_16_100 <= cycles_16_100 + 1;
      end else begin
        cycles_16_100   <= 0;
        new_exact_cycle <= 1;
        if (new_cycles_ahead_v > 0)
          new_cycles_ahead_v = new_cycles_ahead_v - 1;
        else
          new_missing <= 1;
      end

      if (LOCKSPEED)
        cycles_ahead <= new_cycles_ahead_v;
      else
        cycles_ahead <= 0;

      // gba_step 生成
      gba_step <= 0;
      if (LOCKSPEED == 0 || CPUTURBO == 1 || new_cycles_ahead_v < CYCLE_PRECALC)
        gba_step <= 1;
    end
  end

  // ---------- CPU 行为模型 ----------
  // 模拟：每条指令消耗 CPU_LATENCY 个 clk100，产生 1-3 个 GBA cycle
  always @(posedge clk100) begin
    if (!rst_n) begin
      new_cycles_valid_cpu <= 0;
      new_cycles_cpu       <= 0;
      cpu_latency_cnt      <= 0;
    end else begin
      new_cycles_valid_cpu <= 0;
      if (gba_step) begin
        if (cpu_latency_cnt >= CPU_LATENCY - 1) begin
          cpu_latency_cnt      <= 0;
          new_cycles_valid_cpu <= 1;
          // 随机 1~3 cycles（模拟不同指令长度）
          new_cycles_cpu       <= 8'd1 + ($urandom % 3);
        end else begin
          cpu_latency_cnt <= cpu_latency_cnt + 1;
        end
      end else begin
        cpu_latency_cnt <= 0;  // CPU 被节流时重置
      end
    end
  end

  // ---------- GPU timing 状态机（复现 gba_gpu_timing.vhd line 115-272）----------
  always @(posedge clk100) begin
    if (!rst_n) begin
      gpustate       <= GPU_VISIBLE;
      gpu_cycles     <= 0;
      linecounter    <= 0;
      drawsoon       <= 0;  // 关键：reset 后 drawsoon=0
      drawline       <= 0;
      hblank_trigger <= 0;
      vblank_trigger <= 0;
    end else begin
      // 默认脉冲清零
      drawline       <= 0;
      hblank_trigger <= 0;
      vblank_trigger <= 0;

      // gb_on = 1 分支
      begin
        logic [11:0] cycles_work;
        cycles_work = gpu_cycles;
        if (new_cycles_valid)
          cycles_work = gpu_cycles + {4'd0, new_cycles};

        case (gpustate)
          GPU_VISIBLE: begin
            if (!LOCKSPEED || cycles_work >= 160) begin
              if (drawsoon) begin
                drawline <= 1;
                drawsoon <= 0;
              end
            end
            if (cycles_work >= VISIBLE_CYCLES) begin
              cycles_work    = cycles_work - VISIBLE_CYCLES;
              gpustate       <= GPU_HBLANK;
              hblank_trigger <= 1;
            end
          end

          GPU_HBLANK: begin
            if (cycles_work >= HBLANK_CYCLES) begin
              cycles_work = cycles_work - HBLANK_CYCLES;
              linecounter <= linecounter + 1;
              if ((linecounter + 1) < VISIBLE_LINES) begin
                gpustate <= GPU_VISIBLE;
                drawsoon <= 1;
              end else begin
                gpustate       <= GPU_VBLANK;
                vblank_trigger <= 1;
              end
            end
          end

          GPU_VBLANK: begin
            if (cycles_work >= VISIBLE_CYCLES) begin
              cycles_work = cycles_work - VISIBLE_CYCLES;
              gpustate    <= GPU_VBLANKHBLANK;
            end
          end

          GPU_VBLANKHBLANK: begin
            if (cycles_work >= HBLANK_CYCLES) begin
              cycles_work = cycles_work - HBLANK_CYCLES;
              linecounter <= linecounter + 1;
              if ((linecounter + 1) == TOTAL_LINES) begin
                linecounter <= 0;
                gpustate    <= GPU_VISIBLE;
                drawsoon    <= 1;
              end else begin
                gpustate <= GPU_VBLANK;
              end
            end
          end
        endcase

        gpu_cycles <= cycles_work;
      end
    end
  end

  // ---------- 统计 ----------
  always @(posedge clk100) begin
    if (rst_n) begin
      total_clk <= total_clk + 1;
      if (drawline) begin
        drawline_count <= drawline_count + 1;
        if (first_drawline_clk < 0)
          first_drawline_clk <= total_clk;
      end
      if (vblank_trigger)
        frame_count <= frame_count + 1;
    end
  end

  // ---------- 断言 ----------
  // 在 2 帧时间内（约 2 * 280896 GBA cycles），至少应该看到 drawline
  // 2 帧 ≈ 2 * 280896 * SPEEDDIV / (avg_cycles_per_step) ≈ ~3.4M clk100
  // 加上 CPU 延迟，给 5M 个 clk100
  localparam MAX_CLK_2FRAMES = 5_000_000;

  // ---------- 测试主体 ----------
  initial begin
    $display("============================================================");
    $display("TB: GPU Timing + Cycling 行为仿真");
    $display("  SPEEDDIV=%0d  CYCLE_PRECALC=%0d  LOCKSPEED=%0d  CPUTURBO=%0d",
             SPEEDDIV, CYCLE_PRECALC, LOCKSPEED, CPUTURBO);
    $display("  CPU_LATENCY=%0d clk100/instruction", CPU_LATENCY);
    $display("============================================================");

    // 复位
    rst_n = 0;
    repeat (20) @(posedge clk100);
    rst_n = 1;
    $display("[%0t] Reset released", $time);

    // 等待第一个 drawline
    fork
      begin
        wait(drawline_count > 0);
        $display("[%0t] ★ 第一个 drawline 在 clk=%0d 产生！linecounter=%0d",
                 $time, first_drawline_clk, linecounter);
      end
      begin
        repeat (MAX_CLK_2FRAMES) @(posedge clk100);
        if (drawline_count == 0) begin
          $display("[%0t] ✗ FAIL: %0d clk100 后 drawline 从未产生！", $time, MAX_CLK_2FRAMES);
          $display("  gpu_cycles=%0d  gpustate=%0d  linecounter=%0d  drawsoon=%0d",
                   gpu_cycles, gpustate, linecounter, drawsoon);
          $display("  cycles_ahead=%0d  gba_step=%0d  new_cycles_valid=%0d",
                   cycles_ahead, gba_step, new_cycles_valid);
          $finish;
        end
      end
    join_any
    disable fork;

    // 继续运行直到看到 1 完整帧（160 条 drawline + vblank）
    $display("[%0t] 等待第一帧完成 (160 lines + vblank)...", $time);
    wait(frame_count >= 1);
    $display("[%0t] ★ 第一帧 VBlank！drawline_count=%0d  frame_count=%0d",
             $time, drawline_count, frame_count);

    // 继续到第二帧
    wait(frame_count >= 2);
    $display("[%0t] ★ 第二帧 VBlank！drawline_count=%0d", $time, drawline_count);

    // 验证结果
    $display("============================================================");
    $display("结果汇总:");
    $display("  总 clk100 = %0d", total_clk);
    $display("  drawline  = %0d  (期望 ≥ 319: 159 + 160)", drawline_count);
    $display("  frame     = %0d  (期望 ≥ 2)", frame_count);
    $display("  linectr   = %0d", linecounter);
    $display("  gpustate  = %0d", gpustate);

    if (drawline_count >= 319 && frame_count >= 2) begin
      $display("  ✓ PASS: GPU timing 在 lockspeed=1 下正常产生 drawline！");
      $display("  结论: 硬件 pix_we=0 的问题不在 timing 逻辑本身");
    end else begin
      $display("  ✗ FAIL: drawline 数量不足，cycling 或 timing 有 bug！");
    end
    $display("============================================================");

    // 额外测试：drawsoon 在 reset 后为 0，第一行(line 0)被跳过
    $display("");
    $display("附加检查: 第一帧 line 0 跳过测试");
    $display("  first_drawline_clk = %0d", first_drawline_clk);
    $display("  (line 0 的 drawsoon=0 是正常的，MiSTer 原始行为)");

    $finish;
  end

  // 超时保护
  initial begin
    #100_000_000;  // 100ms
    $display("[TIMEOUT] 仿真超时！");
    $display("  drawline_count=%0d  frame_count=%0d  gpustate=%0d  linecounter=%0d",
             drawline_count, frame_count, gpustate, linecounter);
    $display("  gpu_cycles=%0d  cycles_ahead=%0d  gba_step=%0d",
             gpu_cycles, cycles_ahead, gba_step);
    $finish;
  end

endmodule
