// Copyright (c) 2014-2016, The Regents of the University of California.
// Copyright (c) 2016-2017, Nefeli Networks, Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice, this
// list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright notice,
// this list of conditions and the following disclaimer in the documentation
// and/or other materials provided with the distribution.
//
// * Neither the names of the copyright holders nor the names of their
// contributors may be used to endorse or promote products derived from this
// software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#include <glog/logging.h>
#include <poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <stdint.h>
#include <endian.h>
#include <arpa/inet.h>

#include <cerrno>
#include <cstring>
#include <iostream>

#include "unix_stream_socket.h"

#define MAX_PKT_LEN (1500)
#define HDR_LEN_PARAM_SIZE (sizeof(int32_t))

/*
 * Loop runner for accept calls.
 *
 * Note that all socket operations are run non-blocking, so that
 * the only place we block is in the ppoll() system call.
 */
void UnixStreamSocketAcceptThread::Run() {
  struct pollfd fds[2];
  memset(fds, 0, sizeof(fds));
  fds[0].fd = owner_->listen_fd_;
  fds[0].events = POLLIN;
  fds[1].events = POLLRDHUP;

  while (true) {
    // negative FDs are ignored by ppoll()
    fds[1].fd = owner_->client_fd_;
    int res = ppoll(fds, 2, nullptr, Sigmask());

    if (IsExitRequested()) {
      return;

    } else if (res < 0) {
      if (errno == EINTR) {
        continue;
      } else {
        PLOG(ERROR) << "ppoll()";
      }

    } else if (fds[0].revents & POLLIN) {
      // new client connected
      int fd;
      while (true) {
        fd = accept4(owner_->listen_fd_, nullptr, nullptr, SOCK_NONBLOCK);
        if (fd >= 0 || errno != EINTR) {
          break;
        }
      }
      if (fd < 0) {
        PLOG(ERROR) << "accept4()";
      } else if (owner_->client_fd_ != UnixStreamSocketPort::kNotConnectedFd) {
        LOG(WARNING) << "Ignoring additional client\n";
        close(fd);
      } else {
        owner_->client_fd_ = fd;
        if (owner_->confirm_connect_) {
          // Send confirmation that we've accepted their connect().
          send(fd, "yes", 4, 0);
        }
      }

    } else if (fds[1].revents & (POLLRDHUP | POLLHUP)) {
      // connection dropped by client
      int fd = owner_->client_fd_;
      owner_->client_fd_ = UnixStreamSocketPort::kNotConnectedFd;
      close(fd);
    }
  }
}

CommandResponse UnixStreamSocketPort::Init(const bess::pb::UnixStreamSocketPortArg &arg) {
  const std::string path = arg.path();
  int num_txq = num_queues[PACKET_DIR_OUT];
  int num_rxq = num_queues[PACKET_DIR_INC];

  int ret;

  if (num_txq > 1 || num_rxq > 1) {
    return CommandFailure(EINVAL, "Cannot have more than 1 queue per RX/TX");
  }

  if (arg.min_rx_interval_ns() < 0) {
    min_rx_interval_ns_ = 0;
  } else {
    min_rx_interval_ns_ = arg.min_rx_interval_ns() ?: kDefaultMinRxInterval;
  }

  confirm_connect_ = arg.confirm_connect();

  listen_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
  if (listen_fd_ < 0) {
    DeInit();
    return CommandFailure(errno, "socket(AF_UNIX) failed");
  }

  addr_.sun_family = AF_UNIX;

  if (path.length() != 0) {
    snprintf(addr_.sun_path, sizeof(addr_.sun_path), "%s", path.c_str());
  } else {
    snprintf(addr_.sun_path, sizeof(addr_.sun_path), "%s/bess_unix_%s",
             P_tmpdir, name().c_str());
  }

  // This doesn't include the trailing null character.
  size_t addrlen = sizeof(addr_.sun_family) + strlen(addr_.sun_path);

  // Non-abstract socket address?
  if (addr_.sun_path[0] != '@') {
    // Remove existing socket file, if any.
    unlink(addr_.sun_path);
  } else {
    addr_.sun_path[0] = '\0';
  }

  ret = bind(listen_fd_, reinterpret_cast<struct sockaddr *>(&addr_), addrlen);
  if (ret < 0) {
    DeInit();
    return CommandFailure(errno, "bind(%s) failed", addr_.sun_path);
  }

  ret = listen(listen_fd_, 1);
  if (ret < 0) {
    DeInit();
    return CommandFailure(errno, "listen() failed");
  }

  if (!accept_thread_.Start()) {
    DeInit();
    return CommandFailure(errno, "unable to start accept thread");
  }

  if(chmod(addr_.sun_path, 0777) != 0){
    DeInit();
    return CommandFailure(errno, "unable to chmod file");
  }

  return CommandSuccess();
}

void UnixStreamSocketPort::DeInit() {
  // End thread and wait for it (no-op if never started).
  accept_thread_.Terminate();

  if (listen_fd_ != kNotConnectedFd) {
    close(listen_fd_);
  }
  if (client_fd_ != kNotConnectedFd) {
    close(client_fd_);
  }
}

int UnixStreamSocketPort::RecvPackets(queue_t qid,__attribute__((unused)) bess::Packet **pkts, int cnt) {

  int client_fd = client_fd_;//This is always the same

  DCHECK_EQ(qid, 0);

  if (client_fd == kNotConnectedFd) {
    last_idle_ns_ = 0;
    return 0;
  }

  uint64_t now_ns = current_worker.current_tsc();
  if (now_ns - last_idle_ns_ < min_rx_interval_ns_) {
    return 0;
  }

  int received = 0;
  if (cnt > 0){
    bess::Packet *pkt = current_worker.packet_pool()->Alloc();

    if (!pkt) {
      return 0;
    }

    // Datagrams larger than 2KB will be truncated.

    //uint8_t hdrLenBuf[HDR_LEN_PARAM_SIZE] = {0};
    int ret;

    static uint8_t packetDataBuf[MAX_PKT_LEN];
    static size_t remainingBytes = (size_t)0u;
    static size_t bufPosition = (size_t)0u;
    size_t readSize = (bufPosition > 0) ? remainingBytes : HDR_LEN_PARAM_SIZE;

    while ((ret = recv(client_fd, packetDataBuf + bufPosition, readSize, 0)) > 0) {
      if(bufPosition == 0u){
        bess::utils::be32_t len = *((bess::utils::be32_t*)packetDataBuf);
        remainingBytes = len.value();


        if(remainingBytes > MAX_PKT_LEN){
          LOG(ERROR) << "Incoming data too large for BESS packet " << remainingBytes << " > " << MAX_PKT_LEN;
        return 0;
        }
      }
      else
      {
      }

      remainingBytes -= ret;
      bufPosition += ret;


      //LOG(INFO) << "Message ret: " << ret << " remaining bytes: " << remainingBytes << " position: " << bufPosition;



      if(remainingBytes == 0){
        memcpy(pkt->data(), packetDataBuf, bufPosition);

        pkt->append(bufPosition);
        //LOG(INFO) << pkt->Dump();
        pkts[received++] = pkt;
        ////continue;

        bufPosition = 0;
      }

      readSize = (bufPosition > 0) ? remainingBytes : HDR_LEN_PARAM_SIZE;
    }

    if (ret < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EBADF) {
        //break;
      }

      if (errno == EINTR) {
        //continue;
      }
    }

    if(received == 0){

      bess::Packet::Free(pkt);


      // Connection closed.
      //break;
    }
  }

  last_idle_ns_ = (received == 0) ? now_ns : 0;

  return received;
}

int UnixStreamSocketPort::SendPackets(queue_t qid, bess::Packet **pkts, int cnt) {
  int sent = 0;
  int client_fd = client_fd_;

  DCHECK_EQ(qid, 0);

  if (client_fd == kNotConnectedFd) {
    return 0;
  }

  for (int i = 0; i < cnt; i++) {
    bess::Packet *pkt = pkts[i];

    int nb_segs = pkt->nb_segs();
    struct iovec iov[nb_segs];

    struct msghdr msg = msghdr();
    msg.msg_iov = iov;
    msg.msg_iovlen = nb_segs;

    ssize_t ret;

    for (int j = 0; j < nb_segs; j++) {
      iov[j].iov_base = pkt->head_data();
      iov[j].iov_len = pkt->head_len();
      pkt = pkt->next();      
    }

    ret = sendmsg(client_fd, &msg, 0);
    if (ret < 0) {
      break;
    }

    sent++;
  }

  if (sent) {
    bess::Packet::Free(pkts, sent);
  }

  return sent;
}

ADD_DRIVER(UnixStreamSocketPort, "unix_stream_socket_port",
           "packet exchange via a TCP UNIX domain socket")
