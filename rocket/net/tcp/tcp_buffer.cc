#include <iostream>
#include <memory>
#include <string.h>
#include "rocket/common/log.h"
#include "rocket/net/tcp/tcp_buffer.h"

namespace rocket {



  TcpBuffer::TcpBuffer(int size) : m_size(size) {
    m_buffer.resize(size);
  }

  TcpBuffer::~TcpBuffer() {

  }

  // 返回可读字节数
  int TcpBuffer::readAble() {
    return m_write_index - m_read_index;
  }

  // 返回可写的字节数
  int TcpBuffer::writeAble() {
    return m_buffer.size() - m_write_index;
  }

  int TcpBuffer::readIndex() {
    return m_read_index;
  }

  int TcpBuffer::writeIndex() {
    return m_write_index;
  }

  void TcpBuffer::writeToBuffer(const char* buf, int size) {
    if (size > writeAble()) {
      std::cout << "[Error] Buffer overflow! Cannot write to buffer. Requested size: "
        << size << ", Available: " << writeAble() << std::endl;
      //这里不能进行扩容，因为RDMA通信中MR的内存区域是固定的，扩容会出现问题，所以扩容直接报错
      // 调整 buffer 的大小，扩容

      // int new_size = (int)(1.5 * (m_write_index + size));
      // resizeBuffer(new_size);
    }
    memcpy(&m_buffer[m_write_index], buf, size);
    m_write_index += size;
  }


  void TcpBuffer::readFromBuffer(std::vector<char>& re, int size) {
    if (readAble() == 0) {
      std::cout<<"readAble is 0"<<std::endl;  
      return;
    }

    int read_size = readAble() > size ? size : readAble();

    std::vector<char> tmp(read_size);
    memcpy(&tmp[0], &m_buffer[m_read_index], read_size);

    re.swap(tmp);
    m_read_index += read_size;

    //adjustBuffer();
  }


  void TcpBuffer::resizeBuffer(int new_size) {
    std::vector<char> tmp(new_size);
    int count = std::min(new_size, readAble());

    memcpy(&tmp[0], &m_buffer[m_read_index], count);
    m_buffer.swap(tmp);

    m_read_index = 0;
    m_write_index = m_read_index + count;

  }


  void TcpBuffer::adjustBuffer() {
    if (m_read_index < int(m_buffer.size() / 3)) {
      return;
    }
    std::vector<char> buffer(m_buffer.size());
    int count = readAble();

    memcpy(&buffer[0], &m_buffer[m_read_index], count);
    m_buffer.swap(buffer);
    m_read_index = 0;
    m_write_index = m_read_index + count;

    buffer.clear();
  }

  void TcpBuffer::moveReadIndex(int size) {
    size_t j = m_read_index + size;
    if (j >= m_buffer.size()) {
      std::cout << "[Error] moveReadIndex error, size out of bounds. Requested size: "
        << size << ", Readable: " << readAble() << std::endl;
      // ERRORLOG("moveReadIndex error, invalid size %d, old_read_index %d, buffer size %d", size, m_read_index, m_buffer.size());
      return;
    }
    m_read_index = j;
    //adjustBuffer();
  }

  void TcpBuffer::moveWriteIndex(int size) {
    size_t j = m_write_index + size;
    if (j > m_buffer.size()) {
      std::cout << "[Error] moveWriteIndex error, size out of bounds. Requested size: "
        << size << ", Writable: " << writeAble() << std::endl;
      return;
    }
    m_write_index = j;
    //adjustBuffer();

  }

}