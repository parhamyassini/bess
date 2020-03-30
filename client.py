import socket
import sys,os,time
import struct

server_ip = "142.58.22.223"
host_ip = ""
multicast_group = '224.1.1.1'
server_address = ('', 9001)

port = 9001

unix_socket_addr = '../domain_socket_file'
files = [('./broadcast_files/broadcast_17', 3573), ('./broadcast_files/broadcast_3', 3924), ('./broadcast_files/broadcast_10', 5086), ('./broadcast_files/broadcast_20', 3573), ('./broadcast_files/broadcast_15', 9187341), ('./broadcast_files/broadcast_8', 3573), ('./broadcast_files/broadcast_28', 5884), ('./broadcast_files/broadcast_21', 9187341), ('./broadcast_files/broadcast_0', 61768), ('./broadcast_files/broadcast_16', 5352), ('./broadcast_files/broadcast_14', 3573), ('./broadcast_files/broadcast_23', 3573), ('./broadcast_files/broadcast_5', 3573), ('./broadcast_files/broadcast_31', 6017), ('./broadcast_files/broadcast_32', 3573), ('./broadcast_files/broadcast_22', 5618), ('./broadcast_files/broadcast_11', 3573), ('./broadcast_files/broadcast_30', 9187341), ('./broadcast_files/broadcast_7', 4953), ('./broadcast_files/broadcast_2', 2918), ('./broadcast_files/broadcast_19', 5485), ('./broadcast_files/broadcast_4', 4307), ('./broadcast_files/broadcast_12', 9187341), ('./broadcast_files/broadcast_24', 9187341), ('./broadcast_files/broadcast_29', 3573), ('./broadcast_files/broadcast_9', 9187341), ('./broadcast_files/broadcast_18', 9187341), ('./broadcast_files/broadcast_27', 9187341), ('./broadcast_files/broadcast_1', 3238), ('./broadcast_files/broadcast_33', 9187341), ('./broadcast_files/broadcast_6', 9187341), ('./broadcast_files/broadcast_25', 5751), ('./broadcast_files/broadcast_13', 5219), ('./broadcast_files/broadcast_26', 3573)]
total_written_bytes = [0] * len(files)

def init(method):
    print("Config Host IP: " + host_ip)
    if method == "udp":
        sock_send = socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
        sock_send.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        print("UDP Send Socket Ready")
        sock = socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
        sock.bind((host_ip,port))
        print("UDP Receive Socket Ready")
        return sock, sock_send
    elif method == "multicast":
        sock_send = socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
        sock_send.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        print("UDP Send Socket Ready")
        sock = socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
        sock.bind(server_address)
        group = socket.inet_aton(multicast_group)
        mreq = struct.pack('4sL', group, socket.INADDR_ANY)
        sock.setsockopt(
            socket.IPPROTO_IP,
            socket.IP_ADD_MEMBERSHIP,
            mreq)
        # sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        # try:
        #     sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
        # except AttributeError:
        #     pass
        # sock.setsockopt(socket.SOL_IP, socket.IP_MULTICAST_TTL, 20)
        # sock.setsockopt(socket.SOL_IP, socket.IP_MULTICAST_LOOP, 1)
        # sock.bind(server_address)
        # intf = socket.gethostbyname(socket.gethostname())
        # sock.setsockopt(socket.SOL_IP, socket.IP_MULTICAST_IF, socket.inet_aton(intf))
        # sock.setsockopt(socket.SOL_IP, socket.IP_ADD_MEMBERSHIP, socket.inet_aton(multicast_group) + socket.inet_aton(intf))
        print("UDP Multicast Socket Receive Ready")
        return sock, sock_send
    elif method == "orca":
        sock = socket.socket(socket.AF_UNIX,socket.SOCK_STREAM)
        try:
            sock.connect(unix_socket_addr)
        except socket.error as err:
            print(err)
            sys.exit(1)
        print("UNIX Socket Connected")
        return sock, sock
    else:
        print("Method argument can be one of the following strings: \'orca\' or '\udp\' or \'multicast\'")

def receive_files(sock, sock_send, method):
    file_idx = 0
    # data = sock.recv(1024)
    # while data:
    #     print (data)
    #     data = sock.recv(1024)
    
    writer = open(files[file_idx][0],"wb")
    if method == "udp" or method == "multicast":
        data, addr = sock.recvfrom(900)
        
    elif method == "orca":
        data = sock.recv(900)

    while data and file_idx < len(files):
        processed_bytes = 0
        received_len = len(data)
        # if total_written_bytes[file_idx] + len(data) < files[file_idx][1]:
        #     writer.write(data)
        #     total_written_bytes[file_idx] += len(data)
        #     print("FILE " + str(file_idx) + " tot: " + str(total_written_bytes[file_idx]))
        # else:
        while processed_bytes < received_len:
            crop_index = min(len(data), files[file_idx][1] - total_written_bytes[file_idx])
            processed_bytes += crop_index
            writer.write(data[:crop_index])
            total_written_bytes[file_idx] += len(data[:crop_index])
            data = data[crop_index:]
            if total_written_bytes[file_idx] == files[file_idx][1]: #FINISHED RECEIVING THIS FILE
                print("Received File: {}".format(files[file_idx][0]))
                if method == "udp" or method == "multicast":
                    sock_send.sendto("ack " + str(file_idx), (server_ip,port))
                elif method == "orca":
                    sock.sendall("ack " + str(file_idx))
                if file_idx < len(files)-1:
                    file_idx += 1
                    writer = open(files[file_idx][0],"wb")
                else:
                    print("Closing..")
                    sys.exit()
        if method == "udp":
            data, addr = sock.recvfrom(900)
        elif method == "orca" or method == "multicast":
            data = sock.recv(900)

if __name__ == '__main__':
    print("File Transfer [Receiver]")
    if(len(sys.argv) < 3):
        print("Run with the following arguments: <send method> <Host IP>")
    _method = sys.argv[1]
    host_ip = sys.argv[2]
    print("Config Send Method: " + _method)
    sock, sock_send = init(method=_method)
    receive_files(sock, sock_send, _method)

