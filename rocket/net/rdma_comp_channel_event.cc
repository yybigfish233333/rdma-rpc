#include "rocket/net/rdma_comp_channel_event.h"
#include<iostream>
#include<cstring>
#include<unistd.h>
#include<cerrno>

namespace rocket{
    RDMACompChannelEvent::RDMACompChannelEvent(ibv_comp_channel* comp_channel):m_comp_channel(comp_channel){
        if(m_comp_channel == NULL){
            std::cerr << "comp_channel is NULL" << std::endl;
            exit(0);
        }
    }

    RDMACompChannelEvent::~RDMACompChannelEvent(){
        if(m_comp_channel != NULL){
            ibv_destroy_comp_channel(m_comp_channel);
            m_comp_channel = NULL;
        }
    }

    void RDMACompChannelEvent::startListening(std::function<void(struct ibv_cq*)> callback){
        if(m_comp_channel == NULL){
            std::cerr << "comp_channel is NULL" << std::endl;
            exit(0);
        }
        handleEvent(callback);
    }

    void RDMACompChannelEvent::handleEvent(std::function<void(struct ibv_cq*)> callback){
        struct ibv_cq* cq;
        void* cq_context;
        int rt = ibv_get_cq_event(m_comp_channel, &cq, &cq_context);
        if(rt){
            std::cerr << "failed to get cq event" << std::endl;
            return;
        }
        ibv_ack_cq_events(cq, 1);
        callback(cq);
    }
}