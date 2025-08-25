#pragma once

#include <functional>
#include <chrono>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

using namespace std;

struct SchedulerTask{
    function<void()>taskFn;
    chrono::milliseconds interval; 
    chrono::steady_clock::time_point nextRunTime;
};

class Scheduler{
    public:
        Scheduler();
        ~Scheduler();
        void start();
        void stop();
        void addTask(const function<void()>& task, int interval_ms);
    private:
        void run();
        vector<SchedulerTask>tasks;
        thread schedulerThread;
        mutex mtx;
        condition_variable cv;
        atomic<bool> running;
};
