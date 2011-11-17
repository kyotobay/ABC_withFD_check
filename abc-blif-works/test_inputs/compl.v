// Verilog code for AND-OR-INVERT gate
module FD (a, b, c, E, F, G, H, I);
	input a, b, c;
	output E, F, G, H, I;
  assign E = a ^ b;
  assign F = a & c;
  assign G = ~ a;
  assign H = ~ b;
  assign I = H ^ c;
endmodule
// end of Verilog code

