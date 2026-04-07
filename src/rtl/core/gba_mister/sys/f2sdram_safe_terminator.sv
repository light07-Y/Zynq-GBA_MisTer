// ============================================================================
//
//                f2sdram_safe_terminator for MiSTer platform
//
// ============================================================================
// Copyright (c) 2021 bellwood420
//
// Background:
//   Terminating a transaction of burst writing(/reading) in its midstream
//   seems to cause an illegal state to f2sdram interface.
//
// Purpose:
//   Insert this module into the bus line between
//   f2sdram (Avalon-MM slave) and user logic (Avalon-MM master).
//
// ============================================================================

module f2sdram_safe_terminator #(
  parameter     DATA_WIDTH = 64,
  parameter     BURSTCOUNT_WIDTH = 8
) (
	input         clk,
	input         rst_req_sync,

	// Master port: connecting to Alavon-MM slave(f2sdram)
	input                               waitrequest_master,
	output logic [BURSTCOUNT_WIDTH-1:0] burstcount_master,
	output logic [31-$clog2(64/8)-1:0]  address_master,
	input        [DATA_WIDTH-1:0]       readdata_master,
	input                               readdatavalid_master,
	output logic                        read_master,
	output       [DATA_WIDTH-1:0]       writedata_master,
	output logic [(DATA_WIDTH/8)-1:0]   byteenable_master,
	output logic                        write_master,

	// Slave port: connecting to Alavon-MM master(user logic)
	output                              waitrequest_slave,
	input  [BURSTCOUNT_WIDTH-1:0]       burstcount_slave,
	input  [31-$clog2(64/8)-1:0]        address_slave,
	output [DATA_WIDTH-1:0]             readdata_slave,
	output                              readdatavalid_slave,
	input                               read_slave,
	input  [DATA_WIDTH-1:0]             writedata_slave,
	input  [(DATA_WIDTH/8)-1:0]         byteenable_slave,
	input                               write_slave
);

localparam BYTEENABLE_WIDTH = DATA_WIDTH/8;
localparam ADDRESS_WITDH    = 32-$clog2(BYTEENABLE_WIDTH);

reg init_reset_deasserted = 1'b0;

always_ff @(posedge clk) begin
	if (!rst_req_sync) begin
		init_reset_deasserted <= 1'b1;
	end
end

reg lock_stage = 1'b0;

always_ff @(posedge clk) begin
	if (rst_req_sync) begin
		if (init_reset_deasserted) begin
			lock_stage <= 1'b1;
		end
	end
	else begin
		lock_stage <= 1'b0;
	end
end

reg  state_write = 1'b0;
logic next_state_write;

wire burst_write_start     = !state_write  && next_state_write;
wire valid_write_data      = state_write && !waitrequest_master;
wire burst_write_end       = state_write && (write_burstcounter == write_burstcount_latch - 1'd1);
wire valid_non_burst_write = !state_write && write_slave && (burstcount_slave == 1) && !waitrequest_master;

reg [BURSTCOUNT_WIDTH-1:0] write_burstcounter     = 0;
reg [BURSTCOUNT_WIDTH-1:0] write_burstcount_latch = 0;
reg [ADDRESS_WITDH-1:0]    write_address_latch    = 0;

always_ff @(posedge clk) begin
	state_write <= next_state_write;

	if (burst_write_start) begin
		write_burstcounter     <= waitrequest_master ? 1'd0 : 1'd1;
		write_burstcount_latch <= burstcount_slave;
		write_address_latch    <= address_slave;
	end
	else if (valid_write_data) begin
		write_burstcounter     <= write_burstcounter + 1'd1;
	end
end

always_comb begin
	if (!state_write) begin
		if (valid_non_burst_write)
			next_state_write = 1'b0;
		else if (write_slave)
			next_state_write = 1'b1;
		else
			next_state_write = 1'b0;
	end
	else begin
		if (burst_write_end)
			next_state_write = 1'b0;
		else
			next_state_write = 1'b1;
	end
end

reg [BURSTCOUNT_WIDTH-1:0] write_terminate_counter = 0;
reg [BURSTCOUNT_WIDTH-1:0] burstcount_latch        = 0;
reg [ADDRESS_WITDH-1:0]    address_latch           = 0;

reg terminating       = 0;
reg read_terminating  = 0;
reg write_terminating = 0;

wire on_write_transaction       =  state_write && next_state_write;
wire on_start_write_transaction = !state_write && next_state_write;

always_ff @(posedge clk) begin
	if (rst_req_sync) begin
		if (init_reset_deasserted) begin
			if (!lock_stage) begin
				burstcount_latch              <= burstcount_slave;
				address_latch                 <= address_slave;
				terminating                   <= 1;

				if (on_write_transaction) begin
					write_terminating          <= 1;
					burstcount_latch           <= write_burstcount_latch;
					address_latch              <= write_address_latch;
					write_terminate_counter    <= waitrequest_master ? write_burstcounter : write_burstcounter + 1'd1;
				end
				else if (on_start_write_transaction) begin
					if (!valid_non_burst_write) begin
						write_terminating       <= 1;
						write_terminate_counter <= waitrequest_master ? 1'd0 : 1'd1;
					end
				end
				else if (read_slave && waitrequest_master) begin
					read_terminating           <= 1;
				end
			end
			else if (!waitrequest_master) begin
				read_terminating              <= 0;
			end
		end
	end
	else begin
		if (!write_terminating) terminating <= 0;
		read_terminating  <= 0;
	end

	if (write_terminating) begin
		if (!waitrequest_master) write_terminate_counter <= write_terminate_counter + 1'd1;
		if (write_terminate_counter == burstcount_latch - 1'd1) write_terminating <= 0;
	end
end

always_comb begin
	if (terminating) begin
		burstcount_master = burstcount_latch;
		address_master    = address_latch;
		read_master       = read_terminating;
		write_master      = write_terminating;
		byteenable_master = 0;
	end
	else begin
		burstcount_master = burstcount_slave;
		address_master    = address_slave;
		read_master       = read_slave;
		byteenable_master = byteenable_slave;
		write_master      = write_slave;
	end
end

assign writedata_master    = writedata_slave;
assign readdata_slave      = readdata_master;
assign readdatavalid_slave = readdatavalid_master;
assign waitrequest_slave   = waitrequest_master;

endmodule
