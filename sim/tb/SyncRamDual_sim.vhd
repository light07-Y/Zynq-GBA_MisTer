-- SyncRamDual_sim.vhd: 仿真专用行为级双端口 RAM
-- 替代 Xilinx XPM 版本，无需 XPM 库依赖
-- READ_LATENCY = 1 (与 XPM 版一致)

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library mem;

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

architecture sim of SyncRamDual is
   -- 使用 shared variable 避免多进程驱动同一信号导致的 std_logic 分辨冲突
   type ram_type is array (0 to 2**ADDR_WIDTH - 1) of std_logic_vector(DATA_WIDTH-1 downto 0);
   shared variable ram : ram_type := (others => (others => '0'));
begin
   -- 端口 A（独立进程，通过 shared variable 访问 RAM）
   process(clk)
   begin
      if rising_edge(clk) then
         if we_a = '1' then
            ram(addr_a) := datain_a;
         end if;
         if re_a = '1' then
            if we_a = '1' then
               dataout_a <= datain_a;  -- write_first 模式
            else
               dataout_a <= ram(addr_a);
            end if;
         end if;
      end if;
   end process;

   -- 端口 B（独立进程，通过 shared variable 访问 RAM）
   process(clk)
   begin
      if rising_edge(clk) then
         if we_b = '1' then
            ram(addr_b) := datain_b;
         end if;
         if re_b = '1' then
            if we_b = '1' then
               dataout_b <= datain_b;  -- write_first 模式
            else
               dataout_b <= ram(addr_b);
            end if;
         end if;
      end if;
   end process;

end sim;
