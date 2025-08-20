#include "scheduler.hpp"
#include <algorithm>
#include <iostream>

Scheduler::Scheduler() : running(false) {}

Scheduler::~Scheduler() {
    stop();  // 确保退出时安全停止
}

void Scheduler::start() {
    running = true;
    schedulerThread = std::thread(&Scheduler::run, this);
}

void Scheduler::stop() {
    if (!running) return;
    
    std::cout << "Stopping scheduler..." << std::endl;
    
    {
        std::lock_guard<std::mutex> lock(mtx);
        running = false;
        tasks.clear();  // 清理等待中的任务
        cv.notify_all();
    }
    
    if (schedulerThread.joinable()) {
        schedulerThread.join();
    }
    
    std::cout << "Scheduler stopped." << std::endl;
}

void Scheduler::addTask(const std::function<void()>& task, int interval_ms) {
    std::lock_guard<std::mutex> lock(mtx);
    SchedulerTask t;
    t.taskFn = task;
    t.interval = std::chrono::milliseconds(interval_ms);
    t.nextRunTime = std::chrono::steady_clock::now() + t.interval;
    tasks.push_back(t);
    cv.notify_all();  // 有新任务了，唤醒线程
}

void Scheduler::run() {
    while (running) {
        std::unique_lock<std::mutex> lock(mtx);

        if (tasks.empty()) {
            cv.wait(lock);
            continue;
        }

        auto now = std::chrono::steady_clock::now();

        // 找下次要执行任务的最小时间
        auto next_it = std::min_element(tasks.begin(), tasks.end(),
            [](const SchedulerTask& a, const SchedulerTask& b) {
                return a.nextRunTime < b.nextRunTime;
            });

        if (next_it == tasks.end()) {
            cv.wait(lock);
            continue;
        }

        if (now >= next_it->nextRunTime) {
            auto taskToRun = *next_it;
            next_it->nextRunTime = now + next_it->interval;

            // unlock 让其他线程也可以 addTask
            lock.unlock();

            // 异步执行任务，防止阻塞主调度线程
            std::thread(taskToRun.taskFn).detach();
        } else {
            cv.wait_until(lock, next_it->nextRunTime);
        }
    }
}
