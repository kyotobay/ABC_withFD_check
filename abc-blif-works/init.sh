#!/bin/bash
rm -f table* my_in* after_fd.blif
make
echo "read_verilog test_inputs/example_fd.v"
echo "strash"
echo "check_fd"
echo "q"
./abc
