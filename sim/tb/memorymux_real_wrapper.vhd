-- memorymux_real_wrapper.vhd
-- 封装真实 gba_memorymux，只暴露 ROM 读路径相关端口
-- 其余端口内部 tie-off，用于仿真验证

library IEEE;
use IEEE.std_logic_1164.all;
use IEEE.numeric_std.all;

library MEM;

use work.pProc_bus_gba.all;
use work.pReg_savestates.all;

entity memorymux_real_wrapper is
    generic (
        Softmap_GBA_Gamerom_ADDR : integer := 196608
    );
    port (
        clk100             : in  std_logic;
        gb_on              : in  std_logic;

        -- CPU 总线 (ROM 读)
        mem_bus_Adr        : in  std_logic_vector(31 downto 0);
        mem_bus_rnw        : in  std_logic;
        mem_bus_ena        : in  std_logic;
        mem_bus_acc        : in  std_logic_vector(1 downto 0);
        mem_bus_dout       : in  std_logic_vector(31 downto 0);
        mem_bus_din        : out std_logic_vector(31 downto 0);
        mem_bus_done       : out std_logic;

        -- DDR 接口 (cache → DDR)
        sdram_read_ena     : out std_logic;
        sdram_read_done    : in  std_logic;
        sdram_read_addr    : out std_logic_vector(24 downto 0);
        sdram_read_data    : in  std_logic_vector(31 downto 0);
        sdram_second_dword : in  std_logic_vector(31 downto 0);

        -- bus_out 接口 (WRAM ch2)
        bus_out_Din        : out std_logic_vector(31 downto 0);
        bus_out_Dout       : in  std_logic_vector(31 downto 0);
        bus_out_Adr        : out std_logic_vector(25 downto 0);
        bus_out_rnw        : out std_logic;
        bus_out_ena        : out std_logic;
        bus_out_done       : in  std_logic;

        -- 配置
        MaxPakAddr         : in  std_logic_vector(24 downto 0);
        memory_remap       : in  std_logic;
        SramFlashEnable    : in  std_logic
    );
end entity;

architecture sim of memorymux_real_wrapper is
    -- 内部 tie-off 信号
    signal savestate_bus_i : proc_bus_gb_type := (
        (others => 'Z'), (others => 'Z'), (others => 'Z'),
        'Z', 'Z', 'Z', "ZZ", "ZZZZ", 'Z'
    );
    signal gb_bus_out_i    : proc_bus_gb_type := (
        (others => 'Z'), (others => 'Z'), (others => 'Z'),
        'Z', 'Z', 'Z', "ZZ", "ZZZZ", 'Z'
    );

    -- bus_out 现在连接到外部端口
    signal settle_i        : std_logic;

    -- VRAM/OAM/Palette 存根
    signal vram_lo_do      : std_logic_vector(31 downto 0) := (others => '0');
    signal vram_hi_do      : std_logic_vector(31 downto 0) := (others => '0');
    signal oam_do          : std_logic_vector(31 downto 0) := (others => '0');
    signal pal_bg_do       : std_logic_vector(31 downto 0) := (others => '0');
    signal pal_oam_do      : std_logic_vector(31 downto 0) := (others => '0');

    -- 未使用输出
    signal bus_out_din_i   : std_logic_vector(31 downto 0);
    signal bus_out_adr_i   : std_logic_vector(25 downto 0);
    signal bus_out_rnw_i   : std_logic;
    signal bus_out_ena_i   : std_logic;
    signal mem_bus_unread_i: std_logic;
    signal save_eeprom_i   : std_logic;
    signal save_sram_i     : std_logic;
    signal save_flash_i    : std_logic;
    signal vram_lo_addr_i  : integer range 0 to 16383;
    signal vram_lo_din_i   : std_logic_vector(31 downto 0);
    signal vram_lo_we_i    : std_logic;
    signal vram_lo_be_i    : std_logic_vector(3 downto 0);
    signal vram_hi_addr_i  : integer range 0 to 8191;
    signal vram_hi_din_i   : std_logic_vector(31 downto 0);
    signal vram_hi_we_i    : std_logic;
    signal vram_hi_be_i    : std_logic_vector(3 downto 0);
    signal oam_addr_i      : integer range 0 to 255;
    signal oam_din_i       : std_logic_vector(31 downto 0);
    signal oam_we_i        : std_logic_vector(3 downto 0);
    signal pal_bg_addr_i   : integer range 0 to 128;
    signal pal_bg_din_i    : std_logic_vector(31 downto 0);
    signal pal_bg_we_i     : std_logic_vector(3 downto 0);
    signal pal_oam_addr_i  : integer range 0 to 128;
    signal pal_oam_din_i   : std_logic_vector(31 downto 0);
    signal pal_oam_we_i    : std_logic_vector(3 downto 0);
    signal gpio_re_i       : std_logic;
    signal gpio_dout_i     : std_logic_vector(3 downto 0);
    signal gpio_we_i       : std_logic;
    signal gpio_addr_i     : std_logic_vector(1 downto 0);
    signal vram_cycle_i    : std_logic;
    signal debug_mem_i     : std_logic_vector(31 downto 0);

begin

    u_mux : entity work.gba_memorymux
    generic map (
        is_simu                  => '1',
        Softmap_GBA_Gamerom_ADDR => Softmap_GBA_Gamerom_ADDR,
        Softmap_GBA_WRam_ADDR    => 131072,
        Softmap_GBA_FLASH_ADDR   => 0,
        Softmap_GBA_EEPROM_ADDR  => 0
    )
    port map (
        clk100              => clk100,
        gb_on               => gb_on,
        reset               => '0',

        savestate_bus       => savestate_bus_i,

        sdram_read_ena      => sdram_read_ena,
        sdram_read_done     => sdram_read_done,
        sdram_read_addr     => sdram_read_addr,
        sdram_read_data     => sdram_read_data,
        sdram_second_dword  => sdram_second_dword,

        bus_out_Din         => bus_out_Din,
        bus_out_Dout        => bus_out_Dout,
        bus_out_Adr         => bus_out_Adr,
        bus_out_rnw         => bus_out_rnw,
        bus_out_ena         => bus_out_ena,
        bus_out_done        => bus_out_done,

        gb_bus_out          => gb_bus_out_i,

        mem_bus_Adr         => mem_bus_Adr,
        mem_bus_rnw         => mem_bus_rnw,
        mem_bus_ena         => mem_bus_ena,
        mem_bus_acc         => mem_bus_acc,
        mem_bus_dout        => mem_bus_dout,
        mem_bus_din         => mem_bus_din,
        mem_bus_done        => mem_bus_done,
        mem_bus_unread      => mem_bus_unread_i,

        bios_wraddr         => (others => '0'),
        bios_wrdata         => (others => '0'),
        bios_wr             => '0',

        bus_lowbits         => "00",

        dma_soon            => '0',
        settle              => settle_i,

        save_eeprom         => save_eeprom_i,
        save_sram           => save_sram_i,
        save_flash          => save_flash_i,

        new_cycles          => to_unsigned(1, 8),
        new_cycles_valid    => '0',

        PC_in_BIOS          => '0',
        lastread            => (others => '0'),
        lastread_dma        => (others => '0'),
        last_access_dma     => '0',

        dma_eepromcount     => (others => '0'),
        flash_1m            => '0',
        MaxPakAddr          => MaxPakAddr,
        SramFlashEnable     => SramFlashEnable,
        memory_remap        => memory_remap,

        bitmapdrawmode      => '0',

        VRAM_Lo_addr        => vram_lo_addr_i,
        VRAM_Lo_datain      => vram_lo_din_i,
        VRAM_Lo_dataout     => vram_lo_do,
        VRAM_Lo_we          => vram_lo_we_i,
        VRAM_Lo_be          => vram_lo_be_i,
        VRAM_Hi_addr        => vram_hi_addr_i,
        VRAM_Hi_datain      => vram_hi_din_i,
        VRAM_Hi_dataout     => vram_hi_do,
        VRAM_Hi_we          => vram_hi_we_i,
        VRAM_Hi_be          => vram_hi_be_i,
        vram_blocked        => '0',
        vram_cycle          => vram_cycle_i,

        OAMRAM_PROC_addr    => oam_addr_i,
        OAMRAM_PROC_datain  => oam_din_i,
        OAMRAM_PROC_dataout => oam_do,
        OAMRAM_PROC_we      => oam_we_i,

        PALETTE_BG_addr     => pal_bg_addr_i,
        PALETTE_BG_datain   => pal_bg_din_i,
        PALETTE_BG_dataout  => pal_bg_do,
        PALETTE_BG_we       => pal_bg_we_i,
        PALETTE_OAM_addr    => pal_oam_addr_i,
        PALETTE_OAM_datain  => pal_oam_din_i,
        PALETTE_OAM_dataout => pal_oam_do,
        PALETTE_OAM_we      => pal_oam_we_i,

        specialmodule       => '0',
        GPIO_readEna        => gpio_re_i,
        GPIO_done           => '0',
        GPIO_Din            => "0000",
        GPIO_Dout           => gpio_dout_i,
        GPIO_writeEna       => gpio_we_i,
        GPIO_addr           => gpio_addr_i,

        tilt                => '0',
        AnalogTiltX         => (others => '0'),
        AnalogTiltY         => (others => '0'),

        debug_mem           => debug_mem_i
    );

end architecture;
