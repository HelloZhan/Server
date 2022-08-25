# WebServer
用C++实现的高性能web服务器，经过webbenchh压力测试可以实现上万的QPS

## 功能
1.利用IO复用技术epoll与线程池实现多线程的模拟Proactor高并发模型；

2.利用状态机解析HTTP请求报文，实现处理静态资源的请求
