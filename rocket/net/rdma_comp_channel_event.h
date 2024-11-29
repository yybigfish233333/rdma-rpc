#ifndef ROCKET_NET_RDMA_COMP_CHANNEL_EVENT_H_
#define ROCKET_NET_RDMA_COMP_CHANNEL_EVENT_H_

#include "rocket/net/fd_event.h"
#include <infiniband/verbs.h>   
#include <iostream>
#include <functional>
namespace rocket{
    class RDMACompChannelEvent
    {
    private:
        ibv_comp_channel* m_comp_channel;

        void handleEvent(std::function<void(struct ibv_cq*)> callback);
    public:
        RDMACompChannelEvent(ibv_comp_channel* comp_channel);
        ~RDMACompChannelEvent();

        void startListening(std::function<void(struct ibv_cq*)> callback);    
    };
}



#endif