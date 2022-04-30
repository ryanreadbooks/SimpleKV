#include <iostream>
#include <sstream>
#include <cstring>
#include <errno.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <assert.h>
#include "server.h"
#include "utils.h"

using namespace std::placeholders;

static InitIgnoreSigpipe sIgnoreSIGPIPEIniter;

int Server::next_session_id_ = 1;

Server::Server(EventLoop *loop, Engine *engine, const std::string &ip, uint16_t port) :
    loop_(loop), engine_(engine), addr_(ip, port) {
  if (loop == nullptr) {
    std::cerr << "No loop is specified for the server\n";
    exit(0);
  }
  InitListenSession();
}

Server::~Server() {
  /* TODO close all connection and release all session and listen_session */
  FreeListenSession();
}

void Server::InitListenSession() {
  /* init socket and address */
  if ((listen_fd_ = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
    std::cerr << "Can not init listen socket: " << strerror(errno) << std::endl;
    exit(-1);
  }
  SetReuseAddress(listen_fd_);
  SetReusePort(listen_fd_);
  SetFdNonBlock(listen_fd_);
  if (bind(listen_fd_, addr_.GetAddr(), addr_.GetSockAddrLen()) == -1) {
    std::cerr << "Can not bind socket: " << strerror(errno) << std::endl;
    exit(-1);
  }
  /* attach into loop */
  listen_session_ = new(std::nothrow) Session(
      listen_fd_, EPOLLIN,
      std::bind(&Server::AcceptProc, this, _1, _2),
      nullptr,
      loop_, "listen_session");
  assert(listen_session_ != nullptr);
  /* attach listen fd into epoll */
  loop_->epoller->AttachSession(listen_session_);
  listen(listen_fd_, SOMAXCONN);
  listen_session_->watched = true;
}

void Server::FreeListenSession() {
  if (listen_session_) {
    loop_->epoller->DetachSession(listen_session_);
    delete listen_session_;
    listen_session_ = nullptr;
  }
}

void Server::AcceptProc(Session *session, bool &closed) {
  /* accept incoming connection(session) and process */
  Ipv4Addr addr;
  socklen_t socklen = addr.GetSockAddrLen();
  int remote_fd = accept(listen_fd_, addr.GetAddr(), &socklen);
  if (remote_fd != -1) {
    addr.SyncPort();
    std::cout << "accepted connection from: " << addr.ToString() << std::endl;
    char buf[64];
    snprintf(buf, sizeof buf, "*%s#%d", addr.ToString().c_str(), next_session_id_++);
    std::string sess_name = buf;
    /* create a new session for every accepted connection */
    Session *sess = new(std::nothrow) Session(
        remote_fd, EPOLLIN,
        std::bind(&Server::ReadProc, this, _1, _2),
        std::bind(&Server::WriteProc, this, _1, _2),
        loop_, sess_name);
    std::cout << "accepted  session = " << sess << std::endl;
    if (sess != nullptr) {
      /* attach new session into epoll */
      SetFdNonBlock(remote_fd);
      if (loop_->epoller->AttachSession(sess)) {
        std::cout << "fd=" << sess->fd << " added into eventloop watch\n";
        sess->watched = true;
        sessions_[sess_name] = std::shared_ptr<Session>(sess);
      }
    }
  }
}

void Server::AuxiliaryReadProcCleanup(Buffer &buffer, CommandCache &cache, size_t begin_idx, int nbytes) {
  /* clear cache and clean up invalid request in buffer */
  /* TODO 将CommandCache清除并且将buffer中的格式不合法的请求全部处理掉 */
  cache.Clear();
  buffer.Reset(); /* FIXME, should only discard invalid bytes and reserve those valid request bytes */
}

bool Server::AuxiliaryReadProc(Buffer &buffer, CommandCache &cache, int nbytes) {
  /* Helper function to parse request command */
  size_t begin_idx = buffer.BeginReadIdx();
  while (buffer.ReadableBytes() > 0 && cache.argc > cache.argv.size()) {
    DynamicString line = buffer.ReadAndForward(kCRLF);
    if (line.Empty()) {
      /* can not form an whole item, wait for more data coming in */
      break;
    }
    if (line[0] != '$') {
      /* command parsing error, end parsing and reply immediately and clear cache and buffer */
      AuxiliaryReadProcCleanup(buffer, cache, begin_idx, nbytes);
      return false;
    }
    char *p_end;
    size_t arg_len = strtol(line.Data() + 1, &p_end, 10);
    if (buffer.ReadableBytes() < arg_len + 2) {
      /* can not read arg_len args, wait until next time */
      buffer.ReaderIdxBackward(line.Length() + 2);
      break;
    }
    std::string arg = buffer.ReadStdStringAndForward(arg_len);
    if (buffer.ReadStdString(2) != kCRLF) {
      AuxiliaryReadProcCleanup(buffer, cache, begin_idx, nbytes);
      return false;
    }
    buffer.ReaderIdxForward(2);
    cache.argv.emplace_back(arg);
  }
  return true;
}

void Server::AuxiliaryReadProcParseErrorHandling(Session *session) {
  if (!session) return;
  /* Fill write buffer with error msg and send them back to client */
  FillErrorMsg(session->write_buf, ErrType::WRONGREQ, "unidentified request");
  /* trigger write */
  session->SetWrite();
  loop_->epoller->ModifySession(session);
}

void Server::ReadProc(Session *session, bool &closed) {
  int fd = session->fd;
  Buffer &buffer = session->read_buf;
  CommandCache &cache = session->cache;
  char buf[64];
  int nbytes = ReadToBuf(fd, buf, sizeof(buf));
  if (nbytes == 0) {
    /* close connection */
    /* TODO 删除连接处理, 资源释放等工作 */
    session->watched = false;
    buffer.Reset();
    close(fd);
    sessions_.erase(session->name);
    std::cout << "Client exit, now close connection...\n";
    closed = true;
    return;
  }
  std::cout << "received bytes = " << nbytes << std::endl;
  buffer.Append(buf, nbytes);
  auto show_buffer = [&]() {
    std::string ans = buffer.ReadableAsString();
    std::cout << "Buffer => ";
    for (auto &ch : ans) {
      if (ch == '\r') {
        std::cout << "\\r";
      } else if (ch == '\n') {
        std::cout << "\\n";
      } else {
        std::cout << ch;
      }
    }
    std::cout << std::endl;
  };
  show_buffer();
  /*　parse request */
  /* continue processing from cache */
  size_t parse_start_idx = buffer.BeginReadIdx();
  if (cache.inited && cache.argc > cache.argv.size()) {
    if (!AuxiliaryReadProc(buffer, cache, nbytes)) {
      std::cout << "server.cpp-179\n";
      AuxiliaryReadProcParseErrorHandling(session);
      return;
    }
  } else { /* cache not inited or this is a brand new command request */
    parse_protocol_new_request:
    if (buffer.ReadStdString(1) == "*") { /* a brand new command */
      buffer.ReaderIdxForward(1);
      /* argc */
      size_t step;
      cache.argc = buffer.ReadLongAndForward(step);
      if (buffer.ReadStdString(2) != kCRLF) {
        AuxiliaryReadProcCleanup(buffer, cache, parse_start_idx, nbytes);
        std::cout << "server.cpp-192\n";
        AuxiliaryReadProcParseErrorHandling(session);
        return;
      }
      if (buffer.ReadStdString(2) != kCRLF) {
        std::cout << "server.cpp-197\n";
        AuxiliaryReadProcCleanup(buffer, cache, parse_start_idx, nbytes);
        AuxiliaryReadProcParseErrorHandling(session);
        return;
      }
      buffer.ReaderIdxForward(2);
      cache.inited = true;
      if (!AuxiliaryReadProc(buffer, cache, nbytes)) {
        std::cout << "server.cpp-205\n";
        AuxiliaryReadProcParseErrorHandling(session);
        return;
      }
    } else {
      std::cout << "server.cpp-210\n";
      std::cout << "buffer front = " << *(buffer.BufferFront()) << std::endl;
      show_buffer();
      AuxiliaryReadProcCleanup(buffer, cache, parse_start_idx, nbytes);
      AuxiliaryReadProcParseErrorHandling(session);
      return;
    }
  }
  /* successfully parse one whole request, use it to operator the database */
  if (cache.argc != 0 && cache.argc == cache.argv.size()) {
    std::cout << "Command fully received, they are, argc = " << cache.argc << ", argv = ";
    for (auto &arg : cache.argv) {
      std::cout << arg << " ";
    }
    /* clear cache when one command is fully parsed */
    /* TODO use command to make request to the database and organize response and reply */
    std::cout << std::endl;
    std::string handle_result = engine_->HandleCommand(cache);
    session->write_buf.Append(handle_result);
    session->SetWrite();
    loop_->epoller->ModifySession(session);/* FIXME no need to modify every time */
    cache.Clear();
    /* handle multiple request commands received in one read */
    if (buffer.ReadableBytes() > 0 && buffer.ReadStdString(1) == "*") {
      goto parse_protocol_new_request;
    }
  }
  std::cout << "===============================" << std::endl;
}

void Server::FillErrorMsg(Buffer &buffer, ErrType errtype, const char *msg) const {
  const char *errtype_str = kErrStrTable[errtype];
  auto err_str = "-" + std::string(errtype_str) + ' ' + msg + kCRLF;
  buffer.Append(err_str);
}

void Server::WriteProc(Session *session, bool &closed) {
  /* TODO handle write process */
  int fd = session->fd;
  Buffer &buffer = session->write_buf;
  if (buffer.ReadableBytes() <= 0) {
    /* nothing to write */
    session->SetRead();
    loop_->epoller->ModifySession(session);
    return;
  }
  std::cout << "Doing write process, write buffer is => " << buffer.ReadableAsString() << std::endl;
  /* ensure all data has been sent, then unregister EPOLLOUT to this fd */
  size_t readable_bytes = buffer.ReadableBytes();
  int nbytes = WriteFromBuf(fd, static_cast<const char *>(buffer.BeginRead()), readable_bytes);
  std::cout << "Send " << nbytes << " bytes response to client\n";
  /* TODO optimize */
  if ((size_t)nbytes == readable_bytes) {
    session->SetRead();
    loop_->epoller->ModifySession(session);
    buffer.Reset();
  }
}