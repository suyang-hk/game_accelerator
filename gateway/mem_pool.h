// 定长块内存池: 8 MiB 整块预切,alloc/free 只动链表头,不回系统
#ifndef MEM_POOL_H
#define MEM_POOL_H

#include <cstddef>
#include <cstdio>

class MemoryPool {
public:
  static constexpr size_t BLOCK = 2048;  // 单块大小
  static constexpr size_t CAP   = 4096;  // 块数, 总共 8 MiB

  MemoryPool() {
    for (size_t i = 0; i < CAP; ++i) {
      Node *b = reinterpret_cast<Node *>(storage_ + i * BLOCK);
      b->next = free_head_;
      free_head_ = b;
    }
  }

  void *alloc() {
    if (!free_head_) {
      printf("[pool] exhausted: all %zu blocks in use\n", CAP);
      return nullptr;
    }
    Node *b = free_head_;
    free_head_ = b->next;
    return b;
  }

  void free(void *p) {
    if (!p) return;
    Node *b = static_cast<Node *>(p);
    b->next = free_head_;
    free_head_ = b;
  }

private:
  struct Node {
    Node *next;
  };
  alignas(Node) unsigned char storage_[CAP * BLOCK];
  Node *free_head_ = nullptr;
};

#endif  // MEM_POOL_H
