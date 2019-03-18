#!/bin/bash

# update and upgrade instance
apt-get update
apt-get -y upgrade

apt-get install -y python python-pip libffi-dev libssl-dev

# bess dependencies
apt install -y make apt-transport-https ca-certificates g++ make pkg-config libunwind8-dev liblzma-dev zlib1g-dev libpcap-dev libssl-dev libnuma-dev git python python-pip python-scapy libgflags-dev libgoogle-glog-dev libgraph-easy-perl libgtest-dev libgrpc++-dev libprotobuf-dev libc-ares-dev libbenchmark-dev libgtest-dev protobuf-compiler-grpc

pip install protobuf grpcio scapy

./aws_setup_hugepages.sh


