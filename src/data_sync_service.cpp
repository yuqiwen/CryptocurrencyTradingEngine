#include "data_sync_service.h"
#include <iostream>

DataSyncService::DataSyncService(const std::string& db_conninfo,
                                const std::string& redis_host,
                                int redis_port,
                                const std::string& redis_password) {
    
    db_reader_ = std::make_unique<TimescaleDBReader>(db_conninfo);
    redis_writer_ = std::make_unique<RedisWriter>(redis_host, redis_port, redis_password);
    scheduler_ = std::make_unique<Scheduler>();
    
    reset_stats();
    
    std::cout << "DataSyncService initialized with Scheduler" << std::endl;
}

DataSyncService::~DataSyncService() {
    stop_scheduler();
}

bool DataSyncService::is_healthy() const {
    return redis_writer_->is_connected();
}

void DataSyncService::reset_stats() {
    stats_.raw_records_synced = 0;
    stats_.price_stats_records_synced = 0;
    stats_.total_sync_count = 0;
    stats_.failed_sync_count = 0;
    stats_.last_sync_time = std::chrono::system_clock::now();
}

void DataSyncService::update_stats(bool success, int raw_count, int stats_count) {
    stats_.total_sync_count++;
    if (success) {
        stats_.raw_records_synced += raw_count;
        stats_.price_stats_records_synced += stats_count;
    } else {
        stats_.failed_sync_count++;
    }
    stats_.last_sync_time = std::chrono::system_clock::now();
}

bool DataSyncService::sync_raw_data() {
    if (!is_healthy()) {
        std::cerr << "Service not healthy, skipping raw data sync" << std::endl;
        return false;
    }
    
    try {
        std::cout << "Reading latest raw data from TimescaleDB..." << std::endl;
        auto raw_records = db_reader_->read_latest_raw();
        
        if (raw_records.empty()) {
            std::cout << "No raw data found" << std::endl;
            return true;
        }
        
        std::cout << "Found " << raw_records.size() << " raw records, writing to Redis..." << std::endl;
        bool success = redis_writer_->write_raw_records(raw_records);
        
        if (success) {
            std::cout << "Successfully synced " << raw_records.size() << " raw records" << std::endl;
        } else {
            std::cerr << "Failed to write raw records to Redis" << std::endl;
        }
        
        return success;
        
    } catch (const std::exception& e) {
        std::cerr << "Error syncing raw data: " << e.what() << std::endl;
        return false;
    }
}

bool DataSyncService::sync_price_stats_data() {
    if (!is_healthy()) {
        std::cerr << "Service not healthy, skipping price stats sync" << std::endl;
        return false;
    }
    
    try {
        std::cout << "Reading latest price stats from TimescaleDB..." << std::endl;
        auto stats_records = db_reader_->read_latest_price_stats();
        
        if (stats_records.empty()) {
            std::cout << "No price stats data found" << std::endl;
            return true;
        }
        
        std::cout << "Found " << stats_records.size() << " price stats records, writing to Redis..." << std::endl;
        bool success = redis_writer_->write_price_stats_records(stats_records);
        
        if (success) {
            std::cout << "Successfully synced " << stats_records.size() << " price stats records" << std::endl;
        } else {
            std::cerr << "Failed to write price stats records to Redis" << std::endl;
        }
        
        return success;
        
    } catch (const std::exception& e) {
        std::cerr << "Error syncing price stats: " << e.what() << std::endl;
        return false;
    }
}

bool DataSyncService::sync_once() {
    std::cout << "=== Starting data sync ===" << std::endl;
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // 获取数据量用于统计
    auto raw_records = db_reader_->read_latest_raw();
    auto stats_records = db_reader_->read_latest_price_stats();
    
    bool raw_success = sync_raw_data();
    bool stats_success = sync_price_stats_data();
    
    bool overall_success = raw_success && stats_success;
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    update_stats(overall_success, raw_records.size(), stats_records.size());
    
    std::cout << "=== Sync completed in " << duration.count() << "ms ===" << std::endl;
    std::cout << "Raw records: " << raw_records.size() << " (" << (raw_success ? "success" : "failed") << ")" << std::endl;
    std::cout << "Stats records: " << stats_records.size() << " (" << (stats_success ? "success" : "failed") << ")" << std::endl;
    
    return overall_success;
}

void DataSyncService::start_scheduler() {
    std::cout << "Starting task scheduler..." << std::endl;
    scheduler_->start();
}

void DataSyncService::stop_scheduler() {
    if (scheduler_) {
        std::cout << "Stopping task scheduler..." << std::endl;
        scheduler_->stop();
    }
}

void DataSyncService::schedule_sync_task(int interval_ms) {
    auto sync_task = [this]() {
        std::cout << "[Scheduled] Executing full sync..." << std::endl;
        sync_once();
    };
    
    scheduler_->addTask(sync_task, interval_ms);
    std::cout << "Scheduled full sync task every " << interval_ms << "ms" << std::endl;
}

void DataSyncService::set_redis_expire_time(int seconds) {
    redis_writer_->set_expire_time(seconds);
    std::cout << "Set Redis expire time to " << seconds << " seconds" << std::endl;
}