#ifndef ROCKET_NET_TCP_SERVER_H
#define ROCKET_NET_TCP_SERVER_H

#include <set>
#include "rocket/net/tcp/tcp_acceptor.h"
#include "rocket/net/tcp/tcp_connection.h"
#include "rocket/net/tcp/net_addr.h"
#include "rocket/net/eventloop.h"
#include "rocket/net/io_thread_group.h"


#include<infiniband/verbs.h>


namespace rocket {
  class TcpServer {
  public:
    TcpServer(NetAddr::s_ptr local_addr);

    ~TcpServer();

    void start();

    void resources_init_server(resources* res);

    int resources_create_server(resources* res);


  private:
    void init();

    // 当有新客户端连接之后需要执行
    void onAccept();

    // 清除 closed 的连接
    void ClearClientTimerFunc();


  private:
    TcpAcceptor::s_ptr m_acceptor;

    NetAddr::s_ptr m_local_addr;    // 本地监听地址

    EventLoop* m_main_event_loop{ NULL };    // mainReactor

    IOThreadGroup* m_io_thread_group{ NULL };   // subReactor 组

    FdEvent* m_listen_fd_event;

    int m_client_counts{ 0 };

    std::set<TcpConnection::s_ptr> m_client;

    TimerEvent::s_ptr m_clear_client_timer_event;

    struct resources m_res;

    int connect_qp(resources* res);

    int poll_completion(resources *res);

    int sock_sync_data(int sock, int xfer_size, char *local_data, char *remote_data);

    config_t m_config = {
        NULL,  /* dev_name */
        NULL,  /* server_name */
        12345, /* tcp_port */
        1,	   /* ib_port */
        5 /* gid_idx */ };

  };

}


#endif