#!/usr/bin/env python3
"""
GBA ROM 文件分析工具
- 验证头部结构和校验和
- 提取仿真所需的关键数据（入口指令、前 N 个 DWORD）
- 生成 Verilog $readmemh 可用的 hex 文件供 RTL 仿真使用
"""

import struct
import sys
import os

ROM_PATH = r"F:\02_Projects\GBA_ROM\PokemonSapphire-E.gba"
# 仿真用：导出前 4KB (1024 DWORD) 的 hex 数据
SIM_EXPORT_BYTES = 4096
HEX_OUT_PATH = os.path.join(os.path.dirname(__file__), "..", "rom_data", "rom_first_4k.hex")

def read_rom(path):
    with open(path, "rb") as f:
        return f.read()

def parse_header(data):
    """解析 GBA ROM 头部 (0x000 - 0x0BF)"""
    if len(data) < 0xC0:
        print("[ERROR] 文件太小，不是有效的 GBA ROM")
        return None

    entry_word = struct.unpack_from("<I", data, 0x00)[0]
    # Nintendo Logo (0x04 - 0x9F): 156 bytes
    title = data[0xA0:0xAC].decode("ascii", errors="replace").rstrip("\x00")
    game_code = data[0xAC:0xB0].decode("ascii", errors="replace")
    maker_code = data[0xB0:0xB2].decode("ascii", errors="replace")
    fixed_val = data[0xB2]
    unit_code = data[0xB3]
    device_type = data[0xB4]
    rom_version = data[0xBC]
    header_checksum = data[0xBD]

    # 计算头部校验和: 从 0xA0 到 0xBC 的字节累加，取反+1 再截断到 8 位
    chk_sum = 0
    for b in data[0xA0:0xBD]:
        chk_sum = (chk_sum - b) & 0xFF
    calc_checksum = (chk_sum - 0x19) & 0xFF

    return {
        "entry_word": entry_word,
        "title": title,
        "game_code": game_code,
        "maker_code": maker_code,
        "fixed_val": fixed_val,
        "unit_code": unit_code,
        "device_type": device_type,
        "rom_version": rom_version,
        "header_checksum": header_checksum,
        "calc_checksum": calc_checksum,
        "checksum_ok": header_checksum == calc_checksum,
    }

def decode_arm_branch(word, pc=0x08000000):
    """解码 ARM B/BL 指令"""
    cond = (word >> 28) & 0xF
    opcode = (word >> 24) & 0xF
    if opcode == 0xA:  # B
        offset = word & 0x00FFFFFF
        if offset & 0x800000:
            offset |= 0xFF000000  # 符号扩展
        target = pc + 8 + (offset << 2)
        target &= 0xFFFFFFFF
        return f"B 0x{target:08X} (cond={cond:X})"
    elif opcode == 0xB:  # BL
        offset = word & 0x00FFFFFF
        if offset & 0x800000:
            offset |= 0xFF000000
        target = pc + 8 + (offset << 2)
        target &= 0xFFFFFFFF
        return f"BL 0x{target:08X} (cond={cond:X})"
    else:
        return f"non-branch opcode=0x{opcode:X}"

def check_nintendo_logo(data):
    """检查 Nintendo Logo 区域 (0x04-0x9F) 是否全零（简单检查）"""
    logo = data[0x04:0xA0]
    non_zero = sum(1 for b in logo if b != 0)
    return non_zero, len(logo)

def export_hex_for_sim(data, n_bytes, out_path):
    """导出前 n_bytes 为 Verilog $readmemh 格式（每行一个 32-bit DWORD，小端）"""
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    n_words = n_bytes // 4
    with open(out_path, "w") as f:
        for i in range(n_words):
            off = i * 4
            if off + 4 <= len(data):
                word = struct.unpack_from("<I", data, off)[0]
            else:
                word = 0
            f.write(f"{word:08X}\n")
    return n_words

def compute_max_pak_addr(rom_size):
    """与 PS 端 load_rom_from_sd 一致的 MaxPakAddr 计算"""
    aligned = (rom_size + 3) & ~3
    return aligned // 4

def main():
    print(f"=== GBA ROM 分析: {ROM_PATH} ===\n")

    if not os.path.isfile(ROM_PATH):
        print(f"[ERROR] 文件不存在: {ROM_PATH}")
        sys.exit(1)

    data = read_rom(ROM_PATH)
    rom_size = len(data)
    print(f"[INFO] 文件大小: {rom_size} bytes ({rom_size/1024/1024:.2f} MB)")
    print(f"[INFO] MaxPakAddr (DWORD): 0x{compute_max_pak_addr(rom_size):08X}")
    print()

    # --- 头部解析 ---
    hdr = parse_header(data)
    if hdr is None:
        sys.exit(1)

    print("[HEADER]")
    print(f"  入口指令 (0x00): 0x{hdr['entry_word']:08X}  => {decode_arm_branch(hdr['entry_word'])}")
    print(f"  标题:     '{hdr['title']}'")
    print(f"  游戏代码: '{hdr['game_code']}'")
    print(f"  制造商:   '{hdr['maker_code']}'")
    print(f"  Fixed:    0x{hdr['fixed_val']:02X} (应为 0x96)")
    print(f"  Unit:     0x{hdr['unit_code']:02X}")
    print(f"  Device:   0x{hdr['device_type']:02X}")
    print(f"  版本:     0x{hdr['rom_version']:02X}")
    print(f"  头校验和: 0x{hdr['header_checksum']:02X} (计算值=0x{hdr['calc_checksum']:02X}) {'✓ PASS' if hdr['checksum_ok'] else '✗ FAIL'}")
    print()

    # --- Nintendo Logo ---
    nz, total = check_nintendo_logo(data)
    print(f"[LOGO] Nintendo Logo 区域 (0x04-0x9F): {nz}/{total} 字节非零 {'✓' if nz > 100 else '⚠ 可能无效'}")
    print()

    # --- 前 16 个 DWORD ---
    print("[DATA] 前 64 字节 (16 DWORD):")
    for i in range(16):
        off = i * 4
        word = struct.unpack_from("<I", data, off)[0]
        gba_addr = 0x08000000 + off
        annotation = ""
        if i == 0:
            annotation = f"  ; {decode_arm_branch(word)}"
        print(f"  [0x{gba_addr:08X}] 0x{word:08X}{annotation}")
    print()

    # --- 入口点附近的代码 ---
    entry = hdr["entry_word"]
    if (entry >> 24) & 0xFF == 0xEA:
        offset_field = entry & 0x00FFFFFF
        if offset_field & 0x800000:
            offset_field |= 0xFF000000
        target_offset = 8 + (offset_field << 2)  # 相对于 0x08000000
        target_rom_offset = target_offset & 0xFFFFFFFF
        print(f"[ENTRY] 入口分支目标 ROM 偏移: 0x{target_rom_offset:08X}")
        print(f"[ENTRY] 目标处的前 8 个指令:")
        for i in range(8):
            off = target_rom_offset + i * 4
            if off + 4 <= len(data):
                word = struct.unpack_from("<I", data, off)[0]
                gba_addr = 0x08000000 + off
                print(f"  [0x{gba_addr:08X}] 0x{word:08X}")
        print()

    # --- 导出仿真用 hex ---
    n_words = export_hex_for_sim(data, SIM_EXPORT_BYTES, HEX_OUT_PATH)
    print(f"[SIM] 已导出前 {SIM_EXPORT_BYTES} 字节 ({n_words} DWORD) 到: {HEX_OUT_PATH}")

    # --- 地址映射验证 ---
    print()
    print("[ADDR MAP 验证]")
    softmap = 0x30000  # Softmap_GBA_Gamerom_ADDR (DWORD)
    ddr_base = 0x10000000
    mister_base = 0x30000000
    print(f"  Softmap_GBA_Gamerom_ADDR = 0x{softmap:08X} (DWORD)")
    print(f"  G_DDR_BASE               = 0x{ddr_base:08X}")
    print(f"  C_MISTER_BASE            = 0x{mister_base:08X}")
    # cache: mem_read_addr = Softmap + read_addr (DWORD)
    # read_addr = mem_bus_Adr(24 downto 2) = GBA_offset / 4
    # 例: GBA 0x08000000 → offset=0 → mem_read_addr = 0x30000
    # sdram_read_addr (25bit) = mem_read_addr = 0x30000
    # ch1_addr[27:1] = {1'b0, sdram_read_addr, 1'b0} = {0, 0x30000, 0}
    # ch1_addr as 27-bit value = 0x060000
    # DDRAM_ADDR[28:0] = {4'b0011, ch1_addr[27:3]}
    #   ch1_addr[27:3] = 0x060000 >> 2 = 0x018000
    #   DDRAM_ADDR = {4'b0011, 25'h018000} = 0x18018000 ... wait that's wrong

    # Let me recalculate more carefully
    # ch1_addr is [27:1], so 27 bits
    # sdram_read_addr is 25 bits [24:0]
    # In zynq_gba_top.v: ch1_addr = {1'b0, sdram_read_addr[24:0], 1'b0}
    # So ch1_addr[27] = 0, ch1_addr[26:2] = sdram_read_addr[24:0], ch1_addr[1] = 0

    # For sdram_read_addr = 0x30000 (25 bits = 0b0_0011_0000_0000_0000_0000_0):
    # ch1_addr[27:1] as a number:
    #   bit 27 = 0
    #   bits 26:2 = 0x30000 = 196608
    #   bit 1 = 0
    # ch1_addr_val = 0 * 2^27 + 196608 * 2^2 + 0 = 196608 * 4 = 786432 = 0x0C0000
    # Wait no. ch1_addr is indexed [27:1]. Let me think in terms of bit positions.
    # ch1_addr[27] = 0
    # ch1_addr[26] = sdram_read_addr[24]
    # ch1_addr[25] = sdram_read_addr[23]
    # ...
    # ch1_addr[2] = sdram_read_addr[0]
    # ch1_addr[1] = 0

    # sdram_read_addr = 25'h30000 = 25'b0_0011_0000_0000_0000_0000_0
    # sdram_read_addr[24] = 0
    # sdram_read_addr[17] = 1
    # sdram_read_addr[16] = 1
    # All other bits = 0

    # ch1_addr[27:1]:
    # [27]=0, [26:2]=sdram_read_addr = ...[18]=1, [17]=1, ..., [1]=0
    # As a 27-bit number: bit 18 and 17 set = 0x60000

    # DDRAM_ADDR = {4'b0011, ram_address[27:3]}
    # ram_address = ch1_addr = bits [27:1] but stored as [27:1]
    # ram_address[27:3] = ch1_addr[27:3]
    # ch1_addr[27:3] = 25 bits
    # ch1_addr as 27 bits [27:1] = 0x060000
    # ch1_addr[27:3] = drop lowest 2 bits of the 27-bit value: 0x060000 >> 2 = 0x018000

    # DDRAM_ADDR[28:0] = {4'b0011, 0x018000} 
    # = (0x3 << 25) | 0x018000
    # = 0x6000000 | 0x018000
    # = 0x6018000

    # map_addr: full_addr = {DDRAM_ADDR, 3'b000} = 0x6018000 * 8 = 0x300C0000
    # 0x300C0000 >= 0x30000000 → yes
    # result = 0x10000000 + (0x300C0000 - 0x30000000) = 0x100C0000 ✓

    sdram_addr = softmap  # 0x30000
    ch1_val = sdram_addr << 1  # shift left 1 for bit[1]=0, and bit[27]=0
    ch1_27_3 = ch1_val >> 2
    ddram_addr = (0x3 << 25) | ch1_27_3
    full_addr = ddram_addr << 3
    if full_addr >= mister_base:
        axi_addr = ddr_base + (full_addr - mister_base)
    else:
        axi_addr = ddr_base + full_addr

    print(f"  GBA 0x08000000 -> sdram_read_addr = 0x{sdram_addr:08X}")
    print(f"  ch1_addr[27:1] value              = 0x{ch1_val:07X}")
    print(f"  ch1_addr[27:3]                    = 0x{ch1_27_3:07X}")
    print(f"  DDRAM_ADDR[28:0]                  = 0x{ddram_addr:08X}")
    print(f"  map_addr full_addr                = 0x{full_addr:08X}")
    print(f"  AXI 读地址                         = 0x{axi_addr:08X}")
    print(f"  PS ROM 基址                        = 0x100C0000")
    print(f"  地址匹配: {'✓ PASS' if axi_addr == 0x100C0000 else '✗ FAIL'}")

    print("\n=== 分析完成 ===")

if __name__ == "__main__":
    main()
