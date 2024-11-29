#include <unistd.h>
#include "rocket/common/log.h"
#include "rocket/net/fd_event_group.h"
#include "rocket/net/tcp/tcp_connection.h"
#include "rocket/net/coder/string_coder.h"
#include "rocket/net/coder/tinypb_coder.h"

#include <fcntl.h>
#include<thread>
#include<chrono>  
namespace rocket {


  TcpConnection::TcpConnection(resources* res, EventLoop* event_loop, int fd, int buffer_size, NetAddr::s_ptr peer_addr, NetAddr::s_ptr local_addr, TcpConnectionType type /*= TcpConnectionByServer*/)
    : m_res(res), m_event_loop(event_loop), m_local_addr(local_addr), m_peer_addr(peer_addr), m_state(NotConnected), m_fd(fd), m_connection_type(type) {
    m_in_buffer = std::make_shared<TcpBuffer>(buffer_size);
    m_out_buffer = std::make_shared<TcpBuffer>(buffer_size);

    m_fd_event = FdEventGroup::GetFdEventGroup()->getFdEvent(fd);
    m_fd_event->setNonBlock();

    m_coder = new TinyPBCoder();

    //resources_init(m_res);
   // std::cout << "tcpconnection1 m_res->sock=" << m_res->sock << std::endl;

    resources_create(m_res);
    // connect_qp(m_res);
    //std::cout << "tcpconnection2 m_res->sock=" << m_res->sock << std::endl;


    // if (m_connection_type == TcpConnectionByServer) {
    //   //std::this_thread::sleep_for(std::chrono::microseconds(500));

    //   std::cout << "i am server ,i am now listenread" << std::endl;
    //   listenRead();//如果是服务器端创建的连接（指的是服务器处理客户端的请求）
    //   //则监听可读事件，感觉在这里可以进行connect_qp
    // }

  }




  TcpConnection::~TcpConnection() {
    DEBUGLOG("~TcpConnection");
    if (m_coder) {
      delete m_coder;
      m_coder = NULL;
    }
  }



  // void TcpConnection::resources_init(resources* res) {
  //   std::cout << "i am here in resources_init" << std::endl;
  //   memset(res, 0, sizeof * res);
  //   res->sock = -1;
  // }

  int TcpConnection::resources_create(resources* res) {
    int tempsock = m_res->sock;
    std::cout << "i am here in resources_create" << std::endl;
    size_t m_in_size = m_in_buffer->m_buffer.size();
    size_t m_out_size = m_out_buffer->m_buffer.size();

    std::cout << "m_in_size=" << m_in_size << std::endl;
    std::cout << "m_out_size=" << m_out_size << std::endl;
    int i;
    int mr_flag = 0;
    int num_devices;
    int rc = 0;

    int cq_size = 1;

    res->cq = ibv_create_cq(res->ib_ctx, cq_size, NULL, NULL, 0);
    if (!res->cq) {
      std::cerr << "failed to create CQ with" << cq_size << "entries" << std::endl;
      rc = 1;
    }
    // ibv_req_notify_cq(res->cq, 0);//请求通知
    //!这里打算注册两个mr，
    //!一个是输入缓冲区，一个是输出缓冲区

    res->in_buf = m_in_buffer->m_buffer;//这里将输入缓冲区注册为mr
    std::cout << "m_in_buffer data address: " << static_cast<void*>(m_in_buffer->m_buffer.data()) << std::endl;

    mr_flag = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE;
    res->in_mr = ibv_reg_mr(res->pd, res->in_buf.data(), m_in_size, mr_flag);
    if (!res->in_mr) {
      std::cerr << "in_mr ibv_reg_mr failed with mr_flags=0x" << mr_flag << std::endl;
      rc = 1;
    }

    res->out_buf = m_out_buffer->m_buffer;//这里将输出缓冲区注册为mr
    std::cout << "m_out_buffer data address: " << static_cast<void*>(m_out_buffer->m_buffer.data()) << std::endl;

    res->out_mr = ibv_reg_mr(res->pd, res->out_buf.data(), m_out_size, mr_flag);
    if (!res->out_mr) {
      std::cerr << "out_mr ibv_reg_mr failed with mr_flags=0x" << mr_flag << std::endl;
      rc = 1;
    }

    std::cout << "IN_MR was registered with addr=" << static_cast<void*>(res->in_buf.data())
      << ", lkey=0x" << std::hex << res->in_mr->lkey << std::dec
      << ", rkey=0x" << std::hex << res->in_mr->rkey << std::dec
      << ", flags=0x" << std::hex << mr_flag << std::dec
      << std::endl;

    std::cout << "OUT_MR was registered with addr=" << static_cast<void*>(res->out_buf.data())
      << ", lkey=0x" << std::hex << res->out_mr->lkey << std::dec
      << ", rkey=0x" << std::hex << res->out_mr->rkey << std::dec
      << ", flags=0x" << std::hex << mr_flag << std::dec
      << std::endl;

    ibv_qp_init_attr qp_init_attr;
    memset(&qp_init_attr, 0, sizeof(qp_init_attr));
    qp_init_attr.qp_type = IBV_QPT_RC;
    qp_init_attr.sq_sig_all = 1;//设置为1时，sq的每个wr都需要发送完成通知
    qp_init_attr.send_cq = res->cq;
    qp_init_attr.recv_cq = res->cq;
    qp_init_attr.cap.max_send_wr = 5;
    qp_init_attr.cap.max_recv_wr = 5;
    qp_init_attr.cap.max_send_sge = 1;
    //每个wr中的sge的数量，这里设置为1，即每个wr中只能有一段内存区域
    qp_init_attr.cap.max_recv_sge = 1;
    res->qp = ibv_create_qp(res->pd, &qp_init_attr);
    if (!res->qp) {
      std::cerr << "failed to create QP" << std::endl;
      rc = 1;
    }

    return rc;
  }


  int TcpConnection::set_blocking(int& sock) {
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags == -1) {
      std::cout << "Error getting socket flags" << std::endl;
      return -1;
    }
    // 清除非阻塞标志
    flags &= ~O_NONBLOCK;
    if (fcntl(sock, F_SETFL, flags) == -1) {
      perror("Error setting socket to blocking mode");
      return -1;
    }

    std::cout << "Socket set to blocking mode." << std::endl;
    return 0;
  }

  int TcpConnection::set_nonblocking(int& sock) {
    int flags = fcntl(sock, F_GETFL, 0); // 获取当前标志
    if (flags == -1) {
      std::cout << "Error getting socket flags" << std::endl;
      return -1;
    }

    // 设置非阻塞标志
    flags |= O_NONBLOCK;
    if (fcntl(sock, F_SETFL, flags) == -1) {
      std::cout << "Error setting socket to non-blocking mode" << std::endl;
      return -1;
    }

    std::cout << "Socket set to non-blocking mode." << std::endl;
    return 0;
  }

  int TcpConnection::sock_sync_data(int sock, int xfer_size, char* local_data, char* remote_data) {
    int rc = 0;
    int total_written_bytes = 0;
    int total_read_bytes = 0;

    // 写入数据
    while (total_written_bytes < xfer_size) {
      rc = write(sock, local_data + total_written_bytes, xfer_size - total_written_bytes);
      if (rc <= 0) {
        fprintf(stderr, "Error: Failed writing data during sock_sync_data, %s (errno=%d)\n", strerror(errno), errno);
        return -1;
      }
      total_written_bytes += rc;
    }
    fprintf(stderr, "Debug: Successfully wrote %d bytes\n", total_written_bytes);

    // 读取数据
    while (total_read_bytes < xfer_size) {
      rc = read(sock, remote_data + total_read_bytes, xfer_size - total_read_bytes);
      if (rc > 0) {
        total_read_bytes += rc;
      }
      else if (rc == 0) {
        fprintf(stderr, "Error: Connection closed by peer during sock_sync_data\n");
        return -1; // 对端关闭连接
      }
      else {
        fprintf(stderr, "Error: Failed reading data during sock_sync_data, %s (errno=%d)\n", strerror(errno), errno);
        return -1; // 读取失败
      }
    }
    fprintf(stderr, "Debug: Successfully read %d bytes\n", total_read_bytes);

    return 0; // 成功
  }


  int TcpConnection::modify_qp_to_init(ibv_qp* qp) {

    ibv_qp_attr attr;
    int flags;
    int rc;
    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_INIT;
    attr.port_num = m_config.ib_port;
    attr.pkey_index = 0;
    attr.qp_access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE;
    flags = IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS;
    rc = ibv_modify_qp(qp, &attr, flags);
    if (rc)
      fprintf(stderr, "failed to modify QP state to INIT\n");
    return rc;

  }

  int TcpConnection::modify_qp_to_rtr(struct ibv_qp* qp, uint32_t remote_qpn, uint16_t dlid, uint8_t* dgid) {
    ibv_qp_attr attr;
    int flags;
    int rc;
    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_RTR;
    attr.path_mtu = IBV_MTU_256;
    attr.dest_qp_num = remote_qpn;
    attr.rq_psn = 0;
    attr.max_dest_rd_atomic = 1;
    attr.min_rnr_timer = 12;
    attr.ah_attr.is_global = 0;
    attr.ah_attr.dlid = dlid;
    attr.ah_attr.sl = 0;
    attr.ah_attr.src_path_bits = 0;
    attr.ah_attr.port_num = m_config.ib_port;
    if (m_config.gid_idx >= 0)
    {
      attr.ah_attr.is_global = 1;
      attr.ah_attr.port_num = 1;
      memcpy(&attr.ah_attr.grh.dgid, dgid, 16);
      attr.ah_attr.grh.flow_label = 0;
      attr.ah_attr.grh.hop_limit = 1;
      attr.ah_attr.grh.sgid_index = m_config.gid_idx;
      attr.ah_attr.grh.traffic_class = 0;
    }//这个是如果启用RoCE的话，需要设置全局路由头
    flags = IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN |
      IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER;
    rc = ibv_modify_qp(qp, &attr, flags);
    if (rc)
      std::cerr << "failed to modify QP state to RTR" << std::endl;
    return rc;
  }

  int TcpConnection::modify_qp_to_rts(struct ibv_qp* qp) {
    struct ibv_qp_attr attr;
    int flags;
    int rc;
    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_RTS;//qp的状态变为ready to send
    attr.timeout = 0x12;//超时时间
    attr.retry_cnt = 7;//重试次数
    attr.rnr_retry = 0;//指定接收未准备重试次数，表示不重试
    attr.sq_psn = 0;//发送队列的起始包序号
    attr.max_rd_atomic = 1;//最大并发远端原子操作请求数量。
    flags = IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT |
      IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC;
    rc = ibv_modify_qp(qp, &attr, flags);
    if (rc)
      std::cerr << "failed to modify QP state to RTS" << std::endl;
    else {
      std::cout << "modify_qp_to_rts success" << std::endl;
    }
    return rc;
  }

  int TcpConnection::post_receive(resources* res) {
    ibv_recv_wr rr;
    ibv_sge sge;
    ibv_recv_wr* bad_wr;
    int rc;
    memset(&sge, 0, sizeof(sge));
    sge.addr = (uintptr_t)res->in_buf.data();
    sge.length = 256;
    sge.lkey = res->in_mr->lkey;
    memset(&rr, 0, sizeof(rr));
    rr.wr_id = 0;
    rr.next = NULL;
    rr.sg_list = &sge;
    rr.num_sge = 1;
    rc = ibv_post_recv(res->qp, &rr, &bad_wr);
    if (rc)
      std::cerr << "failed to post RR" << std::endl;
    else
      std::cout << "Receive Request was posted" << std::endl;
    return rc;
  }

  int TcpConnection::post_send(resources* res, int opcode) {
    ibv_send_wr sr;
    ibv_sge sge;
    ibv_send_wr* bad_wr = NULL;
    int rc;

    memset(&sge, 0, sizeof(sge));
    sge.addr = (uintptr_t)res->out_buf.data();
    sge.length = 256;
    sge.lkey = res->out_mr->lkey;


    if (!res->out_mr) {
      std::cerr << "Error: Memory region not registered." << std::endl;
      return -1;
    }
    if ((uintptr_t)res->out_buf.data() < (uintptr_t)res->out_mr->addr ||
      (uintptr_t)res->out_buf.data() + sge.length > (uintptr_t)res->out_mr->addr + res->out_mr->length) {
      std::cerr << "Error: SGE address or length out of bounds." << std::endl;
      return -1;
    }

    memset(&sr, 0, sizeof(sr));
    sr.opcode = static_cast<ibv_wr_opcode>(opcode);
    sr.send_flags = IBV_SEND_SIGNALED;//表示操作完成后在完成队列 (CQ) 中生成完成事件。
    sr.sg_list = &sge;
    sr.num_sge = 1;
    sr.wr_id = 0;
    sr.next = NULL;
    if (!sr.sg_list || sr.num_sge <= 0) {
      std::cerr << "Error: sg_list is invalid or num_sge is 0." << std::endl;
      return -1;
    }

    // if (opcode != IBV_WR_SEND)
    // {
    //   sr.wr.rdma.remote_addr = res->remote_props.send_addr;
    //   sr.wr.rdma.rkey = res->remote_props.send_rkey;
    // }

        // 检查 QP 状态
    struct ibv_qp_attr attr;
    struct ibv_qp_init_attr init_attr;
    rc = ibv_query_qp(res->qp, &attr, IBV_QP_STATE, &init_attr);
    if (rc) {
      perror("ibv_query_qp failed");
      std::cerr << "Error querying QP state." << std::endl;
      return rc;
    }
    if (attr.qp_state != IBV_QPS_RTS) {
      std::cerr << "Error: QP not in RTS state. Current state: " << attr.qp_state << std::endl;
      return -1;
    }



    rc = ibv_post_send(res->qp, &sr, &bad_wr);
    if (rc) {
      perror("ibv_post_send failed"); // 打印系统错误信息
      std::cerr << "Failed to post send request, rc=" << rc << std::endl;
      return rc;
    }
    else
      std::cout << "Send Request was posted in post_send" << std::endl;
    return rc;
  }


  int TcpConnection::connect_qp(resources* res) {
    std::cout << "i am here in connect_qp" << std::endl;
    cm_con_data_t local_con_data;//本地连接数据
    cm_con_data_t remote_con_data;//远程连接数据
    cm_con_data_t tmp_con_data;//临时连接数据
    int rc = 0;
    char temp_char;
    union ibv_gid my_gid;

    if (set_blocking(m_res->sock) == 0) {
      std::cout << "Socket is now non-blocking." << std::endl;
    }

    int sock_error = 0;
    socklen_t sock_len = sizeof(sock_error);
    int sock_rc = getsockopt(m_res->sock, SOL_SOCKET, SO_ERROR, &sock_error, &sock_len);
    if (rc == 0 && sock_error == 0) {
      std::cout << "TCP connection is active." << std::endl;
    }
    else {
      std::cerr << "Error: TCP connection failed. Error: " << strerror(sock_error) << std::endl;
    }


    if (sock_sync_data(m_res->sock, 1, "Q", &temp_char) < 0)
    {
      std::cerr << "send Q is failed" << std::endl;
      rc = 1;
    }
    std::cout << "temp char is " << temp_char << std::endl;

    //查询gid，这里gid是5
    rc = ibv_query_gid(m_res->ib_ctx, m_config.ib_port, m_config.gid_idx, &my_gid);
    if (rc)
    {
      std::cout << "could not get gid for port " << m_config.ib_port << " and index " << m_config.gid_idx << std::endl;
      return rc;
    }

    //如果是post_send的话应该是in_buf，先这么写着
    local_con_data.send_addr = bswap_64(reinterpret_cast<uintptr_t>(m_res->out_buf.data()));
    local_con_data.send_rkey = htonl(m_res->out_mr->rkey);

    local_con_data.recv_addr = bswap_64(reinterpret_cast<uintptr_t>(m_res->in_buf.data()));
    local_con_data.recv_rkey = htonl(m_res->in_mr->rkey);

    local_con_data.qp_num = htonl(m_res->qp->qp_num);
    local_con_data.lid = htons(m_res->port_attr.lid);
    memcpy(local_con_data.gid, &my_gid, 16);

    std::cout << "local send_address = 0x" << std::hex << local_con_data.send_addr << std::dec << std::endl;
    std::cout << "local send_rkey=0x" << std::hex << local_con_data.send_rkey << std::dec << std::endl;

    std::cout << "local recv_address = 0x" << std::hex << local_con_data.recv_addr << std::dec << std::endl;
    std::cout << "local recv_rkey=0x" << std::hex << local_con_data.recv_rkey << std::dec << std::endl;

    std::cout << "local QP number=0x" << std::hex << local_con_data.qp_num << std::dec << std::endl;
    std::cout << "local LID=0x" << std::hex << local_con_data.lid << std::dec << std::endl;

    std::cout << "Local LID = 0x" << std::hex << m_res->port_attr.lid << std::dec << std::endl;
    std::cout << "m_res->sock is" << m_res->sock << std::endl;
    if (sock_sync_data(m_res->sock, sizeof(struct cm_con_data_t), (char*)&local_con_data, (char*)&tmp_con_data) < 0)
    {
      std::cout << "failed to exchange connection data between sides" << std::endl;
      rc = 1;
    }

    remote_con_data.send_addr = bswap_64(tmp_con_data.send_addr);
    remote_con_data.send_rkey = ntohl(tmp_con_data.send_rkey);

    remote_con_data.recv_addr = bswap_64(tmp_con_data.recv_addr);
    remote_con_data.recv_rkey = ntohl(tmp_con_data.recv_rkey);

    remote_con_data.qp_num = ntohl(tmp_con_data.qp_num);
    remote_con_data.lid = ntohs(tmp_con_data.lid);
    memcpy(remote_con_data.gid, tmp_con_data.gid, 16);

    m_res->remote_props = remote_con_data;
    std::cout << "Remote send_address = 0x" << std::hex << remote_con_data.send_addr << std::endl;
    std::cout << "Remote send_rkey=0x" << std::hex << remote_con_data.send_rkey << std::endl;

    std::cout << "Remote recv_address = 0x" << std::hex << remote_con_data.recv_addr << std::endl;
    std::cout << "Remote recv_rkey=0x" << std::hex << remote_con_data.recv_rkey << std::endl;

    std::cout << "Remote QP number=0x" << std::hex << remote_con_data.qp_num << std::endl;
    std::cout << "Remote LID=0x" << std::hex << remote_con_data.lid << std::endl;

    uint8_t* p = remote_con_data.gid;
    fprintf(stdout, "Remote GID =%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n ", p[0],
      p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15]);

    rc = modify_qp_to_init(m_res->qp);
    if (rc)
    {
      std::cerr << "change QP state to INIT failed" << std::endl;
      return rc;
    }

    post_receive(m_res);
    std::cout << "already post_receive" << std::endl;


    rc = modify_qp_to_rtr(m_res->qp, remote_con_data.qp_num, remote_con_data.lid, remote_con_data.gid);
    if (rc)
    {
      std::cerr << "failed to modify QP state to RTR" << std::endl;
      return rc;
    }

    rc = modify_qp_to_rts(m_res->qp);
    if (rc)
    {
      std::cerr << "failed to modify QP state to RTS" << std::endl;
      return rc;
    }
    std::cout << "QP state was change to RTS" << std::endl;
    if (sock_sync_data(m_res->sock, 1, "Q", &temp_char) < 0)
    {
      std::cerr << "sync error after QPs are were moved to RTS" << std::endl;
      rc = 1;
    }

    // if (set_nonblocking(m_res->sock) == 0) {
    //   std::cout << "Socket is now non-blocking." << std::endl;
    // }
    m_res->flag = true;
    return rc;
  }

  void TcpConnection::onRead() {
    //std::cout << "tcpconnection onRead" << std::endl;
    // 1. 从 socket 缓冲区，调用 系统的 read 函数读取字节 in_buffer 里面
    std::cout << "here is onRead" << std::endl;
    bool is_close = false;

    if (m_state != Connected) {
      ERRORLOG("onRead error, client has already disconneced, addr[%s], clientfd[%d]", m_peer_addr->toString().c_str(), m_fd);
      return;
    }
    char temp_buffer[24];
    set_blocking(m_fd);
    int rt = read(m_fd, temp_buffer, sizeof(temp_buffer) - 1);
    std::cout << "temp_buffer is " << temp_buffer << std::endl;
    while (rt > 0 && temp_buffer[0] == 'S') {
      std::cout << "onRead will go to poll_completion" << std::endl;
      int poll_result = poll_completion(m_res);
      if (poll_result)
      {
        std::cerr << "poll completion failed" << std::endl;
        return;
      }
      else if (poll_result == 0) {
        std::copy(m_res->in_buf.begin(), m_res->in_buf.end(), m_in_buffer->m_buffer.begin());
        std::cout<<"m_res->in_buf.size() is "<<m_res->in_buf.size()<<std::endl;
        m_in_buffer->moveWriteIndex(m_res->in_buf.size());
        std::cout << "poll completion success" << std::endl;
      }
      else if (poll_result == -1) {
        std::cerr << "poll completion error" << std::endl;
        is_close = true;
      }
      break;
    }
    if (is_close) {
      //TODO: 
      INFOLOG("peer closed, peer addr [%s], clientfd [%d]", m_peer_addr->toString().c_str(), m_fd);
      clear();
      return;
    }
    // TODO: 简单的 echo, 后面补充 RPC 协议解析 
    excute();

  }

  void TcpConnection::excute() {
    std::cout << "now in excute" << std::endl;
    if (m_connection_type == TcpConnectionByServer) {
      // 将 RPC 请求执行业务逻辑，获取 RPC 响应, 再把 RPC 响应发送回去
      std::vector<AbstractProtocol::s_ptr> result;

      m_coder->decode(result, m_in_buffer);
      std::cout << "result.size() is " << result.size() << std::endl;
      for (size_t i = 0; i < result.size(); ++i) {
        // 1. 针对每一个请求，调用 rpc 方法，获取响应 message
        // 2. 将响应 message 放入到发送缓冲区，监听可写事件回包
        INFOLOG("success get request[%s] from client[%s]", result[i]->m_msg_id.c_str(), m_peer_addr->toString().c_str());

        std::shared_ptr<TinyPBProtocol> message = std::make_shared<TinyPBProtocol>();
        // message->m_pb_data = "hello. this is rocket rpc test data";
        // message->m_msg_id = result[i]->m_msg_id;

        RpcDispatcher::GetRpcDispatcher()->dispatch(result[i], message, this);
      }

    }
    else {
      // 从 buffer 里 decode 得到 message 对象, 执行其回调
      std::vector<AbstractProtocol::s_ptr> result;
      m_coder->decode(result, m_in_buffer);

      for (size_t i = 0; i < result.size(); ++i) {
        std::string msg_id = result[i]->m_msg_id;
        auto it = m_read_dones.find(msg_id);
        if (it != m_read_dones.end()) {
          it->second(result[i]);
          m_read_dones.erase(it);
        }
      }

    }

  }


  void TcpConnection::reply(std::vector<AbstractProtocol::s_ptr>& replay_messages) {
    m_coder->encode(replay_messages, m_out_buffer);
    listenWrite();
  }

  void TcpConnection::onWrite() {
    std::cout << "tcpconnection onWrite" << std::endl;
    std::cout<<"m_res->out_buf.size() is "<<m_res->out_buf.size()<<" at onWrite begin"<<std::endl;

    // 将当前 out_buffer 里面的数据全部发送给 client
    if (m_res->flag == false)
    {
      std::cout << "m_res->flag is false" << std::endl;
      return;
    }
    if (m_state != Connected) {
      ERRORLOG("onWrite error, client has already disconneced, addr[%s], clientfd[%d]", m_peer_addr->toString().c_str(), m_fd);
      return;
    }


    if (m_connection_type == TcpConnectionByClient) {
      //  1. 将 message encode 得到字节流
      // 2. 将字节流入到 buffer 里面，然后全部发送

      std::vector<AbstractProtocol::s_ptr> messages;

      for (size_t i = 0; i < m_write_dones.size(); ++i) {
        messages.push_back(m_write_dones[i].first);
      }
      std::cout << "messages are: ";
      for (const auto& message : messages) {
        std::cout << message << " ";
      }
      std::cout << std::endl;
      m_coder->encode(messages, m_out_buffer);
    }

    bool is_write_all = false;

    if (m_out_buffer->readAble() == 0) {
      DEBUGLOG("no data need to send to client [%s]", m_peer_addr->toString().c_str());
      is_write_all = true;
    }
    int write_size = m_out_buffer->readAble();
    std::cout << "write_size is " << write_size << std::endl;
    int read_index = m_out_buffer->readIndex();

    std::cout << "Before writing to out_buf: " << std::string(m_res->out_buf.data(), m_res->out_buf.size()) << std::endl;
    memcpy(m_res->out_buf.data(), &(m_out_buffer->m_buffer[read_index]), write_size);
    std::cout<<"m_res->out_buf.size() is "<<m_res->out_buf.size()<<std::endl;
    std::cout << "After writing to out_buf: " << std::string(m_res->out_buf.data(), m_res->out_buf.size()) << std::endl;

    if (post_send(m_res, IBV_WR_SEND))
    {
      std::cerr << "failed to post SR" << std::endl;
      return;
    }
    else
    {
      std::cout << "Send Request was posted in onWrite" << std::endl;
    }
    // 等待发送完成
    if (poll_completion(m_res)) {
      ERRORLOG("Failed to poll completion for send");
      return;
    }

    // if (rt >= write_size) {
    //   DEBUGLOG("no data need to send to client [%s]", m_peer_addr->toString().c_str());
    //   is_write_all = true;
    // } if (rt == -1 && errno == EAGAIN) {
    //   // 发送缓冲区已满，不能再发送了。
    //   // 这种情况我们等下次 fd 可写的时候再次发送数据即可
    //   ERRORLOG("write data error, errno==EAGIN and rt == -1");
    // }

    set_blocking(m_fd);

    const char* msg = "Send from client";  // 要发送的字符串
    int write_size_msg = strlen(msg);  // 获取字符串的长度
    // 使用 write 函数发送该字符串
    int rt = write(m_fd, msg, write_size_msg);

    m_fd_event->cancle(FdEvent::OUT_EVENT);
    //m_event_loop->addEpollEvent(m_fd_event);


    if (m_connection_type == TcpConnectionByClient) {
      for (size_t i = 0; i < m_write_dones.size(); ++i) {
        m_write_dones[i].second(m_write_dones[i].first);
      }
      m_write_dones.clear();
    }
  }

  void TcpConnection::setState(const TcpState state) {
    m_state = Connected;
  }

  TcpState TcpConnection::getState() {
    return m_state;
  }

  void TcpConnection::clear() {
    // 处理一些关闭连接后的清理动作
    if (m_state == Closed) {
      return;
    }
    m_fd_event->cancle(FdEvent::IN_EVENT);
    m_fd_event->cancle(FdEvent::OUT_EVENT);

    m_event_loop->deleteEpollEvent(m_fd_event);

    m_state = Closed;

  }

  void TcpConnection::shutdown() {
    if (m_state == Closed || m_state == NotConnected) {
      return;
    }

    // 处于半关闭
    m_state = HalfClosing;

    // 调用 shutdown 关闭读写，意味着服务器不会再对这个 fd 进行读写操作了
    // 发送 FIN 报文， 触发了四次挥手的第一个阶段
    // 当 fd 发生可读事件，但是可读的数据为0，即 对端发送了 FIN
    ::shutdown(m_fd, SHUT_RDWR);

  }


  void TcpConnection::setConnectionType(TcpConnectionType type) {
    m_connection_type = type;
  }


  int TcpConnection::poll_completion(resources* res) {
    ibv_wc wc;
    int poll_result;
    int rc = 0;
    do {
      //std::cout<<"i am in poll_completion do while"<<std::endl;
      poll_result = ibv_poll_cq(res->cq, 1, &wc);
    } while (poll_result == 0);
    if (poll_result < 0)
    {
      std::cerr << "poll CQ failed" << std::endl;
      rc = 1;
    }
    else if (poll_result == 0)
    {
      std::cerr << "completion wasn't found in the CQ after timeout" << std::endl;
      rc = 1;
    }
    else
    {
      std::cout << "wr.opcode is " << wc.opcode << std::endl;
      std::cout << "completion was found in CQ with status 0x" << wc.status << std::endl;
      if (wc.status != IBV_WC_SUCCESS)
      {
        fprintf(stderr, "got bad completion with status: 0x%x, vendor syndrome: 0x%x\n", wc.status,
          wc.vendor_err);
        rc = 1;
      }
    }
  }

  void TcpConnection::listenWrite() {
    m_fd_event->listen(FdEvent::OUT_EVENT, std::bind(&TcpConnection::onWrite, this));
    m_event_loop->addEpollEvent(m_fd_event);
  }


  void TcpConnection::listenRead() {
    m_fd_event->listen(FdEvent::IN_EVENT, std::bind(&TcpConnection::onRead, this));
    m_event_loop->addEpollEvent(m_fd_event);
  }


  void TcpConnection::pushSendMessage(AbstractProtocol::s_ptr message, std::function<void(AbstractProtocol::s_ptr)> done) {
    m_write_dones.push_back(std::make_pair(message, done));
  }

  void TcpConnection::pushReadMessage(const std::string& msg_id, std::function<void(AbstractProtocol::s_ptr)> done) {
    m_read_dones.insert(std::make_pair(msg_id, done));
  }


  NetAddr::s_ptr TcpConnection::getLocalAddr() {
    return m_local_addr;
  }

  NetAddr::s_ptr TcpConnection::getPeerAddr() {
    return m_peer_addr;
  }


  int TcpConnection::getFd() {
    return m_fd;
  }

}