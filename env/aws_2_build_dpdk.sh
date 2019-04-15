#!/usr/bin/env bash

(cd ~/bess/deps
git clone https://github.com/DPDK/dpdk.git
git clone https://github.com/amzn/amzn-drivers.git
mv dpdk dpdk-17.11
)

(cd ~/bess/deps/dpdk-17.11
git checkout v17.11
git am ~/bess/deps/amzn-drivers/userspace/dpdk/17.11/00*
)

(cd ~/bess
./build.py dpdk
./build.py
)