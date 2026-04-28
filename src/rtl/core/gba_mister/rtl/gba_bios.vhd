library IEEE;
use IEEE.std_logic_1164.all;
use IEEE.numeric_std.all;
library xpm;
use xpm.vcomponents.all;

entity gba_bios is
   port
   (
      clk       : in std_logic;
      address   : in std_logic_vector(11 downto 0);
      data      : out std_logic_vector(31 downto 0);

      wraddress : in std_logic_vector(11 downto 0);
      wrdata    : in std_logic_vector(31 downto 0);
      wren      : in std_logic
   );
end entity;

architecture arch of gba_bios is

   signal wea_a : std_logic_vector(0 downto 0);
   signal web_b : std_logic_vector(0 downto 0);

begin

   wea_a(0) <= wren;
   web_b(0) <= '0';

   i_bios_bram : xpm_memory_tdpram
   generic map (
      ADDR_WIDTH_A            => 12,
      ADDR_WIDTH_B            => 12,
      AUTO_SLEEP_TIME         => 0,
      BYTE_WRITE_WIDTH_A      => 32,
      BYTE_WRITE_WIDTH_B      => 32,
      CASCADE_HEIGHT          => 0,
      CLOCKING_MODE           => "common_clock",
      ECC_MODE                => "no_ecc",
      MEMORY_INIT_FILE        => "gba_bios.mem",
      MEMORY_INIT_PARAM       => "",
      MEMORY_OPTIMIZATION     => "true",
      MEMORY_PRIMITIVE        => "block",
      MEMORY_SIZE             => 32 * 4096,
      MESSAGE_CONTROL         => 0,
      READ_DATA_WIDTH_A       => 32,
      READ_DATA_WIDTH_B       => 32,
      READ_LATENCY_A          => 1,
      READ_LATENCY_B          => 1,
      READ_RESET_VALUE_A      => "0",
      READ_RESET_VALUE_B      => "0",
      RST_MODE_A              => "SYNC",
      RST_MODE_B              => "SYNC",
      SIM_ASSERT_CHK          => 0,
      USE_EMBEDDED_CONSTRAINT => 0,
      USE_MEM_INIT            => 1,
      WAKEUP_TIME             => "disable_sleep",
      WRITE_DATA_WIDTH_A      => 32,
      WRITE_DATA_WIDTH_B      => 32,
      WRITE_MODE_A            => "read_first",
      WRITE_MODE_B            => "read_first"
   )
   port map (
      addra          => wraddress,
      addrb          => address,
      clka           => clk,
      clkb           => clk,
      dina           => wrdata,
      dinb           => (others => '0'),
      douta          => open,
      doutb          => data,
      ena            => '1',
      enb            => '1',
      injectdbiterra => '0',
      injectdbiterrb => '0',
      injectsbiterra => '0',
      injectsbiterrb => '0',
      regcea         => '1',
      regceb         => '1',
      rsta           => '0',
      rstb           => '0',
      sleep          => '0',
      wea            => wea_a,
      web            => web_b,
      dbiterra       => open,
      dbiterrb       => open,
      sbiterra       => open,
      sbiterrb       => open
   );

end architecture;
