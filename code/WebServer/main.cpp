#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include "./pool/locker.h"
#include "./pool/threadpool.h"
#include "./http/http_conn.h"
#include "./log/log.h"

#define MAX_FD 65536   // 最大的文件描述符个数
#define MAX_EVENT_NUMBER 10000  // 监听的最大的事件数量

// 添加文件描述符
extern void addfd( int epollfd, int fd, bool one_shot );
extern void removefd( int epollfd, int fd );

void addsig(int sig, void( handler )(int)){
    struct sigaction sa;
    memset( &sa, '\0', sizeof( sa ) );
    sa.sa_handler = handler;
    sigfillset( &sa.sa_mask );
    assert( sigaction( sig, &sa, NULL ) != -1 );
}

int main( int argc, char* argv[] ) {

    if( argc <= 1 ) {
        printf( "usage: %s port_number\n", basename(argv[0]));
        return 1;
    }
    // 端口
    int port = atoi( argv[1] );

    // 当一个进程向某个已收到RST的套接字执行写操作时，内核向该进程发送SIGPIPE信号。
    // 此时为了防止服务器不退出 忽略
    addsig( SIGPIPE, SIG_IGN );
    // 创建线程池
    threadpool< http_conn >* pool = NULL;
    try {
        pool = new threadpool<http_conn>;
    } catch( ... ) {
        return 1;
    }
    // 创建http连接对象
    http_conn* users = new http_conn[ MAX_FD ];
    // 创建套接字
    int listenfd = socket( PF_INET, SOCK_STREAM, 0 );

    int ret = 0;

    struct sockaddr_in address;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_family = AF_INET;
    address.sin_port = htons( port );

    // 创建日志文件系统
    Log::Instance()->init(1, "./log", ".log", 1024);
    LOG_INFO("========== Server init ==========");

    // 端口复用
    int reuse = 1;
    setsockopt( listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof( reuse ) );
    ret = bind( listenfd, ( struct sockaddr* )&address, sizeof( address ) );
    ret = listen( listenfd, 5 );

    // 创建epoll对象，和事件数组，添加
    epoll_event events[ MAX_EVENT_NUMBER ];
    int epollfd = epoll_create( 5 );
    // 添加到epoll对象中
    addfd( epollfd, listenfd, false );
    // 初始化静态变量
    http_conn::m_epollfd = epollfd;

    while(true) {

        int number = epoll_wait( epollfd, events, MAX_EVENT_NUMBER, -1 );

        if ( ( number < 0 ) && ( errno != EINTR ) ) {
            printf( "epoll failure\n" );
            break;
        }

        for ( int i = 0; i < number; i++ ) {

            int sockfd = events[i].data.fd;

            // 如果是监听描述符（表示有新用户加入）
            if( sockfd == listenfd ) {

                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof( client_address );
                // 接收该用户
                int connfd = accept( listenfd, ( struct sockaddr* )&client_address, &client_addrlength );

                if ( connfd < 0 ) {
                    printf( "errno is: %d\n", errno );
                    continue;
                }

                if( http_conn::m_user_count >= MAX_FD ) {
                    close(connfd);
                    continue;
                }
                users[connfd].init( connfd, client_address);

            } else if( events[i].events & ( EPOLLRDHUP | EPOLLHUP | EPOLLERR ) ) {

                users[sockfd].close_conn();

            } else if(events[i].events & EPOLLIN) {
                // 如果是读事件
                if(users[sockfd].read()) {
                    pool->append(users + sockfd);
                } else {
                    users[sockfd].close_conn();
                }

            }  else if( events[i].events & EPOLLOUT ) {
                // 如果是写事件
                if( !users[sockfd].write() ) {
                    users[sockfd].close_conn();
                }

            }
        }
    }

    close( epollfd );
    close( listenfd );
    delete [] users;
    delete pool;
    return 0;
}
