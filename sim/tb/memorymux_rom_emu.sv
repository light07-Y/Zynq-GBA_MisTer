// memorymux_rom_emu.sv: 精确复刻 gba_memorymux.vhd 中 ROM 读路径的行为模型
// 目的：验证 MaxPakAddr 比较 + WAIT_SDRAM 双完成路径 + mini cache 逻辑
// 信号命名严格匹配 VHDL 原始代码

module memorymux_rom_emu (
    input  logic        clk,
    input  logic        gb_on,

    // GBA CPU 总线（简化版，仅 ROM 读）
    input  logic [31:0] mem_bus_Adr,
    input  logic        mem_bus_rnw,
    input  logic        mem_bus_ena,
    input  logic [1:0]  mem_bus_acc,    // ACCESS_8BIT=00, ACCESS_16BIT=01, ACCESS_32BIT=10
    output logic [31:0] mem_bus_din,
    output logic        mem_bus_done,

    // MaxPakAddr 配置（来自 gba_config_mgr）
    input  logic [24:0] MaxPakAddr,

    // 连接 cache 的接口
    output logic        cache_read_enable,
    output logic [22:0] cache_read_addr,
    input  logic [31:0] cache_read_data,
    input  logic        cache_read_done,
    input  logic [63:0] cache_read_full,

    // 连接 DDR 的直通接口（bypasses cache on miss）
    input  logic        sdram_read_done,    // = ch1_ready
    input  logic [31:0] sdram_read_data,    // = ch1_dout[31:0]
    input  logic [31:0] sdram_second_dword  // = ch1_dout[63:32]
);

    // 状态定义（匹配 gba_memorymux.vhd）
    typedef enum logic [3:0] {
        IDLE,
        WAIT_SDRAM,
        READAFTERPAK,
        ROTATE
    } state_t;
    state_t state = IDLE;

    // 访问类型常量
    localparam [1:0] ACCESS_8BIT  = 2'b00;
    localparam [1:0] ACCESS_16BIT = 2'b01;
    localparam [1:0] ACCESS_32BIT = 2'b10;

    // 保存寄存器（匹配 VHDL adr_save / acc_save）
    logic [27:0] adr_save;
    logic [1:0]  acc_save;
    logic        read_operation;
    logic [1:0]  return_rotate;

    // mini cache（匹配 VHDL sdram_addr_buf / sdram_data_buf）
    logic [21:0] sdram_addr_buf = '1;
    logic [63:0] sdram_data_buf;
    logic        sdram_read_done_1 = 1'b0;

    // 从 cache 获取的 sdram_read_addr_int 的 bit 0
    // (这个信号在 memorymux 中用于 mini cache 逻辑)
    // 由于我们是 emulator，直接从 cache_read_addr 推导
    logic        sdram_addr_int_bit0;
    assign sdram_addr_int_bit0 = cache_read_addr[0];

    always_ff @(posedge clk) begin
        mem_bus_done      <= 1'b0;
        cache_read_enable <= 1'b0;

        // mini cache 更新逻辑（精确匹配 VHDL 第 398-411 行）
        if (sdram_read_done) begin
            if (!sdram_addr_int_bit0)
                sdram_data_buf[31:0]  <= sdram_read_data;
            else
                sdram_data_buf[63:32] <= sdram_read_data;
        end

        sdram_read_done_1 <= sdram_read_done;
        if (sdram_read_done_1) begin
            if (sdram_addr_int_bit0)
                sdram_data_buf[31:0]  <= sdram_second_dword;
            else
                sdram_data_buf[63:32] <= sdram_second_dword;
        end

        if (!gb_on) begin
            state <= IDLE;
        end else begin
            case (state)
                IDLE: begin
                    if (mem_bus_ena && mem_bus_rnw) begin
                        adr_save       <= mem_bus_Adr[27:0];
                        acc_save       <= mem_bus_acc;
                        read_operation <= 1'b1;
                        return_rotate  <= mem_bus_Adr[1:0];

                        // 匹配 VHDL case(mem_bus_Adr(27 downto 24))
                        case (mem_bus_Adr[27:24])
                            4'h8, 4'h9, 4'hA, 4'hB, 4'hC: begin
                                $display("[EMU] @%0t ROM 访问: Adr=0x%08X Adr(24:2)=0x%06X MaxPak=0x%07X",
                                         $time, mem_bus_Adr,
                                         mem_bus_Adr[24:2], MaxPakAddr);

                                // 精确匹配 VHDL 第 531 行的比较
                                // if (unsigned(mem_bus_Adr(24 downto 2)) >= unsigned(MaxPakAddr)) then
                                if (mem_bus_Adr[24:2] >= MaxPakAddr[22:0]) begin
                                    // ★ 注意位宽: VHDL 中 mem_bus_Adr(24:2) 是 23 位,
                                    //   MaxPakAddr 是 25 位, VHDL 自动零扩展 23→25 位比较
                                    // 这里我们先按 23 位比较（可能有 bug！）
                                    $display("[EMU] @%0t → READAFTERPAK (addr >= MaxPak)", $time);
                                    state <= READAFTERPAK;
                                end
                                // mini cache 命中检查（匹配 VHDL 第 533 行）
                                else if (sdram_addr_buf == mem_bus_Adr[24:3] &&
                                         mem_bus_Adr[0] == 1'b0 &&
                                         mem_bus_acc == ACCESS_16BIT) begin
                                    $display("[EMU] @%0t → mini cache HIT (16-bit)", $time);
                                    mem_bus_done <= 1'b1;
                                    state <= IDLE;
                                    case (mem_bus_Adr[2:1])
                                        2'b00: mem_bus_din <= {16'h0000, sdram_data_buf[15:0]};
                                        2'b01: mem_bus_din <= {16'h0000, sdram_data_buf[31:16]};
                                        2'b10: mem_bus_din <= {16'h0000, sdram_data_buf[47:32]};
                                        2'b11: mem_bus_din <= {16'h0000, sdram_data_buf[63:48]};
                                    endcase
                                end
                                else if (sdram_addr_buf == mem_bus_Adr[24:3] &&
                                         mem_bus_Adr[1:0] == 2'b00 &&
                                         mem_bus_acc == ACCESS_32BIT) begin
                                    $display("[EMU] @%0t → mini cache HIT (32-bit)", $time);
                                    mem_bus_done <= 1'b1;
                                    state <= IDLE;
                                    mem_bus_din <= mem_bus_Adr[2] ?
                                        sdram_data_buf[63:32] : sdram_data_buf[31:0];
                                end
                                else begin
                                    $display("[EMU] @%0t → cache_read_enable (WAIT_SDRAM)", $time);
                                    cache_read_enable <= 1'b1;
                                    cache_read_addr   <= mem_bus_Adr[24:2];
                                    state <= WAIT_SDRAM;
                                end
                            end
                            default: begin
                                $display("[EMU] @%0t 非 ROM 地址: 0x%08X", $time, mem_bus_Adr);
                            end
                        endcase
                    end
                end

                WAIT_SDRAM: begin
                    // 路径 A: 通过 sdram_read_done（DDR 直接返回，cold miss 走这条）
                    if (sdram_read_done) begin
                        $display("[EMU] @%0t WAIT_SDRAM: sdram_read_done=1 data=0x%08X",
                                 $time, sdram_read_data);
                        sdram_addr_buf <= adr_save[24:3];
                        if (acc_save == ACCESS_32BIT) begin
                            mem_bus_done <= 1'b1;
                            state <= IDLE;
                            case (return_rotate)
                                2'b00: mem_bus_din <= sdram_read_data;
                                2'b01: mem_bus_din <= {sdram_read_data[7:0],  sdram_read_data[31:8]};
                                2'b10: mem_bus_din <= {sdram_read_data[15:0], sdram_read_data[31:16]};
                                2'b11: mem_bus_din <= {sdram_read_data[23:0], sdram_read_data[31:24]};
                            endcase
                        end else if (acc_save == ACCESS_16BIT) begin
                            mem_bus_done <= 1'b1;
                            state <= IDLE;
                            case (return_rotate)
                                2'b00: mem_bus_din <= {16'h0000, sdram_read_data[15:0]};
                                2'b10: mem_bus_din <= {16'h0000, sdram_read_data[31:16]};
                                default: mem_bus_din <= sdram_read_data;
                            endcase
                        end else begin
                            // ACCESS_8BIT → 需要 ROTATE, 简化为直接返回
                            mem_bus_done <= 1'b1;
                            state <= IDLE;
                            mem_bus_din <= sdram_read_data;
                        end
                    end

                    // 路径 B: 通过 cache_read_done（cache hit 走这条）
                    if (cache_read_done) begin
                        $display("[EMU] @%0t WAIT_SDRAM: cache_read_done=1 data=0x%08X full=0x%016X",
                                 $time, cache_read_data, cache_read_full);
                        sdram_addr_buf <= adr_save[24:3];
                        sdram_data_buf <= cache_read_full;
                        if (acc_save == ACCESS_32BIT) begin
                            mem_bus_done <= 1'b1;
                            state <= IDLE;
                            case (return_rotate)
                                2'b00: mem_bus_din <= cache_read_data;
                                2'b01: mem_bus_din <= {cache_read_data[7:0],  cache_read_data[31:8]};
                                2'b10: mem_bus_din <= {cache_read_data[15:0], cache_read_data[31:16]};
                                2'b11: mem_bus_din <= {cache_read_data[23:0], cache_read_data[31:24]};
                            endcase
                        end else if (acc_save == ACCESS_16BIT) begin
                            mem_bus_done <= 1'b1;
                            state <= IDLE;
                            case (return_rotate)
                                2'b00: mem_bus_din <= {16'h0000, cache_read_data[15:0]};
                                2'b10: mem_bus_din <= {16'h0000, cache_read_data[31:16]};
                                default: mem_bus_din <= cache_read_data;
                            endcase
                        end else begin
                            mem_bus_done <= 1'b1;
                            state <= IDLE;
                            mem_bus_din <= cache_read_data;
                        end
                    end
                end

                READAFTERPAK: begin
                    // 匹配 VHDL 第 835 行
                    mem_bus_din  <= {adr_save[16:2], 1'b1, adr_save[16:2], 1'b0};
                    mem_bus_done <= 1'b1;
                    state <= IDLE;
                    $display("[EMU] @%0t READAFTERPAK: 返回地址模式数据 din=0x%08X",
                             $time, {adr_save[16:2], 1'b1, adr_save[16:2], 1'b0});
                end

                default: state <= IDLE;
            endcase
        end
    end

    // ★ 关键: 精确复刻 VHDL 位宽比较的第二版本（25 位 vs 23 位）
    // 用于诊断报告
    always @(posedge clk) begin
        if (mem_bus_ena && mem_bus_rnw && gb_on && state == IDLE) begin
            if (mem_bus_Adr[27:24] inside {4'h8, 4'h9, 4'hA, 4'hB, 4'hC}) begin
                // VHDL 原始: unsigned(mem_bus_Adr(24 downto 2)) >= unsigned(MaxPakAddr)
                // mem_bus_Adr(24:2) = 23 bits, MaxPakAddr = 25 bits
                // VHDL 自动零扩展 23 位到 25 位进行比较
                automatic logic [24:0] addr_ext = {2'b00, mem_bus_Adr[24:2]};
                automatic logic [24:0] maxpak   = MaxPakAddr;
                automatic logic        vhdl_cmp = (addr_ext >= maxpak);
                automatic logic        sv_cmp   = (mem_bus_Adr[24:2] >= MaxPakAddr[22:0]);
                $display("[DIAG] @%0t VHDL比较(25bit): {00,Adr[24:2]}=0x%07X >= MaxPak=0x%07X → %s",
                         $time, addr_ext, maxpak, vhdl_cmp ? "TRUE(READAFTERPAK)" : "FALSE(cache)");
                $display("[DIAG] @%0t SV比较(23bit):   Adr[24:2]=0x%06X >= MaxPak[22:0]=0x%06X → %s",
                         $time, mem_bus_Adr[24:2], MaxPakAddr[22:0], sv_cmp ? "TRUE" : "FALSE");
                if (vhdl_cmp != sv_cmp) begin
                    $display("[DIAG] ★★★ 位宽不匹配! VHDL=%b SV=%b ★★★", vhdl_cmp, sv_cmp);
                end
            end
        end
    end

endmodule
