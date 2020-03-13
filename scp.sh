#!/bin/bash
# Ask the user for the file path
#echo Please enter file path:
varname=$1
machine1="slave01:"
machine2="slave02:"
scp $varname $machine1$varname
scp $varname $machine2$varname

