import getpass
import sys
from fabric import Connection, Config

RESULTS_PATH = '/local-scratch/results'

DPDK_PATH_55 = '/home/pyassini/bess/bin/dpdk-devbind.py'
DPDK_PATH_56 = '/home/pyassini/bess/bin/dpdk-devbind.py'
DPDK_PATH_62 = '/local-scratch/bess/bin/dpdk-devbind.py'
DPDK_PATH_42 = '/local-scratch/bess/bin/dpdk-devbind.py'
DPDK_PATH_RAMSES = '/local-scratch/pyassini/bess/bin/dpdk-devbind.py'

IFACE_RAMSES_RACK_1 = 'ens6f0'
IFACE_RAMSES_RACK_2 = 'ens6f1'
IFACE_42 = 'enp1s0f0'
IFACE_62 = 'enp2s0f0'
IFACE_55 = 'enp1s0f0'
IFACE_56 = 'eth0'

DEV_RAMSES = '0000:81:00'
DEV_42 = '0000:01:00'
DEV_62 = '0000:02:00'
DEV_55 = '0000:01:00'
DEV_56 = '0000:01:00'

def firewall_allow_iface(connection, iface_name):
    connection.sudo('iptables -I INPUT 1 -i ' + iface_name + ' -p all -j ACCEPT')

def nic_unbind(connection, dpdk_path, device_name):
    print ('Unbinding ' + device_name)
    connection.sudo(dpdk_path + ' --unbind ' + device_name + '.0')
    connection.sudo(dpdk_path + ' --unbind ' + device_name + '.1')

def nic_bind_uio(connection, dpdk_path, device_name):
    connection.sudo('modprobe uio_pci_generic')
    nic_unbind(connection, dpdk_path, device_name)
    connection.sudo(dpdk_path + ' -b uio_pci_generic ' + device_name + '.0')
    connection.sudo(dpdk_path + ' -b uio_pci_generic ' + device_name + '.1')

def nic_bind_kernel(connection, dpdk_path, device_name):
    connection.sudo(dpdk_path + ' -b ixgbe ' + device_name + '.0')
    connection.sudo(dpdk_path + ' -b ixgbe ' + device_name + '.1')

def iface_down(connection, iface_name):
    connection.sudo('ifdown ' + iface_name, hide='stderr')

def iface_up(connection, iface_name):
    connection.sudo('ifup ' + iface_name, hide='stderr')

def switch_to_multicast(host_list):
    for host in host_list:
        # Deactivate the interfaces from kernel
        iface_down(host['connection'], host['iface_name_1'])
        if 'iface_name_2' in host:
            iface_down(host['connection'], host['iface_name_2'])
        nic_bind_uio(host['connection'], host['dpdk_path'], host['device_name'])
        

def switch_to_unicast(host_list):
    for host in host_list:
        nic_unbind(host['connection'], host['dpdk_path'], host['device_name'])
        nic_bind_kernel(host['connection'], host['dpdk_path'], host['device_name'])
        iface_up(host['connection'], host['iface_name_1'])
        firewall_allow_iface(host['connection'], host['iface_name_1'])
        if 'iface_name_2' in host:
            iface_up(host['connection'], host['iface_name_2'])
            firewall_allow_iface(host['connection'], host['iface_name_2'])
            
if __name__ == '__main__':
    print("Deployment Script")
    if len(sys.argv) < 3:
        print("Run with the following arguments: <username> <multicast/unicast>")
        exit(1)
    _username = sys.argv[1]    
    _goal = sys.argv[2]
    host_list = []

    sudo_pass = getpass.getpass("What's your sudo password?")
    config = Config(overrides={'sudo': {'password': sudo_pass}})
    print ("Connecting to ramses")
    ramses = Connection("localhost", port=22, user="pyassini", connect_kwargs={'password': sudo_pass}, config=config)
    print ("Connecting to nsl-42")
    nsl_42 = Connection("cs-nsl-42.cmpt.sfu.ca", port=22, user=_username, connect_kwargs={'password': sudo_pass}, config=config)
    print ("Connecting to nsl-62")
    nsl_62 = Connection("cs-nsl-62.cmpt.sfu.ca", port=22, user=_username, connect_kwargs={'password': sudo_pass}, config=config)
    print ("Connecting to nsl-55")
    nsl_55 = Connection("cs-nsl-55.cmpt.sfu.ca", port=22, user=_username, connect_kwargs={'password': sudo_pass}, config=config)
    print ("Connecting to nsl-56")
    nsl_56 = Connection("cs-nsl-56.cmpt.sfu.ca", port=22, user=_username, connect_kwargs={'password': sudo_pass}, config=config)
    ramses.sudo('whoami', hide='stderr')
    host_list.append({'connection': ramses,
    'dpdk_path': DPDK_PATH_RAMSES,
    'iface_name_1':IFACE_RAMSES_RACK_1,
    'iface_name_2': IFACE_RAMSES_RACK_2,
    'device_name':DEV_RAMSES
    })
    host_list.append({'connection': nsl_42,
    'dpdk_path': DPDK_PATH_42,
    'iface_name_1':IFACE_42,
    'device_name':DEV_42
    })
    host_list.append({'connection': nsl_62,
    'dpdk_path': DPDK_PATH_62,
    'iface_name_1':IFACE_62,
    'device_name':DEV_62
    })
    host_list.append({'connection': nsl_55,
    'dpdk_path': DPDK_PATH_55,
    'iface_name_1':IFACE_55,
    'device_name':DEV_55
    })
    host_list.append({'connection': nsl_56,
    'dpdk_path': DPDK_PATH_56,
    'iface_name_1':IFACE_56,
    'device_name':DEV_56
    })
    if (_goal == 'multicast'):
        switch_to_multicast(host_list)
        print("\nSuccesfully done multicast configuration")
    elif (_goal == 'unicast'):
        switch_to_unicast(host_list)
        print("\nSuccesfully done unicast configuration")


