module  xor_example ( a0, a1, a2, a3, b0, b1, b2, b3, R );
 output R;
 input  a0, a1, a2 , a3, b0, b1, b2, b3;
 
 reg R;
 

R <= ( a0 && b3 && a3 && b0 || a0 && b3 && a1 && b2 || a1 && b2 && a3 && b0 );
 
endmodule
