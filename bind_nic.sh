#!/bin/bash
sudo modprobe uio_pci_generic
sudo bin/dpdk-devbind.py --unbind 0000:81:00.0
sudo bin/dpdk-devbind.py --unbind 0000:81:00.1
sudo bin/dpdk-devbind.py -b uio_pci_generic 0000:81:00.1
sudo bin/dpdk-devbind.py -b uio_pci_generic 0000:81:00.0
bin/dpdk-devbind.py --status
