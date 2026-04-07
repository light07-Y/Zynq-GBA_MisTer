library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
library xpm;
use xpm.vcomponents.all;

entity SyncRamDual is
   generic 
   (
      DATA_WIDTH : natural := 8;
      ADDR_WIDTH : natural := 6
   );
   port 
   (
      clk        : in std_logic;
      
      addr_a     : in natural range 0 to 2**ADDR_WIDTH - 1;
      datain_a   : in std_logic_vector((DATA_WIDTH-1) downto 0);
      dataout_a  : out std_logic_vector((DATA_WIDTH -1) downto 0);
      we_a       : in std_logic := '1';
      re_a       : in std_logic := '1';
                 
      addr_b     : in natural range 0 to 2**ADDR_WIDTH - 1;
      datain_b   : in std_logic_vector((DATA_WIDTH-1) downto 0);
      dataout_b  : out std_logic_vector((DATA_WIDTH -1) downto 0);
      we_b       : in std_logic := '1';
      re_b       : in std_logic := '1'
   );
end;

architecture rtl of SyncRamDual is
   signal addra_slv : std_logic_vector(ADDR_WIDTH-1 downto 0);
   signal addrb_slv : std_logic_vector(ADDR_WIDTH-1 downto 0);
   signal ena       : std_logic;
   signal enb       : std_logic;
   signal wea       : std_logic_vector(0 downto 0);
   signal web       : std_logic_vector(0 downto 0);

begin

   addra_slv <= std_logic_vector(to_unsigned(addr_a, ADDR_WIDTH));
   addrb_slv <= std_logic_vector(to_unsigned(addr_b, ADDR_WIDTH));
   ena       <= we_a or re_a;
   enb       <= we_b or re_b;
   wea(0)    <= we_a;
   web(0)    <= we_b;

   -- Use Xilinx XPM explicitly so Vivado does not dissolve large memories into registers.
   i_tdpram : xpm_memory_tdpram
   generic map (
      ADDR_WIDTH_A           => ADDR_WIDTH,
      ADDR_WIDTH_B           => ADDR_WIDTH,
      AUTO_SLEEP_TIME        => 0,
      BYTE_WRITE_WIDTH_A     => DATA_WIDTH,
      BYTE_WRITE_WIDTH_B     => DATA_WIDTH,
      CASCADE_HEIGHT         => 0,
      CLOCKING_MODE          => "common_clock",
      ECC_MODE               => "no_ecc",
      MEMORY_INIT_FILE       => "none",
      MEMORY_INIT_PARAM      => "",
      MEMORY_OPTIMIZATION    => "true",
      MEMORY_PRIMITIVE       => "block",
      MEMORY_SIZE            => DATA_WIDTH * (2 ** ADDR_WIDTH),
      MESSAGE_CONTROL        => 0,
      READ_DATA_WIDTH_A      => DATA_WIDTH,
      READ_DATA_WIDTH_B      => DATA_WIDTH,
      READ_LATENCY_A         => 1,
      READ_LATENCY_B         => 1,
      READ_RESET_VALUE_A     => "0",
      READ_RESET_VALUE_B     => "0",
      RST_MODE_A             => "SYNC",
      RST_MODE_B             => "SYNC",
      SIM_ASSERT_CHK         => 0,
      USE_EMBEDDED_CONSTRAINT=> 0,
      USE_MEM_INIT           => 1,
      WAKEUP_TIME            => "disable_sleep",
      WRITE_DATA_WIDTH_A     => DATA_WIDTH,
      WRITE_DATA_WIDTH_B     => DATA_WIDTH,
      WRITE_MODE_A           => "write_first",
      WRITE_MODE_B           => "write_first"
   )
   port map (
      addra          => addra_slv,
      addrb          => addrb_slv,
      clka           => clk,
      clkb           => clk,
      dina           => datain_a,
      dinb           => datain_b,
      douta          => dataout_a,
      doutb          => dataout_b,
      ena            => ena,
      enb            => enb,
      injectdbiterra => '0',
      injectdbiterrb => '0',
      injectsbiterra => '0',
      injectsbiterrb => '0',
      regcea         => re_a,
      regceb         => re_b,
      rsta           => '0',
      rstb           => '0',
      sleep          => '0',
      wea            => wea,
      web            => web,
      dbiterra       => open,
      dbiterrb       => open,
      sbiterra       => open,
      sbiterrb       => open
   );

end rtl;
