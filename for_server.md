网络层 6 组件速查
Buffer — 网络缓冲区

[prependable | readable | writable]
             ↑ readerIdx_ ↑ writerIdx_

用途：收数据和发数据的容器
核心操作：
  readFd(fd)       → readv 从 socket 读到可写区
  retrieve(len)    → 消费 len 字节（只动指针，不 free 内存）
  retrieveAsString → 消费并返回 string（取完整帧）
  append(data, len) → 追到可写区
Channel — fd + 事件回调

每个 fd 一个 Channel：
  fd_         → 文件描述符
  events_     → 关注 EPOLLIN | EPOLLOUT | ...
  revents_    → Poller 回填
  read/write/close/error 四个 callback

用法：
  Channel ch(loop, fd);
  ch.setReadCallback([]() { ... recv ... });
  ch.enableReading();    // call loop->updateChannel → epoll_ctl(ADD)
  ch.disableAll();       // epoll_ctl(DEL)
  ch.remove();           // disableAll + 从 Poller 注销
Poller — epoll 封装

epfd_       → epoll_create1 创建
channels_   → map<int, Channel*> 登记表
events_     → epoll_wait 输出缓冲区（pre-allocate）

核心操作：
  poll(timeout, activeChannels)   → epoll_wait → fillActive
  updateChannel(Channel*)         → ADD / MOD / DEL
  removeChannel(Channel*)         → DEL + erase

关键设计：ev.data.ptr = Channel*，epoll_wait 返回时直接拿到 Channel
EventLoop — 事件循环（调度者）

poller_           → unique_ptr<Poller>
activeChannels_   → 每轮就绪列表
pollTimeoutMs_    → 超时控制

loop():
  while (!quit_) {
      activeChannels_.clear();
      poller_->poll(pollTimeoutMs_, activeChannels_);
      for each ch: ch->handleEvent();   // → 回调分发
  }

转发：updateChannel / removeChannel → 调 Poller
Acceptor — 监听端口

acceptFd_        → socket + bind + listen
acceptChannel_   → Channel(listenfd, EPOLLIN)
newConnectionCallback_ → accept 到新连接后的回调

构造时：socket → SO_REUSEADDR → bind → listen → fcntl → Channel → enableReading
handleRead：accept → fcntl(connfd) → newConnectionCallback_(connfd, peerAddr)
TcpConnection — 一个客户端连接

fd_         → connfd
channel_    → Channel(connfd)
inputBuf_   → 收数据
outputBuf_  → 发数据

handleRead  → readFd → messageCallback_(conn, inputBuf_)
handleWrite → write → retrieve → 写完关 EPOLLOUT
handleClose → disableAll → close(fd) → closeCallback_
send(msg)   → 直接 write，写不完塞 outputBuf_ + enableWriting
集成：server.cpp 要做的

1. 创建 DB（打开存储目录）
2. 创建 EventLoop
3. 创建 Acceptor(loop, InetAddress(port))
4. 设 Acceptor 的 newConnectionCallback：
     accept(connfd, peerAddr)
       → shared_ptr<TcpConnection> conn(loop, connfd, port)
       → conn->setMessageCallback([](conn, inputBuf) {
             从 inputBuf 取数据 → protocol.h ParseCommand → db 调用
             → 格式化响应 → conn->send(response)
         })
       → conn->connectEstablished()
5. loop.loop()  ← 主线程开始事件循环
数据流全链路：


网卡 → epoll_wait → EventLoop → Channel::handleEvent
  → TcpConnection::handleRead → readFd → inputBuf_
    → messageCallback → proto.h 解析 → DB 调用
      → FormatResponse → conn->send(response) → write/sendInLoop
调用链不到 10 行，继续写。