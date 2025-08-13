cmake_minimum_required(VERSION 3.10)
project(trading_engine_server)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pthread")

# 查找依赖
find_package(PostgreSQL REQUIRED)
find_package(CURL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(HIREDIS REQUIRED hiredis)

# 头文件路径
include_directories(include)
include_directories(${PostgreSQL_INCLUDE_DIRS})
include_directories(${HIREDIS_INCLUDE_DIRS})
include_directories(${CURL_INCLUDE_DIRS})

# 各模块构建为独立库
add_library(timescaledb_reader STATIC src/timescaledb_reader.cpp)
target_link_libraries(timescaledb_reader PRIVATE pq)

add_library(redis_writer STATIC src/redis_writer.cpp)
target_link_libraries(redis_writer PRIVATE hiredis)

add_library(ccxt_client STATIC src/ccxt_client.cpp)
target_link_libraries(ccxt_client PRIVATE curl)

add_library(scheduler STATIC src/scheduler.cpp)

add_library(data_sync_service STATIC src/data_sync_service.cpp)
target_link_libraries(data_sync_service PRIVATE timescaledb_reader redis_writer scheduler)

add_library(order_manager STATIC src/order_manager.cpp)
target_link_libraries(order_manager PRIVATE ccxt_client)

add_library(arbitrage_strategy STATIC src/arbitrage_strategy.cpp)
add_library(market_making_strategy STATIC src/market_making_strategy.cpp)

add_library(trading_engine_manager STATIC src/trading_engine_manager.cpp)
target_link_libraries(trading_engine_manager PRIVATE
    timescaledb_reader redis_writer data_sync_service scheduler
    order_manager arbitrage_strategy market_making_strategy
)

# 主服务程序入口
add_executable(engine_server
    src/engine_server.cpp
    src/engine_api.cpp
)
target_link_libraries(engine_server PRIVATE
    trading_engine_manager
    pq hiredis curl
)
