#ifndef ROCKET_NET_TCP_TCP_CONNECTION_H
#define ROCKET_NET_TCP_TCP_CONNECTION_H

#include <memory>
#include <map>
#include <queue>
#include "rocket/net/tcp/net_addr.h"
#include "rocket/net/tcp/tcp_buffer.h"
#include "rocket/net/io_thread.h"
#include "rocket/net/coder/abstract_coder.h"
#include "rocket/net/rpc/rpc_dispatcher.h"

#include<infiniband/verbs.h>

namespace rocket {

  enum TcpState {
    NotConnected = 1,
    Connected = 2,
    HalfClosing = 3,
    Closed = 4,
  };

  enum TcpConnectionType {
    TcpConnectionByServer = 1,  // 作为服务端使用，代表跟对端客户端的连接
    TcpConnectionByClient = 2,  // 作为客户端使用，代表跟对端服务端的连接
  };

  struct config_t
  {
    const char* dev_name; /* IB device name */
    char* server_name;	  /* server host name */
    u_int32_t tcp_port;	  /* server TCP port */
    int ib_port;		  /* local IB port to work with */
    int gid_idx;		  /* gid index to use */
  };

  /* structure to exchange data which is needed to connect the QPs */
  struct cm_con_data_t
  {
    uint64_t send_addr;//发送缓冲区地址
    uint32_t send_rkey;//发送缓冲区rkey

    uint64_t recv_addr;	 //接收缓冲区地址
    uint32_t recv_rkey;	 //接收缓冲区的rkey

    uint32_t qp_num; /* QP number */
    uint16_t lid;	 /* LID of the IB port */
    uint8_t gid[16]; /* gid */
  } __attribute__((packed));

  struct resources
  {

    
    struct ibv_device_attr device_attr;
    /* Device attributes */
    struct ibv_port_attr port_attr;	   /* IB port attributes */
    struct cm_con_data_t remote_props; /* values to connect to remote side */
    struct ibv_context* ib_ctx;		   /* device handle */
    struct ibv_pd* pd;				   /* PD handle */
    struct ibv_cq* cq;				   /* CQ handle */
    struct ibv_qp* qp;				   /* QP handle */
    struct ibv_mr* in_mr;				   /* MR handle for buf */
    struct ibv_mr* out_mr;				   /* MR handle for buf */  
    std::vector<char> in_buf;		 /* memory buffer pointer, used for RDMA and send ops */
    std::vector<char> out_buf;   /* memory buffer pointer, used for RDMA and send*/
    int sock;						   /* TCP socket file descriptor */
    //用于生成fd的completion channel，每个客户端一个，服务器端
    //struct ibv_comp_channel* comp_channel; /* completion channel */ 
    bool flag=false;//qp是否已经是rts状态
  };


  class TcpConnection {
  public:

    typedef std::shared_ptr<TcpConnection> s_ptr;


  public:
    TcpConnection(resources* res, EventLoop* event_loop, int fd, int buffer_size, NetAddr::s_ptr peer_addr, NetAddr::s_ptr local_addr, TcpConnectionType type = TcpConnectionByServer);

    ~TcpConnection();
    
    //void resources_init(resources* res);

    int resources_create(resources* res); 

    int connect_qp(resources* res);

    int sock_sync_data(int sock, int xfer_size, char* local_data, char* remote_data);

    int modify_qp_to_init(ibv_qp* qp);

    int modify_qp_to_rtr(struct ibv_qp* qp, uint32_t remote_qpn, uint16_t dlid, uint8_t* dgid);

    int modify_qp_to_rts(struct ibv_qp* qp);

    int post_receive(resources* res);

    int post_send(resources* res, int opcode);

    int poll_completion(resources* res);

    void onRead();

    void excute();

    void onWrite();

    void setState(const TcpState state);

    TcpState getState();

    void clear();

    int getFd();

    // 服务器主动关闭连接
    void shutdown();

    void setConnectionType(TcpConnectionType type);

    // 启动监听可写事件
    void listenWrite();

    // 启动监听可读事件
    void listenRead();

    void pushSendMessage(AbstractProtocol::s_ptr message, std::function<void(AbstractProtocol::s_ptr)> done);

    void pushReadMessage(const std::string& msg_id, std::function<void(AbstractProtocol::s_ptr)> done);

    NetAddr::s_ptr getLocalAddr();

    NetAddr::s_ptr getPeerAddr();

    void reply(std::vector<AbstractProtocol::s_ptr>& replay_messages);

    int set_blocking(int& sock);//套接字改成阻塞模式

    int set_nonblocking(int& sock);//套接字改成非阻塞模式



  private:

    EventLoop* m_event_loop{ NULL };   // 代表持有该连接的 IO 线程

    NetAddr::s_ptr m_local_addr;
    NetAddr::s_ptr m_peer_addr;

    TcpBuffer::s_ptr m_in_buffer;   // 接收缓冲区
    TcpBuffer::s_ptr m_out_buffer;  // 发送缓冲区

    FdEvent* m_fd_event{ NULL };

    AbstractCoder* m_coder{ NULL };

    TcpState m_state;

    int m_fd{ 0 };

    TcpConnectionType m_connection_type{ TcpConnectionByServer };

    // std::pair<AbstractProtocol::s_ptr, std::function<void(AbstractProtocol::s_ptr)>>
    std::vector<std::pair<AbstractProtocol::s_ptr, std::function<void(AbstractProtocol::s_ptr)>>> m_write_dones;

    // key 为 msg_id
    std::map<std::string, std::function<void(AbstractProtocol::s_ptr)>> m_read_dones;

    resources* m_res;

    config_t m_config = {
      NULL,  /* dev_name */
      NULL,  /* server_name */
      12345, /* tcp_port */
      1,	   /* ib_port */
      5 /* gid_idx */ };
  };

}

#endif
