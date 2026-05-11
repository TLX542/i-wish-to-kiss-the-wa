module board_test (
    input sys_clk,          // clk input
    input sys_rst_n,        // reset input
    output [5:0] bled,  // 6 board LEDS pin
    input [3:0] sw,
    input [3:0] btn,
    output [7:0] led,
    output reg [7:0] seven,
    output reg [3:0] segment
);

reg [6:0]  hex2seven[15:0];
reg [15:0] dispCounter; // = 0;
reg [1:0]  segCounter; //  = 0;
reg [24:0] divCounter;

wire [3:0] DISP3;
wire [3:0] DISP2;
wire [3:0] DISP1;
wire [3:0] DISP0;

wire [3:0] debounced;
debounce button0(sys_clk, btn[0], debounced[0]);
debounce button1(sys_clk, btn[1], debounced[1]);
debounce button2(sys_clk, btn[2], debounced[2]);
debounce button3(sys_clk, btn[3], debounced[3]);

mydesign md1(sys_clk, sw, debounced, sys_rst_n, DISP3, DISP2, DISP1, DISP0, led, bled);
 

initial begin
	hex2seven[0]  = 7'b00111111;
	hex2seven[1]  = 7'b00000110;
	hex2seven[2]  = 7'b01011011;
	hex2seven[3]  = 7'b01001111;
	hex2seven[4]  = 7'b01100110;
	hex2seven[5]  = 7'b01101101;
	hex2seven[6]  = 7'b01111101;
	hex2seven[7]  = 7'b00000111;
	hex2seven[8]  = 7'b01111111;
	hex2seven[9]  = 7'b01101111;
	hex2seven[10] = 7'b01110111;
	hex2seven[11] = 7'b01111100;
	hex2seven[12] = 7'b00111001;
	hex2seven[13] = 7'b01011110;
	hex2seven[14] = 7'b01111001;
	hex2seven[15] = 7'b01110001;
	segment = 4'b0001;
end

always @(posedge sys_clk or negedge sys_rst_n) begin
    if (!sys_rst_n) begin
        dispCounter <= 16'd0;
        segCounter <= 2'd0;
    end
    else begin
        dispCounter <= dispCounter + 1;
        if (dispCounter == 0) begin
            segCounter <= segCounter + 1;
        end
    end
end

always @(segCounter or DISP0 or DISP1 or DISP2 or DISP3) begin
	case (segCounter)
	2'b00 : begin seven <= {1'b0, hex2seven[DISP0]}; segment <= 4'b0001; end
	2'b01 : begin seven <= {1'b0, hex2seven[DISP1]}; segment <= 4'b0010; end
	2'b10 : begin seven <= {1'b1, hex2seven[DISP2]}; segment <= 4'b0100; end
	2'b11 : begin seven <= {1'b0, hex2seven[DISP3]}; segment <= 4'b1000; end
	endcase
end

endmodule

