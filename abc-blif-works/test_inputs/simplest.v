// Verilog code for the classic, simple example I have been using 
module SIMPLER (a, b, c, E, F, G, H);
        input a, b, c;
        output E, F, G, H;
    assign E = a ^ b;
    assign F = a & c;
    assign G = ~ a;
    assign H = ~ b;
endmodule
// end of Verilog code
