#include "rocket/net/rdma/pd.h"

#include <cstdio>
#include <cstring>
#include <memory>

#include <infiniband/verbs.h>

#include "rocket/net/rdma/device.h"
#include "rocket/net/rdma/error.h"
#include "rocket/net/rdma/mr.h"


namespace rocket {

pd::pd(std::shared_ptr<rocket::device> device) : device_(device) {
  pd_ = ::ibv_alloc_pd(device->ctx_);
  check_ptr(pd_, "failed to alloc pd");
  //RDMAPP_LOG_TRACE("alloc pd %p", reinterpret_cast<void *>(pd_));
}

std::shared_ptr<device> pd::device_ptr() const { return device_; }

local_mr pd::reg_mr(void *buffer, size_t length, int flags) {
  auto mr = ::ibv_reg_mr(pd_, buffer, length, flags);
  check_ptr(mr, "failed to reg mr");
  return rocket::local_mr(this->shared_from_this(), mr);
}

pd::~pd() {
  if (pd_ == nullptr) [[unlikely]] {
    return;
  }
  if (auto rc = ::ibv_dealloc_pd(pd_); rc != 0) [[unlikely]] {
    // RDMAPP_LOG_ERROR("failed to dealloc pd %p: %s",
    //                  reinterpret_cast<void *>(pd_), strerror(errno));
  } else {
   // RDMAPP_LOG_TRACE("dealloc pd %p", reinterpret_cast<void *>(pd_));
  }
}

} // namespace rocket