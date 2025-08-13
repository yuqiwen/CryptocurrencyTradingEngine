#include "../include/redis_writer.h"
#include "../include/timescaledb_reader.h"  // 为了使用数据结构
#include <hiredis/hiredis.h>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <nlohmann/json.hpp>
using json = nlohmann::json;

RedisWriter::RedisWriter(const std::string& host, int port, const std::string& password)
    : context_(nullptr), expire_time_(3600), host_(host), port_(port), password_(password) {
    connect();
}

RedisWriter::~RedisWriter() {
    disconnect();
}

bool RedisWriter::connect() {
    context_ = redisConnect(host_.c_str(), port_);
    
    if (context_ == nullptr || context_->err) {
        if (context_) {
            std::cerr << "Redis connection error: " << context_->errstr << std::endl;
            redisFree(context_);
            context_ = nullptr;
        } else {
            std::cerr << "Redis connection error: can't allocate redis context" << std::endl;
        }
        return false;
    }
    
    // 如果有密码，进行认证
    if (!password_.empty()) {
        redisReply* reply = (redisReply*)redisCommand(context_, "AUTH %s", password_.c_str());
        if (reply == nullptr || reply->type == REDIS_REPLY_ERROR) {
            std::cerr << "Redis authentication failed" << std::endl;
            if (reply) freeReplyObject(reply);
            disconnect();
            return false;
        }
        freeReplyObject(reply);
    }
    
    std::cout << "Connected to Redis successfully" << std::endl;
    return true;
}

void RedisWriter::disconnect() {
    if (context_) {
        redisFree(context_);
        context_ = nullptr;
    }
}

bool RedisWriter::is_connected() const {
    return context_ != nullptr && context_->err == 0;
}

std::string RedisWriter::get_raw_key(const std::string& exchange, const std::string& symbol) {
    return "crypto:raw:" + exchange + ":" + symbol;
}

std::string RedisWriter::get_price_stats_key(const std::string& symbol) {
    return "crypto:stats:" + symbol;
}

std::string RedisWriter::serialize_raw_record(const RawRecord& record) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(8);
    oss << "{"
        << "\"id\":" << record.id << ","
        << "\"exchange\":\"" << record.exchange << "\","
        << "\"symbol\":\"" << record.symbol << "\","
        << "\"last\":" << record.last << ","
        << "\"bid\":" << record.bid << ","
        << "\"ask\":" << record.ask << ","
        << "\"high\":" << record.high << ","
        << "\"low\":" << record.low << ","
        << "\"volume\":" << record.volume << ","
        << "\"timestamp\":" << record.timestamp
        << "}";
    return oss.str();
}

std::string RedisWriter::serialize_price_stats_record(const PriceStatsRecord& record) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(8);
    oss << "{"
        << "\"id\":" << record.id << ","
        << "\"symbol\":\"" << record.symbol << "\","
        << "\"highest_price\":" << record.highest_price << ","
        << "\"highest_exchange\":\"" << record.highest_exchange << "\","
        << "\"lowest_price\":" << record.lowest_price << ","
        << "\"lowest_exchange\":\"" << record.lowest_exchange << "\","
        << "\"record_count\":" << record.record_count << ","
        << "\"earliest_timestamp\":" << record.earliest_timestamp << ","
        << "\"latest_timestamp\":" << record.latest_timestamp
        << "}";
    return oss.str();
}

bool RedisWriter::write_raw_record(const RawRecord& record) {
    if (!is_connected()) {
        std::cerr << "Redis not connected" << std::endl;
        return false;
    }
    
    std::string key = get_raw_key(record.exchange, record.symbol);
    std::string value = serialize_raw_record(record);
    
    redisReply* reply = (redisReply*)redisCommand(context_, 
        "SETEX %s %d %s", key.c_str(), expire_time_, value.c_str());
    
    if (reply == nullptr || reply->type == REDIS_REPLY_ERROR) {
        std::cerr << "Failed to write raw record to Redis" << std::endl;
        if (reply) freeReplyObject(reply);
        return false;
    }
    
    freeReplyObject(reply);
    return true;
}

bool RedisWriter::write_raw_records(const std::vector<RawRecord>& records) {
    if (!is_connected()) {
        std::cerr << "Redis not connected" << std::endl;
        return false;
    }
    
    if (records.empty()) return true;
    
    // 使用 pipeline 提高性能
    for (const auto& record : records) {
        std::string key = get_raw_key(record.exchange, record.symbol);
        std::string value = serialize_raw_record(record);
        
        redisAppendCommand(context_, "SETEX %s %d %s", 
            key.c_str(), expire_time_, value.c_str());
    }
    
    // 获取所有回复
    bool success = true;
    for (size_t i = 0; i < records.size(); ++i) {
        redisReply* reply;
        if (redisGetReply(context_, (void**)&reply) != REDIS_OK) {
            success = false;
            std::cerr << "Failed to get reply for raw record " << i << std::endl;
            continue;
        }
        
        if (reply->type == REDIS_REPLY_ERROR) {
            success = false;
            std::cerr << "Error writing raw record " << i << ": " << reply->str << std::endl;
        }
        
        freeReplyObject(reply);
    }
    
    std::cout << "Wrote " << records.size() << " raw records to Redis" << std::endl;
    return success;
}

bool RedisWriter::write_price_stats_record(const PriceStatsRecord& record) {
    if (!is_connected()) {
        std::cerr << "Redis not connected" << std::endl;
        return false;
    }
    
    std::string key = get_price_stats_key(record.symbol);
    std::string value = serialize_price_stats_record(record);
    
    redisReply* reply = (redisReply*)redisCommand(context_, 
        "SETEX %s %d %s", key.c_str(), expire_time_, value.c_str());
    
    if (reply == nullptr || reply->type == REDIS_REPLY_ERROR) {
        std::cerr << "Failed to write price stats record to Redis" << std::endl;
        if (reply) freeReplyObject(reply);
        return false;
    }
    
    freeReplyObject(reply);
    return true;
}

bool RedisWriter::write_price_stats_records(const std::vector<PriceStatsRecord>& records) {
    if (!is_connected()) {
        std::cerr << "Redis not connected" << std::endl;
        return false;
    }
    
    if (records.empty()) return true;
    
    // 使用 pipeline 提高性能
    for (const auto& record : records) {
        std::string key = get_price_stats_key(record.symbol);
        std::string value = serialize_price_stats_record(record);
        
        redisAppendCommand(context_, "SETEX %s %d %s", 
            key.c_str(), expire_time_, value.c_str());
    }
    
    // 获取所有回复
    bool success = true;
    for (size_t i = 0; i < records.size(); ++i) {
        redisReply* reply;
        if (redisGetReply(context_, (void**)&reply) != REDIS_OK) {
            success = false;
            std::cerr << "Failed to get reply for price stats record " << i << std::endl;
            continue;
        }
        
        if (reply->type == REDIS_REPLY_ERROR) {
            success = false;
            std::cerr << "Error writing price stats record " << i << ": " << reply->str << std::endl;
        }
        
        freeReplyObject(reply);
    }
    
    std::cout << "Wrote " << records.size() << " price stats records to Redis" << std::endl;
    return success;
}

RawRecord RedisWriter::deserialize_raw_record(const std::string& json_str) {
    RawRecord record;
    auto j = json::parse(json_str);

    record.id        = j.value("id", 0);
    record.exchange  = j.value("exchange", "");
    record.symbol    = j.value("symbol", "");
    record.last      = j.value("last", 0.0);
    record.bid       = j.value("bid", 0.0);
    record.ask       = j.value("ask", 0.0);
    record.high      = j.value("high", 0.0);
    record.low       = j.value("low", 0.0);
    record.volume    = j.value("volume", 0.0);
    record.timestamp = j.value("timestamp", 0LL);

    return record;
}

PriceStatsRecord RedisWriter::deserialize_price_stats_record(const std::string& json_str) {
    PriceStatsRecord record;
    auto j = json::parse(json_str);

    record.id                 = j.value("id", 0);
    record.symbol             = j.value("symbol", "");
    record.highest_price      = j.value("highest_price", 0.0);
    record.highest_exchange   = j.value("highest_exchange", "");
    record.lowest_price       = j.value("lowest_price", 0.0);
    record.lowest_exchange    = j.value("lowest_exchange", "");
    record.record_count       = j.value("record_count", 0);
    record.earliest_timestamp = j.value("earliest_timestamp", 0LL);
    record.latest_timestamp   = j.value("latest_timestamp", 0LL);

    return record;
}

bool RedisWriter::read_raw_record(const std::string& exchange, const std::string& symbol, RawRecord& record) {
    if (!is_connected()) return false;
    
    std::string key = get_raw_key(exchange, symbol);
    redisReply* reply = (redisReply*)redisCommand(context_, "GET %s", key.c_str());
    
    if (reply == nullptr || reply->type != REDIS_REPLY_STRING) {
        if (reply) freeReplyObject(reply);
        return false;
    }
    
    record = deserialize_raw_record(reply->str);
    freeReplyObject(reply);
    return true;
}

std::vector<RawRecord> RedisWriter::read_all_raw_records() {
    std::vector<RawRecord> records;
    if (!is_connected()) return records;
    
    // 获取所有 crypto:raw:* 的 keys
    redisReply* reply = (redisReply*)redisCommand(context_, "KEYS crypto:raw:*");
    if (reply == nullptr || reply->type != REDIS_REPLY_ARRAY) {
        if (reply) freeReplyObject(reply);
        return records;
    }
    
    // 批量获取所有值
    if (reply->elements > 0) {
        std::string mget_cmd = "MGET";
        for (size_t i = 0; i < reply->elements; ++i) {
            mget_cmd += " " + std::string(reply->element[i]->str);
        }
        
        freeReplyObject(reply);
        reply = (redisReply*)redisCommand(context_, mget_cmd.c_str());
        
        if (reply && reply->type == REDIS_REPLY_ARRAY) {
            for (size_t i = 0; i < reply->elements; ++i) {
                if (reply->element[i]->type == REDIS_REPLY_STRING) {
                    records.push_back(deserialize_raw_record(reply->element[i]->str));
                }
            }
        }
    }
    
    if (reply) freeReplyObject(reply);
    return records;
}

std::vector<RawRecord> RedisWriter::read_raw_records_by_exchange(const std::string& exchange) {
    std::vector<RawRecord> records;
    if (!is_connected()) return records;
    
    std::string pattern = "crypto:raw:" + exchange + ":*";
    redisReply* reply = (redisReply*)redisCommand(context_, "KEYS %s", pattern.c_str());
    
    if (reply == nullptr || reply->type != REDIS_REPLY_ARRAY) {
        if (reply) freeReplyObject(reply);
        return records;
    }
    
    // 类似 read_all_raw_records 的逻辑
    // ... 实现细节
    
    freeReplyObject(reply);
    return records;
}

bool RedisWriter::read_price_stats_record(const std::string& symbol, PriceStatsRecord& record) {
    if (!is_connected()) return false;
    
    std::string key = get_price_stats_key(symbol);
    redisReply* reply = (redisReply*)redisCommand(context_, "GET %s", key.c_str());
    
    if (reply == nullptr || reply->type != REDIS_REPLY_STRING) {
        if (reply) freeReplyObject(reply);
        return false;
    }
    
    record = deserialize_price_stats_record(reply->str);
    freeReplyObject(reply);
    return true;
}

std::vector<PriceStatsRecord> RedisWriter::read_all_price_stats_records() {
    std::vector<PriceStatsRecord> records;
    if (!is_connected()) return records;
    
    redisReply* reply = (redisReply*)redisCommand(context_, "KEYS crypto:stats:*");
    if (reply == nullptr || reply->type != REDIS_REPLY_ARRAY) {
        if (reply) freeReplyObject(reply);
        return records;
    }
    
    // 类似 read_all_raw_records 的实现
    // ... 
    
    freeReplyObject(reply);
    return records;
}

bool RedisWriter::exists_raw_record(const std::string& exchange, const std::string& symbol) {
    if (!is_connected()) return false;
    
    std::string key = get_raw_key(exchange, symbol);
    redisReply* reply = (redisReply*)redisCommand(context_, "EXISTS %s", key.c_str());
    
    bool exists = (reply && reply->type == REDIS_REPLY_INTEGER && reply->integer == 1);
    if (reply) freeReplyObject(reply);
    return exists;
}

bool RedisWriter::exists_price_stats_record(const std::string& symbol) {
    if (!is_connected()) return false;
    
    std::string key = get_price_stats_key(symbol);
    redisReply* reply = (redisReply*)redisCommand(context_, "EXISTS %s", key.c_str());
    
    bool exists = (reply && reply->type == REDIS_REPLY_INTEGER && reply->integer == 1);
    if (reply) freeReplyObject(reply);
    return exists;
}

std::vector<std::string> RedisWriter::get_all_raw_keys() {
    std::vector<std::string> keys;
    if (!is_connected()) return keys;
    
    redisReply* reply = (redisReply*)redisCommand(context_, "KEYS crypto:raw:*");
    if (reply && reply->type == REDIS_REPLY_ARRAY) {
        for (size_t i = 0; i < reply->elements; ++i) {
            keys.push_back(reply->element[i]->str);
        }
    }
    if (reply) freeReplyObject(reply);
    return keys;
}

std::vector<std::string> RedisWriter::get_all_price_stats_keys() {
    std::vector<std::string> keys;
    if (!is_connected()) return keys;
    
    redisReply* reply = (redisReply*)redisCommand(context_, "KEYS crypto:stats:*");
    if (reply && reply->type == REDIS_REPLY_ARRAY) {
        for (size_t i = 0; i < reply->elements; ++i) {
            keys.push_back(reply->element[i]->str);
        }
    }
    if (reply) freeReplyObject(reply);
    return keys;
}

int RedisWriter::get_ttl(const std::string& key) {
    if (!is_connected()) return -1;
    
    redisReply* reply = (redisReply*)redisCommand(context_, "TTL %s", key.c_str());
    int ttl = -1;
    if (reply && reply->type == REDIS_REPLY_INTEGER) {
        ttl = reply->integer;
    }
    if (reply) freeReplyObject(reply);
    return ttl;
}

bool RedisWriter::clear_price_stats_data() {
    if (!is_connected()) return false;
    
    redisReply* reply = (redisReply*)redisCommand(context_, "DEL crypto:stats:*");
    bool success = (reply && reply->type != REDIS_REPLY_ERROR);
    if (reply) freeReplyObject(reply);
    return success;
}