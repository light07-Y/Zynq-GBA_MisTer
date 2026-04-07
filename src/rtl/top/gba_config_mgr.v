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
    
    // 板载物理输入 (来自 Pin Domain)
    input [3:0]   btns,           // 物理按键 (A, B, Select, Start)
    input [3:0]   sws,            // 物理拨码 ( 方向键)
    
    // 同步后的核心配置 (Core Domain)
    output reg [31:0] cfg_ctrl_core,
    output     [9:0]  cfg_keys_core,
    output reg [24:0] cfg_max_pak_addr_core,
    output reg [15:0] cfg_cycle_precalc_core,
    output reg [31:0] cfg_rtc_timestamp_core
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

    // AXI 配置提交脉冲检测
    assign cfg_commit_pulse = (cfg_commit_sync[2] ^ cfg_commit_sync[1]);

    always @(posedge clk_core) begin
        if (!rst_n) begin
            {cfg_ctrl_meta, cfg_ctrl_sync} <= 64'h0000_1600_0000_1600;
            {cfg_keys_meta, cfg_keys_sync} <= 20'd0;
            {cfg_max_pak_addr_meta, cfg_max_pak_addr_sync} <= 50'd0;
            {cfg_cycle_precalc_meta, cfg_cycle_precalc_sync} <= 32'd100;
            {cfg_rtc_timestamp_meta, cfg_rtc_timestamp_sync} <= 64'd0;
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
        end
    end

    // --- 2. 配置提交逻辑 (Atomic Commit) ---
    // AXI 传入的按键寄存器在提交时才参与合并逻辑
    reg [9:0] cfg_keys_axi_reg;

    always @(posedge clk_core) begin
        if (!rst_n) begin
            cfg_ctrl_core          <= 32'h0000_1600;
            cfg_keys_axi_reg       <= 10'd0;
            cfg_max_pak_addr_core  <= 25'd0;
            cfg_cycle_precalc_core <= 16'd100;
            cfg_rtc_timestamp_core <= 32'd0;
        end else if (cfg_commit_pulse) begin
            cfg_ctrl_core          <= cfg_ctrl_sync;
            cfg_keys_axi_reg       <= cfg_keys_sync;
            cfg_max_pak_addr_core  <= cfg_max_pak_addr_sync;
            cfg_cycle_precalc_core <= cfg_cycle_precalc_sync;
            cfg_rtc_timestamp_core <= cfg_rtc_timestamp_sync;
        end
    end

    // --- 3. 物理按键 CDC 同步与合并 ---
    (* ASYNC_REG = "TRUE" *) reg [9:0] physical_keys_meta, physical_keys_sync;

    always @(posedge clk_core) begin
        // 物理按键直接从引脚同步到核心时钟域
        physical_keys_meta <= {
            2'b0,         // R, L (暂无物理映射)
            sws[1],       // Down
            sws[0],       // Up
            sws[3],       // Left
            sws[2],       // Right
            btns[3],      // Start
            btns[2],      // Select
            btns[1],      // B
            btns[0]       // A
        };
        physical_keys_sync <= physical_keys_meta;
    end

    // 最终核心按键输入 = AXI控制值 | 物理输入值
    assign cfg_keys_core = cfg_keys_axi_reg | physical_keys_sync;

endmodule
