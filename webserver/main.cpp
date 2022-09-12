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
#include "locker.h"
#include "threadpool.h"
#include "http_conn.h" 
#define MAX_FD 65536   // 最大的文件描述符个数
#define MAX_EVENT_NUMBER 10000 // 监听的最大的事件数量

#define TIMESLOT 5
static sort_timer_lst timer_lst;
static int pipefd[2];//统一事件源
//信号处理函数****************************************************************************************
void timer_handler()
{
    cout<<"time_handler()"<<endl;
    // 定时处理任务，实际上就是调用tick()函数，which删除超时的连接
    timer_lst.tick();//
    // 因为一次 alarm 调用只会引起一次SIGALARM 信号，所以我们要重新定时，以不断触发 SIGALARM信号。
    alarm(TIMESLOT);//在TIMESLOT秒后，发送SIGALARM信号给进程
}
// 定时器回调函数，它删除非活动连接socket上的注册事件，并关闭之。，有tick()调用
void cb_func( http_conn* user )
{   cout<<"cb_func（）"<<endl;
    user->close_conn();
   
}

void sig_handler( int sig )
{
    int save_errno = errno;
    int msg = sig;
    send( pipefd[1], ( char* )&msg, 1, 0 );
    errno = save_errno;//？
}
void addsig( int sig )//统一事件源的，重载版本
{
    struct sigaction sa;
    memset( &sa, '\0', sizeof( sa ) );
    sa.sa_handler = sig_handler;
    sa.sa_flags |= SA_RESTART;
    sigfillset( &sa.sa_mask );
    assert( sigaction( sig, &sa, NULL ) != -1 );
}
//****************************************************************************************************
void addsig(int sig, void( handler )(int)){
    struct sigaction sa;
    memset( &sa, '\0', sizeof( sa ) );
    sa.sa_handler = handler;//指定信号
    sigfillset( &sa.sa_mask );
    assert( sigaction( sig, &sa, NULL ) != -1 );
}

// 添加文件描述符
extern void addfd( int epollfd, int fd, bool one_shot );
extern void removefd( int epollfd, int fd );
extern int setnonblocking( int fd ) ;



int main( int argc, char* argv[] ) {//定义有效参数为两个，一个是可执行程序的名[0],一个是端口号[1]
    if( argc <= 1 ) {
        printf( "usage: %s port_number\n", basename(argv[0]));
        return 1;
    }

    int port = atoi( argv[1] );
    addsig( SIGPIPE, SIG_IGN );
 
    threadpool< http_conn >* pool = NULL;//
    try {
        pool = new threadpool<http_conn>;//线程循环开始，在循环中执行work()，始终等待事件队列被填充
    } catch( ... ) {
        return 1;
    }

    http_conn* users = new http_conn[ MAX_FD ];//连接数由文件描述符数量限制决定，每个连接对应一个文件描述符


//创建本地socket，作为服务器，为监听socket
    int listenfd = socket( PF_INET, SOCK_STREAM, 0);//参数分别对应IPV4，数据流服务
      assert( listenfd >= 0 );
//建立地址结构体
    int ret = 0;
    struct sockaddr_in address;//该结构体是ipv4专用地址结构体，ipv6相应的为sockaddr_in6
    //INADDR_ANY，指使用本机的任意一个地址作为服务器的IP地址，这样服务器有很强的可移植性
    //如果本机有多个网卡，多个地址，这样设置的好处是可以用一个socket接受所有数据，只要是绑定的端口号过来的数据，都可以接受

    address.sin_addr.s_addr = INADDR_ANY;//ipv4地址结构体中的成员，值ipv4采用网络字节序表示
    address.sin_family = AF_INET;//地址族，固定值
    address.sin_port = htons( port );//将主机字节序转换为网络字节序，host to network short

    // 端口复用
    
    int reuse = 1;
    //对监听socket进行设置，
    //第二个参数选择理由：表示的是被设置的选项的级别，如果要在“套接字级别”上设置选项，就必须这么设置
    //第三个参数，端口复用，与timewait状态有关，客户端进入此状态，说明已经受到服务器的fin请求，服务器在
    //等待这个ack且出于lastwait状态，此时tcp连接处于半关闭，
    //设置这个选项的目的是，在连接处于该状态，连接尚未释放的时候，让与之绑定的socket地址可以立即重用
    //这么做的意义？端口复用最常用的用途应该是防止服务器重启时之前绑定的端口还未释放或者程序突然退出而系统
    //没有释放端口，那么将导致这个端口在系统中一直是被占用的状态，
    //第四个参数，用来指定前一个选项是否生效
    //最后一个参数，告知前一个参数的长度
    setsockopt( listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof( reuse ) );
//命名socket，指定socket的地址，
    ret = bind( listenfd, ( struct sockaddr* )&address, sizeof( address ) ); 
 ret = listen( listenfd, 5 );
    // 创建epoll对象，和事件数组，添加
    epoll_event events[ MAX_EVENT_NUMBER ];
    int epollfd = epoll_create(5 );
    // 添加到epoll对象中
    addfd( epollfd, listenfd, false );
    http_conn::m_epollfd = epollfd;


//创建管道，发送信号用
  ret = socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd);
    assert( ret != -1 );
    //setnonblocking( pipefd[1] );
    addfd( epollfd, pipefd[0],false );
    //添加信号
    addsig( SIGALRM );
    addsig( SIGTERM );
    bool stop_server = false;
//定时器 
  bool timeout = false;

int maxfd=-1;

  alarm(TIMESLOT);  // 定时,5秒后产生SIGALARM信号 
    while(!stop_server) {
    
      cout<<"epoll准备中"<<endl;
        int number = epoll_wait( epollfd, events, MAX_EVENT_NUMBER, -1 );
        
        if ( ( number < 0 ) && ( errno != EINTR ) ) {
           cout<<"epoll准备失败"<<endl;
       
            break;
        }

        for ( int i = 0; i < number; i++ ) { 
            int sockfd = events[i].data.fd; 
            maxfd=max(maxfd,sockfd);
            cout<<"正在处理文件描述符："<<sockfd<<"事件为 ";
            if( sockfd == listenfd ) {//新的连接，在这里建立，对应的fd通过users对象添加进入epoll
                cout<<"*****新连接******"<<endl;   // sleep(5);
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof( client_address );
                int connfd = accept( listenfd, ( struct sockaddr* )&client_address, &client_addrlength );
                
                if ( connfd < 0 ) {
                    printf( "接受连接失败，错误码为: %d\n", errno );
                    continue;
                } 

                if( http_conn::m_user_count >= MAX_FD ) {//但是没有解决空闲的连接/文件描述符
                cout<<"连接数量超过文件描述符范围"<<endl;
                    close(connfd);
                    continue;
                }
                users[connfd].init( connfd, client_address);//超程访问风险，在上一个if语句规避了
               // 创建定时器，设置其回调函数与超时时间，然后绑定定时器与用户数据，最后将定时器添加到链表timer_lst中
                util_timer* timer = new util_timer;
                timer->user_data = &users[connfd];
                 timer->cb_func = cb_func;//指定定时器节点的回调函数
                 time_t cur = time( NULL );
                timer->expire = cur + 3 * TIMESLOT;
                users[connfd].timer = timer;
                timer_lst.add_timer( timer );

            } else if( events[i].events & ( EPOLLRDHUP | EPOLLHUP | EPOLLERR ) ) {
                    cout<<"挂起/错误/网络问题"<<endl;
                users[sockfd].close_conn();
                     
            } else if(sockfd != pipefd[0]&&events[i].events & EPOLLIN) {
            
                    util_timer* timer = users[sockfd].timer;
                    cout<<sockfd<<"可读"<<endl; 
                if(users[sockfd].read()) {//在主程序完成读
                     if( timer ) {
                        cout<<"读取请求成功，正在延长连接存活时间"<<endl;
                        time_t cur = time( NULL );
                        timer->expire = cur + 3 * TIMESLOT; 
                        timer_lst.adjust_timer( timer );
                    }
                    pool->append(users + sockfd);//事件加入请求队列
                } else {
                cout<<"读取失败，正在删除对应的定时器节点"<<endl;
                       if( timer )
                        {
                            timer_lst.del_timer( timer );
                        }
                    users[sockfd].close_conn();//读失败，关闭连接
                }

            }  else if( events[i].events & EPOLLOUT ) {
                  cout<<sockfd<<"可写"<<endl;
                if( !users[sockfd].write() ) {//在主程序完成写
                    users[sockfd].close_conn();//写失败，关闭连接
                }
            }
            else if( ( sockfd == pipefd[0] ) && ( events[i].events & EPOLLIN ) ) {
                // 处理信号 
                int sig;  //  cout<<"发现alarm信号"<<endl;
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
                                cout<<"发现超时信号,准备关闭某一个文件描述符"<<endl;
                                timeout = true;
                                break;
                            }
                            case SIGTERM:
                            {
                                cout<<"发现终止信号,准备关闭服务器"<<endl;
                                stop_server = true;
                            }
                        }
                    }
                }
            }
            cout<<"出现过的最大fd值为"<<maxfd<<endl;
        }
           if( timeout ) {
           // cout<<"发生超时"<<endl;
            //IO事件优先级高，保证读写期间不会因为超时关闭连接
            timer_handler();//   -> timer_lst.tick()
            timeout = false;
        }
    }
    
    close( epollfd );
    close( listenfd );
    close( pipefd[1] );
    close( pipefd[0] );
    delete [] users;
    delete pool;
    return 0;
}