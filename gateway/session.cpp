#include "session.h"

Session::Session(uint32_t c, const sockaddr_in &addr)
    : conv(c), client_addr(addr) {}

// 析构时把 KCP 一起放掉;kcp 从没 init 过时是 no-op
Session::~Session() { kcp_session_release(kcp); }
