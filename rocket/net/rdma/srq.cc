#include "srq.h"

#include <cstring>
#include <memory>

#include <infiniband/verbs.h>

#include "device.h"
#include "error.h"


namespace rocket {

srq::srq(std::shared_ptr<pd> pd, size_t max_wr) : srq_(nullptr), pd_(pd) {
  struct ibv_srq_init_attr srq_init_attr;
  srq_init_attr.srq_context = this;
  srq_init_attr.attr.max_sge = 1;
  srq_init_attr.attr.max_wr = max_wr;
  srq_init_attr.attr.srq_limit = max_wr;

  srq_ = ::ibv_create_srq(pd_->pd_, &srq_init_attr);
  check_ptr(srq_, "failed to create srq");
  //RDMAPP_LOG_DEBUG("created srq %p", reinterpret_cast<void *>(srq_));
}

srq::~srq() {
  if (srq_ == nullptr) [[unlikely]] {
    return;
  }

  if (auto rc = ::ibv_destroy_srq(srq_); rc != 0) [[unlikely]] {
    // RDMAPP_LOG_ERROR("failed to destroy srq %p: %s (rc=%d)",
    //                  reinterpret_cast<void *>(srq_), strerror(rc), rc);
  } else {
    // RDMAPP_LOG_DEBUG("destroyed srq %p", reinterpret_cast<void *>(srq_));
  }
}

} // namespace rocket