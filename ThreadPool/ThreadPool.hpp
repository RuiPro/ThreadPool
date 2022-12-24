#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <list>
#include <vector>
#include <functional>
#include <chrono>

#ifndef THREADPOOL_H
#define THREADPOOL_H

class WorkerThread;

class ThreadPool {
    friend WorkerThread;
public:
    ThreadPool(int min_thread_num, int max_thread_num, int max_task_num);     // 构造
    ~ThreadPool();                                                            // 析构
    template<class F, class... Args>
    int AddTask(F func, Args... args);                                        // 【模板】添加任务
    int Start();                                                              // 开始运作
    void Terminate();                                                         // 结束运作
    void SafelyExit(bool);                                                    // 设置强制结束
    void ReceiveAllTask(bool);                                                // 设置队列满时阻塞添加任务
    int GetMaxThreadNum() const;
    int GetMinThreadNum() const;
    int GetMaxTaskNum() const;

private:
    int max_thread_num;                         // 线程池内能达到的最大线程数量
    int min_thread_num;                         // 线程池内至少存在的线程数量
    int max_task_num;                           // 线程池存储的最大任务数量，0代表无上限
    int wave_range = 3;                         // 每次调整加/减多少线程
    int alive_thread_num;                       // 存活的线程数，只有管理者线程修改，不需加锁
    std::atomic<int> working_thread_num{};      // 正在工作的线程数
    std::atomic<int> exit_thread_num{};         // 准备退出的线程数
    std::queue<std::function<void()>> *task_queue = nullptr;    // 任务队列
    std::mutex task_queue_mutex;                // 锁任务队列
    std::list<WorkerThread *> *worker_list = nullptr; // 存放线程的链表
    std::thread *manager = nullptr;             // 管理者线程
    std::condition_variable cond;               // 任务条件变量
    std::vector<std::thread::id> *to_destroy = nullptr;          // 存放需要进行销毁的线程ID
    std::mutex to_destroy_mutex;                // 锁销毁队列
    short state_code;                           // 状态码：0创建但未运行 1正在运行 2结束准备销毁
    bool destroy_with_no_task = true;           // 是否在销毁线程池之前执行完任务队列中剩下的任务，默认是
    bool block_task_when_full = true;           // 在任务队列满的时候是否阻塞添加任务函数，默认是
    // 如果选择否，那么在任务队列满的时候添加任务会返回-1

    void manager_func();                        // 管理者线程函数
};

// 把工作线程包装成一个类，类有构造和析构可以用于创建和销毁
class WorkerThread {
    friend ThreadPool;
    std::thread::id thread_id;              // 线程自己的线程ID
    std::thread *thread_ptr = nullptr;     // 自己要做的事
    ThreadPool *pool = nullptr;            // 自己属于哪个线程池

    void worker_func();
    explicit WorkerThread(ThreadPool *);
    ~WorkerThread();
};

/***************以下是类的成员函数实现***************/

ThreadPool::ThreadPool(int min_thread_num, int max_thread_num, int max_task_num) {
    if (max_thread_num < min_thread_num || max_thread_num <= 0 ||
        min_thread_num < 0 || max_task_num < 0)
        return;
    this->max_thread_num = max_thread_num;
    this->min_thread_num = min_thread_num;
    this->max_task_num = max_task_num;
    alive_thread_num = 0;
    working_thread_num.store(0);
    exit_thread_num.store(0);
    state_code = 0;

    do {
        this->to_destroy = new std::vector<std::thread::id>;
        if (this->to_destroy == nullptr) break;
        this->task_queue = new std::queue<std::function<void()>>;
        if (this->task_queue == nullptr) break;
        this->worker_list = new std::list<WorkerThread *>;
        if (this->worker_list == nullptr) break;
        std::cout << "创建线程池成功，线程数量为" << this->min_thread_num << "~" << this->max_thread_num << "个"
                  << std::endl;
        return;
    } while (false);
    delete this->to_destroy;
    delete this->task_queue;
    delete this->worker_list;
}

ThreadPool::~ThreadPool() {
    if (state_code != 2) {
        Terminate();
    }
    delete this->to_destroy;
    delete this->task_queue;
    delete this->manager;
    delete this->worker_list;
}

template<class F, class... Args>
int ThreadPool::AddTask(F func, Args... args) {
    // 线程池还没开启的时候不可以添加任务
    if (state_code != 1) return -1;
    std::unique_lock<std::mutex> uniqueLock(task_queue_mutex);
    if (block_task_when_full) {
        // 满的时候休眠
        while (task_queue->size() >= max_task_num) {
            cond.wait(uniqueLock);
            // 当被唤醒之后发现不允许添加任务立即返回
            if (max_task_num == -1) {
                uniqueLock.unlock();
                return -1;
            }
        }
    } else {
        // 当不允许添加任务立即返回
        if (max_task_num == -1) {
            uniqueLock.unlock();
            return -1;
        }
        // 当max_task_num不为0并且任务数量抵达上限时返回-1
        if (max_task_num != 0 && task_queue->size() >= max_task_num) {
            uniqueLock.unlock();
            return -1;
        }
    }
    task_queue->push(std::bind(func, args...));
    uniqueLock.unlock();

    cond.notify_all();
    return 0;
}

int ThreadPool::Start() {
    if (state_code != 0) {
        return -1;
    }
    state_code = 1;
    // 创建管理者线程
    this->manager = new std::thread(&ThreadPool::manager_func, this);
    // 创建工作线程
    for (int i = 0; i < min_thread_num; ++i) {
        worker_list->push_back(new WorkerThread(this));
        ++alive_thread_num;
    }
    return 0;
}

void ThreadPool::Terminate() {
    if (state_code == 0) {
        state_code = 2;
        return;
    }
    if (state_code == 1) {
        // 如果要放弃剩下的任务，清空任务队列
        if (!destroy_with_no_task) {
            task_queue_mutex.lock();
            delete task_queue;
            task_queue = new std::queue<std::function<void()>>;
            task_queue_mutex.unlock();
        }
        // 阻止继续添加任务
        max_task_num = -1;
        // 如果还有线程在工作，则等待
        while (working_thread_num.load() != 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        // 让管理者线程和工作线程退出循环
        state_code = 2;
        // 回收管理者线程
        manager->join();
        // 唤醒正在睡眠的子线程
        cond.notify_all();
        // 回收工作线程
        for (auto x: *worker_list) {
            delete x;
        }
        return;
    }
}

void ThreadPool::manager_func() {
    std::cout << "管理者线程开始工作" << std::endl;
    // 当线程池开启时一直工作
    while (state_code == 1) {
        // 每2秒监视一次
        std::this_thread::sleep_for(std::chrono::seconds(2));
        printf("管理者线程监视一次，当前存活线程数量%d个，正在工作线程%d个，任务%zu个\n", alive_thread_num,
               working_thread_num.load(), task_queue->size());
        // 线程不够时创建线程：存活数<最大数 并且 所有线程都在工作
        if (alive_thread_num < max_thread_num &&
            alive_thread_num - working_thread_num.load() == 0) {
            // 创建的线程数量 = 调整量 或 最大-当前
            int addNum = wave_range < max_thread_num - alive_thread_num ?
                         wave_range : max_thread_num - alive_thread_num;
            for (int i = 0; i < addNum; ++i) {
                worker_list->push_back(new WorkerThread(this));
                ++alive_thread_num;
            }
            continue;
        }

        // 线程过多时销毁线程：存活数>最小数 并且 一些线程没事干在睡眠
        // 让一些线程主动退出
        task_queue_mutex.lock();    // 为了防止在清点闲置线程的时候突然来任务干扰，把任务队列锁起来
        if (alive_thread_num > min_thread_num &&
            working_thread_num.load() + wave_range < alive_thread_num) {
            // 销毁的线程数量 = 调整量 或 当前 - 最小
            int destroyNum = wave_range < alive_thread_num - min_thread_num ?
                             wave_range : alive_thread_num - min_thread_num;
            exit_thread_num.store(destroyNum);
            task_queue_mutex.unlock();
            // 唤醒对应个睡眠的线程
            // 在此情景下，任务队列应该是空的，添加任务的函数那里不会休眠，所以不会被唤醒
            for (int i = 0; i < destroyNum; ++i) {
                cond.notify_one();
            }
            // 等待要被回收的线程就绪
            while (exit_thread_num.load() != 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            // 回收主动退出的线程
            to_destroy_mutex.lock();
            for (auto x: *to_destroy) {
                for (auto iter = worker_list->begin(); iter != worker_list->end(); ++iter) {
                    if (x == (*iter)->thread_id) {
                        delete (*iter);
                        worker_list->erase(iter);
                        --alive_thread_num;
                        break;
                    }
                }
            }
            to_destroy->clear();
            to_destroy_mutex.unlock();
        } else {
            task_queue_mutex.unlock();
        }
    }
    std::cout << "管理者线程结束工作" << std::endl;
}

void ThreadPool::SafelyExit(bool arg) {
    if(state_code == 0)
        this->destroy_with_no_task = arg;
}

void ThreadPool::ReceiveAllTask(bool arg) {
    if(state_code == 0)
        this->block_task_when_full = arg;
}

int ThreadPool::GetMaxThreadNum() const {
    return this->max_thread_num;
}
int ThreadPool::GetMinThreadNum() const {
    return this->min_thread_num;
}
int ThreadPool::GetMaxTaskNum() const {
    return this->max_task_num;
}

WorkerThread::WorkerThread(ThreadPool *pool) {
    this->pool = pool;
    this->thread_ptr = new std::thread(&WorkerThread::worker_func, this);
    this->thread_id = thread_ptr->get_id();
    printf("创建了一个线程++++++++++++++++++++++\n");
}

WorkerThread::~WorkerThread() {
    this->thread_ptr->join();
    delete this->thread_ptr;
    printf("销毁了一个线程-----------------------\n");
}

void WorkerThread::worker_func() {
    std::cout << "工作线程开始工作" << std::endl;
    // 当线程池开启时一直工作
    while (pool->state_code == 1) {
        // 给任务队列加锁
        std::unique_lock<std::mutex> uniqueLock(pool->task_queue_mutex);
        // 当任务队列为空时睡眠等待
        while (pool->task_queue->empty()) {
            pool->cond.wait(uniqueLock);
            // 当线程醒来发现需要有线程退出
            if (pool->exit_thread_num.load() != 0) {
                --pool->exit_thread_num;
                uniqueLock.unlock();
                // 在要销毁的线程列表中存入自己的线程ID
                pool->to_destroy_mutex.lock();
                pool->to_destroy->push_back(this->thread_id);
                pool->to_destroy_mutex.unlock();
                return;
            }
            // 当线程醒来发现要关闭
            if (pool->state_code != 1) return;
            // 先判断需不需要退出再判断是不是要关闭线程池
            // 防止关闭线程池时管理者还在等待工作线程退出，造成管理者一直阻塞等待
        }
        // 从任务队列取一个任务运行
        std::function<void()> task = pool->task_queue->front();
        pool->task_queue->pop();
        // 释放任务队列锁
        uniqueLock.unlock();
        // 唤醒添加任务的函数
        pool->cond.notify_one();
        // 工作线程数量加一
        ++pool->working_thread_num;
        // 执行任务
        task();
        // 执行完任务，工作线程数量减一
        --pool->working_thread_num;
    }
}

#endif
