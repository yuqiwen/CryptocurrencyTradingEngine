#pragma once
#include "timescaledb_reader.h"
#include "redis_writer.h"
#include "scheduler.hpp"
#include <memory>
#include <chrono>

class DataSyncService {
public:
    DataSyncService(const std::string& db_conninfo, 
                   const std::string& redis_host = "127.0.0.1",
                   int redis_port = 6379,
                   const std::string& redis_password = "");
    
    ~DataSyncService();
    
    // === 核心同步功能 ===
    
    /**
     * 执行一次完整的数据同步（raw + stats）
     * 这是最常用的核心功能
     */
    bool sync_once();
    
    /**
     * 只同步原始价格数据
     * 适用于需要单独更新原始数据的场景
     */
    bool sync_raw_data();
    
    /**
     * 只同步价格统计数据  
     * 适用于需要单独更新统计数据的场景
     */
    bool sync_price_stats_data();
    
    // === 调度器功能 ===
    
    /**
     * 启动任务调度器
     * 必须在添加任务前调用
     */
    void start_scheduler();
    
    /**
     * 停止任务调度器
     * 停止所有定时任务
     */
    void stop_scheduler();
    
    /**
     * 添加定时完整同步任务（同时同步 raw + stats）
     * @param interval_ms 间隔时间（毫秒）
     * 推荐：30000ms (30秒) 用于生产环境
     */
    void schedule_sync_task(int interval_ms);
    
    // === 配置和监控 ===
    
    /**
     * 设置 Redis 数据过期时间
     * @param seconds 过期时间（秒），默认3600（1小时）
     */
    void set_redis_expire_time(int seconds);
    
    /**
     * 检查服务健康状态
     * 主要检查 Redis 连接是否正常
     */
    bool is_healthy() const;
    
    // === 统计信息 ===
    
    struct SyncStats {
        int raw_records_synced;           // 已同步的原始记录数
        int price_stats_records_synced;   // 已同步的统计记录数
        int total_sync_count;             // 总同步次数
        int failed_sync_count;            // 失败同步次数
        std::chrono::system_clock::time_point last_sync_time;  // 最后同步时间
    };
    
    /**
     * 获取同步统计信息
     * 用于监控和调试
     */
    SyncStats get_stats() const { return stats_; }

private:
    // 核心组件
    std::unique_ptr<TimescaleDBReader> db_reader_;
    std::unique_ptr<RedisWriter> redis_writer_;
    std::unique_ptr<Scheduler> scheduler_;
    
    // 统计数据
    SyncStats stats_;
    
    // 内部辅助方法
    void reset_stats();
    void update_stats(bool success, int raw_count, int stats_count);
};