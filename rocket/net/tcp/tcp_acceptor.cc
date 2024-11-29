#include <assert.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <string.h>
#include <iostream>
#include "rocket/common/log.h"
#include "rocket/net/tcp/net_addr.h"
#include "rocket/net/tcp/tcp_acceptor.h"


namespace rocket {

  TcpAcceptor::TcpAcceptor(NetAddr::s_ptr local_addr) : m_local_addr(local_addr) {
    std::cout << "TcpAcceptor begin" << std::endl;
    if (!local_addr->checkValid()) {
      std::cout << "TcpAcceptor checkValid" << std::endl;
      ERRORLOG("invalid local addr %s", local_addr->toString().c_str());
      exit(0);
    }

    m_family = m_local_addr->getFamily();

    m_listenfd = socket(m_family, SOCK_STREAM, 0);

    // std::cout << "m_listenfd" << m_listenfd << std::endl;

    if (m_listenfd < 0) {
      std::cout << "TcpAcceptor socket" << std::endl;
      ERRORLOG("invalid listenfd %d", m_listenfd);
      exit(0);
    }

    int val = 1;
    if (setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)) != 0) {
      std::cout << "TcpAcceptor setsockopt" << std::endl;
      ERRORLOG("setsockopt REUSEADDR error, errno=%d, error=%s", errno, strerror(errno));
    }

    socklen_t len = m_local_addr->getSockLen();
    printf("SockAddr: %p, SockLen: %d\n", m_local_addr->getSockAddr(), m_local_addr->getSockLen());
    if (bind(m_listenfd, m_local_addr->getSockAddr(), len) != 0) {
      std::cout << "TcpAcceptor bind" << std::endl;
      std::cerr << "bind error, errno=" << errno << ", error=" << strerror(errno) << std::endl;
      exit(0);
    }

    if (listen(m_listenfd, 1000) != 0) {
      std::cout << "TcpAcceptor listen" << std::endl;
      ERRORLOG("listen error, errno=%d, error=%s", errno, strerror(errno));
      exit(0);
    }
    std::cout << "TcpAcceptor end" << std::endl;

  }

  TcpAcceptor::~TcpAcceptor() {
  }


  int TcpAcceptor::getListenFd() {
    return m_listenfd;
  }


  std::pair<int, NetAddr::s_ptr> TcpAcceptor::accept(int& m_res_sock) {
    if (m_family == AF_INET) {
      sockaddr_in client_addr;
      memset(&client_addr, 0, sizeof(client_addr));
      socklen_t clien_addr_len = sizeof(clien_addr_len);

      int client_fd = ::accept(m_listenfd, reinterpret_cast<sockaddr*>(&client_addr), &clien_addr_len);
      if (client_fd < 0) {
        ERRORLOG("accept error, errno=%d, error=%s", errno, strerror(errno));
      }
      m_res_sock = client_fd;
      std::cout << "accept m_res_sock is" << m_res_sock << std::endl;
      IPNetAddr::s_ptr peer_addr = std::make_shared<IPNetAddr>(client_addr);
      std::cout << "tcp_server connect success" << std::endl;
      INFOLOG("A client have accpeted succ, peer addr [%s]", peer_addr->toString().c_str());

      return std::make_pair(client_fd, peer_addr);
    }
    else {
      // ...
      return std::make_pair(-1, nullptr);
    }

  }

}