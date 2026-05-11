module debounce
(
	input clk,
	input bin,
	output bout
);

localparam Sreleased = 2'b00, Sdeb0 = 2'b01, Sdeb1 = 2'b10;

reg [14:0] debounceCnt = 0;
reg [1:0] buttonState = Sreleased;

assign bout = (buttonState==Sdeb0);

always @(posedge clk)
	case (buttonState)
		Sreleased : begin
				if(bin==0) begin
					buttonState <= Sdeb0;
					debounceCnt = 15'b111111111111111;
				end
			end
		Sdeb0: begin
				if(debounceCnt==0) begin
					if (bin==1) begin
						buttonState <= Sdeb1;
						debounceCnt = 15'b111111111111111;
					end
				end
				else
				begin
					debounceCnt <= debounceCnt - 1;
				end
			end
		Sdeb1: begin
				if(debounceCnt==0) begin
					if (bin==0) begin
						buttonState <= Sreleased;
					end
				end
				else
				begin
					debounceCnt <= debounceCnt - 1;
				end
			end		
	endcase

endmodule
