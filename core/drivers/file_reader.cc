#include <glog/logging.h>
#include <poll.h>
#include <signal.h>

#include <cerrno>
#include <cstring>
#include <iostream>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "file_reader.h"

/*
 * Loop runner for accept calls.
 *
 * Note that all socket operations are run non-blocking, so that
 * the only place we block is in the ppoll() system call.
 */
void FileReaderAcceptThread::Run() {
  LOG(INFO) << "Run thread for file reader";

  bool file_complete = false;
  int fd = owner_->read_fd_;
  LOG(INFO) << "Got FD: " << fd;

  while(!file_complete){
    //read_fd_
    if (IsExitRequested()){
      return;
    }

  }

  // struct pollfd fds[2];
  // memset(fds, 0, sizeof(fds));
  // //fds[0].fd = owner_->listen_fd_;
  // //fds[0].events = POLLIN;
  // fds[1].events = POLLRDHUP;

  // while (true) {
  //   // negative FDs are ignored by ppoll()
  //   fds[1].fd = owner_->client_fd_;
  //   int res = ppoll(fds, 2, nullptr, Sigmask());

  //   if (IsExitRequested()) {
  //     return;

  //   } else if (res < 0) {
  //     if (errno == EINTR) {
  //       continue;
  //     } else {
  //       PLOG(ERROR) << "ppoll()";
  //     }

  //   } else if (fds[0].revents & POLLIN) {
  //     // new client connected
  //     int fd;
  //     while (true) {
  //       fd = accept4(owner_->listen_fd_, nullptr, nullptr, SOCK_NONBLOCK);
  //       if (fd >= 0 || errno != EINTR) {
  //         break;
  //       }
  //     }
  //     if (fd < 0) {
  //       PLOG(ERROR) << "accept4()";
  //     } else if (owner_->client_fd_ != FileReaderPort::kNotConnectedFd) {
  //       LOG(WARNING) << "Ignoring additional client\n";
  //       close(fd);
  //     } else {
  //       owner_->client_fd_ = fd;
  //       if (owner_->confirm_connect_) {
  //         // Send confirmation that we've accepted their connect().
  //         send(fd, "yes", 4, 0);
  //       }
  //     }

  //   } else if (fds[1].revents & (POLLRDHUP | POLLHUP)) {
  //     // connection dropped by client
  //     int fd = owner_->client_fd_;
  //     owner_->client_fd_ = FileReaderPort::kNotConnectedFd;
  //     close(fd);
  //   }
  // }
}

CommandResponse FileReaderPort::Init(const bess::pb::FileReaderPortArg &arg) {
  const std::string path = arg.path();//Path to the file which we wish to read
  LOG(INFO) << "File reader path: " << path << std::endl;
  
  int num_txq = num_queues[PACKET_DIR_OUT];
  int num_rxq = num_queues[PACKET_DIR_INC];

  //int ret;

  if (num_txq > 1 || num_rxq > 1) {
    return CommandFailure(EINVAL, "Cannot have more than 1 queue per RX/TX");
  }

  if (arg.min_rx_interval_ns() < 0) {
    min_rx_interval_ns_ = 0;
  } else {
    min_rx_interval_ns_ = arg.min_rx_interval_ns() ?: kDefaultMinRxInterval;
  }

  confirm_connect_ = arg.confirm_connect();
  LOG(INFO) << "Connect confirmed.";
  // listen_fd_ = socket(AF_UNIX, SOCK_SEQPACKET, 0);
  // if (listen_fd_ < 0) {
  //   DeInit();
  //   return CommandFailure(errno, "socket(AF_UNIX) failed");
  // }

  // addr_.sun_family = AF_UNIX;

  // if (path.length() != 0) {
  //   snprintf(addr_.sun_path, sizeof(addr_.sun_path), "%s", path.c_str());
  // } else {
  //   snprintf(addr_.sun_path, sizeof(addr_.sun_path), "%s/bess_unix_%s",
  //            P_tmpdir, name().c_str());
  // }

  //My code
  if (path.length() > 0){
    LOG(INFO) << "Going to open: " << path.c_str();
    if((read_fd_ = open(path.c_str(), O_RDONLY)) == -1){
      LOG(INFO) << "Open failed";
      return CommandFailure(EINVAL, "Could not open file");
    }
    LOG(INFO) << "File opened successfully.";
  }
  else{
    return CommandFailure(EINVAL, "Must supply a path");
  }

  // // This doesn't include the trailing null character.
  // size_t addrlen = sizeof(addr_.sun_family) + strlen(addr_.sun_path);

  // // Non-abstract socket address?
  // if (addr_.sun_path[0] != '@') {
  //   // Remove existing socket file, if any.
  //   unlink(addr_.sun_path);
  // } else {
  //   addr_.sun_path[0] = '\0';
  // }

  // ret = bind(listen_fd_, reinterpret_cast<struct sockaddr *>(&addr_), addrlen);
  // if (ret < 0) {
  //   DeInit();
  //   return CommandFailure(errno, "bind(%s) failed", addr_.sun_path);
  // }

  // ret = listen(listen_fd_, 1);
  // if (ret < 0) {
  //   DeInit();
  //   return CommandFailure(errno, "listen() failed");
  // }

  if (!accept_thread_.Start()) {
    DeInit();
    return CommandFailure(errno, "unable to start accept thread");
  }

  return CommandSuccess();
}

void FileReaderPort::DeInit() {
  // End thread and wait for it (no-op if never started).
  accept_thread_.Terminate();

  // if (listen_fd_ != kNotConnectedFd) {
  //   close(listen_fd_);
  // }
  // if (client_fd_ != kNotConnectedFd) {
  //   close(client_fd_);
  // }
  if (read_fd_ > 0){
    close(read_fd_);
  }
}


//Send packets INTO bess, receive data from file
int FileReaderPort::RecvPackets(queue_t qid, bess::Packet **pkts, int cnt) {
  
  if (cnt != 32){
    LOG(INFO) << "In Recv Packets. cnt: " << cnt;
  }
  else{
    // LOG(INFO) << "In Recv Packets. cnt: " << cnt;
  }
  int read_fd = read_fd_;

  DCHECK_EQ(qid, 0);

  if (read_fd == kNotConnectedFd || read_fd == -1) {
    last_idle_ns_ = 0;
    // LOG(INFO) << "RecvPackets, kNotConnected. cnt: " << cnt;
    LOG(INFO) << "read_fd not ready!!!";
    return 0;
  }
  // LOG(INFO) << "Recv packet, connected. cnt: " << cnt;

  uint64_t now_ns = current_worker.current_tsc();
  if (now_ns - last_idle_ns_ < min_rx_interval_ns_) {
    return 0;
  }

  int received = 0;
  while (received < cnt) {
    bess::Packet *pkt = current_worker.packet_pool()->Alloc();
    //LOG(INFO) << "Alloc pkt: " << pkt;
    if (!pkt) {
      break;
    }

    // Datagrams larger than 2KB will be truncated.
    // int ret = recv(client_fd, pkt->data(), SNBUF_DATA, 0);
    // lseek(read_fd, 0, SEEK_SET);
    int ret = read(read_fd, pkt->data(), SNBUF_DATA);

    if (ret > 0) {
      LOG(INFO) << "Read successful. ret: " << ret;
      pkt->append(ret);
      pkts[received++] = pkt;
      continue;
    }

    bess::Packet::Free(pkt);

    if (ret < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EBADF) {
        break;
      }

      if (errno == EINTR) {
        continue;
      }
    }

    // Connection closed.
    break;
  }

  last_idle_ns_ = (received == 0) ? now_ns : 0;

  return received;
  /*DCHECK_EQ(qid,0);
  uint64_t now_ns = current_worker.current_tsc();
  if (now_ns - last_idle_ns_ < min_rx_interval_ns_){
    return 0;
  }
  bess::Packet *pkt = current_worker.packet_pool()->Alloc();
  if(!pkt){
    LOG(INFO) << "Could not allocate pkt";
    return 0;
  }
  char msgBuf[15] = "ThisIsToBeSent";
  memcpy(pkt->data(), msgBuf, sizeof(char)*15);
  LOG(INFO) << "Sending pkt content: " << (char*)pkt->data();
  pkt->append(15);
  pkts[0] = pkt;
  return 1;*/
}

int FileReaderPort::SendPackets(queue_t qid, bess::Packet **pkts, int cnt) {
  //This should be sending data from the file, to packetize it
  int sent = cnt;
  sent = 0;
  if(pkts){
    sent++;
    sent--;
  }
  // int client_fd = client_fd_;
  // LOG(INFO) << "In sendpacket, cnt: " << cnt;

  DCHECK_EQ(qid, 0);

  // if (client_fd == kNotConnectedFd) {
  //   LOG(INFO) << "SendPackets: kNotConnectedFd";
  //   return 0;
  // }

  // for (int i = 0; i < cnt; i++) {
  //   bess::Packet *pkt = pkts[i];



  //   LOG(INFO) << "Sendpkt data: " << pkt->data();

  //   int nb_segs = pkt->nb_segs();
  //   struct iovec iov[nb_segs];

  //   struct msghdr msg = msghdr();
  //   msg.msg_iov = iov;
  //   msg.msg_iovlen = nb_segs;

  //   ssize_t ret;

  //   for (int j = 0; j < nb_segs; j++) {
  //     iov[j].iov_base = pkt->head_data();
  //     iov[j].iov_len = pkt->head_len();
  //     pkt = pkt->next();
  //     // printf("Sending iov[%d] of: \"%s\"\n", j, (char*)iov[j].iov_base);
  //     LOG(INFO) << "Sending pkt iov[" << j << "] of: " << (char*)iov[j].iov_base;
  //   }

  //   ret = sendmsg(client_fd, &msg, 0);
  //   if (ret < 0) {
  //     break;
  //   }

  //   sent++;
  // }

  // if (sent) {
  //   bess::Packet::Free(pkts, sent);
  // }

  return sent;
}

ADD_DRIVER(FileReaderPort, "file_reader_port",
           "Read a file as a port")
