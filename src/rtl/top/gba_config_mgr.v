// gba_config_mgr.v: 系统控制层 - 处理寄存器同步、CDC 与按键映射
// 责任：将 PS 端的非同步配置转化为 PL 核心端的稳定同步信号

module gba_config_mgr (
    input         clk_core,       // 核心频率时钟 (100MHz)
    input         rst_n,          // 系统复位

    // AXI 控制寄存器接口 (来自 AXI Domain)
    input [31:0]  cfg_ctrl_axi,
    input [9:0]   cfg_keys_axi,
    input [24:0]  cfg_max_pak_addr_axi,
    input [15:0]  cfg_cycle_precalc_axi,
    input [31:0]  cfg_rtc_timestamp_axi,
    input         cfg_commit_toggle_axi,

    input [5:0]   cfg_feature_ctrl_axi,
    input [2:0]   cfg_savestate_slot_axi,
    input [31:0]  cfg_cheat_flags_axi,
    input [31:0]  cfg_cheat_addr_axi,
    input [31:0]  cfg_cheat_compare_axi,
    input [31:0]  cfg_cheat_replace_axi,
    input [23:0]  cfg_sensor_input_axi,
    input [31:0]  cfg_rtc_savedtime_lo_axi,
    input [31:0]  cfg_rtc_savedtime_hi_axi,

    input         cfg_action_save_toggle_axi,
    input         cfg_action_load_toggle_axi,
    input         cfg_action_cheat_push_toggle_axi,
    input         cfg_action_cheat_clear_toggle_axi,
    input         cfg_action_rtc_new_toggle_axi,

    // 板载物理输入 (来自 Pin Domain)
    input [3:0]   btns,           // 物理按键 (Up, Down, Left, Right)
    input [3:0]   sws,            // 物理拨码 (A, B, Select, Start)

    // 同步后的核心配置 (Core Domain)
    output reg [31:0] cfg_ctrl_core,
    output     [9:0]  cfg_keys_core,
    output reg [24:0] cfg_max_pak_addr_core,
    output reg [15:0] cfg_cycle_precalc_core,
    output reg [31:0] cfg_rtc_timestamp_core,

    output reg [5:0]  cfg_feature_ctrl_core,
    output reg [2:0]  cfg_savestate_slot_core,
    output reg [31:0] cfg_cheat_flags_core,
    output reg [31:0] cfg_cheat_addr_core,
    output reg [31:0] cfg_cheat_compare_core,
    output reg [31:0] cfg_cheat_replace_core,
    output reg [2:0]  cfg_sensor_solar_core,
    output reg signed [7:0] cfg_sensor_tilt_x_core,
    output reg signed [7:0] cfg_sensor_tilt_y_core,
    output reg [41:0] cfg_rtc_savedtime_core,
    output reg        cfg_rtc_save_loaded_core,

    output            pulse_save_state_core,
    output            pulse_load_state_core,
    output            pulse_cheat_push_core,
    output            pulse_cheat_clear_core,
    output            pulse_rtc_new_core
);

    // --- 1. AXI 到 Core Domain 的多级 CDC 同步 ---
    (* ASYNC_REG = "TRUE" *) reg [2:0]  cfg_commit_sync;
    wire       cfg_commit_pulse;

    // 状态阴影寄存器 (Shadow Registers)
    (* ASYNC_REG = "TRUE" *) reg [31:0] cfg_ctrl_meta, cfg_ctrl_sync;
    (* ASYNC_REG = "TRUE" *) reg [9:0]  cfg_keys_meta, cfg_keys_sync;
    (* ASYNC_REG = "TRUE" *) reg [24:0] cfg_max_pak_addr_meta, cfg_max_pak_addr_sync;
    (* ASYNC_REG = "TRUE" *) reg [15:0] cfg_cycle_precalc_meta, cfg_cycle_precalc_sync;
    (* ASYNC_REG = "TRUE" *) reg [31:0] cfg_rtc_timestamp_meta, cfg_rtc_timestamp_sync;

    (* ASYNC_REG = "TRUE" *) reg [5:0]  cfg_feature_ctrl_meta, cfg_feature_ctrl_sync;
    (* ASYNC_REG = "TRUE" *) reg [2:0]  cfg_savestate_slot_meta, cfg_savestate_slot_sync;
    (* ASYNC_REG = "TRUE" *) reg [31:0] cfg_cheat_flags_meta, cfg_cheat_flags_sync;
    (* ASYNC_REG = "TRUE" *) reg [31:0] cfg_cheat_addr_meta, cfg_cheat_addr_sync;
    (* ASYNC_REG = "TRUE" *) reg [31:0] cfg_cheat_compare_meta, cfg_cheat_compare_sync;
    (* ASYNC_REG = "TRUE" *) reg [31:0] cfg_cheat_replace_meta, cfg_cheat_replace_sync;
    (* ASYNC_REG = "TRUE" *) reg [23:0] cfg_sensor_input_meta, cfg_sensor_input_sync;
    (* ASYNC_REG = "TRUE" *) reg [31:0] cfg_rtc_savedtime_lo_meta, cfg_rtc_savedtime_lo_sync;
    (* ASYNC_REG = "TRUE" *) reg [31:0] cfg_rtc_savedtime_hi_meta, cfg_rtc_savedtime_hi_sync;

    (* ASYNC_REG = "TRUE" *) reg [2:0] cfg_action_save_sync;
    (* ASYNC_REG = "TRUE" *) reg [2:0] cfg_action_load_sync;
    (* ASYNC_REG = "TRUE" *) reg [2:0] cfg_action_cheat_push_sync;
    (* ASYNC_REG = "TRUE" *) reg [2:0] cfg_action_cheat_clear_sync;
    (* ASYNC_REG = "TRUE" *) reg [2:0] cfg_action_rtc_new_sync;

    // AXI 配置提交脉冲检测
    assign cfg_commit_pulse = (cfg_commit_sync[2] ^ cfg_commit_sync[1]);

    assign pulse_save_state_core  = cfg_action_save_sync[2] ^ cfg_action_save_sync[1];
    assign pulse_load_state_core  = cfg_action_load_sync[2] ^ cfg_action_load_sync[1];
    assign pulse_cheat_push_core  = cfg_action_cheat_push_sync[2] ^ cfg_action_cheat_push_sync[1];
    assign pulse_cheat_clear_core = cfg_action_cheat_clear_sync[2] ^ cfg_action_cheat_clear_sync[1];
    assign pulse_rtc_new_core     = cfg_action_rtc_new_sync[2] ^ cfg_action_rtc_new_sync[1];

    always @(posedge clk_core) begin
        if (!rst_n) begin
            {cfg_ctrl_meta, cfg_ctrl_sync} <= 64'h0000_1612_0000_1612;
            {cfg_keys_meta, cfg_keys_sync} <= 20'd0;
            {cfg_max_pak_addr_meta, cfg_max_pak_addr_sync} <= 50'd0;
            {cfg_cycle_precalc_meta, cfg_cycle_precalc_sync} <= 32'd100;
            {cfg_rtc_timestamp_meta, cfg_rtc_timestamp_sync} <= 64'd0;

            {cfg_feature_ctrl_meta, cfg_feature_ctrl_sync} <= {6'h1C, 6'h1C};
            {cfg_savestate_slot_meta, cfg_savestate_slot_sync} <= 6'd0;
            {cfg_cheat_flags_meta, cfg_cheat_flags_sync} <= 64'd0;
            {cfg_cheat_addr_meta, cfg_cheat_addr_sync} <= 64'd0;
            {cfg_cheat_compare_meta, cfg_cheat_compare_sync} <= 64'd0;
            {cfg_cheat_replace_meta, cfg_cheat_replace_sync} <= 64'd0;
            {cfg_sensor_input_meta, cfg_sensor_input_sync} <= {24'h000003, 24'h000003};
            {cfg_rtc_savedtime_lo_meta, cfg_rtc_savedtime_lo_sync} <= 64'd0;
            {cfg_rtc_savedtime_hi_meta, cfg_rtc_savedtime_hi_sync} <= 64'd0;

            cfg_action_save_sync <= 3'b000;
            cfg_action_load_sync <= 3'b000;
            cfg_action_cheat_push_sync <= 3'b000;
            cfg_action_cheat_clear_sync <= 3'b000;
            cfg_action_rtc_new_sync <= 3'b000;
            cfg_commit_sync <= 3'b000;
        end else begin
            // 交叉时钟域采样
            cfg_ctrl_meta          <= cfg_ctrl_axi;
            cfg_ctrl_sync          <= cfg_ctrl_meta;
            cfg_keys_meta          <= cfg_keys_axi;
            cfg_keys_sync          <= cfg_keys_meta;
            cfg_max_pak_addr_meta  <= cfg_max_pak_addr_axi;
            cfg_max_pak_addr_sync  <= cfg_max_pak_addr_meta;
            cfg_cycle_precalc_meta <= cfg_cycle_precalc_axi;
            cfg_cycle_precalc_sync <= cfg_cycle_precalc_meta;
            cfg_rtc_timestamp_meta <= cfg_rtc_timestamp_axi;
            cfg_rtc_timestamp_sync <= cfg_rtc_timestamp_meta;
            cfg_commit_sync        <= {cfg_commit_sync[1:0], cfg_commit_toggle_axi};

            cfg_feature_ctrl_meta <= cfg_feature_ctrl_axi;
            cfg_feature_ctrl_sync <= cfg_feature_ctrl_meta;
            cfg_savestate_slot_meta <= cfg_savestate_slot_axi;
            cfg_savestate_slot_sync <= cfg_savestate_slot_meta;
            cfg_cheat_flags_meta <= cfg_cheat_flags_axi;
            cfg_cheat_flags_sync <= cfg_cheat_flags_meta;
            cfg_cheat_addr_meta <= cfg_cheat_addr_axi;
            cfg_cheat_addr_sync <= cfg_cheat_addr_meta;
            cfg_cheat_compare_meta <= cfg_cheat_compare_axi;
            cfg_cheat_compare_sync <= cfg_cheat_compare_meta;
            cfg_cheat_replace_meta <= cfg_cheat_replace_axi;
            cfg_cheat_replace_sync <= cfg_cheat_replace_meta;
            cfg_sensor_input_meta <= cfg_sensor_input_axi;
            cfg_sensor_input_sync <= cfg_sensor_input_meta;
            cfg_rtc_savedtime_lo_meta <= cfg_rtc_savedtime_lo_axi;
            cfg_rtc_savedtime_lo_sync <= cfg_rtc_savedtime_lo_meta;
            cfg_rtc_savedtime_hi_meta <= cfg_rtc_savedtime_hi_axi;
            cfg_rtc_savedtime_hi_sync <= cfg_rtc_savedtime_hi_meta;

            cfg_action_save_sync <= {cfg_action_save_sync[1:0], cfg_action_save_toggle_axi};
            cfg_action_load_sync <= {cfg_action_load_sync[1:0], cfg_action_load_toggle_axi};
            cfg_action_cheat_push_sync <= {cfg_action_cheat_push_sync[1:0], cfg_action_cheat_push_toggle_axi};
            cfg_action_cheat_clear_sync <= {cfg_action_cheat_clear_sync[1:0], cfg_action_cheat_clear_toggle_axi};
            cfg_action_rtc_new_sync <= {cfg_action_rtc_new_sync[1:0], cfg_action_rtc_new_toggle_axi};
        end
    end

    // --- 2. 配置提交逻辑 (Atomic Commit) ---
    // 关键点：cfg_* 与 commit_toggle 是独立 CDC，同拍提交会存在采样旧值的窗口。
    // 这里在检测到 commit 后额外等待若干 core 周期，再锁存同步后的 cfg_*。
    localparam [1:0] COMMIT_SETTLE_CYCLES = 2'd2;

    reg [9:0] cfg_keys_axi_reg;
    reg       cfg_commit_pending;
    reg [1:0] cfg_commit_settle_count;

    always @(posedge clk_core) begin
        if (!rst_n) begin
            cfg_ctrl_core          <= 32'h0000_1612;
            cfg_keys_axi_reg       <= 10'd0;
            cfg_max_pak_addr_core  <= 25'd0;
            cfg_cycle_precalc_core <= 16'd100;
            cfg_rtc_timestamp_core <= 32'd0;
            cfg_commit_pending     <= 1'b0;
            cfg_commit_settle_count <= 2'd0;

            cfg_feature_ctrl_core <= 6'h1C;
            cfg_savestate_slot_core <= 3'd0;
            cfg_cheat_flags_core <= 32'd0;
            cfg_cheat_addr_core <= 32'd0;
            cfg_cheat_compare_core <= 32'd0;
            cfg_cheat_replace_core <= 32'd0;
            cfg_sensor_solar_core <= 3'd3;
            cfg_sensor_tilt_x_core <= 8'sd0;
            cfg_sensor_tilt_y_core <= 8'sd0;
            cfg_rtc_savedtime_core <= 42'd0;
            cfg_rtc_save_loaded_core <= 1'b0;
        end else begin
            if (cfg_commit_pulse) begin
                cfg_commit_pending <= 1'b1;
                cfg_commit_settle_count <= COMMIT_SETTLE_CYCLES;
            end else if (cfg_commit_pending) begin
                if (cfg_commit_settle_count != 2'd0) begin
                    cfg_commit_settle_count <= cfg_commit_settle_count - 2'd1;
                end else begin
                    cfg_ctrl_core          <= cfg_ctrl_sync;
                    cfg_keys_axi_reg       <= cfg_keys_sync;
                    cfg_max_pak_addr_core  <= cfg_max_pak_addr_sync;
                    cfg_cycle_precalc_core <= cfg_cycle_precalc_sync;
                    cfg_rtc_timestamp_core <= cfg_rtc_timestamp_sync;
                    cfg_commit_pending     <= 1'b0;
                end
            end

            cfg_feature_ctrl_core <= cfg_feature_ctrl_sync;
            cfg_savestate_slot_core <= cfg_savestate_slot_sync;
            cfg_cheat_flags_core <= cfg_cheat_flags_sync;
            cfg_cheat_addr_core <= cfg_cheat_addr_sync;
            cfg_cheat_compare_core <= cfg_cheat_compare_sync;
            cfg_cheat_replace_core <= cfg_cheat_replace_sync;
            cfg_sensor_solar_core <= cfg_sensor_input_sync[2:0];
            cfg_sensor_tilt_x_core <= $signed(cfg_sensor_input_sync[15:8]);
            cfg_sensor_tilt_y_core <= $signed(cfg_sensor_input_sync[23:16]);
            cfg_rtc_savedtime_core <= {cfg_rtc_savedtime_hi_sync[9:0], cfg_rtc_savedtime_lo_sync};
            cfg_rtc_save_loaded_core <= cfg_rtc_savedtime_hi_sync[31];
        end
    end

    // --- 3. 物理按键 CDC 同步与合并 ---
    (* ASYNC_REG = "TRUE" *) reg [9:0] physical_keys_meta, physical_keys_sync;

    always @(posedge clk_core) begin
        // 物理按键直接从引脚同步到核心时钟域
        physical_keys_meta <= {
            (sws[3] & btns[3]),
            (sws[3] & btns[0]),
            btns[1],
            btns[2],
            (btns[3] & ~sws[3]),
            (btns[0] & ~sws[3]),
            sws[2],
            (sws[3] & ~btns[3] & ~btns[0]),
            sws[1],
            sws[0]
        };
        physical_keys_sync <= physical_keys_meta;
    end

    // 最终核心按键输入 = AXI控制值 | 物理输入值
    assign cfg_keys_core = cfg_keys_axi_reg | physical_keys_sync;

endmodule
