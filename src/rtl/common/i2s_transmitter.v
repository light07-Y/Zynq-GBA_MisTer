// i2s_transmitter.v: 层次化音频层 - 处理 PCM 到 I2S 的串行协议转换
// 责任：解耦音频时分复用、分频及移位寄存器逻辑

module i2s_transmitter (
    input         clk_100,      // 系统时钟 (100MHz)
    input [15:0]  audio_l,      // 左声道 PCM 输入 (16-bit)
    input [15:0]  audio_r,      // 右声道 PCM 输入 (16-bit)
    
    // I2S 物理接口
    output        ac_mclk,      // Master Clock
    output        ac_bclk,      // Bit Clock
    output        ac_pblrc,     // LR Clock (Playback)
    output        ac_pbdat,     // Playback Data
    output        ac_muten      // Mute Enable (Active High)
);

    // --- I2S 系统时钟分频 (48.8kHz @ 100MHz Input) ---
    reg [10:0] audio_div = 11'd0;
    wire [10:0] audio_div_next = audio_div + 11'd1;

    // 分拼方案:
    // Bit 1 -> 12.5MHz (MCLK)
    // Bit 4 -> 1.56MHz (BCLK, 32bits * 48kHz)
    // Bit 9 -> 48.828kHz (LRCLK)
    assign ac_mclk  = audio_div[1];
    assign ac_bclk  = audio_div[4];
    assign ac_pblrc = audio_div[9];
    assign ac_muten = 1'b1; // 取消静音

    // --- 串行移位逻辑 ---
    // 保持所有寄存器只在 clk_100 时钟域触发，避免派生时钟导致 no_clock/CDC 约束告警。
    reg [15:0] shift_reg = 16'd0;
    always @(posedge clk_100) begin
        audio_div <= audio_div_next;

        // 检测下一拍 BCLK 上升沿，等效于原来的 posedge ac_bclk 触发。
        if (audio_div[4] == 1'b0 && audio_div_next[4] == 1'b1) begin
            // 在 LRCK 转换前的一个 BCLK 周期加载数据
            if (audio_div_next[8:4] == 5'b11111) begin
                if (audio_div_next[9] == 1'b1) shift_reg <= audio_l;
                else                           shift_reg <= audio_r;
            end else begin
                shift_reg <= {shift_reg[14:0], 1'b0};
            end
        end
    end
    
    assign ac_pbdat = shift_reg[15]; // MSB First

endmodule
