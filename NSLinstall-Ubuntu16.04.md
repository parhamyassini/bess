For Ubuntu 16.04

# Install `bess`

	sudo apt-add-repository -y ppa:ansible/ansible
	sudo apt-get install -y software-properties-common
	sudo apt-get update
	sudo apt-get install -y ansible python-pip

	ansible-playbook -i localhost, -c local env/build-dep.yml
	ansible-playbook -i localhost, -c local env/runtime.yml

	sudo update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-7 10

	git checkout file_reader

	./build.py

	sudo modprobe uio
	sudo insmod deps/dpdk-17.11/build/kmod/igb_uio.ko
	sudo ./bin/dpdk-devbind.py --bind=igb_uio 0000:01:00.0
	0000:81:00.0
	0000:81:00.1

	./build.py --plugin mdc_pkt_gen_plugin --plugin mdc_receiver_plugin --plugin aws_mdc_throughput_plugin


# Install `mdcpkts`

	git clone https://cs-git-research.cs.surrey.sfu.ca/nsl/DataCenterMulticast/mdcpkts.git
	cd mdcpkts
	python setup.py build
	sudo python setup.py install


# Run Agent

	sudo ./bessctl/bessctl daemon start; sudo ./bessctl/bessctl run samples/mdc_full_agent
	
# Run TGen
		
	sudo ./bessctl/bessctl daemon start; sudo ./bessctl/bessctl run samples/mdc_tgen
	
# Stop Agent

	sudo ./bessctl/bessctl daemon stop
	
	
('7.38.233.147', '55.169.157.31'),
('208.97.72.186', '157.28.205.149'),
('117.199.131.176', '79.219.231.247'),
('213.151.109.0', '44.122.57.30'),
('138.101.141.33', '197.122.187.56'),
('228.93.108.53', '64.211.103.156'),
('178.204.3.28', '4.63.60.250'),
('16.123.19.226', '10.199.212.27'),
