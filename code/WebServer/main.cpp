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
#include "./timer/dll_timer.h"

#define MAX_FD 65536   // 最大的文件描述符个数
#define MAX_EVENT_NUMBER 10000  // 监听的最大的事件数量
#define TIMESLOT 5 // 单位时间

static int pipefd[2]; // noactive的管道
static sort_timer_dll timer_dll; //noactive的容器（双向链表）

// 添加文件描述符
extern void addfd( int epollfd, int fd, bool one_shot );
extern void removefd( int epollfd, int fd );
extern int setnonblocking( int fd );
// noactive向管道发送信号
void sig_handler( int sig )
{
    int save_errno = errno;
    int msg = sig;
    send( pipefd[1], ( char* )&msg, 1, 0 );
    errno = save_errno;
}

void addsig(int sig, void( handler )(int)){
    struct sigaction sa;
    memset( &sa, '\0', sizeof( sa ) );
    sa.sa_handler = handler;
    sigfillset( &sa.sa_mask );
    assert( sigaction( sig, &sa, NULL ) != -1 );
}

// 当时间到时，处理非活跃用户
void timer_handler()
{
    // 定时处理任务，实际上就是调用tick()函数
    timer_dll.tick();
    // 因为一次 alarm 调用只会引起一次SIGALARM 信号，所以我们要重新定时，以不断触发 SIGALARM信号。
    alarm(TIMESLOT);
}

// 定时器回调函数，它删除非活动连接socket上的注册事件，并关闭之。
void cb_func( http_conn* user_data )
{
    user_data->close_conn();
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

    // 创建管道 noactive-1 创建一个两端通信的管道
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd);
    assert( ret != -1 );
    setnonblocking( pipefd[1] );
    addfd( epollfd, pipefd[0], false);

    // 设置信号处理函数 noactive-2 SIGALRM定时器信号，SIGTERM进程终止信号
    addsig( SIGALRM, sig_handler);
    addsig( SIGTERM, sig_handler);
    bool stop_server = false;

    bool timeout = false;
    alarm(TIMESLOT);  // 定时,5秒后产生SIGALARM信号

    //printf("listen fd = %d\n",listenfd);
    //printf("epoll fd = %d\n",epollfd);
    while(!stop_server) {
        //printf("m_user_count: %d\n",http_conn::m_user_count);
        int number = epoll_wait( epollfd, events, MAX_EVENT_NUMBER, -1 );
        printf("users[%d].sockfd = %d\n",7,users[7].m_sockfd);
        printf("users[%d].sockfd = %d\n",8,users[8].m_sockfd);
        if ( ( number < 0 ) && ( errno != EINTR ) ) {
            printf( "epoll failure\n" );
            break;
        }

        for ( int i = 0; i < number; i++ ) {

            int sockfd = events[i].data.fd;
            //printf("sockfd == %d\n",sockfd);
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
                // 创建定时器，设置其回调函数与超时时间，然后绑定定时器与用户数据，最后将定时器添加到链表timer_lst中

                printf("新用户connfd = %d\n",connfd);

                dll_timer* timer = new dll_timer;
                timer->user_data = &users[connfd];
                timer->cb_func = cb_func;
                time_t cur = time( NULL );
                timer->expire = cur + 3 * TIMESLOT;
                users[connfd].timer = timer;
                timer_dll.add_timer( timer );
                printf("向timer中添加fd = %d\n",connfd);
            } else if( ( sockfd == pipefd[0] ) && ( events[i].events & EPOLLIN ) ) {
                // 处理信号
                int sig;
                char signals[1024];
                ret = recv( pipefd[0], signals, sizeof( signals ), 0 );
                if( ret == -1 ) {
                    continue;
                } else if( ret == 0 ) {
                    continue;
                } else  {
                    for( int i = 0; i < ret; ++i ) {
                        switch( signals[i] )  {
                            case SIGALRM:
                            {
                                // 用timeout变量标记有定时任务需要处理，但不立即处理定时任务
                                // 这是因为定时任务的优先级不是很高，我们优先处理其他更重要的任务。
                                timeout = true;
                                break;
                            }
                            case SIGTERM:
                            {
                                stop_server = true;
                            }
                        }
                    }
                }
            }else if( events[i].events & ( EPOLLRDHUP | EPOLLHUP | EPOLLERR ) ) {
                dll_timer* timer = users[sockfd].timer;
                cb_func( &users[sockfd] );
                if( timer )
                {
                    timer_dll.del_timer( timer );
                }

            } else if(events[i].events & EPOLLIN) {
                dll_timer* timer = users[sockfd].timer;
                // 如果是读事件
                if(users[sockfd].read()) {
                    pool->append(users + sockfd);
                    //延迟该连接被关闭的时间
                    if( timer ) {
                        time_t cur = time( NULL );
                        timer->expire = cur + 2 * TIMESLOT;
                        printf( "adjust timer once\n" );
                        timer_dll.adjust_timer( timer );
                    }
                } else {
                    cb_func( &users[sockfd] );
                    if( timer )
                    {
                        timer_dll.del_timer( timer );
                    }
                }

            }  else if( events[i].events & EPOLLOUT ) {
                // 如果是写事件
                if( !users[sockfd].write() ) {
                    users[sockfd].close_conn();
                }
            }
        }
        // 最后处理定时事件，因为I/O事件有更高的优先级。当然，这样做将导致定时任务不能精准的按照预定的时间执行。
        if( timeout ) {
            timer_handler();
            timeout = false;
        }
    }

    close( pipefd[1] );
    close( pipefd[0] );
    close( epollfd );
    close( listenfd );
    delete [] users;
    delete pool;
    return 0;
}
