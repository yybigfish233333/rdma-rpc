#include "rocket/net/tcp/tcp_server.h"
#include "rocket/net/eventloop.h"
#include "rocket/net/tcp/tcp_connection.h"
#include "rocket/common/log.h"
#include "rocket/common/config.h"


#include <unistd.h>


namespace rocket {

  TcpServer::TcpServer(NetAddr::s_ptr local_addr) : m_local_addr(local_addr) {

    init();

    INFOLOG("rocket TcpServer listen sucess on [%s]", m_local_addr->toString().c_str());
  }

  TcpServer::~TcpServer() {
    if (m_main_event_loop) {
      delete m_main_event_loop;
      m_main_event_loop = NULL;
    }
    if (m_io_thread_group) {
      delete m_io_thread_group;
      m_io_thread_group = NULL;
    }
    if (m_listen_fd_event) {
      delete m_listen_fd_event;
      m_listen_fd_event = NULL;
    }
  }


  void TcpServer::resources_init_server(resources* res)
  {
    memset(res, 0, sizeof * res);
    res->sock = -1;
    std::cout << "resources_init_server in tcp_server" << std::endl;
  }

  int TcpServer::resources_create_server(resources* res) {
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
    m_res.ib_ctx = ibv_open_device(ib_dev);
    if (!m_res.ib_ctx)
    {
      std::cout << "failed to open device" << m_config.dev_name << std::endl;
      rc = 1;
    }
    ibv_free_device_list(dev_list);
    dev_list = NULL;
    ib_dev = NULL;
    if (ibv_query_port(m_res.ib_ctx, m_config.ib_port, &m_res.port_attr))
    {
      std::cout << "ibv_query_port on port" << m_config.ib_port << "failed" << std::endl;
      rc = 1;
    }

    //整个服务器端只创建一个pd
    m_res.pd = ibv_alloc_pd(m_res.ib_ctx);
    if (!m_res.pd)
    {
      std::cout << "ibv_alloc_pd failed" << std::endl;
      rc = 1;
    }
    return rc;
  }


  void TcpServer::init() {

    resources_init_server(&m_res);

    m_acceptor = std::make_shared<TcpAcceptor>(m_local_addr);

    m_res.sock = m_acceptor->getListenFd();

    resources_create_server(&m_res);

    m_main_event_loop = EventLoop::GetCurrentEventLoop();
    m_io_thread_group = new IOThreadGroup(Config::GetGlobalConfig()->m_io_threads);

    m_listen_fd_event = new FdEvent(m_acceptor->getListenFd());
    m_listen_fd_event->listen(FdEvent::IN_EVENT, std::bind(&TcpServer::onAccept, this));

    m_main_event_loop->addEpollEvent(m_listen_fd_event);



    //定时清理关闭连接的客户端
    m_clear_client_timer_event = std::make_shared<TimerEvent>(5000, true, std::bind(&TcpServer::ClearClientTimerFunc, this));
    m_main_event_loop->addTimerEvent(m_clear_client_timer_event);

  }

  void TcpServer::onAccept() {
    auto re = m_acceptor->accept(m_res.sock);
    std::cout<<"onaccept m_res_sock is"<<m_res.sock<<std::endl;
    int client_fd = re.first;
    NetAddr::s_ptr peer_addr = re.second;

    m_client_counts++;

    // 把 clientfd 添加到任意 IO 线程里面
    IOThread* io_thread = m_io_thread_group->getIOThread();
    TcpConnection::s_ptr connetion = std::make_shared<TcpConnection>(&m_res, io_thread->getEventLoop(), client_fd, 256, peer_addr, m_local_addr);
    connetion->setState(Connected);
    m_client.insert(connetion);
    connetion->connect_qp(&m_res);  
    connetion->listenRead();



    INFOLOG("TcpServer succ get client, fd=%d", client_fd);
  }

  void TcpServer::start() {
    m_io_thread_group->start();
    m_main_event_loop->loop();
  }


  void TcpServer::ClearClientTimerFunc() {
    auto it = m_client.begin();
    for (it = m_client.begin(); it != m_client.end(); ) {
      // TcpConnection::ptr s_conn = i.second;
      // DebugLog << "state = " << s_conn->getState();
      if ((*it) != nullptr && (*it).use_count() > 0 && (*it)->getState() == Closed) {
        // need to delete TcpConnection
        DEBUGLOG("TcpConection [fd:%d] will delete, state=%d", (*it)->getFd(), (*it)->getState());
        it = m_client.erase(it);
      }
      else {
        it++;
      }

    }

  }

}