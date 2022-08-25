#ifndef LOCKER_H
#define LOCKER_H

#include <exception>
#include <pthread.h>
#include <semaphore.h>

// 互斥锁类
// 主要就是对锁的封装，提供加锁，解锁，只需要一个函数而不是一个大长串的函数
class locker{
public:
    locker();

    ~locker();

    bool lock(); // 加锁

    bool unlock(); // 解锁

    pthread_mutex_t *get(); // 获取互斥量
private: 
    pthread_mutex_t m_mutex;
};

// 条件变量类
class cond{
public:
    cond();

    ~cond();

    bool wait(pthread_mutex_t *m_mutex);

    bool timewait(pthread_mutex_t *m_mutex, struct timespec t);

    bool signal();

    bool broadcast();
private:
    pthread_cond_t m_cond;
};

// 信号量类
class sem{
public:
    sem();

    sem(int num);

    ~sem();

    bool wait();

    bool post();
private:
    sem_t m_sem;
};

#endif