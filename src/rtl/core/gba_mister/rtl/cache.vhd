library IEEE;
use IEEE.std_logic_1164.all;
use IEEE.numeric_std.all;
use ieee.math_real.all;

library mem;

entity cache is
   generic
   (
      SIZE                     : integer;  -- number of sets (2-way associative)
      SIZEBASEBITS             : integer;  -- size of memory to be cached
      BITWIDTH                 : integer;
      Softmap_GBA_Gamerom_ADDR : integer
   );
   port
   (
      clk               : in  std_logic;
      gb_on             : in  std_logic;

      read_enable       : in  std_logic;
      read_addr         : in  std_logic_vector(SIZEBASEBITS-1 downto 0);
      read_data         : out std_logic_vector(BITWIDTH-1 downto 0) := (others => '0');
      read_done         : out std_logic := '0';
      read_full         : out std_logic_vector((BITWIDTH * 2)-1 downto 0) := (others => '0');

      mem_read_ena      : out   std_logic := '0';
      mem_read_done     : in    std_logic := '0';
      mem_read_addr     : out   std_logic_vector(24 downto 0) := (others => '0');
      mem_read_data     : in    std_logic_vector(31 downto 0);
      mem_read_data2    : in    std_logic_vector(31 downto 0)

   );
end entity;

architecture arch of cache is

   constant SIZEBITS     : integer := integer(ceil(log2(real(SIZE))));
   constant ADDRSAVEBITS : integer := SIZEBASEBITS - SIZEBITS;

   type tState is
   (
      IDLE,
      CLEARCACHE,
      READCACHE_OUT,
      READCACHE_WAITDONE,
      READCACHE_SECOND
   );
   attribute fsm_encoding : string;
   signal state : tstate := IDLE;
   attribute fsm_encoding of state : signal is "one_hot";

   signal up_low_select      : std_logic := '0';

   -- memory: way 0
   signal mem0_addr_a      : natural range 0 to SIZE - 1;
   signal mem0_addr_b      : natural range 0 to SIZE - 1;
   signal mem0_datain      : std_logic_vector((BITWIDTH*2)-1 downto 0);
   signal mem0_dataout     : std_logic_vector((BITWIDTH*2)-1 downto 0);
   signal mem0_we          : std_logic := '1';

   -- memory: way 1
   signal mem1_addr_a      : natural range 0 to SIZE - 1;
   signal mem1_addr_b      : natural range 0 to SIZE - 1;
   signal mem1_datain      : std_logic_vector((BITWIDTH*2)-1 downto 0);
   signal mem1_dataout     : std_logic_vector((BITWIDTH*2)-1 downto 0);
   signal mem1_we          : std_logic := '1';

   -- tag RAM: way 0
   signal tag0_addr_a      : natural range 0 to SIZE - 1;
   signal tag0_addr_b      : natural range 0 to SIZE - 1;
   signal tag0_datain      : std_logic_vector(ADDRSAVEBITS downto 0);
   signal tag0_dataout     : std_logic_vector(ADDRSAVEBITS downto 0);
   signal tag0_we          : std_logic := '1';

   -- tag RAM: way 1
   signal tag1_addr_a      : natural range 0 to SIZE - 1;
   signal tag1_addr_b      : natural range 0 to SIZE - 1;
   signal tag1_datain      : std_logic_vector(ADDRSAVEBITS downto 0);
   signal tag1_dataout     : std_logic_vector(ADDRSAVEBITS downto 0);
   signal tag1_we          : std_logic := '1';

   -- LRU: 0 = way0 LRU (fill way0), 1 = way1 LRU (fill way1)
   signal lru_addr_a       : natural range 0 to SIZE - 1;
   signal lru_addr_b       : natural range 0 to SIZE - 1;
   signal lru_datain       : std_logic_vector(0 downto 0);
   signal lru_dataout      : std_logic_vector(0 downto 0);
   signal lru_we           : std_logic := '1';

   signal upperbits          : std_logic_vector(SIZEBASEBITS - SIZEBITS - 1 downto 0);
   signal hit_way            : std_logic := '0';  -- 0 = way0 hit, 1 = way1 hit
   signal hit_detected       : std_logic := '0';
   signal fill_way           : std_logic := '0';  -- which way to fill on miss

   -- clear cache
   signal clear_counter      : natural range 0 to SIZE - 1;

   -- output buffers
   signal read_done_buffer   : std_logic := '0';

begin

   -- Way 0 data RAM (block for synthesis)
   iRamMem0: entity mem.SyncRamDual
   generic map
   (
      DATA_WIDTH       => BITWIDTH*2,
      ADDR_WIDTH       => SIZEBITS,
      MEMORY_PRIMITIVE => "block"
   )
   port map
   (
      clk        => clk,
      addr_a     => mem0_addr_a,
      datain_a   => (mem0_dataout'range => '0'),
      dataout_a  => mem0_dataout,
      we_a       => '0',
      re_a       => '1',
      addr_b     => mem0_addr_b,
      datain_b   => mem0_datain,
      dataout_b  => open,
      we_b       => mem0_we,
      re_b       => '0'
   );

   -- Way 1 data RAM
   iRamMem1: entity mem.SyncRamDual
   generic map
   (
      DATA_WIDTH       => BITWIDTH*2,
      ADDR_WIDTH       => SIZEBITS,
      MEMORY_PRIMITIVE => "block"
   )
   port map
   (
      clk        => clk,
      addr_a     => mem1_addr_a,
      datain_a   => (mem1_dataout'range => '0'),
      dataout_a  => mem1_dataout,
      we_a       => '0',
      re_a       => '1',
      addr_b     => mem1_addr_b,
      datain_b   => mem1_datain,
      dataout_b  => open,
      we_b       => mem1_we,
      re_b       => '0'
   );

   -- Way 0 tag RAM
   iRamTag0: entity mem.SyncRamDual
   generic map
   (
      DATA_WIDTH => ADDRSAVEBITS + 1,
      ADDR_WIDTH => SIZEBITS
   )
   port map
   (
      clk        => clk,
      addr_a     => tag0_addr_a,
      datain_a   => (tag0_dataout'range => '0'),
      dataout_a  => tag0_dataout,
      we_a       => '0',
      re_a       => '1',
      addr_b     => tag0_addr_b,
      datain_b   => tag0_datain,
      dataout_b  => open,
      we_b       => tag0_we,
      re_b       => '0'
   );

   -- Way 1 tag RAM
   iRamTag1: entity mem.SyncRamDual
   generic map
   (
      DATA_WIDTH => ADDRSAVEBITS + 1,
      ADDR_WIDTH => SIZEBITS
   )
   port map
   (
      clk        => clk,
      addr_a     => tag1_addr_a,
      datain_a   => (tag1_dataout'range => '0'),
      dataout_a  => tag1_dataout,
      we_a       => '0',
      re_a       => '1',
      addr_b     => tag1_addr_b,
      datain_b   => tag1_datain,
      dataout_b  => open,
      we_b       => tag1_we,
      re_b       => '0'
   );

   -- LRU: 1 bit per set. '0' = way0 LRU, '1' = way1 LRU.
   iRamLRU: entity mem.SyncRamDual
   generic map
   (
      DATA_WIDTH => 1,
      ADDR_WIDTH => SIZEBITS
   )
   port map
   (
      clk        => clk,
      addr_a     => lru_addr_a,
      datain_a   => "0",
      dataout_a  => lru_dataout,
      we_a       => '0',
      re_a       => '1',
      addr_b     => lru_addr_b,
      datain_b   => lru_datain,
      dataout_b  => open,
      we_b       => lru_we,
      re_b       => '0'
   );

   -- Read ports (all share the same address)
   mem0_addr_a    <= to_integer(unsigned(read_addr(SIZEBITS downto 1)));
   mem1_addr_a    <= to_integer(unsigned(read_addr(SIZEBITS downto 1)));
   tag0_addr_a    <= to_integer(unsigned(read_addr(SIZEBITS downto 1)));
   tag1_addr_a    <= to_integer(unsigned(read_addr(SIZEBITS downto 1)));
   lru_addr_a     <= to_integer(unsigned(read_addr(SIZEBITS downto 1)));

   -- Hit detection (combinatorial)
   hit_detected <= '1' when state = READCACHE_OUT and (
      (tag0_dataout = '0' & upperbits) or (tag1_dataout = '0' & upperbits)
   ) else '0';

   hit_way <= '1' when (tag1_dataout = '0' & upperbits) else '0';

   read_done_buffer <= hit_detected;

   -- Data output (combinatorial, depends on which way hit or was last filled)
   read_data  <= mem1_dataout(63 downto 32) when (hit_detected = '1' and hit_way = '1' and up_low_select = '1') else
                 mem1_dataout(31 downto 0)  when (hit_detected = '1' and hit_way = '1') else
                 mem0_dataout(63 downto 32) when (up_low_select = '1') else
                 mem0_dataout(31 downto 0);

   read_full  <= mem1_dataout when (hit_detected = '1' and hit_way = '1') else mem0_dataout;

   process (clk)
   begin
      if rising_edge(clk) then

         mem0_we          <= '0';
         mem1_we          <= '0';
         tag0_we          <= '0';
         tag1_we          <= '0';
         lru_we           <= '0';

         mem_read_ena     <= '0';

         if (gb_on = '0') then
            state         <= CLEARCACHE;
            clear_counter <= 0;
         else

            case(state) is

               when CLEARCACHE =>
                  if (clear_counter < SIZE - 1) then
                     clear_counter <= clear_counter + 1;
                  else
                     state          <= IDLE;
                  end if;
                  -- Invalidate both ways' tags
                  tag0_addr_b    <= clear_counter;
                  tag0_datain    <= (others => '1');
                  tag0_we        <= '1';
                  tag1_addr_b    <= clear_counter;
                  tag1_datain    <= (others => '1');
                  tag1_we        <= '1';

               when IDLE =>
                  if (read_enable = '1') then
                     mem_read_addr   <= std_logic_vector(to_unsigned(Softmap_GBA_Gamerom_ADDR, 25) + unsigned(read_addr));
                     upperbits       <= read_addr(SIZEBASEBITS-1 downto SIZEBITS);
                     up_low_select   <= read_addr(0);
                     -- Record fill target: LRU way (0 = way0 LRU, fill way0)
                     fill_way        <= lru_dataout(0);
                     state           <= READCACHE_OUT;
                  end if;

               when READCACHE_OUT =>
                  if (hit_detected = '1') then
                     -- Update LRU: the OTHER way becomes LRU
                     lru_addr_b    <= to_integer(unsigned(read_addr(SIZEBITS downto 1)));
                     lru_datain(0) <= not hit_way;
                     lru_we        <= '1';
                     state         <= IDLE;
                  else
                     -- Miss: initiate DDR read
                     state             <= READCACHE_WAITDONE;
                     mem_read_ena      <= '1';
                     -- Write port targets pre-latched in IDLE
                     mem0_addr_b       <= to_integer(unsigned(read_addr(SIZEBITS downto 1)));
                     mem1_addr_b       <= to_integer(unsigned(read_addr(SIZEBITS downto 1)));
                     tag0_addr_b       <= to_integer(unsigned(read_addr(SIZEBITS downto 1)));
                     tag1_addr_b       <= to_integer(unsigned(read_addr(SIZEBITS downto 1)));
                     lru_addr_b        <= to_integer(unsigned(read_addr(SIZEBITS downto 1)));
                  end if;

               when READCACHE_WAITDONE =>
                  if (mem_read_done = '1') then
                     state <= READCACHE_SECOND;
                     -- Capture first dword into the fill way
                     if (up_low_select = '0') then
                        if (fill_way = '0') then
                           mem0_datain(31 downto 0) <= mem_read_data;
                        else
                           mem1_datain(31 downto 0) <= mem_read_data;
                        end if;
                     else
                        if (fill_way = '0') then
                           mem0_datain(63 downto 32) <= mem_read_data;
                        else
                           mem1_datain(63 downto 32) <= mem_read_data;
                        end if;
                     end if;
                  end if;

               when READCACHE_SECOND =>
                  state       <= IDLE;
                  -- Capture second dword (valid 1 cycle after done)
                  if (up_low_select = '1') then
                     if (fill_way = '0') then
                        mem0_datain(31 downto 0)  <= mem_read_data2;
                     else
                        mem1_datain(31 downto 0)  <= mem_read_data2;
                     end if;
                  else
                     if (fill_way = '0') then
                        mem0_datain(63 downto 32) <= mem_read_data2;
                     else
                        mem1_datain(63 downto 32) <= mem_read_data2;
                     end if;
                  end if;
                  -- Write to the fill way
                  if (fill_way = '0') then
                     mem0_we   <= '1';
                     tag0_datain <= '0' & upperbits;
                     tag0_we   <= '1';
                  else
                     mem1_we   <= '1';
                     tag1_datain <= '0' & upperbits;
                     tag1_we   <= '1';
                  end if;
                  -- LRU: the OTHER way becomes LRU
                  lru_datain(0) <= not fill_way;
                  lru_we        <= '1';

            end case;

         end if;

      end if;
   end process;

   read_done  <= read_done_buffer;

end architecture;
