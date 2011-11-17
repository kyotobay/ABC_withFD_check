module  mux2to1 ( dout, sel, i0, i1 );
  output dout;
  input  sel;
  input  i0, i1;

  reg dout;
 
  always @ (sel or i0 or i1)
  begin
    if (sel) dout <= i1;
    else     dout <= i0;
  end
endmodule
