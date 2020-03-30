import os
import socket
import sys,os,time
from multiprocessing import Process
import collections
import struct

unix_socket_addr = '../domain_socket_file'
basedir = './broadcast_files'

names = os.listdir(basedir)
paths = [os.path.join(basedir, name) for name in names]
files = [(path, os.stat(path).st_size) for path in paths]

# files = collections.defaultdict(list)
# for path, size in sizes:
#     files[size].append(path)
#print (files)
server_ip = "142.58.22.104" #Ramses IP
hosts = ["142.58.22.215", "142.58.22.223", "142.58.22.136", "142.58.22.126"]
multicast_group = ('224.1.1.1', 9001)

host_times = []
port = 9001
client_count = 2
#files = [("broadcast_0", 4075), ("dummy2", 21), ]
p_list = []

def send_thread(p_id, file_name, host, sock):
    print ('p_id: ' + str(p_id))
    with open(file_name) as reader:
        data = reader.read(900)
        while (data):
            sock.sendto(data, (host,port))
            data = reader.read(900)
        print("File Sent " + str(p_id))

def receive_thread(recv_sock, method):
    ack_count = 0
    if method == "udp" or method == "multicast":
        data, addr = recv_sock.recvfrom(900)
    
    elif method == "orca":
        data = recv_sock.recv(900)
    while data:
        
        print("Received : "+ str(data))
        acks_in_data = data.count("ack")
        #print ("ACKS IN DATA: "+ str(acks_in_data))
        ack_count +=acks_in_data
        if ack_count >= len(hosts):
            print("Got all acks..")
            break
        if method == "udp" or method == "multicast":
            data, addr = recv_sock.recvfrom(900)
        elif method == "orca":
            data = recv_sock.recv(900)

def init(method):
    # Use this output in client for receiving files
    with open("file_names.txt", "wb") as writer:
        writer.write(str(files).encode('ascii'))
    if method == "udp":
        sock = socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        print("UDP Send Socket Ready")
        recv_sock = socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
        recv_sock.bind((server_ip,port))
        print("UDP Recv Socket Ready")
        return sock, recv_sock
    elif method == 'multicast':
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        ttl = struct.pack('b', 1)
        sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, ttl)
        print("UDP Multicast Send Socket Ready")
        recv_sock = socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
        recv_sock.bind((server_ip,port))
        print("UDP Recv Socket Ready")
        return sock, recv_sock
    elif method == "orca":
        sock = socket.socket(socket.AF_UNIX,socket.SOCK_STREAM)
        try:
            sock.connect(unix_socket_addr)
        except socket.error as err:
            print(err)
            sys.exit(1)
        print("UNIX Socket Created")
        return sock, sock
    else:
        print("Method argument can be one of the following strings: \'orca\'  \'udp\'  \'multicast\'")
        exit(1)

def send_orca(file_name, sock):
    with open(file_name) as reader:
        data = reader.read(900)
        while (data):
            sock.sendall(data)
            data = reader.read(900)
    print("File Sent")

def send_multicast(file_name, sock):
    with open(file_name) as reader:
        data = reader.read(900)
        while (data):
            sock.sendto(data, multicast_group)
            data = reader.read(900)
    print("File Sent")
    
def send_files(sock, recv_sock, method):
    for file_name, file_len in files:
        # receive_proc = Process(target=receive_thread, args=(recv_sock, method, ))
        # p_list.append(receive_proc)
        # receive_proc.start()
        if method == 'udp':
            for i, host in enumerate(hosts):
                p = Process(target=send_thread, args=(i, file_name, host, sock, ))
                p_list.append(p)
                p.start()
        elif method == 'orca':
            send_orca(file_name, sock)
        elif method == 'multicast':
            send_multicast(file_name, sock)
        for process in p_list:
                process.join()

if __name__ == '__main__':
    print("File Transfer [Sender]")
    if len(sys.argv) < 2:
        print("Run with the following arguments: <send method> <Host IP>")
    _method = sys.argv[1]
    print("Config Send Method: " + _method)
    sock, recv_sock = init(method=_method)
    send_files(sock, recv_sock, _method)

sys.exit()
