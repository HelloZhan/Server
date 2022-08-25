#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <queue>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "locker.h"
// 线程池类
template<typename T>
class threadpool{
public:
    // thread_number是线程池的线程数列，max_requests是请求队列中最多允许的、等待处理的请求的数量
    threadpool(int thread_number = 8, int max_requests = 10000);

    ~threadpool();
    // 添加工作到工作队列
    bool append(T* request);
private:
    // 工作线程运行的函数，它不断从工作队列中取出任务并执行
    static void* worker(void *arg);

    void run();

private:
    // 线程的数量
    int m_thread_number;

    // 描述线程池的数组，大小为m_thread_number
    pthread_t * m_threads;

    // 请求队列中最多允许的、等待处理的请求的数量
    int m_max_requests;

    // 请求队列
    std::queue<T *> m_workqueue;

    // 保护请求队列的互斥锁
    locker m_queuelocker;

    // 是否有任务需要处理，信号量
    sem m_queuestat;

    // 是否结束线程
    bool m_stop;
};

template< typename T >
threadpool<T>::threadpool(int thread_number, int max_requests):
    m_thread_number(thread_number), m_max_requests{max_requests},
    m_stop{false}, m_threads{NULL}
{
    // 如果输入的线程数量和请求队列的数量不合法
    if((thread_number <= 0) || (max_requests <= 0)){
        throw std::exception();
    }

    m_threads = new pthread_t[m_thread_number];
    if(!m_threads){
        throw std::exception();
    }

    // 创建thread_number 个线程，并将它们设置为线程脱离
    printf("线程池：创建%d个线程\n", thread_number);
    for(int i = 0; i < thread_number; i++){
        // 创建线程
        if(pthread_create(m_threads + i, NULL, worker, this) != 0){
            delete [] m_threads;
            throw std::exception();
        }
        // 设置线程脱离
        if(pthread_detach(m_threads[i])){
            delete [] m_threads;
            throw std::exception();
        }
    }
}

template< typename T >
threadpool<T>::~threadpool()
{
    delete [] m_threads;
    m_stop = true;
}

 template< typename T >
 bool threadpool<T>::append(T* request)
 {
     // 操作工作队列时一定要加锁，因为它被所有线程共享。
     m_queuelocker.lock();
     if(m_workqueue.size() > m_max_requests){
         m_queuelocker.unlock();
         return false;
     }
     m_workqueue.push(request);
     m_queuelocker.unlock();
     m_queuestat.post();
     return true;
 }

template< typename T >
void* threadpool<T>::worker(void *arg)
{
    // 让线程执行run
    threadpool *pool = (threadpool*)arg;
    pool->run();
    return pool;
}


template< typename T >
void threadpool<T>::run()
{
    // 一直循环等待请求队列的请求并执行
    while(!m_stop){
        m_queuestat.wait(); // 信号量
        m_queuelocker.lock(); // 互斥锁
        if(m_workqueue.empty()){
            m_queuelocker.unlock();
            continue;
        }
        T* request = m_workqueue.front();
        m_workqueue.pop();
        m_queuelocker.unlock();
        if(!request){
            continue;
        }
        request->process();
    }
}

#endif
