#include <sys/socket.h>
#include <unistd.h>
#include <string.h>
#include <iostream>
#include "rocket/common/log.h"
#include "rocket/net/tcp/tcp_client.h"
#include "rocket/net/eventloop.h"
#include "rocket/net/fd_event_group.h"
#include "rocket/common/error_code.h"
#include "rocket/net/tcp/net_addr.h"

#include<infiniband/verbs.h>

namespace rocket {

  TcpClient::TcpClient(NetAddr::s_ptr peer_addr) : m_peer_addr(peer_addr) {
    m_event_loop = EventLoop::GetCurrentEventLoop();
    m_fd = socket(peer_addr->getFamily(), SOCK_STREAM, 0);

    if (m_fd < 0) {
      ERRORLOG("TcpClient::TcpClient() error, failed to create fd");
      return;
    }

    m_res->sock = m_fd;
    std::cout<<"m_res->sock="<<m_res->sock<<std::endl;
    resources_init_client(m_res);
    resources_create_client(m_res);

    m_fd_event = FdEventGroup::GetFdEventGroup()->getFdEvent(m_fd);
    m_fd_event->setNonBlock();

    m_connection = std::make_shared<TcpConnection>(m_res, m_event_loop, m_fd, 256, peer_addr, nullptr, TcpConnectionByClient);
    m_connection->setConnectionType(TcpConnectionByClient);

  } 


  TcpClient::~TcpClient() {
    DEBUGLOG("TcpClient::~TcpClient()");
    if (m_fd > 0) {
      close(m_fd);
    }
  }

  void TcpClient::resources_init_client(resources* res) {
    int tempsock=res->sock;
    memset(res, 0, sizeof * res);
    res->sock = tempsock;
    std::cout << "resources_init_client in tcp_client" << std::endl;
  }

  int TcpClient::resources_create_client(resources* res) {
    std::cout<<"resources_create_client in tcp_client"<<std::endl;
    ibv_device** dev_list = NULL;
    ibv_qp_init_attr qp_init_attr;
    ibv_device* ib_dev = NULL;

    size_t m_size;

    int num_devices;
    int rc = 0;
    dev_list = ibv_get_device_list(&num_devices);
    if (!dev_list)
    {
      std::cout << "failed to get IB devices list" << std::endl;
      rc = 1;
    }
    if (!num_devices) {
      std::cout << "found none devices" << std::endl;
      rc = 1;
    }
    std::cout << "found" << num_devices << "devices" << std::endl;
    for (int i = 0; i < num_devices; i++)
    {
      if (!m_config.dev_name)
      {
        m_config.dev_name = strdup(ibv_get_device_name(dev_list[i]));
        fprintf(stdout, "device not specified, using first one found: %s\n", m_config.dev_name);
      }
      if (!strcmp(ibv_get_device_name(dev_list[i]), m_config.dev_name))
      {
        ib_dev = dev_list[i];
        break;
      }
    }
    if (!ib_dev) {
      std::cout << "IB device " << m_config.dev_name << "wasn't found" << std::endl;
      rc = 1;
    }
    m_res->ib_ctx = ibv_open_device(ib_dev);
    if (!m_res->ib_ctx)
    {
      std::cout << "failed to open device" << m_config.dev_name << std::endl;
      rc = 1;
    }
    ibv_free_device_list(dev_list);
    dev_list = NULL;
    if (ibv_query_port(m_res->ib_ctx, m_config.ib_port, &m_res->port_attr))
    {
      std::cout << "ibv_query_port on port" << m_config.ib_port << "failed" << std::endl;
      rc = 1;
    }

    m_res->pd = ibv_alloc_pd(m_res->ib_ctx);
    if (!m_res->pd)
    {
      std::cout << "ibv_alloc_pd failed" << std::endl;
      rc = 1;
    }
    return rc;

  }

  TcpConnection::s_ptr TcpClient::getConnection() {
    return m_connection;
  }


  resources* TcpClient::getRes() {
    return m_res;
  } 

  // 异步的进行 conenct
  // 如果connect 成功，done 会被执行
  void TcpClient::connect(std::function<void()> done) {
    std::cout<<"connect in tcp_client"<<std::endl;
    int rt = ::connect(m_fd, m_peer_addr->getSockAddr(), m_peer_addr->getSockLen());
    if (rt == 0) {
      DEBUGLOG("connect [%s] sussess", m_peer_addr->toString().c_str());
      m_connection->setState(Connected);
      initLocalAddr();
      if (done) {
        done();
      }
    }
    else if (rt == -1) {
      if (errno == EINPROGRESS) {
        // epoll 监听可写事件，然后判断错误码
        m_fd_event->listen(FdEvent::OUT_EVENT,
          [this, done]() {
            int rt = ::connect(m_fd, m_peer_addr->getSockAddr(), m_peer_addr->getSockLen());
            if ((rt < 0 && errno == EISCONN) || (rt == 0)) {
              DEBUGLOG("connect [%s] sussess", m_peer_addr->toString().c_str());
              initLocalAddr();
              m_connection->setState(Connected);
            }
            else {
              if (errno == ECONNREFUSED) {
                m_connect_error_code = ERROR_PEER_CLOSED;
                m_connect_error_info = "connect refused, sys error = " + std::string(strerror(errno));
              }
              else {
                m_connect_error_code = ERROR_FAILED_CONNECT;
                m_connect_error_info = "connect unkonwn error, sys error = " + std::string(strerror(errno));
              }
              ERRORLOG("connect errror, errno=%d, error=%s", errno, strerror(errno));
              close(m_fd);
              m_fd = socket(m_peer_addr->getFamily(), SOCK_STREAM, 0);
            }

            // 连接完后需要去掉可写事件的监听，不然会一直触发
            m_event_loop->deleteEpollEvent(m_fd_event);
            DEBUGLOG("now begin to done");
            // 如果连接完成，才会执行回调函数
            if (done) {
              done();
            }
          }
        );
        m_event_loop->addEpollEvent(m_fd_event);

        if (!m_event_loop->isLooping()) {
          m_event_loop->loop();
        }
      }
      else {
        ERRORLOG("connect errror, errno=%d, error=%s", errno, strerror(errno));
        m_connect_error_code = ERROR_FAILED_CONNECT;
        m_connect_error_info = "connect error, sys error = " + std::string(strerror(errno));
        if (done) {
          done();
        }
      }
    }

  }


  void TcpClient::stop() {
    if (m_event_loop->isLooping()) {
      m_event_loop->stop();
    }
  }

  // 异步的发送 message
  // 如果发送 message 成功，会调用 done 函数， 函数的入参就是 message 对象 
  void TcpClient::writeMessage(AbstractProtocol::s_ptr message, std::function<void(AbstractProtocol::s_ptr)> done) {
    // 1. 把 message 对象写入到 Connection 的 buffer, done 也写入
    // 2. 启动 connection 可写事件
    std::cout<<"writeMessage in tcp_client"<<std::endl;
    
    m_connection->pushSendMessage(message, done);
    m_connection->listenWrite();
  }




  // 异步的读取 message
  // 如果读取 message 成功，会调用 done 函数， 函数的入参就是 message 对象 
  void TcpClient::readMessage(const std::string& msg_id, std::function<void(AbstractProtocol::s_ptr)> done) {
    // 1. 监听可读事件
    // 2. 从 buffer 里 decode 得到 message 对象, 判断是否 msg_id 相等，相等则读成功，执行其回调
    m_connection->pushReadMessage(msg_id, done);
    m_connection->listenRead();
  }

  int TcpClient::getConnectErrorCode() {
    return m_connect_error_code;
  }

  std::string TcpClient::getConnectErrorInfo() {
    return m_connect_error_info;

  }

  NetAddr::s_ptr TcpClient::getPeerAddr() {
    return m_peer_addr;
  }

  NetAddr::s_ptr TcpClient::getLocalAddr() {
    return m_local_addr;
  }

  void TcpClient::initLocalAddr() {
    sockaddr_in local_addr;
    socklen_t len = sizeof(local_addr);

    int ret = getsockname(m_fd, reinterpret_cast<sockaddr*>(&local_addr), &len);
    if (ret != 0) {
      ERRORLOG("initLocalAddr error, getsockname error. errno=%d, error=%s", errno, strerror(errno));
      return;
    }

    m_local_addr = std::make_shared<IPNetAddr>(local_addr);

  }


  void TcpClient::addTimerEvent(TimerEvent::s_ptr timer_event) {
    m_event_loop->addTimerEvent(timer_event);
  }

  int TcpClient::getFd() {
    return m_fd;
  }

}