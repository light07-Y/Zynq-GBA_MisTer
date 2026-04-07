library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity SyncRamDualByteEnable is
   generic 
   (
      is_simu     : std_logic;
      is_cyclone5 : std_logic := '0';
      BYTE_WIDTH  : natural := 8;
      ADDR_WIDTH  : natural := 6;
      BYTES       : natural := 4
   );
   port 
   (
      clk        : in std_logic;
      
      addr_a     : in natural range 0 to 2**ADDR_WIDTH - 1;
      datain_a0  : in std_logic_vector((BYTE_WIDTH-1) downto 0);
      datain_a1  : in std_logic_vector((BYTE_WIDTH-1) downto 0);
      datain_a2  : in std_logic_vector((BYTE_WIDTH-1) downto 0);
      datain_a3  : in std_logic_vector((BYTE_WIDTH-1) downto 0);
      dataout_a  : out std_logic_vector((BYTES*BYTE_WIDTH-1) downto 0);
      we_a       : in std_logic := '1';
      be_a       : in  std_logic_vector (BYTES - 1 downto 0);
		            
      addr_b     : in natural range 0 to 2**ADDR_WIDTH - 1;
      datain_b0  : in std_logic_vector((BYTE_WIDTH-1) downto 0);
      datain_b1  : in std_logic_vector((BYTE_WIDTH-1) downto 0);
      datain_b2  : in std_logic_vector((BYTE_WIDTH-1) downto 0);
      datain_b3  : in std_logic_vector((BYTE_WIDTH-1) downto 0);
      dataout_b  : out std_logic_vector((BYTES*BYTE_WIDTH-1) downto 0);
      we_b       : in std_logic := '1';
      be_b       : in  std_logic_vector (BYTES - 1 downto 0)
   );
end;

architecture rtl of SyncRamDualByteEnable is
   constant DATA_WIDTH : natural := BYTES * BYTE_WIDTH;

   signal addra_slv : std_logic_vector(ADDR_WIDTH - 1 downto 0);
   signal addrb_slv : std_logic_vector(ADDR_WIDTH - 1 downto 0);
   signal dina_slv  : std_logic_vector(DATA_WIDTH - 1 downto 0);
   signal dinb_slv  : std_logic_vector(DATA_WIDTH - 1 downto 0);
   signal douta_slv : std_logic_vector(DATA_WIDTH - 1 downto 0);
   signal doutb_slv : std_logic_vector(DATA_WIDTH - 1 downto 0);

   component SyncRamDualByteEnable_core is
      generic
      (
         BYTE_WIDTH : integer := 8;
         ADDR_WIDTH : integer := 6;
         BYTES      : integer := 4
      );
      port
      (
         clk       : in  std_logic;
         addr_a    : in  std_logic_vector(ADDR_WIDTH - 1 downto 0);
         datain_a  : in  std_logic_vector(BYTES * BYTE_WIDTH - 1 downto 0);
         dataout_a : out std_logic_vector(BYTES * BYTE_WIDTH - 1 downto 0);
         we_a      : in  std_logic;
         be_a      : in  std_logic_vector(BYTES - 1 downto 0);
         addr_b    : in  std_logic_vector(ADDR_WIDTH - 1 downto 0);
         datain_b  : in  std_logic_vector(BYTES * BYTE_WIDTH - 1 downto 0);
         dataout_b : out std_logic_vector(BYTES * BYTE_WIDTH - 1 downto 0);
         we_b      : in  std_logic;
         be_b      : in  std_logic_vector(BYTES - 1 downto 0)
      );
   end component;
begin

   -- Existing interface exposes four byte lanes, so BYTES must remain 4.
   assert BYTES = 4
      report "SyncRamDualByteEnable supports BYTES=4 only"
      severity failure;

   addra_slv <= std_logic_vector(to_unsigned(addr_a, ADDR_WIDTH));
   addrb_slv <= std_logic_vector(to_unsigned(addr_b, ADDR_WIDTH));

   dina_slv(BYTE_WIDTH * 1 - 1 downto BYTE_WIDTH * 0) <= datain_a0;
   dina_slv(BYTE_WIDTH * 2 - 1 downto BYTE_WIDTH * 1) <= datain_a1;
   dina_slv(BYTE_WIDTH * 3 - 1 downto BYTE_WIDTH * 2) <= datain_a2;
   dina_slv(BYTE_WIDTH * 4 - 1 downto BYTE_WIDTH * 3) <= datain_a3;

   dinb_slv(BYTE_WIDTH * 1 - 1 downto BYTE_WIDTH * 0) <= datain_b0;
   dinb_slv(BYTE_WIDTH * 2 - 1 downto BYTE_WIDTH * 1) <= datain_b1;
   dinb_slv(BYTE_WIDTH * 3 - 1 downto BYTE_WIDTH * 2) <= datain_b2;
   dinb_slv(BYTE_WIDTH * 4 - 1 downto BYTE_WIDTH * 3) <= datain_b3;

   dataout_a <= douta_slv;
   dataout_b <= doutb_slv;

   i_core : SyncRamDualByteEnable_core
   generic map
   (
      BYTE_WIDTH => BYTE_WIDTH,
      ADDR_WIDTH => ADDR_WIDTH,
      BYTES      => BYTES
   )
   port map
   (
      clk       => clk,
      addr_a    => addra_slv,
      datain_a  => dina_slv,
      dataout_a => douta_slv,
      we_a      => we_a,
      be_a      => be_a,
      addr_b    => addrb_slv,
      datain_b  => dinb_slv,
      dataout_b => doutb_slv,
      we_b      => we_b,
      be_b      => be_b
   );

end rtl;
