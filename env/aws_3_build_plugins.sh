#!/usr/bin/env bash

(cd ~/bess
./build.py --plugin aws_mdc_switch_plugin --plugin aws_mdc_receiver_plugin --plugin aws_mdc_pkt_gen_plugin --plugin aws_mdc_throughput_plugin
)