#!/bin/bash
# Ask the user for the file path
#echo Please enter file path:
varname=$1
machine1="cs-nsl-55.cmpt.sfu.ca:/home/pyassini/bess/"
machine2="cs-nsl-56.cmpt.sfu.ca:/home/pyassini/bess/"
machine3="cs-nsl-62.cmpt.sfu.ca:/local-scratch/bess/"
machine4="cs-nsl-42.cmpt.sfu.ca:/local-scratch/bess/"

scp $varname $machine1$varname
scp $varname $machine2$varname
scp $varname $machine3$varname
scp $varname $machine4$varname
