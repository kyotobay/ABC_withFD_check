// Verilog code for AND-OR-INVERT gate
module ANDOR (a, b, c, d, E, F, G, H, I, J, K, L, M, N);
	input a, b, c, d;
	output E, F, G, H, I, J, K, L, M, N;
  assign E = a ^ b;
  assign F = a & b;
  assign G = ~ a;
  assign H = ~ b;
  assign I = G & H;
  assign J = c ^ d;
  assign K = c & d;
  assign L = ~ c;
  assign M = ~ d;
  assign N = L & M; 
endmodule
// end of Verilog code

