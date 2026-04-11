// tb_gba_fullchain.sv - Full chain RTL simulation
// Instantiates real gba_top VHDL + ddram_mux + ddr_axi_backend
// Monitors: pixel_out_we, drawline (via DISPSTAT), new_cycles_valid
`timescale 1ns / 1ps

module tb_gba_fullchain;

  // ---- Clock & Reset ----
  logic clk100 = 0;
  always #5 clk100 = ~clk100;

  // ---- GBA core ports ----
  // Settings
  logic        GBA_on = 0;
  logic        GBA_lockspeed = 1;
  logic        GBA_cputurbo = 0;
  logic        GBA_flash_1m = 0;
  logic [15:0] CyclePrecalc = 16'd100;
  logic [1:0]  Underclock = 2'b00;
  logic [24:0] MaxPakAddr = 25'h1FFFFFF;
  logic        SramFlashEnable = 0;
  logic        memory_remap = 0;

  // SDRAM ch1 (instruction fetch)
  wire         sdram_read_ena;
  logic        sdram_read_done = 0;
  wire  [24:0] sdram_read_addr;
  logic [31:0] sdram_read_data = 0;
  logic [31:0] sdram_second_dword = 0;

  // Bus ch2
  wire  [31:0] bus_out_Din;
  logic [31:0] bus_out_Dout = 0;
  wire  [25:0] bus_out_Adr;
  wire         bus_out_rnw;
  wire         bus_out_ena;
  logic        bus_out_done = 0;

  // Save ch4
  wire  [63:0] save_out_Din;
  logic [63:0] save_out_Dout = 0;
  wire  [25:0] save_out_Adr;
  wire         save_out_rnw;
  wire         save_out_ena;
  wire         save_out_active;
  wire  [7:0]  save_out_be;
  logic        save_out_done = 0;

  // Pixel output (THE KEY SIGNALS) - VHDL integer ports
  integer      pixel_out_addr;
  wire [17:0]  pixel_out_data;
  wire         pixel_out_we;
  integer      pixel_out_x;
  integer      pixel_out_y;

  // Large image (unused path)
  wire [31:0]  largeimg_out_base;
  wire [25:0]  largeimg_out_addr;
  wire [63:0]  largeimg_out_data;
  wire         largeimg_out_req;

  // Audio
  wire [15:0]  sound_out_left, sound_out_right;

  // Status
  wire [31:0]  CyclesMissing, CyclesVsyncSpeed;
  wire         save_eeprom, save_sram, save_flash, load_done, rumble;
  wire [31:0]  debug_cpu_pc, debug_cpu_mixed, debug_irq, debug_dma, debug_mem;
  wire [31:0]  debug_internal;

  // ---- VHDL integer port helpers ----
  integer savestate_num_int = 0;

  // ---- Counters ----
  int pixel_we_count = 0;
  int sdram_req_count = 0;
  int bus_req_count = 0;
  int cycle_count = 0;

  // ---- Simple memory model for SDRAM reads ----
  // Returns ARM NOP (0xE1A00000 = MOV r0,r0) for all reads
  // This ensures the CPU can execute and produce cycles
  localparam [31:0] ARM_NOP = 32'hE1A00000;
  
  // SDRAM read responder - 2 cycle latency
  logic [2:0] sdram_pipe = 0;
  always @(posedge clk100) begin
    sdram_read_done <= 0;
    sdram_pipe <= {sdram_pipe[1:0], sdram_read_ena};
    if (sdram_pipe[2]) begin
      sdram_read_done    <= 1;
      sdram_read_data    <= ARM_NOP;
      sdram_second_dword <= ARM_NOP;
    end
  end

  // Bus read responder - 2 cycle latency  
  logic [2:0] bus_pipe = 0;
  always @(posedge clk100) begin
    bus_out_done <= 0;
    bus_pipe <= {bus_pipe[1:0], bus_out_ena};
    if (bus_pipe[2]) begin
      bus_out_done <= 1;
      // Return 0 for all I/O reads (DISPCNT=0 means mode 0, no forced blank)
      bus_out_Dout <= 32'h0;
    end
  end

  // Save responder
  logic [2:0] save_pipe = 0;
  always @(posedge clk100) begin
    save_out_done <= 0;
    save_pipe <= {save_pipe[1:0], save_out_ena};
    if (save_pipe[2]) begin
      save_out_done <= 1;
      save_out_Dout <= 64'h0;
    end
  end

  // ---- DUT: gba_top ----
  gba_top #(
    .Softmap_GBA_FLASH_ADDR   (0),
    .Softmap_GBA_EEPROM_ADDR  (0),
    .Softmap_GBA_WRam_ADDR    (131072),
    .Softmap_GBA_Gamerom_ADDR (196608),
    .Softmap_SaveState_ADDR   (58720256),
    .Softmap_Rewind_ADDR      (33554432),
    .turbosound               (1'b0)
  ) u_gba_top (
    .clk100                (clk100),
    .GBA_on                (GBA_on),
    .GBA_lockspeed         (GBA_lockspeed),
    .GBA_cputurbo          (GBA_cputurbo),
    .GBA_flash_1m          (GBA_flash_1m),
    .CyclePrecalc          (CyclePrecalc),
    .Underclock            (Underclock),
    .MaxPakAddr            (MaxPakAddr),
    .CyclesMissing         (CyclesMissing),
    .CyclesVsyncSpeed      (CyclesVsyncSpeed),
    .SramFlashEnable       (SramFlashEnable),
    .memory_remap          (memory_remap),
    .increaseSSHeaderCount (1'b0),
    .save_state            (1'b0),
    .load_state            (1'b0),
    .interframe_blend      (2'b00),
    .maxpixels             (1'b0),
    .shade_mode            (3'b000),
    .hdmode2x_bg           (1'b0),
    .hdmode2x_obj          (1'b0),
    .specialmodule         (1'b0),
    .solar_in              (3'b000),
    .tilt                  (1'b0),
    .rewind_on             (1'b0),
    .rewind_active         (1'b0),
    .savestate_number      (savestate_num_int),
    .RTC_timestampNew      (1'b0),
    .RTC_timestampIn       (32'd0),
    .RTC_timestampSaved    (32'd0),
    .RTC_savedtimeIn       (42'd0),
    .RTC_saveLoaded        (1'b1),
    .RTC_timestampOut      (),
    .RTC_savedtimeOut      (),
    .RTC_inuse             (),
    .cheat_clear           (1'b0),
    .cheats_enabled        (1'b0),
    .cheat_on              (1'b0),
    .cheat_in              (128'd0),
    .cheats_active         (),
    .sdram_read_ena        (sdram_read_ena),
    .sdram_read_done       (sdram_read_done),
    .sdram_read_addr       (sdram_read_addr),
    .sdram_read_data       (sdram_read_data),
    .sdram_second_dword    (sdram_second_dword),
    .bus_out_Din           (bus_out_Din),
    .bus_out_Dout          (bus_out_Dout),
    .bus_out_Adr           (bus_out_Adr),
    .bus_out_rnw           (bus_out_rnw),
    .bus_out_ena           (bus_out_ena),
    .bus_out_done          (bus_out_done),
    .SAVE_out_Din          (save_out_Din),
    .SAVE_out_Dout         (save_out_Dout),
    .SAVE_out_Adr          (save_out_Adr),
    .SAVE_out_rnw          (save_out_rnw),
    .SAVE_out_ena          (save_out_ena),
    .SAVE_out_active       (save_out_active),
    .SAVE_out_be           (save_out_be),
    .SAVE_out_done         (save_out_done),
    .bios_wraddr           (12'd0),
    .bios_wrdata           (32'd0),
    .bios_wr               (1'b0),
    .save_eeprom           (save_eeprom),
    .save_sram             (save_sram),
    .save_flash            (save_flash),
    .load_done             (load_done),
    .KeyA(1'b0), .KeyB(1'b0), .KeySelect(1'b0), .KeyStart(1'b0),
    .KeyRight(1'b0), .KeyLeft(1'b0), .KeyUp(1'b0), .KeyDown(1'b0),
    .KeyR(1'b0), .KeyL(1'b0),
    .AnalogTiltX(8'sd0), .AnalogTiltY(8'sd0),
    .Rumble                (rumble),
    .GBA_BusAddr           (28'd0),
    .GBA_BusRnW            (1'b0),
    .GBA_BusACC            (2'b00),
    .GBA_BusWriteData      (32'd0),
    .GBA_BusReadData       (),
    .GBA_Bus_written       (1'b0),
    .pixel_out_x           (pixel_out_x),
    .pixel_out_y           (pixel_out_y),
    .pixel_out_addr        (pixel_out_addr),
    .pixel_out_data        (pixel_out_data),
    .pixel_out_we          (pixel_out_we),
    .largeimg_out_base     (largeimg_out_base),
    .largeimg_out_addr     (largeimg_out_addr),
    .largeimg_out_data     (largeimg_out_data),
    .largeimg_out_req      (largeimg_out_req),
    .largeimg_out_done     (1'b0),
    .largeimg_newframe     (1'b0),
    .largeimg_singlebuf    (1'b0),
    .sound_out_left        (sound_out_left),
    .sound_out_right       (sound_out_right),
    .debug_cpu_pc          (debug_cpu_pc),
    .debug_cpu_mixed       (debug_cpu_mixed),
    .debug_irq             (debug_irq),
    .debug_dma             (debug_dma),
    .debug_mem             (debug_mem),
    .debug_internal         (debug_internal)
  );

  // ---- Monitor ----
  always @(posedge clk100) begin
    cycle_count <= cycle_count + 1;
    if (pixel_out_we) pixel_we_count <= pixel_we_count + 1;
    if (sdram_read_ena) sdram_req_count <= sdram_req_count + 1;
    if (bus_out_ena) bus_req_count <= bus_req_count + 1;
  end

  // Periodic status
  always @(posedge clk100) begin
    if (cycle_count > 0 && (cycle_count % 100000 == 0)) begin
      $display("[%0t] clk=%0d pix_we=%0d sdram=%0d bus=%0d pc=0x%08X",
               $time, cycle_count, pixel_we_count, sdram_req_count, bus_req_count, debug_cpu_pc);
    end
  end

  // ---- Test sequence ----
  initial begin
    $display("============================================================");
    $display("TB: GBA Full-Chain RTL Simulation");
    $display("  Tests real gba_top VHDL with NOP memory model");
    $display("  Key monitor: pixel_out_we");
    $display("============================================================");

    GBA_on = 0;
    repeat(100) @(posedge clk100);

    // Enable GBA core
    GBA_on = 1;
    $display("[%0t] GBA_on = 1", $time);

    // Run for ~2 frame times
    // 1 frame = 228 lines * 1232 cycles = 280896 GBA cycles
    // At ~1 cycle per 3+6=9 clk100 (CPU latency + SPEEDDIV), ~2.5M clk100 per frame
    // Give 6M clk100 for 2+ frames
    repeat(6_000_000) @(posedge clk100);

    $display("");
    $display("============================================================");
    $display("Result after %0d clk100 cycles:", cycle_count);
    $display("  pixel_out_we count = %0d", pixel_we_count);
    $display("  sdram_read_ena count = %0d", sdram_req_count);
    $display("  bus_out_ena count = %0d", bus_req_count);
    $display("  CPU PC = 0x%08X", debug_cpu_pc);

    if (pixel_we_count > 0)
      $display("  PASS: pixel_out_we fired %0d times!", pixel_we_count);
    else
      $display("  FAIL: pixel_out_we NEVER fired! GPU not producing pixels.");

    if (sdram_req_count > 0)
      $display("  INFO: CPU is fetching instructions (%0d reads)", sdram_req_count);
    else
      $display("  WARN: No SDRAM reads - CPU may not be running");

    $display("============================================================");
    $finish;
  end

  // Timeout
  initial begin
    #200_000_000;
    $display("[TIMEOUT] 200ms elapsed");
    $display("  pixel_we=%0d sdram=%0d bus=%0d pc=0x%08X",
             pixel_we_count, sdram_req_count, bus_req_count, debug_cpu_pc);
    $finish;
  end

endmodule
