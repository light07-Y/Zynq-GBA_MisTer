module zynq_gba_top #(
  parameter [31:0] G_DDR_BASE = 32'h1000_0000,
  parameter         G_ENABLE_DEBUG = 1'b0
) (
  // Clock and Reset Interfaces
  (* X_INTERFACE_INFO = "xilinx.com:signal:clock:1.0 clk_100 CLK" *)
  (* X_INTERFACE_PARAMETER = "ASSOCIATED_BUSIF M_AXI, ASSOCIATED_RESET rst_n, FREQ_HZ 100000000" *)
  input         clk_100,
  (* X_INTERFACE_INFO = "xilinx.com:signal:reset:1.0 rst_n RST" *)
  (* X_INTERFACE_PARAMETER = "POLARITY ACTIVE_LOW" *)
  input         rst_n,

  // AXI4-Lite slave (PS -> PL control)
  (* X_INTERFACE_INFO = "xilinx.com:signal:clock:1.0 s_axi_aclk CLK" *)
  (* X_INTERFACE_PARAMETER = "ASSOCIATED_BUSIF s_axi, ASSOCIATED_RESET s_axi_aresetn, FREQ_HZ 50000000" *)
  input         s_axi_aclk,
  (* X_INTERFACE_INFO = "xilinx.com:signal:reset:1.0 s_axi_aresetn RST" *)
  (* X_INTERFACE_PARAMETER = "POLARITY ACTIVE_LOW" *)
  input         s_axi_aresetn,

  (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 s_axi AWADDR" *)
  input [11:0]  s_axi_awaddr,
  (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 s_axi AWPROT" *)
  input [2:0]   s_axi_awprot,
  (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 s_axi AWVALID" *)
  input         s_axi_awvalid,
  (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 s_axi AWREADY" *)
  output        s_axi_awready,
  (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 s_axi WDATA" *)
  input [31:0]  s_axi_wdata,
  (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 s_axi WSTRB" *)
  input [3:0]   s_axi_wstrb,
  (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 s_axi WVALID" *)
  input         s_axi_wvalid,
  (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 s_axi WREADY" *)
  output        s_axi_wready,
  (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 s_axi BRESP" *)
  output [1:0]  s_axi_bresp,
  (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 s_axi BVALID" *)
  output        s_axi_bvalid,
  (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 s_axi BREADY" *)
  input         s_axi_bready,
  (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 s_axi ARADDR" *)
  input [11:0]  s_axi_araddr,
  (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 s_axi ARPROT" *)
  input [2:0]   s_axi_arprot,
  (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 s_axi ARVALID" *)
  input         s_axi_arvalid,
  (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 s_axi ARREADY" *)
  output        s_axi_arready,
  (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 s_axi RDATA" *)
  output [31:0] s_axi_rdata,
  (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 s_axi RRESP" *)
  output [1:0]  s_axi_rresp,
  (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 s_axi RVALID" *)
  output        s_axi_rvalid,
  (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 s_axi RREADY" *)
  input         s_axi_rready,

  (* X_INTERFACE_INFO = "xilinx.com:interface:bram:1.0 fb_cap_bram CLK" *)
  (* X_INTERFACE_PARAMETER = "XIL_INTERFACENAME fb_cap_bram, MASTER_TYPE BRAM_CTRL, MEM_SIZE 262144, MEM_WIDTH 32, MEM_ECC NONE, READ_WRITE_MODE READ_WRITE" *)
  input         fb_cap_bram_clk,
  (* X_INTERFACE_INFO = "xilinx.com:interface:bram:1.0 fb_cap_bram RST" *)
  input         fb_cap_bram_rst,
  (* X_INTERFACE_INFO = "xilinx.com:interface:bram:1.0 fb_cap_bram EN" *)
  input         fb_cap_bram_en,
  (* X_INTERFACE_INFO = "xilinx.com:interface:bram:1.0 fb_cap_bram WE" *)
  input [3:0]   fb_cap_bram_we,
  (* X_INTERFACE_INFO = "xilinx.com:interface:bram:1.0 fb_cap_bram ADDR" *)
  input [17:0]  fb_cap_bram_addr,
  (* X_INTERFACE_INFO = "xilinx.com:interface:bram:1.0 fb_cap_bram DIN" *)
  input [31:0]  fb_cap_bram_din,
  (* X_INTERFACE_INFO = "xilinx.com:interface:bram:1.0 fb_cap_bram DOUT" *)
  output [31:0] fb_cap_bram_dout,

  // AXI4 master to PS DDR
  (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI AWADDR" *)
  output [31:0]  M_AXI_AWADDR,
  (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI AWLEN" *)
  output [7:0]   M_AXI_AWLEN,
  (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI AWSIZE" *)
  output [2:0]   M_AXI_AWSIZE,
  (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI AWBURST" *)
  output [1:0]   M_AXI_AWBURST,
  (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI AWVALID" *)
  output         M_AXI_AWVALID,
  (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI AWREADY" *)
  input          M_AXI_AWREADY,

  (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI WDATA" *)
  output [63:0]  M_AXI_WDATA,
  (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI WSTRB" *)
  output [7:0]   M_AXI_WSTRB,
  (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI WLAST" *)
  output         M_AXI_WLAST,
  (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI WVALID" *)
  output         M_AXI_WVALID,
  (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI WREADY" *)
  input          M_AXI_WREADY,

  (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI BRESP" *)
  input [1:0]    M_AXI_BRESP,
  (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI BVALID" *)
  input          M_AXI_BVALID,
  (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI BREADY" *)
  output         M_AXI_BREADY,

  (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI ARADDR" *)
  output [31:0]  M_AXI_ARADDR,
  (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI ARLEN" *)
  output [7:0]   M_AXI_ARLEN,
  (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI ARSIZE" *)
  output [2:0]   M_AXI_ARSIZE,
  (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI ARBURST" *)
  output [1:0]   M_AXI_ARBURST,
  (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI ARVALID" *)
  output         M_AXI_ARVALID,
  (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI ARREADY" *)
  input          M_AXI_ARREADY,

  (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI RDATA" *)
  input [63:0]   M_AXI_RDATA,
  (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI RRESP" *)
  input [1:0]    M_AXI_RRESP,
  (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI RLAST" *)
  input          M_AXI_RLAST,
  (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI RVALID" *)
  input          M_AXI_RVALID,
  (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 M_AXI RREADY" *)
  output         M_AXI_RREADY,

  // Audio Codec Interface (I2S)
  output        ac_bclk,
  output        ac_mclk,
  output        ac_muten,
  output        ac_pbdat,
  output        ac_pblrc,

  // Board Physical I/O
  input  [3:0]  btns,
  input  [3:0]  sws,
  output [3:0]  leds,

  // PL-to-PS Interrupt
  (* X_INTERFACE_INFO = "xilinx.com:signal:interrupt:1.0 irq INTERRUPT" *)
  (* X_INTERFACE_PARAMETER = "SENSITIVITY LEVEL_HIGH" *)
  output        irq
);

  // ==========================================================================
  // --- 内部布线与中间信号 (Interconnects) ---
  // ==========================================================================
  
  // 1. 系统配置信号 (Synchronized)
  wire [31:0] w_core_cfg_ctrl;
  wire [9:0]  w_core_cfg_keys;
  wire [24:0] w_core_cfg_max_pak_addr;
  wire [15:0] w_core_cfg_cycle_precalc;
  wire [31:0] w_core_cfg_rtc_timestamp;

  // 2. AXI 寄存器到系统控制器的接口
  wire [31:0] w_axi_cfg_ctrl;
  wire [9:0]  w_axi_cfg_keys;
  wire [24:0] w_axi_cfg_max_pak_addr;
  wire [15:0] w_axi_cfg_cycle_precalc;
  wire [31:0] w_axi_cfg_rtc_timestamp;
  wire [1:0]  w_axi_display_frame_idx;
  wire        w_axi_cfg_commit_toggle;
  wire        w_axi_cfg_sw_reset;
  wire [11:0] w_axi_bios_wr_addr;
  wire [31:0] w_axi_bios_wr_data;
  wire        w_axi_bios_wr_req_toggle;

  // 3. 内存系统信号 (DDRAM Mux & Backend)
  wire        ddram_busy;
  wire [7:0]  ddram_burstcnt;
  wire [28:0] ddram_addr;
  wire [63:0] ddram_dout;
  wire        ddram_dout_ready;
  wire        ddram_rd;
  wire [63:0] ddram_din;
  wire [7:0]  ddram_be;
  wire        ddram_we;

  wire [27:1] ch1_addr, ch2_addr, ch4_addr;
  wire [24:1] ch3_addr;
  wire [63:0] ch1_dout, ch1_din, ch2_din, ch4_dout, ch4_din;
  wire [31:0] ch2_dout;
  wire [15:0] ch3_din, ch3_dout_unused;
  wire        ch1_ready, ch2_ready, ch3_ready_unused, ch4_ready;
  wire        ch1_req, ch2_req, ch3_req, ch4_req;
  wire        ch1_rnw, ch2_rnw, ch3_rnw, ch4_rnw;
  wire [7:0]  ch4_be;

  // 4. GBA 核心内存请求接口
  wire        sdram_read_ena, sdram_read_done;
  wire [24:0] sdram_read_addr;
  wire [31:0] sdram_read_data, sdram_second_dword;

  wire [31:0] bus_out_din, bus_out_dout;
  wire [25:0] bus_out_adr;
  wire        bus_out_rnw, bus_out_ena, bus_out_done;

  wire [63:0] save_out_din, save_out_dout;
  wire [25:0] save_out_adr;
  wire        save_out_rnw, save_out_ena, save_out_active, save_out_done;
  wire [7:0]  save_out_be;

  // 5. 视频与帧缓冲信号
  wire        fb_frame_pulse;
  // fb_frame_idx_unused 已随 frame_tick_480p 一并移除
  wire [1:0]  display_frame_idx_core;
  wire [31:0] unused_core_fb_base;
  wire [25:0] unused_core_fb_addr;
  wire [63:0] unused_core_fb_data;
  wire        unused_core_fb_req;
  wire        core_fb_done;
  wire        core_fb_newframe;
  wire [15:0] core_pixel_addr;
  wire [17:0] core_pixel_data;
  wire        core_pixel_we;
  wire [31:0] fbcap_frame_seq;
  wire        fbcap_frame_buf_idx;
  wire [31:0] core_debug_internal;

  // 6. 状态与统计
  wire [31:0] cycles_missing, cycles_vsync_speed;
  wire [31:0] sys_err_vec_w;
  wire        sys_err_pulse_w;
  wire        w_core_cfg_sw_reset;
  wire        irq_vsync_pulse_axi;
  wire        irq_error_pulse_axi;
  wire        sys_rom_loading_axi;
  wire [9:0]  physical_keys_axi;
  wire [31:0] debug_cpu_pc_axi;
  wire [31:0] debug_cpu_mixed_axi;
  wire [31:0] debug_irq_axi;
  wire [31:0] debug_dma_axi;
  wire [31:0] debug_mem_axi;
  wire [31:0] dbg_chain_flags_axi;
  wire [31:0] dbg_chain_counts0_axi;
  wire [31:0] dbg_chain_counts1_axi;
  wire [31:0] dbg_ch1_first_addr_axi;
  wire [31:0] dbg_ch1_first_meta_axi;
  wire [31:0] dbg_ch1_last_addr_axi;
  wire [31:0] dbg_ch1_last_meta_axi;
  wire [31:0] dbg_ddr_first_addr_axi;
  wire [31:0] dbg_ddr_first_meta_axi;
  wire [31:0] dbg_ddr_last_addr_axi;
  wire [31:0] dbg_ddr_last_meta_axi;
  wire [31:0] dbg_axi_ar_first_addr_axi;
  wire [31:0] dbg_axi_ar_first_meta_axi;
  wire [31:0] dbg_axi_ar_last_addr_axi;
  wire [31:0] dbg_axi_ar_last_meta_axi;
  wire [31:0] dbg_axi_r_first_addr_axi;
  wire [31:0] dbg_axi_r_first_meta_axi;
  wire [31:0] dbg_axi_r_last_addr_axi;
  wire [31:0] dbg_axi_r_last_meta_axi;
  wire [31:0] dbg_done_first_addr_axi;
  wire [31:0] dbg_done_first_meta_axi;
  wire [31:0] dbg_done_last_addr_axi;
  wire [31:0] dbg_done_last_meta_axi;
  wire [31:0] save_status_axi;

  (* ASYNC_REG = "TRUE" *) reg        w_core_cfg_sw_reset_meta, w_core_cfg_sw_reset_sync;
  (* ASYNC_REG = "TRUE" *) reg [1:0]  display_frame_idx_core_meta, display_frame_idx_core_sync;
  (* ASYNC_REG = "TRUE" *) reg [13:0] cycles_missing_axi_meta, cycles_missing_axi_sync;
  (* ASYNC_REG = "TRUE" *) reg [15:0] cycles_vsync_speed_axi_meta, cycles_vsync_speed_axi_sync;
  (* ASYNC_REG = "TRUE" *) reg [31:0] sys_err_vec_axi_meta, sys_err_vec_axi_sync;
  (* ASYNC_REG = "TRUE" *) reg [1:0]  fb_frame_idx_axi_meta, fb_frame_idx_axi_sync;
  (* ASYNC_REG = "TRUE" *) reg        sys_rom_loading_axi_meta, sys_rom_loading_axi_sync;
  (* ASYNC_REG = "TRUE" *) reg [9:0]  physical_keys_axi_meta, physical_keys_axi_sync;
  (* ASYNC_REG = "TRUE" *) reg [31:0] debug_cpu_pc_axi_meta, debug_cpu_pc_axi_sync;
  (* ASYNC_REG = "TRUE" *) reg [31:0] debug_cpu_mixed_axi_meta, debug_cpu_mixed_axi_sync;
  (* ASYNC_REG = "TRUE" *) reg [31:0] debug_irq_axi_meta, debug_irq_axi_sync;
  (* ASYNC_REG = "TRUE" *) reg [31:0] debug_dma_axi_meta, debug_dma_axi_sync;
  (* ASYNC_REG = "TRUE" *) reg [31:0] debug_mem_axi_meta, debug_mem_axi_sync;
  (* ASYNC_REG = "TRUE" *) reg [31:0] dbg_chain_flags_axi_meta, dbg_chain_flags_axi_sync;
  (* ASYNC_REG = "TRUE" *) reg [31:0] dbg_chain_counts0_axi_meta, dbg_chain_counts0_axi_sync;
  (* ASYNC_REG = "TRUE" *) reg [31:0] dbg_chain_counts1_axi_meta, dbg_chain_counts1_axi_sync;
  (* ASYNC_REG = "TRUE" *) reg [31:0] dbg_ch1_first_addr_axi_meta, dbg_ch1_first_addr_axi_sync;
  (* ASYNC_REG = "TRUE" *) reg [31:0] dbg_ch1_first_meta_axi_meta, dbg_ch1_first_meta_axi_sync;
  (* ASYNC_REG = "TRUE" *) reg [31:0] dbg_ch1_last_addr_axi_meta, dbg_ch1_last_addr_axi_sync;
  (* ASYNC_REG = "TRUE" *) reg [31:0] dbg_ch1_last_meta_axi_meta, dbg_ch1_last_meta_axi_sync;
  (* ASYNC_REG = "TRUE" *) reg [31:0] dbg_ddr_first_addr_axi_meta, dbg_ddr_first_addr_axi_sync;
  (* ASYNC_REG = "TRUE" *) reg [31:0] dbg_ddr_first_meta_axi_meta, dbg_ddr_first_meta_axi_sync;
  (* ASYNC_REG = "TRUE" *) reg [31:0] dbg_ddr_last_addr_axi_meta, dbg_ddr_last_addr_axi_sync;
  (* ASYNC_REG = "TRUE" *) reg [31:0] dbg_ddr_last_meta_axi_meta, dbg_ddr_last_meta_axi_sync;
  (* ASYNC_REG = "TRUE" *) reg [31:0] dbg_axi_ar_first_addr_axi_meta, dbg_axi_ar_first_addr_axi_sync;
  (* ASYNC_REG = "TRUE" *) reg [31:0] dbg_axi_ar_first_meta_axi_meta, dbg_axi_ar_first_meta_axi_sync;
  (* ASYNC_REG = "TRUE" *) reg [31:0] dbg_axi_ar_last_addr_axi_meta, dbg_axi_ar_last_addr_axi_sync;
  (* ASYNC_REG = "TRUE" *) reg [31:0] dbg_axi_ar_last_meta_axi_meta, dbg_axi_ar_last_meta_axi_sync;
  (* ASYNC_REG = "TRUE" *) reg [31:0] dbg_axi_r_first_addr_axi_meta, dbg_axi_r_first_addr_axi_sync;
  (* ASYNC_REG = "TRUE" *) reg [31:0] dbg_axi_r_first_meta_axi_meta, dbg_axi_r_first_meta_axi_sync;
  (* ASYNC_REG = "TRUE" *) reg [31:0] dbg_axi_r_last_addr_axi_meta, dbg_axi_r_last_addr_axi_sync;
  (* ASYNC_REG = "TRUE" *) reg [31:0] dbg_axi_r_last_meta_axi_meta, dbg_axi_r_last_meta_axi_sync;
  (* ASYNC_REG = "TRUE" *) reg [31:0] dbg_done_first_addr_axi_meta, dbg_done_first_addr_axi_sync;
  (* ASYNC_REG = "TRUE" *) reg [31:0] dbg_done_first_meta_axi_meta, dbg_done_first_meta_axi_sync;
  (* ASYNC_REG = "TRUE" *) reg [31:0] dbg_done_last_addr_axi_meta, dbg_done_last_addr_axi_sync;
  (* ASYNC_REG = "TRUE" *) reg [31:0] dbg_done_last_meta_axi_meta, dbg_done_last_meta_axi_sync;
  reg [31:0]                          fbcap_frame_seq_axi_sync;
  reg                                 fbcap_frame_buf_idx_axi_sync;
  reg [31:0]                          fbcap_shadow_seq_core;
  reg                                 fbcap_shadow_buf_core;
  reg                                 fbcap_shadow_req_tgl_core;
  reg                                 fbcap_shadow_ack_tgl_axi;
  (* ASYNC_REG = "TRUE" *) reg [2:0]  fbcap_shadow_req_sync_axi;
  (* ASYNC_REG = "TRUE" *) reg [2:0]  fbcap_shadow_ack_sync_core;
  reg [31:0]                          fbcap_shadow_seq_axi_s1;
  reg [31:0]                          fbcap_shadow_seq_axi_s2;
  reg                                 fbcap_shadow_buf_axi_s1;
  reg                                 fbcap_shadow_buf_axi_s2;
  reg [1:0]                           fbcap_shadow_sample_cnt;
  (* ASYNC_REG = "TRUE" *) reg [31:0] save_status_axi_meta, save_status_axi_sync;
  (* ASYNC_REG = "TRUE" *) reg [2:0]  bios_wr_req_sync_core;
  (* ASYNC_REG = "TRUE" *) reg [2:0]  bios_wr_ack_sync_axi;
  reg                                 bios_wr_ack_toggle_core;
  reg [11:0]                          bios_wr_addr_core_s1;
  reg [11:0]                          bios_wr_addr_core_s2;
  reg [31:0]                          bios_wr_data_core_s1;
  reg [31:0]                          bios_wr_data_core_s2;
  reg [1:0]                           bios_wr_sample_cnt_core;
  reg [31:0]                          bios_wr_ack_seq_axi;
  reg [11:0]                          bios_wr_addr_core;
  reg [31:0]                          bios_wr_data_core;
  reg                                 bios_wr_core;

  wire [15:0] unused_core_vcount;
  reg                                  fb_frame_pulse_toggle;
  reg                                  sys_err_pulse_toggle;
  (* ASYNC_REG = "TRUE" *) reg [2:0]  fb_frame_pulse_toggle_axi_sync;
  (* ASYNC_REG = "TRUE" *) reg [2:0]  sys_err_pulse_toggle_axi_sync;
  reg [31:0]                          dbg_chain_flags_core;
  reg [31:0]                          dbg_chain_counts0_core;
  reg [31:0]                          dbg_chain_counts1_core;
  reg [31:0]                          dbg_ch1_first_addr_core;
  reg [31:0]                          dbg_ch1_first_meta_core;
  reg [31:0]                          dbg_ch1_last_addr_core;
  reg [31:0]                          dbg_ch1_last_meta_core;
  reg [31:0]                          dbg_ddr_first_addr_core;
  reg [31:0]                          dbg_ddr_first_meta_core;
  reg [31:0]                          dbg_ddr_last_addr_core;
  reg [31:0]                          dbg_ddr_last_meta_core;
  reg [31:0]                          dbg_axi_ar_first_addr_core;
  reg [31:0]                          dbg_axi_ar_first_meta_core;
  reg [31:0]                          dbg_axi_ar_last_addr_core;
  reg [31:0]                          dbg_axi_ar_last_meta_core;
  reg [31:0]                          dbg_axi_r_first_addr_core;
  reg [31:0]                          dbg_axi_r_first_meta_core;
  reg [31:0]                          dbg_axi_r_last_addr_core;
  reg [31:0]                          dbg_axi_r_last_meta_core;
  reg [31:0]                          dbg_done_first_addr_core;
  reg [31:0]                          dbg_done_first_meta_core;
  reg [31:0]                          dbg_done_last_addr_core;
  reg [31:0]                          dbg_done_last_meta_core;
  reg [31:0]                          dbg_pending_line_addr;
  reg [1:0]                           dbg_pending_lane;
  reg [31:0]                          dbg_pending_axi_ar_addr;
  reg [1:0]                           dbg_last_axi_rresp;
  reg                                 dbg_last_axi_rlast;
  reg [7:0]                           save_sram_count_core;
  reg [7:0]                           save_flash_count_core;
  reg [7:0]                           save_eeprom_count_core;

  // 7. 明确收口上游核心当前未接入的可选功能，避免综合依赖隐式默认值
  localparam [1:0] GBA_UNDERCLOCK_OFF       = 2'b00;
  localparam [1:0] GBA_INTERFRAME_BLEND_OFF = 2'b00;
  localparam [2:0] GBA_SHADE_MODE_OFF       = 3'b000;

  wire [31:0] unused_bus_read_data;
  wire [31:0] unused_rtc_timestamp_out;
  wire [41:0] unused_rtc_savedtime_out;
  wire        unused_rtc_inuse;
  wire        unused_cheats_active;
  wire        unused_save_eeprom;
  wire        unused_save_sram;
  wire        unused_save_flash;
  wire        unused_load_done;
  wire        unused_rumble;
  wire [31:0] unused_debug_cpu_pc;
  wire [31:0] unused_debug_cpu_mixed;
  wire [31:0] unused_debug_irq;
  wire [31:0] unused_debug_dma;
  wire [31:0] unused_debug_mem;
  wire [15:0] core_audio_l;
  wire [15:0] core_audio_r;
  wire        audio_sample_ce;
  wire [15:0] audio_out_l;
  wire [15:0] audio_out_r;

  assign w_core_cfg_sw_reset = w_core_cfg_sw_reset_sync;
  assign display_frame_idx_core = display_frame_idx_core_sync;
  assign irq_vsync_pulse_axi = fb_frame_pulse_toggle_axi_sync[2] ^ fb_frame_pulse_toggle_axi_sync[1];
  assign irq_error_pulse_axi = sys_err_pulse_toggle_axi_sync[2] ^ sys_err_pulse_toggle_axi_sync[1];
  assign sys_rom_loading_axi = sys_rom_loading_axi_sync;
  assign physical_keys_axi = physical_keys_axi_sync;
  assign debug_cpu_pc_axi = G_ENABLE_DEBUG ? debug_cpu_pc_axi_sync : 32'd0;
  assign debug_cpu_mixed_axi = G_ENABLE_DEBUG ? debug_cpu_mixed_axi_sync : 32'd0;
  assign debug_irq_axi = G_ENABLE_DEBUG ? debug_irq_axi_sync : 32'd0;
  assign debug_dma_axi = G_ENABLE_DEBUG ? debug_dma_axi_sync : 32'd0;
  assign debug_mem_axi = G_ENABLE_DEBUG ? debug_mem_axi_sync : 32'd0;
  assign audio_out_l = core_audio_l;
  assign audio_out_r = core_audio_r;
  // Keep the PS-side frame IRQ aligned with the core's original largeimg frame
  // boundary. Using pixel_addr==0 fires at the first pixel of a new frame and
  // can arrive before FB_CAP_SEQ has advanced, so the PS blit path misses the
  // completed frame.
  assign core_fb_newframe = unused_core_fb_req && (unused_core_fb_addr[19:0] == 20'd0);
  assign dbg_chain_flags_axi = G_ENABLE_DEBUG ? dbg_chain_flags_axi_sync : 32'd0;
  assign dbg_chain_counts0_axi = G_ENABLE_DEBUG ? dbg_chain_counts0_axi_sync : 32'd0;
  assign dbg_chain_counts1_axi = G_ENABLE_DEBUG ? dbg_chain_counts1_axi_sync : 32'd0;
  assign dbg_ch1_first_addr_axi = G_ENABLE_DEBUG ? dbg_ch1_first_addr_axi_sync : 32'd0;
  assign dbg_ch1_first_meta_axi = G_ENABLE_DEBUG ? dbg_ch1_first_meta_axi_sync : 32'd0;
  assign dbg_ch1_last_addr_axi = G_ENABLE_DEBUG ? dbg_ch1_last_addr_axi_sync : 32'd0;
  assign dbg_ch1_last_meta_axi = G_ENABLE_DEBUG ? dbg_ch1_last_meta_axi_sync : 32'd0;
  assign dbg_ddr_first_addr_axi = G_ENABLE_DEBUG ? dbg_ddr_first_addr_axi_sync : 32'd0;
  assign dbg_ddr_first_meta_axi = G_ENABLE_DEBUG ? dbg_ddr_first_meta_axi_sync : 32'd0;
  assign dbg_ddr_last_addr_axi = G_ENABLE_DEBUG ? dbg_ddr_last_addr_axi_sync : 32'd0;
  assign dbg_ddr_last_meta_axi = G_ENABLE_DEBUG ? dbg_ddr_last_meta_axi_sync : 32'd0;
  assign dbg_axi_ar_first_addr_axi = G_ENABLE_DEBUG ? dbg_axi_ar_first_addr_axi_sync : 32'd0;
  assign dbg_axi_ar_first_meta_axi = G_ENABLE_DEBUG ? dbg_axi_ar_first_meta_axi_sync : 32'd0;
  assign dbg_axi_ar_last_addr_axi = G_ENABLE_DEBUG ? dbg_axi_ar_last_addr_axi_sync : 32'd0;
  assign dbg_axi_ar_last_meta_axi = G_ENABLE_DEBUG ? dbg_axi_ar_last_meta_axi_sync : 32'd0;
  assign dbg_axi_r_first_addr_axi = G_ENABLE_DEBUG ? dbg_axi_r_first_addr_axi_sync : 32'd0;
  assign dbg_axi_r_first_meta_axi = G_ENABLE_DEBUG ? dbg_axi_r_first_meta_axi_sync : 32'd0;
  assign dbg_axi_r_last_addr_axi = G_ENABLE_DEBUG ? dbg_axi_r_last_addr_axi_sync : 32'd0;
  assign dbg_axi_r_last_meta_axi = G_ENABLE_DEBUG ? dbg_axi_r_last_meta_axi_sync : 32'd0;
  assign dbg_done_first_addr_axi = G_ENABLE_DEBUG ? dbg_done_first_addr_axi_sync : 32'd0;
  assign dbg_done_first_meta_axi = G_ENABLE_DEBUG ? dbg_done_first_meta_axi_sync : 32'd0;
  assign dbg_done_last_addr_axi = G_ENABLE_DEBUG ? dbg_done_last_addr_axi_sync : 32'd0;
  assign dbg_done_last_meta_axi = G_ENABLE_DEBUG ? dbg_done_last_meta_axi_sync : 32'd0;
  assign save_status_axi = save_status_axi_sync;

  function [7:0] sat_inc8;
    input [7:0] value;
    begin
      sat_inc8 = (value == 8'hFF) ? value : (value + 8'd1);
    end
  endfunction

  always @(posedge clk_100) begin
    if (!rst_n) begin
      w_core_cfg_sw_reset_meta <= 1'b0;
      w_core_cfg_sw_reset_sync <= 1'b0;
      display_frame_idx_core_meta <= 2'd0;
      display_frame_idx_core_sync <= 2'd0;
      fb_frame_pulse_toggle    <= 1'b0;
      sys_err_pulse_toggle     <= 1'b0;
      dbg_chain_flags_core     <= 32'd0;
      dbg_chain_counts0_core   <= 32'd0;
      dbg_chain_counts1_core   <= 32'd0;
      dbg_ch1_first_addr_core  <= 32'd0;
      dbg_ch1_first_meta_core  <= 32'd0;
      dbg_ch1_last_addr_core   <= 32'd0;
      dbg_ch1_last_meta_core   <= 32'd0;
      dbg_ddr_first_addr_core  <= 32'd0;
      dbg_ddr_first_meta_core  <= 32'd0;
      dbg_ddr_last_addr_core   <= 32'd0;
      dbg_ddr_last_meta_core   <= 32'd0;
      dbg_axi_ar_first_addr_core <= 32'd0;
      dbg_axi_ar_first_meta_core <= 32'd0;
      dbg_axi_ar_last_addr_core <= 32'd0;
      dbg_axi_ar_last_meta_core <= 32'd0;
      dbg_axi_r_first_addr_core <= 32'd0;
      dbg_axi_r_first_meta_core <= 32'd0;
      dbg_axi_r_last_addr_core <= 32'd0;
      dbg_axi_r_last_meta_core <= 32'd0;
      dbg_done_first_addr_core <= 32'd0;
      dbg_done_first_meta_core <= 32'd0;
      dbg_done_last_addr_core <= 32'd0;
      dbg_done_last_meta_core <= 32'd0;
      dbg_pending_line_addr   <= 32'd0;
      dbg_pending_lane        <= 2'd0;
      dbg_pending_axi_ar_addr <= 32'd0;
      dbg_last_axi_rresp      <= 2'd0;
      dbg_last_axi_rlast      <= 1'b0;
      save_sram_count_core    <= 8'd0;
      save_flash_count_core   <= 8'd0;
      save_eeprom_count_core  <= 8'd0;
    end else begin
      w_core_cfg_sw_reset_meta <= w_axi_cfg_sw_reset;
      w_core_cfg_sw_reset_sync <= w_core_cfg_sw_reset_meta;
      display_frame_idx_core_meta <= w_axi_display_frame_idx;
      display_frame_idx_core_sync <= display_frame_idx_core_meta;

      if (fb_frame_pulse) begin
        fb_frame_pulse_toggle <= ~fb_frame_pulse_toggle;
      end

      if (sys_err_pulse_w) begin
        sys_err_pulse_toggle <= ~sys_err_pulse_toggle;
      end

      if (w_core_cfg_sw_reset) begin
        dbg_chain_flags_core     <= 32'd0;
        dbg_chain_counts0_core   <= 32'd0;
        dbg_chain_counts1_core   <= 32'd0;
        dbg_ch1_first_addr_core  <= 32'd0;
        dbg_ch1_first_meta_core  <= 32'd0;
        dbg_ch1_last_addr_core   <= 32'd0;
        dbg_ch1_last_meta_core   <= 32'd0;
        dbg_ddr_first_addr_core  <= 32'd0;
        dbg_ddr_first_meta_core  <= 32'd0;
        dbg_ddr_last_addr_core   <= 32'd0;
        dbg_ddr_last_meta_core   <= 32'd0;
        dbg_axi_ar_first_addr_core <= 32'd0;
        dbg_axi_ar_first_meta_core <= 32'd0;
        dbg_axi_ar_last_addr_core <= 32'd0;
        dbg_axi_ar_last_meta_core <= 32'd0;
        dbg_axi_r_first_addr_core <= 32'd0;
        dbg_axi_r_first_meta_core <= 32'd0;
        dbg_axi_r_last_addr_core <= 32'd0;
        dbg_axi_r_last_meta_core <= 32'd0;
        dbg_done_first_addr_core <= 32'd0;
        dbg_done_first_meta_core <= 32'd0;
        dbg_done_last_addr_core <= 32'd0;
        dbg_done_last_meta_core <= 32'd0;
        dbg_pending_line_addr   <= 32'd0;
        dbg_pending_lane        <= 2'd0;
        dbg_pending_axi_ar_addr <= 32'd0;
        dbg_last_axi_rresp      <= 2'd0;
        dbg_last_axi_rlast      <= 1'b0;
        save_sram_count_core    <= 8'd0;
        save_flash_count_core   <= 8'd0;
        save_eeprom_count_core  <= 8'd0;
      end else begin
        if (unused_save_sram) begin
          save_sram_count_core <= save_sram_count_core + 8'd1;
        end

        if (unused_save_flash) begin
          save_flash_count_core <= save_flash_count_core + 8'd1;
        end

        if (unused_save_eeprom) begin
          save_eeprom_count_core <= save_eeprom_count_core + 8'd1;
        end

        if (ch1_req) begin
          dbg_pending_line_addr <= {4'b0011, ch1_addr[27:3], 3'b000};
          dbg_pending_lane <= ch1_addr[2:1];
          if (!dbg_chain_flags_core[0]) begin
            dbg_ch1_first_addr_core <= {4'b0011, ch1_addr[27:3], 3'b000};
            dbg_ch1_first_meta_core <= {19'd0, display_frame_idx_core, ddram_busy, ch1_addr[2:1], dbg_chain_counts0_core[7:0]};
            dbg_chain_flags_core[0] <= 1'b1;
          end
          dbg_ch1_last_addr_core <= {4'b0011, ch1_addr[27:3], 3'b000};
          dbg_ch1_last_meta_core <= {19'd0, display_frame_idx_core, ddram_busy, ch1_addr[2:1], dbg_chain_counts0_core[7:0]};
          dbg_chain_counts0_core[7:0] <= sat_inc8(dbg_chain_counts0_core[7:0]);
        end

        if (ddram_rd || ddram_we) begin
          dbg_pending_line_addr <= {ddram_addr, 3'b000};
          if (!dbg_chain_flags_core[1]) begin
            dbg_ddr_first_addr_core <= {ddram_addr, 3'b000};
            dbg_ddr_first_meta_core <= {13'd0, ddram_busy, ddram_we, ddram_rd, ddram_burstcnt, dbg_chain_counts0_core[15:8]};
            dbg_chain_flags_core[1] <= 1'b1;
          end
          dbg_ddr_last_addr_core <= {ddram_addr, 3'b000};
          dbg_ddr_last_meta_core <= {13'd0, ddram_busy, ddram_we, ddram_rd, ddram_burstcnt, dbg_chain_counts0_core[15:8]};
          dbg_chain_counts0_core[15:8] <= sat_inc8(dbg_chain_counts0_core[15:8]);
        end

        if (M_AXI_ARVALID && M_AXI_ARREADY) begin
          dbg_pending_axi_ar_addr <= M_AXI_ARADDR;
          if (!dbg_chain_flags_core[2]) begin
            dbg_axi_ar_first_addr_core <= M_AXI_ARADDR;
            dbg_axi_ar_first_meta_core <= {10'd0, M_AXI_ARREADY, M_AXI_ARBURST, M_AXI_ARSIZE, M_AXI_ARLEN, dbg_chain_counts0_core[23:16]};
            dbg_chain_flags_core[2] <= 1'b1;
          end
          dbg_axi_ar_last_addr_core <= M_AXI_ARADDR;
          dbg_axi_ar_last_meta_core <= {10'd0, M_AXI_ARREADY, M_AXI_ARBURST, M_AXI_ARSIZE, M_AXI_ARLEN, dbg_chain_counts0_core[23:16]};
          dbg_chain_counts0_core[23:16] <= sat_inc8(dbg_chain_counts0_core[23:16]);
        end

        if (M_AXI_RVALID && M_AXI_RREADY) begin
          dbg_last_axi_rresp <= M_AXI_RRESP;
          dbg_last_axi_rlast <= M_AXI_RLAST;
          if (!dbg_chain_flags_core[3]) begin
            dbg_axi_r_first_addr_core <= dbg_pending_axi_ar_addr;
            dbg_axi_r_first_meta_core <= {21'd0, M_AXI_RLAST, M_AXI_RRESP, dbg_chain_counts0_core[31:24]};
            dbg_chain_flags_core[3] <= 1'b1;
          end
          dbg_axi_r_last_addr_core <= dbg_pending_axi_ar_addr;
          dbg_axi_r_last_meta_core <= {21'd0, M_AXI_RLAST, M_AXI_RRESP, dbg_chain_counts0_core[31:24]};
          dbg_chain_counts0_core[31:24] <= sat_inc8(dbg_chain_counts0_core[31:24]);
          if (M_AXI_RRESP != 2'b00) begin
            dbg_chain_flags_core[7] <= 1'b1;
          end
        end

        if (ddram_dout_ready) begin
          dbg_chain_flags_core[4] <= 1'b1;
          dbg_chain_counts1_core[7:0] <= sat_inc8(dbg_chain_counts1_core[7:0]);
        end

        if (sdram_read_done) begin
          if (!dbg_chain_flags_core[5]) begin
            dbg_done_first_addr_core <= dbg_pending_line_addr;
            dbg_done_first_meta_core <= {17'd0, dbg_last_axi_rlast, dbg_last_axi_rresp, display_frame_idx_core, dbg_pending_lane, dbg_chain_counts1_core[15:8]};
            dbg_chain_flags_core[5] <= 1'b1;
          end
          dbg_done_last_addr_core <= dbg_pending_line_addr;
          dbg_done_last_meta_core <= {17'd0, dbg_last_axi_rlast, dbg_last_axi_rresp, display_frame_idx_core, dbg_pending_lane, dbg_chain_counts1_core[15:8]};
          dbg_chain_counts1_core[15:8] <= sat_inc8(dbg_chain_counts1_core[15:8]);
        end

        if (sys_err_pulse_w) begin
          dbg_chain_flags_core[6] <= 1'b1;
          dbg_chain_counts1_core[23:16] <= sat_inc8(dbg_chain_counts1_core[23:16]);
        end

        // flags[21:8] = core_debug_internal sticky flags
        // bit8=gbaon, 9=reset, 10=new_cycles_valid, 11=gba_step,
        // 12=sleep_ss, 13=sleep_cheat, 14=sleep_rewind, 15=loading_ss,
        // 16=settle, 17=pix_we_int, 18=cpu_done, 19=dma_on,
        // 20=GBA_on_raw, 21=lockspeed
        dbg_chain_flags_core[21:8] <= dbg_chain_flags_core[21:8] | core_debug_internal[13:0];

        // counts1[27:24] = ddram_we 4-bit saturating counter
        if (ddram_we) begin
          if (dbg_chain_counts1_core[27:24] != 4'hF)
            dbg_chain_counts1_core[27:24] <= dbg_chain_counts1_core[27:24] + 4'd1;
        end

        // counts1[31:28] = core_pixel_we 4-bit saturating counter
        if (core_pixel_we) begin
          if (dbg_chain_counts1_core[31:28] != 4'hF)
            dbg_chain_counts1_core[31:28] <= dbg_chain_counts1_core[31:28] + 4'd1;
        end
      end
    end
  end

  // Multi-bit FB capture status CDC: snapshot in core clock domain, transfer
  // to AXI domain with toggle handshake.
  always @(posedge clk_100) begin
    if (!rst_n) begin
      fbcap_shadow_seq_core <= 32'd0;
      fbcap_shadow_buf_core <= 1'b0;
      fbcap_shadow_req_tgl_core <= 1'b0;
      fbcap_shadow_ack_sync_core <= 3'b000;
    end else begin
      fbcap_shadow_ack_sync_core <= {fbcap_shadow_ack_sync_core[1:0], fbcap_shadow_ack_tgl_axi};
      if ((fbcap_frame_seq != fbcap_shadow_seq_core) &&
          (fbcap_shadow_req_tgl_core == fbcap_shadow_ack_sync_core[2])) begin
        fbcap_shadow_seq_core <= fbcap_frame_seq;
        fbcap_shadow_buf_core <= fbcap_frame_buf_idx;
        fbcap_shadow_req_tgl_core <= ~fbcap_shadow_req_tgl_core;
      end
    end
  end

  always @(posedge s_axi_aclk) begin
    if (!s_axi_aresetn) begin
      cycles_missing_axi_meta      <= 14'd0;
      cycles_missing_axi_sync      <= 14'd0;
      cycles_vsync_speed_axi_meta  <= 16'd0;
      cycles_vsync_speed_axi_sync  <= 16'd0;
      sys_err_vec_axi_meta         <= 32'd0;
      sys_err_vec_axi_sync         <= 32'd0;
      fb_frame_idx_axi_meta        <= 2'd0;
      fb_frame_idx_axi_sync        <= 2'd0;
      sys_rom_loading_axi_meta     <= 1'b0;
      sys_rom_loading_axi_sync     <= 1'b0;
      physical_keys_axi_meta       <= 10'd0;
      physical_keys_axi_sync       <= 10'd0;
      debug_cpu_pc_axi_meta        <= 32'd0;
      debug_cpu_pc_axi_sync        <= 32'd0;
      debug_cpu_mixed_axi_meta     <= 32'd0;
      debug_cpu_mixed_axi_sync     <= 32'd0;
      debug_irq_axi_meta           <= 32'd0;
      debug_irq_axi_sync           <= 32'd0;
      debug_dma_axi_meta           <= 32'd0;
      debug_dma_axi_sync           <= 32'd0;
      debug_mem_axi_meta           <= 32'd0;
      debug_mem_axi_sync           <= 32'd0;
      dbg_chain_flags_axi_meta     <= 32'd0;
      dbg_chain_flags_axi_sync     <= 32'd0;
      dbg_chain_counts0_axi_meta   <= 32'd0;
      dbg_chain_counts0_axi_sync   <= 32'd0;
      dbg_chain_counts1_axi_meta   <= 32'd0;
      dbg_chain_counts1_axi_sync   <= 32'd0;
      dbg_ch1_first_addr_axi_meta  <= 32'd0;
      dbg_ch1_first_addr_axi_sync  <= 32'd0;
      dbg_ch1_first_meta_axi_meta  <= 32'd0;
      dbg_ch1_first_meta_axi_sync  <= 32'd0;
      dbg_ch1_last_addr_axi_meta   <= 32'd0;
      dbg_ch1_last_addr_axi_sync   <= 32'd0;
      dbg_ch1_last_meta_axi_meta   <= 32'd0;
      dbg_ch1_last_meta_axi_sync   <= 32'd0;
      dbg_ddr_first_addr_axi_meta  <= 32'd0;
      dbg_ddr_first_addr_axi_sync  <= 32'd0;
      dbg_ddr_first_meta_axi_meta  <= 32'd0;
      dbg_ddr_first_meta_axi_sync  <= 32'd0;
      dbg_ddr_last_addr_axi_meta   <= 32'd0;
      dbg_ddr_last_addr_axi_sync   <= 32'd0;
      dbg_ddr_last_meta_axi_meta   <= 32'd0;
      dbg_ddr_last_meta_axi_sync   <= 32'd0;
      dbg_axi_ar_first_addr_axi_meta <= 32'd0;
      dbg_axi_ar_first_addr_axi_sync <= 32'd0;
      dbg_axi_ar_first_meta_axi_meta <= 32'd0;
      dbg_axi_ar_first_meta_axi_sync <= 32'd0;
      dbg_axi_ar_last_addr_axi_meta <= 32'd0;
      dbg_axi_ar_last_addr_axi_sync <= 32'd0;
      dbg_axi_ar_last_meta_axi_meta <= 32'd0;
      dbg_axi_ar_last_meta_axi_sync <= 32'd0;
      dbg_axi_r_first_addr_axi_meta <= 32'd0;
      dbg_axi_r_first_addr_axi_sync <= 32'd0;
      dbg_axi_r_first_meta_axi_meta <= 32'd0;
      dbg_axi_r_first_meta_axi_sync <= 32'd0;
      dbg_axi_r_last_addr_axi_meta <= 32'd0;
      dbg_axi_r_last_addr_axi_sync <= 32'd0;
      dbg_axi_r_last_meta_axi_meta <= 32'd0;
      dbg_axi_r_last_meta_axi_sync <= 32'd0;
      dbg_done_first_addr_axi_meta <= 32'd0;
      dbg_done_first_addr_axi_sync <= 32'd0;
      dbg_done_first_meta_axi_meta <= 32'd0;
      dbg_done_first_meta_axi_sync <= 32'd0;
      dbg_done_last_addr_axi_meta <= 32'd0;
      dbg_done_last_addr_axi_sync <= 32'd0;
      dbg_done_last_meta_axi_meta <= 32'd0;
      dbg_done_last_meta_axi_sync <= 32'd0;
      fbcap_frame_seq_axi_sync <= 32'd0;
      fbcap_frame_buf_idx_axi_sync <= 1'b0;
      fbcap_shadow_ack_tgl_axi <= 1'b0;
      fbcap_shadow_req_sync_axi <= 3'b000;
      fbcap_shadow_seq_axi_s1 <= 32'd0;
      fbcap_shadow_seq_axi_s2 <= 32'd0;
      fbcap_shadow_buf_axi_s1 <= 1'b0;
      fbcap_shadow_buf_axi_s2 <= 1'b0;
      fbcap_shadow_sample_cnt <= 2'd0;
      save_status_axi_meta <= 32'd0;
      save_status_axi_sync <= 32'd0;
      bios_wr_ack_sync_axi <= 3'b000;
      bios_wr_ack_seq_axi <= 32'd0;
      fb_frame_pulse_toggle_axi_sync <= 3'b000;
      sys_err_pulse_toggle_axi_sync  <= 3'b000;
    end else begin
      cycles_missing_axi_meta      <= cycles_missing[13:0];
      cycles_missing_axi_sync      <= cycles_missing_axi_meta;
      cycles_vsync_speed_axi_meta  <= cycles_vsync_speed[15:0];
      cycles_vsync_speed_axi_sync  <= cycles_vsync_speed_axi_meta;
      sys_err_vec_axi_meta         <= sys_err_vec_w;
      sys_err_vec_axi_sync         <= sys_err_vec_axi_meta;
      fb_frame_idx_axi_meta        <= display_frame_idx_core;
      fb_frame_idx_axi_sync        <= fb_frame_idx_axi_meta;
      sys_rom_loading_axi_meta     <= w_core_cfg_ctrl[8];
      sys_rom_loading_axi_sync     <= sys_rom_loading_axi_meta;
      physical_keys_axi_meta       <= {(sws[3] & btns[3]), (sws[3] & btns[0]), btns[1], btns[2], (btns[3] & ~sws[3]), (btns[0] & ~sws[3]), sws[2], (sws[3] & ~btns[3] & ~btns[0]), sws[1], sws[0]};
      physical_keys_axi_sync       <= physical_keys_axi_meta;
      debug_cpu_pc_axi_meta        <= unused_debug_cpu_pc;
      debug_cpu_pc_axi_sync        <= debug_cpu_pc_axi_meta;
      debug_cpu_mixed_axi_meta     <= unused_debug_cpu_mixed;
      debug_cpu_mixed_axi_sync     <= debug_cpu_mixed_axi_meta;
      debug_irq_axi_meta           <= unused_debug_irq;
      debug_irq_axi_sync           <= debug_irq_axi_meta;
      debug_dma_axi_meta           <= unused_debug_dma;
      debug_dma_axi_sync           <= debug_dma_axi_meta;
      debug_mem_axi_meta           <= unused_debug_mem;
      debug_mem_axi_sync           <= debug_mem_axi_meta;
      dbg_chain_flags_axi_meta     <= dbg_chain_flags_core;
      dbg_chain_flags_axi_sync     <= dbg_chain_flags_axi_meta;
      dbg_chain_counts0_axi_meta   <= dbg_chain_counts0_core;
      dbg_chain_counts0_axi_sync   <= dbg_chain_counts0_axi_meta;
      dbg_chain_counts1_axi_meta   <= dbg_chain_counts1_core;
      dbg_chain_counts1_axi_sync   <= dbg_chain_counts1_axi_meta;
      dbg_ch1_first_addr_axi_meta  <= dbg_ch1_first_addr_core;
      dbg_ch1_first_addr_axi_sync  <= dbg_ch1_first_addr_axi_meta;
      dbg_ch1_first_meta_axi_meta  <= dbg_ch1_first_meta_core;
      dbg_ch1_first_meta_axi_sync  <= dbg_ch1_first_meta_axi_meta;
      dbg_ch1_last_addr_axi_meta   <= dbg_ch1_last_addr_core;
      dbg_ch1_last_addr_axi_sync   <= dbg_ch1_last_addr_axi_meta;
      dbg_ch1_last_meta_axi_meta   <= dbg_ch1_last_meta_core;
      dbg_ch1_last_meta_axi_sync   <= dbg_ch1_last_meta_axi_meta;
      dbg_ddr_first_addr_axi_meta  <= dbg_ddr_first_addr_core;
      dbg_ddr_first_addr_axi_sync  <= dbg_ddr_first_addr_axi_meta;
      dbg_ddr_first_meta_axi_meta  <= dbg_ddr_first_meta_core;
      dbg_ddr_first_meta_axi_sync  <= dbg_ddr_first_meta_axi_meta;
      dbg_ddr_last_addr_axi_meta   <= dbg_ddr_last_addr_core;
      dbg_ddr_last_addr_axi_sync   <= dbg_ddr_last_addr_axi_meta;
      dbg_ddr_last_meta_axi_meta   <= dbg_ddr_last_meta_core;
      dbg_ddr_last_meta_axi_sync   <= dbg_ddr_last_meta_axi_meta;
      dbg_axi_ar_first_addr_axi_meta <= dbg_axi_ar_first_addr_core;
      dbg_axi_ar_first_addr_axi_sync <= dbg_axi_ar_first_addr_axi_meta;
      dbg_axi_ar_first_meta_axi_meta <= dbg_axi_ar_first_meta_core;
      dbg_axi_ar_first_meta_axi_sync <= dbg_axi_ar_first_meta_axi_meta;
      dbg_axi_ar_last_addr_axi_meta <= dbg_axi_ar_last_addr_core;
      dbg_axi_ar_last_addr_axi_sync <= dbg_axi_ar_last_addr_axi_meta;
      dbg_axi_ar_last_meta_axi_meta <= dbg_axi_ar_last_meta_core;
      dbg_axi_ar_last_meta_axi_sync <= dbg_axi_ar_last_meta_axi_meta;
      dbg_axi_r_first_addr_axi_meta <= dbg_axi_r_first_addr_core;
      dbg_axi_r_first_addr_axi_sync <= dbg_axi_r_first_addr_axi_meta;
      dbg_axi_r_first_meta_axi_meta <= dbg_axi_r_first_meta_core;
      dbg_axi_r_first_meta_axi_sync <= dbg_axi_r_first_meta_axi_meta;
      dbg_axi_r_last_addr_axi_meta <= dbg_axi_r_last_addr_core;
      dbg_axi_r_last_addr_axi_sync <= dbg_axi_r_last_addr_axi_meta;
      dbg_axi_r_last_meta_axi_meta <= dbg_axi_r_last_meta_core;
      dbg_axi_r_last_meta_axi_sync <= dbg_axi_r_last_meta_axi_meta;
      dbg_done_first_addr_axi_meta <= dbg_done_first_addr_core;
      dbg_done_first_addr_axi_sync <= dbg_done_first_addr_axi_meta;
      dbg_done_first_meta_axi_meta <= dbg_done_first_meta_core;
      dbg_done_first_meta_axi_sync <= dbg_done_first_meta_axi_meta;
      dbg_done_last_addr_axi_meta <= dbg_done_last_addr_core;
      dbg_done_last_addr_axi_sync <= dbg_done_last_addr_axi_meta;
      dbg_done_last_meta_axi_meta <= dbg_done_last_meta_core;
      dbg_done_last_meta_axi_sync <= dbg_done_last_meta_axi_meta;
      fbcap_shadow_req_sync_axi <= {fbcap_shadow_req_sync_axi[1:0], fbcap_shadow_req_tgl_core};
      if (fbcap_shadow_req_sync_axi[2] ^ fbcap_shadow_req_sync_axi[1]) begin
        // Need three destination cycles before commit:
        // 1) sample s1, 2) propagate to s2, 3) commit old s2 (NBA order).
        fbcap_shadow_sample_cnt <= 2'd3;
      end else if (fbcap_shadow_sample_cnt != 2'd0) begin
        fbcap_shadow_seq_axi_s1 <= fbcap_shadow_seq_core;
        fbcap_shadow_seq_axi_s2 <= fbcap_shadow_seq_axi_s1;
        fbcap_shadow_buf_axi_s1 <= fbcap_shadow_buf_core;
        fbcap_shadow_buf_axi_s2 <= fbcap_shadow_buf_axi_s1;
        fbcap_shadow_sample_cnt <= fbcap_shadow_sample_cnt - 2'd1;
        if (fbcap_shadow_sample_cnt == 2'd1) begin
          fbcap_frame_seq_axi_sync <= fbcap_shadow_seq_axi_s2;
          fbcap_frame_buf_idx_axi_sync <= fbcap_shadow_buf_axi_s2;
          fbcap_shadow_ack_tgl_axi <= ~fbcap_shadow_ack_tgl_axi;
        end
      end
      save_status_axi_meta         <= {8'd0, save_eeprom_count_core, save_flash_count_core, save_sram_count_core};
      save_status_axi_sync         <= save_status_axi_meta;
      bios_wr_ack_sync_axi <= {bios_wr_ack_sync_axi[1:0], bios_wr_ack_toggle_core};
      if (bios_wr_ack_sync_axi[2] ^ bios_wr_ack_sync_axi[1]) begin
        bios_wr_ack_seq_axi <= bios_wr_ack_seq_axi + 32'd1;
      end
      fb_frame_pulse_toggle_axi_sync <= {fb_frame_pulse_toggle_axi_sync[1:0], fb_frame_pulse_toggle};
      sys_err_pulse_toggle_axi_sync  <= {sys_err_pulse_toggle_axi_sync[1:0], sys_err_pulse_toggle};
    end
  end

  always @(posedge clk_100) begin
    if (!rst_n) begin
      bios_wr_req_sync_core <= 3'b000;
      bios_wr_ack_toggle_core <= 1'b0;
      bios_wr_addr_core_s1 <= 12'd0;
      bios_wr_addr_core_s2 <= 12'd0;
      bios_wr_data_core_s1 <= 32'd0;
      bios_wr_data_core_s2 <= 32'd0;
      bios_wr_sample_cnt_core <= 2'd0;
      bios_wr_addr_core <= 12'd0;
      bios_wr_data_core <= 32'd0;
      bios_wr_core <= 1'b0;
    end else begin
      bios_wr_core <= 1'b0;
      bios_wr_req_sync_core <= {bios_wr_req_sync_core[1:0], w_axi_bios_wr_req_toggle};
      if (bios_wr_req_sync_core[2] ^ bios_wr_req_sync_core[1]) begin
        bios_wr_sample_cnt_core <= 2'd3;
      end else if (bios_wr_sample_cnt_core != 2'd0) begin
        bios_wr_addr_core_s1 <= w_axi_bios_wr_addr;
        bios_wr_addr_core_s2 <= bios_wr_addr_core_s1;
        bios_wr_data_core_s1 <= w_axi_bios_wr_data;
        bios_wr_data_core_s2 <= bios_wr_data_core_s1;
        bios_wr_sample_cnt_core <= bios_wr_sample_cnt_core - 2'd1;
        if (bios_wr_sample_cnt_core == 2'd1) begin
          bios_wr_addr_core <= bios_wr_addr_core_s2;
          bios_wr_data_core <= bios_wr_data_core_s2;
          bios_wr_core <= 1'b1;
          bios_wr_ack_toggle_core <= ~bios_wr_ack_toggle_core;
        end
      end
    end
  end

  // ==========================================================================
  // --- 第一层：系统控制单元 (System Control Layer) ---
  // ==========================================================================
  // 责任：处理 PS 通信、CDC 同步以及核心配置管理
  
  axi_lite_ctrl_regs u_axi_regs (
    .clk                 (s_axi_aclk),
    .rst_n               (s_axi_aresetn),
    .s_axi_awaddr        (s_axi_awaddr),
    .s_axi_awvalid       (s_axi_awvalid),
    .s_axi_awready       (s_axi_awready),
    .s_axi_wdata         (s_axi_wdata),
    .s_axi_wstrb         (s_axi_wstrb),
    .s_axi_wvalid        (s_axi_wvalid),
    .s_axi_wready        (s_axi_wready),
    .s_axi_bresp         (s_axi_bresp),
    .s_axi_bvalid        (s_axi_bvalid),
    .s_axi_bready        (s_axi_bready),
    .s_axi_araddr        (s_axi_araddr),
    .s_axi_arvalid       (s_axi_arvalid),
    .s_axi_arready       (s_axi_arready),
    .s_axi_rdata         (s_axi_rdata),
    .s_axi_rresp         (s_axi_rresp),
    .s_axi_rvalid        (s_axi_rvalid),
    .s_axi_rready        (s_axi_rready),
    .stat_cycles_missing (cycles_missing_axi_sync),
    .stat_cycles_vsync_speed(cycles_vsync_speed_axi_sync),
    .stat_fb_frame_idx   (fb_frame_idx_axi_sync),
    .stat_physical_keys  (physical_keys_axi),
    .stat_debug_cpu_pc   (debug_cpu_pc_axi),
    .stat_debug_cpu_mixed(debug_cpu_mixed_axi),
    .stat_debug_irq      (debug_irq_axi),
    .stat_debug_dma      (debug_dma_axi),
    .stat_debug_mem      (debug_mem_axi),
    .stat_dbg_chain_flags(dbg_chain_flags_axi),
    .stat_dbg_chain_counts0(dbg_chain_counts0_axi),
    .stat_dbg_chain_counts1(dbg_chain_counts1_axi),
    .stat_dbg_ch1_first_addr(dbg_ch1_first_addr_axi),
    .stat_dbg_ch1_first_meta(dbg_ch1_first_meta_axi),
    .stat_dbg_ch1_last_addr(dbg_ch1_last_addr_axi),
    .stat_dbg_ch1_last_meta(dbg_ch1_last_meta_axi),
    .stat_dbg_ddr_first_addr(dbg_ddr_first_addr_axi),
    .stat_dbg_ddr_first_meta(dbg_ddr_first_meta_axi),
    .stat_dbg_ddr_last_addr(dbg_ddr_last_addr_axi),
    .stat_dbg_ddr_last_meta(dbg_ddr_last_meta_axi),
    .stat_dbg_axi_ar_first_addr(dbg_axi_ar_first_addr_axi),
    .stat_dbg_axi_ar_first_meta(dbg_axi_ar_first_meta_axi),
    .stat_dbg_axi_ar_last_addr(dbg_axi_ar_last_addr_axi),
    .stat_dbg_axi_ar_last_meta(dbg_axi_ar_last_meta_axi),
    .stat_dbg_axi_r_first_addr(dbg_axi_r_first_addr_axi),
    .stat_dbg_axi_r_first_meta(dbg_axi_r_first_meta_axi),
    .stat_dbg_axi_r_last_addr(dbg_axi_r_last_addr_axi),
    .stat_dbg_axi_r_last_meta(dbg_axi_r_last_meta_axi),
    .stat_dbg_done_first_addr(dbg_done_first_addr_axi),
    .stat_dbg_done_first_meta(dbg_done_first_meta_axi),
    .stat_dbg_done_last_addr(dbg_done_last_addr_axi),
    .stat_dbg_done_last_meta(dbg_done_last_meta_axi),
    .sys_rom_loading     (sys_rom_loading_axi), // 使用控制寄存器位 8 标识加载状态
    .sys_error_in        (sys_err_vec_axi_sync),
    .irq_vsync_pulse     (irq_vsync_pulse_axi),
    .irq_error_pulse     (irq_error_pulse_axi),
    .irq_out             (irq),
    .cfg_ctrl            (w_axi_cfg_ctrl),
    .cfg_keys            (w_axi_cfg_keys),
    .cfg_max_pak_addr    (w_axi_cfg_max_pak_addr),
    .cfg_cycle_precalc   (w_axi_cfg_cycle_precalc),
    .cfg_rtc_timestamp   (w_axi_cfg_rtc_timestamp),
    .cfg_display_frame_idx(w_axi_display_frame_idx),
    .cfg_sw_reset        (w_axi_cfg_sw_reset),
    .cfg_commit_toggle   (w_axi_cfg_commit_toggle),
    .stat_fbcap_frame_seq(fbcap_frame_seq_axi_sync),
    .stat_fbcap_frame_buf_idx(fbcap_frame_buf_idx_axi_sync),
    .stat_save_status    (save_status_axi),
    .cfg_bios_wr_addr    (w_axi_bios_wr_addr),
    .cfg_bios_wr_data    (w_axi_bios_wr_data),
    .cfg_bios_wr_req_toggle(w_axi_bios_wr_req_toggle),
    .stat_bios_wr_ack_seq(bios_wr_ack_seq_axi)
  );

  gba_config_mgr u_config_mgr (
    .clk_core              (clk_100),
    .rst_n                 (rst_n),
    .cfg_ctrl_axi          (w_axi_cfg_ctrl),
    .cfg_keys_axi          (w_axi_cfg_keys),
    .cfg_max_pak_addr_axi  (w_axi_cfg_max_pak_addr),
    .cfg_cycle_precalc_axi (w_axi_cfg_cycle_precalc),
    .cfg_rtc_timestamp_axi (w_axi_cfg_rtc_timestamp),
    .cfg_commit_toggle_axi (w_axi_cfg_commit_toggle),
    .btns                  (btns),
    .sws                   (sws),
    .cfg_ctrl_core         (w_core_cfg_ctrl),
    .cfg_keys_core         (w_core_cfg_keys),
    .cfg_max_pak_addr_core (w_core_cfg_max_pak_addr),
    .cfg_cycle_precalc_core(w_core_cfg_cycle_precalc),
    .cfg_rtc_timestamp_core(w_core_cfg_rtc_timestamp)
  );

  // ==========================================================================
  // --- 第二层：音频处理单元 (Audio Unit) ---
  // ==========================================================================
  // 责任：将核心 PCM 流转换为串行 I2S 协议
  
  i2s_transmitter u_audio_out (
    .clk_100               (clk_100),
    .rst_n                 (rst_n),
    .audio_l               (audio_out_l),
    .audio_r               (audio_out_r),
    .ac_mclk               (ac_mclk),
    .ac_bclk               (ac_bclk),
    .ac_pblrc              (ac_pblrc),
    .ac_pbdat              (ac_pbdat),
    .ac_muten              (ac_muten),
    .sample_ce             (audio_sample_ce)
  );

  // 系统统计/指示灯映射
  assign leds[0] = audio_out_l[15];      // 播放活动指示
  assign leds[1] = ddram_busy;           // 内存活动指示
  assign leds[2] = |{audio_out_l, audio_out_r};  // 音频信号检测
  assign leds[3] = w_core_cfg_sw_reset;  // 软复位状态指示

  // ==========================================================================
  // --- 第三层：GBA 核心与内存中枢 (Core & Memory Hub) ---
  // ==========================================================================
  // 责任：运行 GBA 核心逻辑并管理其复杂的 DDR 访问路径
  
  gba_top #(
    .Softmap_GBA_FLASH_ADDR   (0),
    .Softmap_GBA_EEPROM_ADDR  (32768),
    .Softmap_GBA_WRam_ADDR    (131072),
    .Softmap_GBA_Gamerom_ADDR (196608),
    .Softmap_SaveState_ADDR   (58720256),
    .Softmap_Rewind_ADDR      (33554432),
    .turbosound               (1'b0)
  ) u_core (
    .clk100                (clk_100),
    .GBA_on                (w_core_cfg_ctrl[0] & ~w_core_cfg_sw_reset), // 核心使能受软复位控制
    .GBA_lockspeed         (w_core_cfg_ctrl[1]),
    .GBA_cputurbo          (w_core_cfg_ctrl[2]),
    .GBA_flash_1m          (w_core_cfg_ctrl[9]),
    .CyclePrecalc          (w_core_cfg_cycle_precalc),
    .Underclock            (GBA_UNDERCLOCK_OFF),
    .MaxPakAddr            (w_core_cfg_max_pak_addr),
    .CyclesMissing         (cycles_missing),
    .CyclesVsyncSpeed      (cycles_vsync_speed),
    .SramFlashEnable       (w_core_cfg_ctrl[4]),
    .Sram32KMirrorTest     (w_core_cfg_ctrl[13]),
    .memory_remap          (w_core_cfg_ctrl[5]),
    .increaseSSHeaderCount (1'b0),
    .save_state            (1'b0),
    .load_state            (1'b0),
    .interframe_blend      (GBA_INTERFRAME_BLEND_OFF),
    .maxpixels             (1'b0),
    .shade_mode            (GBA_SHADE_MODE_OFF),
    .hdmode2x_bg           (1'b0),
    .hdmode2x_obj          (1'b0),
    .specialmodule         (w_core_cfg_ctrl[10]),
    .solar_in              (3'b000),
    .tilt                  (w_core_cfg_ctrl[11]),
    .rewind_on             (1'b0),
    .rewind_active         (1'b0),
    .savestate_number      (0),
    .RTC_timestampNew      (1'b0),
    .RTC_timestampIn       (w_core_cfg_rtc_timestamp),
    .RTC_timestampSaved    (32'd0),
    .RTC_savedtimeIn       (42'd0),
    .RTC_saveLoaded        (1'b1),
    .RTC_timestampOut      (unused_rtc_timestamp_out),
    .RTC_savedtimeOut      (unused_rtc_savedtime_out),
    .RTC_inuse             (unused_rtc_inuse),
    .cheat_clear           (1'b0),
    .cheats_enabled        (1'b0),
    .cheat_on              (1'b0),
    .cheat_in              (128'd0),
    .cheats_active         (unused_cheats_active),

    // SDRAM 通道 1 (Core Instruction/Data Fetch)
    .sdram_read_ena        (sdram_read_ena),
    .sdram_read_done       (sdram_read_done),
    .sdram_read_addr       (sdram_read_addr),
    .sdram_read_data       (sdram_read_data),
    .sdram_second_dword    (sdram_second_dword),

    // 总线通道 2 (Generic Bus)
    .bus_out_Din           (bus_out_din),
    .bus_out_Dout          (bus_out_dout),
    .bus_out_Adr           (bus_out_adr),
    .bus_out_rnw           (bus_out_rnw),
    .bus_out_ena           (bus_out_ena),
    .bus_out_done          (bus_out_done),

    // 保存通道 4 (Save States / SRAM)
    .SAVE_out_Din          (save_out_din),
    .SAVE_out_Dout         (save_out_dout),
    .SAVE_out_Adr          (save_out_adr),
    .SAVE_out_rnw          (save_out_rnw),
    .SAVE_out_ena          (save_out_ena),
    .SAVE_out_active       (save_out_active),
    .SAVE_out_be           (save_out_be),
    .SAVE_out_done         (save_out_done),

    .bios_wraddr           (bios_wr_addr_core),
    .bios_wrdata           (bios_wr_data_core),
    .bios_wr               (bios_wr_core),
    .save_eeprom           (unused_save_eeprom),
    .save_sram             (unused_save_sram),
    .save_flash            (unused_save_flash),
    .load_done             (unused_load_done),

    // 输入映射
    .KeyA                  (w_core_cfg_keys[0]),
    .KeyB                  (w_core_cfg_keys[1]),
    .KeySelect             (w_core_cfg_keys[2]),
    .KeyStart              (w_core_cfg_keys[3]),
    .KeyRight              (w_core_cfg_keys[4]),
    .KeyLeft               (w_core_cfg_keys[5]),
    .KeyUp                 (w_core_cfg_keys[6]),
    .KeyDown               (w_core_cfg_keys[7]),
    .KeyR                  (w_core_cfg_keys[8]),
    .KeyL                  (w_core_cfg_keys[9]),
    .AnalogTiltX           (8'sd0),
    .AnalogTiltY           (8'sd0),
    .Rumble                (unused_rumble),
    .GBA_BusAddr           (28'd0),
    .GBA_BusRnW            (1'b0),
    .GBA_BusACC            (2'b00),
    .GBA_BusWriteData      (32'd0),
    .GBA_BusReadData       (unused_bus_read_data),
    .GBA_Bus_written       (1'b0),
    .pixel_out_x           (),
    .pixel_out_y           (),
    .pixel_out_addr        (core_pixel_addr),
    .pixel_out_data        (core_pixel_data),
    .pixel_out_we          (core_pixel_we),

    // 复用 core 内部原生的 largeimg 打包链路，让 framebuffer 写入
    // 与上游 MiSTer 参考实现保持一致。
    .largeimg_out_base     (unused_core_fb_base),
    .largeimg_out_addr     (unused_core_fb_addr),
    .largeimg_out_data     (unused_core_fb_data),
    .largeimg_out_req      (unused_core_fb_req),
    .largeimg_out_done     (core_fb_done),
    .largeimg_newframe     (core_fb_newframe),
    .largeimg_singlebuf    (1'b0),

    // 音频流输出
    .sound_out_left        (core_audio_l),
    .sound_out_right       (core_audio_r),
    .debug_cpu_pc          (unused_debug_cpu_pc),
    .debug_cpu_mixed       (unused_debug_cpu_mixed),
    .debug_irq             (unused_debug_irq),
    .debug_dma             (unused_debug_dma),
    .debug_mem             (unused_debug_mem),
    .debug_internal         (core_debug_internal)
  );

  gba_frame_capture_bram u_fb_capture (
    .wr_clk            (clk_100),
    .wr_rst_n          (rst_n),
    .pixel_addr        (core_pixel_addr),
    .pixel_data        (core_pixel_data),
    .pixel_we          (core_pixel_we),
    .frame_seq         (fbcap_frame_seq),
    .frame_buf_idx     (fbcap_frame_buf_idx),
    .bram_clk_b        (fb_cap_bram_clk),
    .bram_rst_b        (fb_cap_bram_rst),
    .bram_en_b         (fb_cap_bram_en),
    .bram_we_b         (fb_cap_bram_we),
    .bram_addr_b       (fb_cap_bram_addr),
    .bram_din_b        (fb_cap_bram_din),
    .bram_dout_b       (fb_cap_bram_dout)
  );

  // 内存仲裁与总线打包
  assign ch1_addr = {1'b0, sdram_read_addr, 1'b0};
  assign ch1_din  = 64'd0;
  assign ch1_req  = sdram_read_ena;
  assign ch1_rnw  = 1'b1;

  assign ch2_addr = {bus_out_adr, 1'b0};
  assign ch2_din  = {32'd0, bus_out_din};
  assign ch2_req  = bus_out_ena;
  assign ch2_rnw  = bus_out_rnw;

  assign ch3_addr = 24'd0;
  assign ch3_din  = 16'd0;
  assign ch3_req  = 1'b0;
  assign ch3_rnw  = 1'b1;

  assign ch4_addr = {save_out_adr, 1'b0};
  assign ch4_din  = save_out_din;
  assign ch4_req  = save_out_ena;
  assign ch4_rnw  = save_out_rnw;
  assign ch4_be   = save_out_be;

  ddram_mux u_ddram_mux (
    .DDRAM_CLK        (clk_100),
    .DDRAM_BUSY       (ddram_busy),
    .DDRAM_BURSTCNT   (ddram_burstcnt),
    .DDRAM_ADDR       (ddram_addr),
    .DDRAM_DOUT       (ddram_dout),
    .DDRAM_DOUT_READY (ddram_dout_ready),
    .DDRAM_RD         (ddram_rd),
    .DDRAM_DIN        (ddram_din),
    .DDRAM_BE         (ddram_be),
    .DDRAM_WE         (ddram_we),
    .ch1_addr(ch1_addr), .ch1_dout(ch1_dout), .ch1_din(ch1_din[15:0]), .ch1_req(ch1_req), .ch1_rnw(ch1_rnw), .ch1_ready(sdram_read_done),
    .ch2_addr(ch2_addr), .ch2_dout(ch2_dout),     .ch2_din(ch2_din[31:0]), .ch2_req(ch2_req), .ch2_rnw(ch2_rnw), .ch2_ready(bus_out_done),
    .ch3_addr(ch3_addr), .ch3_dout(ch3_dout_unused), .ch3_din(ch3_din), .ch3_req(ch3_req), .ch3_rnw(ch3_rnw), .ch3_ready(ch3_ready_unused),
    .ch4_addr(ch4_addr), .ch4_dout(ch4_dout), .ch4_din(ch4_din), .ch4_req(ch4_req), .ch4_rnw(ch4_rnw), .ch4_be(ch4_be), .ch4_ready(save_out_done),
    .ch5_addr(27'd0), .ch5_dout(), .ch5_din(64'd0), .ch5_req(1'b0), .ch5_rnw(1'b1), .ch5_ready()
  );

  assign sdram_read_data = ch1_dout[31:0];
  assign sdram_second_dword = ch1_dout[63:32];
  assign bus_out_dout = ch2_dout[31:0];
  assign save_out_dout = ch4_dout;

  ddr_axi_backend_sv #(
    .G_DDR_BASE (G_DDR_BASE)
  ) u_backend (
    .clk(clk_100), .rst_n(rst_n),
    .DDRAM_BUSY(ddram_busy), .DDRAM_BURSTCNT(ddram_burstcnt), .DDRAM_ADDR(ddram_addr),
    .DDRAM_DOUT(ddram_dout), .DDRAM_DOUT_READY(ddram_dout_ready), .DDRAM_RD(ddram_rd),
    .DDRAM_DIN(ddram_din), .DDRAM_BE(ddram_be), .DDRAM_WE(ddram_we),
    .M_AXI_AWADDR(M_AXI_AWADDR), .M_AXI_AWLEN(M_AXI_AWLEN), .M_AXI_AWSIZE(M_AXI_AWSIZE), .M_AXI_AWBURST(M_AXI_AWBURST),
    .M_AXI_AWVALID(M_AXI_AWVALID), .M_AXI_AWREADY(M_AXI_AWREADY), .M_AXI_WDATA(M_AXI_WDATA), .M_AXI_WSTRB(M_AXI_WSTRB),
    .M_AXI_WLAST(M_AXI_WLAST), .M_AXI_WVALID(M_AXI_WVALID), .M_AXI_WREADY(M_AXI_WREADY), .M_AXI_BRESP(M_AXI_BRESP),
    .M_AXI_BVALID(M_AXI_BVALID), .M_AXI_BREADY(M_AXI_BREADY), .M_AXI_ARADDR(M_AXI_ARADDR), .M_AXI_ARLEN(M_AXI_ARLEN),
    .M_AXI_ARSIZE(M_AXI_ARSIZE), .M_AXI_ARBURST(M_AXI_ARBURST), .M_AXI_ARVALID(M_AXI_ARVALID), .M_AXI_ARREADY(M_AXI_ARREADY),
    .M_AXI_RDATA(M_AXI_RDATA), .M_AXI_RRESP(M_AXI_RRESP), .M_AXI_RLAST(M_AXI_RLAST), .M_AXI_RVALID(M_AXI_RVALID), .M_AXI_RREADY(M_AXI_RREADY),
    .ERR_VEC(sys_err_vec_w), .ERR_PULSE(sys_err_pulse_w)
  );

  assign fb_frame_pulse = core_fb_newframe;
  assign core_fb_done = 1'b1;

endmodule
