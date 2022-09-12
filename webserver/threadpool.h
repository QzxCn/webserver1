
#ifndef THREADPOOL_H
#define THREADPOOL_H
#include<iostream>
using namespace std;
#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "locker.h"
#include <thread>
#include<unordered_map>
#include <unistd.h>
#include <sys/syscall.h>
// 线程池类，将它定义为模板类是为了代码复用，模板参数T是任务类
template<typename T>
class threadpool {
public:
    /*thread_number是线程池中线程的数量，max_requests是请求队列中最多允许的、等待处理的请求的数量*/
    threadpool(int thread_number =8, int max_requests = 10000);
    ~threadpool();
    bool append(T* request);
   // unordered_map<pthread_t*,int>get_index;

private:
    /*工作线程运行的函数，它不断从工作队列中取出任务并执行之*/
    static void* worker(void* arg);
    void run();

private:
    // 线程的数量,
    int m_thread_number;  
    
    // 描述线程池的数组，大小为m_thread_number    
    pthread_t * m_threads;

    // 请求队列中最多允许的、等待处理的请求的数量  
    //作用？
    int m_max_requests; 
    
    // 请求队列
    //线程池“监控这个内容，具体表现为，所有线程执行的函数都不停循环查看这个队列是否存在新任务
    std::list< T* > m_workqueue;  

    // 保护请求队列的互斥锁
    locker m_queuelocker;   

    // 是否有任务需要处理
    //作用？我线程已经在监控请求队列了，这个信号用来通知谁？
    sem m_queuestat;

    // 是否结束线程
    //什么时候关闭线程？ 
    public:         
    bool m_stop;    
    bool teststop;                
};

template< typename T >
threadpool< T >::threadpool(int thread_number, int max_requests) : 
        m_thread_number(thread_number), m_max_requests(max_requests), 
        m_stop(false), m_threads(NULL),teststop(0) {

    if((thread_number <= 0) || (max_requests <= 0) ) {
        throw std::exception();
    }

    m_threads = new pthread_t[m_thread_number];//******************
    if(!m_threads) {
        throw std::exception();
    }

    // 创建thread_number 个线程，并将他们设置为脱离线程。脱离线程意义在于，关闭时候自动释放资源
    for ( int i = 0; i < thread_number; ++i ) {
       //printf( "创建第 %d个线程，id为%d\n", i,m_threads+i);
                 
     //  get_index[m_threads+i]=i+1;
        if(pthread_create(m_threads + i, NULL, worker, this ) != 0) {
            delete [] m_threads;
            throw std::exception();
        }
        else{
  cout<<"创建第"<<i+1<<"个线程，tid为"<<*(m_threads+i)<<endl;
        }
        if( pthread_detach( m_threads[i] ) ) {
            delete [] m_threads;
            throw std::exception();
        }
    }
}

template< typename T >
threadpool< T >::~threadpool() {
    delete [] m_threads;
    cout<<"关闭所有线程"<<endl;
    teststop=1;
    m_stop = true;
}

template< typename T >
bool threadpool< T >::append( T* request )
{
    // 操作工作队列时一定要加锁，因为它被所有线程共享。
    m_queuelocker.lock();
    if ( m_workqueue.size() > m_max_requests ) {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

template< typename T >
void* threadpool< T >::worker( void* arg )
{
    threadpool* pool = ( threadpool* )arg; 
    pool->run();
    return pool;
}

template< typename T >
void threadpool< T >::run() {

    while (1) { 
         int tid = syscall(SYS_gettid);
           
        m_queuestat.wait();//阻塞这个线程
        m_queuelocker.lock();
        if ( m_workqueue.empty() ) { 
            m_queuelocker.unlock();
            continue;
        }
        T* request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if ( !request ) {
            continue;   
        }
 
      // //cout <<"工作线程"<< tid<<"即将等待10s" <<endl;   
      // sleep(10);
        request->process();
    }

}

#endif
