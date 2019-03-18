#!/usr/bin/env bash


cd ~/bess

modprobe uio
insmod ~/bess/deps/dpdk-17.11/build/kmod/igb_uio.ko wc_activate=1

echo "Please bind proper network interfaces to igb_uio"
