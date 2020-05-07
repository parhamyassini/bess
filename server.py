import os
import socket
import sys,os,time
import collections
import struct
import pickle
import csv
import sendfile
import random
import string
from multiprocessing import Process, Queue, Value, Array

unix_socket_addr = '../domain_socket_file'
basedir = 'broadcast_files_3'

UDP_BUF_LEN = 65507
LOSS_TOLERANCE = 0.00
SEND_THREADS = 4
#dummy_len = int(UDP_BUF_LEN*LOSS_TOLERANCE)
names = os.listdir(basedir)
paths = [os.path.join(basedir, name) for name in names]
files = [(path, os.stat(path).st_size) for path in paths]
files.sort()

server_ip_net_0 = "192.168.0.1" #Ramses IP on port0
server_ip_net_1 = "192.168.1.1" #Ramses IP on port1
hosts = ["192.168.0.4", "192.168.0.3", "192.168.1.2", "192.168.1.3"]
#hosts = ["142.58.22.215", "142.58.22.223"]
multicast_group = ('224.1.1.1', 9001)

host_per_thread = int(len(hosts)/SEND_THREADS)
ack_times = []
total_time = 0
port = 9001
client_count = 2
p_list = []

def send_thread(p_id, file_name, file_len, hosts, socks):
    print ('p_id: ' + str(p_id))
    # retrans_len = int(file_len * LOSS_TOLERANCE)
    # sent_retrans_bytes = 0
    offset = p_id * host_per_thread
    with open(file_name) as reader:
        data = reader.read(UDP_BUF_LEN)
        while (data):
            for i in range(host_per_thread):
                # TODO: Try sendall
                socks[offset + i].sendto(data, (hosts[offset + i], port))
            data = reader.read(UDP_BUF_LEN)

    # while (sent_retrans_bytes < int(file_len * LOSS_TOLERANCE)):
    #     data_len = min(retrans_len - sent_retrans_bytes, UDP_BUF_LEN)
    #     retrans_data = 'z' * data_len
    #     sock.sendto(retrans_data, (host, port))
    #     sent_retrans_bytes += data_len

    print("File Sent " + str(p_id))

def receive_thread(i, recv_sock, method):
    ack_count = 0
    #return
    if method == "udp" or method == "multicast":
        data, addr = recv_sock.recvfrom(UDP_BUF_LEN)
    elif method == "orca":
        data = recv_sock.recv(900)
    while data:
        print("Received : "+ str(data))
        acks_in_data = data.count("ack_" + str(i))
        #print ("ACKS IN DATA: "+ str(acks_in_data))
        ack_count += acks_in_data
        if (method == 'orca'):
            if (ack_count >= len(hosts)):
                print("Got all acks for file " + files[i][0])
                return
        elif (method == 'udp'):
            if (ack_count >= (len(hosts)/2)):
                print("Got all acks for file " + files[i][0])
                return
        if method == "udp" or method == "multicast":
            data, addr = recv_sock.recvfrom(UDP_BUF_LEN)
        elif method == "orca":
            data = recv_sock.recv(900)

def init(method):
    global ack_times
    socks = []
    ack_times = [0] * len(files)
    tot = 0
    for file in files:
        tot += file[1]
    print ('Total bytes: ' + str(tot))
    with open(basedir + ".txt", "wb") as writer:
        pickle.dump(files, writer)
    if method == "udp":
        for host in hosts:
            sock = socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            socks.append(sock)
        print("UDP Send Socket Ready")
        # Listen on two network addresses for each rack
        recv_sock_net_0 = socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
        recv_sock_net_0.bind((server_ip_net_0,port))
        recv_sock_net_1 = socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
        recv_sock_net_1.bind((server_ip_net_1,port))
        print("UDP Recv Socket Ready")
        return socks, recv_sock_net_0, recv_sock_net_1
    elif method == 'multicast':
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        ttl = struct.pack('b', 1)
        sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, ttl)
        print("UDP Multicast Send Socket Ready")
        recv_sock = socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
        recv_sock.bind((server_ip_net_0,port))
        print("UDP Recv Socket Ready")
        return sock, recv_sock, recv_sock
    elif method == "orca":
        sock = socket.socket(socket.AF_UNIX,socket.SOCK_STREAM)
        try:
            sock.connect(unix_socket_addr)
        except socket.error as err:
            print(err)
            sys.exit(1)
        print("UNIX Socket Created")
        socks.append(sock)
        return socks, sock, sock
    else:
        print("Method argument can be one of the following strings: \'orca\'  \'udp\'  \'multicast\'")
        exit(1)

def send_orca(file_name, file_len, sock):
    with open(file_name) as reader:
        data = reader.read()
        #sock.sendall(data)
        sent = 0
        while (sent < len(data)):
            sock.send(data[sent:sent+15000])
            sent += 15000
    print("File Sent")

def send_multicast(file_name, sock):
    with open(file_name) as reader:
        data = reader.read()
        while (data):
            sock.sendto(data, multicast_group)
            data = reader.read(UDP_BUF_LEN)
    print("File Sent")
    
def send_files(socks, recv_sock_net_0, recv_sock_net_1, method):
    global ack_times
    ack_times = [0] * len(files)
    for j, (file_name, file_len) in enumerate(files):
        receive_proc_net_0 = Process(target=receive_thread, args=(j, recv_sock_net_0, method, ))
        p_list.append(receive_proc_net_0)
        receive_proc_net_0.start()
        if (method == 'udp'):
            receive_proc_net_1 = Process(target=receive_thread, args=(j, recv_sock_net_1, method,  ))
            p_list.append(receive_proc_net_1)
            receive_proc_net_1.start()
        start_send = time.time()
        if method == 'udp':
            for i in range(SEND_THREADS):
                p = Process(target=send_thread, args=(i, file_name, file_len, hosts, socks, ))
                p_list.append(p)
                p.start()
        elif method == 'orca':
            p = Process(target=send_orca, args=(file_name, file_len, socks[0], ))
            p_list.append(p)
            p.start()
            #send_orca(file_name, sock)
        elif method == 'multicast':
            send_multicast(file_name, sock)
        for process in p_list:
                process.join()
        ack_times[j] = time.time() - start_send

if __name__ == '__main__':
    print("File Transfer [Sender]")
    if len(sys.argv) < 2:
        print("Run with the following arguments: <send method>")
        exit(1)
    _method = sys.argv[1]
    _run_num = int(sys.argv[2])
    print("Config Send Method: " + _method)
    avg_total_time = 0
    for run_idx in range(_run_num):
        total_time = 0
        start_time = time.time()
        if run_idx == 0:
            socks, recv_sock_net_0, recv_sock_net_1 = init(method=_method)
            avg_ack_times = [0] * len(files)
        send_files(socks, recv_sock_net_0, recv_sock_net_1, _method)
        total_time = time.time() - start_time
        print ("Total Time: " + str(total_time))
        avg_total_time += total_time / _run_num
        for j in range(len(files)):
            avg_ack_times[j] += ack_times[j] / _run_num
        time.sleep(7)
    with open("out_" + _method + "_t" + str(SEND_THREADS) + "_" + basedir + ".csv", 'wb') as file:
        fieldnames = ['name', 'time']
        writer = csv.DictWriter(file, fieldnames=fieldnames)
        writer.writeheader()
        for i, time in enumerate(ack_times):
            writer.writerow({
                'name': files[i][0],
                'time': avg_ack_times[i]
            })
        writer.writerow({
                    'name': 'App Runing Time',
                    'time': avg_total_time
                })
sys.exit()

