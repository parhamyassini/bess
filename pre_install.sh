#!/bin/bash

apt-get install -y software-properties-common
apt-add-repository -y ppa:ansible/ansible
apt-get update
apt-get install -y ansible

echo "Run:
ansible-playbook -i localhost, -c local env/build-dep.yml
ansible-playbook -i localhost, -c local env/runtime.yml 
sudo reboot

sudo start_hugepages_single.sh
or 
sudo start_huge_pages_numa.sh
"



