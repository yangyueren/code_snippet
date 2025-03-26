/*

Ref: https://github.com/apache/brpc/tree/master/example/streaming_echo_c++

target_link_libraries(streaming_echo_client PRIVATE z)
target_link_libraries(streaming_echo_server PRIVATE z)


- POOL_SIZE = 1000:

Latency statistics (μs):
Median: 8237
90th percentile: 9885
99th percentile: 11862
Max: 15693
QPS: 91735.5
Total count: 11000000


Latency statistics (μs):
Median: 8237
90th percentile: 9885
99th percentile: 11862
Max: 15693
QPS: 91630.4
Total count: 11500000


- POOL_SIZE = 20:

Latency statistics (μs):
Median: 256
90th percentile: 308
99th percentile: 370
Max: 4300
QPS: 83814
Total count: 3500000

*/



#include <gflags/gflags.h>
#include <butil/logging.h>
#include <brpc/channel.h>
#include <brpc/stream.h>
#include "echo.pb.h"

#include <thread>
#include <chrono>
#include <atomic>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <mutex>
#include <cmath>

// 全局原子变量，用于统计发送和接收的消息数
std::atomic<int64_t> g_sent_count{0};
std::atomic<int64_t> g_recv_count{0};

// 用于统计延迟的直方图（单位：μs）
class LatencyHistogram {
 public:
  LatencyHistogram() : total_(0), max_(0) {
    uint64_t cur = 1;
    const uint64_t max_bound = 10000000000ULL;  // 10秒 = 10_000_000_000μs
    while (cur < max_bound) {
      boundaries_.push_back(cur);
      uint64_t next = static_cast<uint64_t>(std::ceil(cur * 1.2));
      if (next <= cur) break;
      cur = next;
    }
    boundaries_.push_back(max_bound);
    counts_.resize(boundaries_.size(), 0);
  }

  void record(uint64_t micros) {
    size_t idx = 0;
    while (idx < boundaries_.size() && micros > boundaries_[idx]) ++idx;
    if (idx >= counts_.size()) idx = counts_.size() - 1;
    ++counts_[idx];
    ++total_;
    if (micros > max_) max_ = micros;
  }

  uint64_t quantile(double q) const {
    if (total_ == 0) return 0;
    uint64_t target = static_cast<uint64_t>(std::ceil(total_ * q));
    uint64_t sum = 0;
    for (size_t i = 0; i < counts_.size(); ++i) {
      sum += counts_[i];
      if (sum >= target) return boundaries_[i];
    }
    return boundaries_.back();
  }

  uint64_t max() const { return max_; }
  uint64_t total() const { return total_; }

 private:
  std::vector<uint64_t> boundaries_;
  std::vector<uint64_t> counts_;
  uint64_t total_;
  uint64_t max_;
};

// 固定2KB消息大小
const size_t MESSAGE_SIZE = 2048;
// 初始发送消息数（池大小）
const int POOL_SIZE = 1000;

// 获取当前时间（单位：μs）
uint64_t get_current_time_us() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
           std::chrono::steady_clock::now().time_since_epoch()).count();
}

// 构造固定大小的 payload（填充 'x'）
std::string create_payload() {
    return std::string(MESSAGE_SIZE, 'x');
}

// 发送 EchoRequest 请求，通过序列化 proto 消息发送
// msg_id 这里依然用作记录发送时刻
int send_request(brpc::StreamId stream, int64_t msg_id) {
    example::EchoRequest req;
    req.set_message(create_payload());
    req.set_id(msg_id);
    std::string serialized;
    if (!req.SerializeToString(&serialized)) {
        LOG(ERROR) << "Failed to serialize EchoRequest";
        return -1;
    }
    butil::IOBuf payload;
    payload.append(serialized);
    // 如果写失败则稍作等待，直到成功为止
    while (true) {
        if (brpc::StreamWrite(stream, payload) == 0) {
            return 0;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return -1;
}

// 客户端异步接收处理器：收到回复后解析 EchoResponse，计算 RTT，并更新接收计数
class ClientStreamReceiver : public brpc::StreamInputHandler {
public:
    // send_times 数组用于记录每个 msg_id 对应的发送时间（本例中未再使用）
    ClientStreamReceiver(LatencyHistogram* histogram,
                         std::vector<uint64_t>* send_times)
        : histogram_(histogram), send_times_(send_times), start_time_(get_current_time_us()) {}

    virtual int on_received_messages(brpc::StreamId stream,
                                     butil::IOBuf* const messages[],
                                     size_t size) override {
        for (size_t i = 0; i < size; i++) {
            std::string data;
            data.resize(messages[i]->size());
            messages[i]->copy_to(&data[0], data.size());
            example::EchoResponse resp;
            if (!resp.ParseFromString(data)) {
                LOG(ERROR) << "Failed to parse EchoResponse";
                continue;
            }
            int64_t send_time = resp.id();
            uint64_t recv_time = get_current_time_us();
            uint64_t latency = recv_time - send_time;
            histogram_->record(latency);
            // 更新接收计数
            g_recv_count.fetch_add(1, std::memory_order_relaxed);

            // 每收到一定数量的回复，打印延迟统计信息
            if (histogram_->total() % 500000 == 0) {
                std::cout << "Latency statistics (μs):" << std::endl;
                std::cout << "Median: " << histogram_->quantile(0.5) << std::endl;
                std::cout << "90th percentile: " << histogram_->quantile(0.9) << std::endl;
                std::cout << "99th percentile: " << histogram_->quantile(0.99) << std::endl;
                std::cout << "Max: " << histogram_->max() << std::endl;
                double elapsed = (get_current_time_us() - start_time_) / 1000000.0;
                std::cout << "QPS: " << histogram_->total() / elapsed << std::endl;
                std::cout << "Total count: " << histogram_->total() << std::endl;
            }
        }
        return 0;
    }

    virtual void on_idle_timeout(brpc::StreamId id) override {
        LOG(INFO) << "Client stream idle timeout: " << id;
    }
    virtual void on_closed(brpc::StreamId id) override {
        LOG(INFO) << "Client stream closed: " << id;
    }

private:
    LatencyHistogram* histogram_;
    std::vector<uint64_t>* send_times_;
    uint64_t start_time_;
};

DEFINE_bool(send_attachment, true, "Carry attachment along with requests");
DEFINE_string(connection_type, "pooled", "Connection type. Available values: single, pooled, short");
DEFINE_string(server, "0.0.0.0:8001", "IP Address of server");
DEFINE_int32(timeout_ms, 100, "RPC timeout in milliseconds");
DEFINE_int32(max_retry, 3, "Max retries (not including the first RPC)");

int main(int argc, char* argv[]) {
    // 解析命令行参数
    GFLAGS_NAMESPACE::ParseCommandLineFlags(&argc, &argv, true);

    // 创建并初始化 Channel
    brpc::Channel channel;
    brpc::ChannelOptions options;
    options.protocol = brpc::PROTOCOL_BAIDU_STD;
    options.connection_type = FLAGS_connection_type;
    options.timeout_ms = FLAGS_timeout_ms;
    options.max_retry = FLAGS_max_retry;
    if (channel.Init(FLAGS_server.c_str(), nullptr) != 0) {
         LOG(ERROR) << "Failed to initialize channel";
         return -1;
    }

    // 构造 stub
    example::EchoService_Stub stub(&channel);
    brpc::Controller cntl;

    // 延迟统计对象
    LatencyHistogram histogram;
    // 创建大小为 POOL_SIZE 的数组（本例中未使用此数组做其他用途）
    std::vector<uint64_t> send_times(POOL_SIZE, 0);

    // 创建客户端异步接收处理器，并注册到 StreamOptions 中
    ClientStreamReceiver client_receiver(&histogram, &send_times);
    brpc::StreamOptions stream_options;
    stream_options.handler = &client_receiver;
    brpc::StreamId stream;
    if (brpc::StreamCreate(&stream, cntl, &stream_options) != 0) {
         LOG(ERROR) << "Failed to create stream";
         return -1;
    }
    LOG(INFO) << "Created stream=" << stream;

    // 发送 Echo RPC 建立流连接（服务端需要修改为基于请求构造 EchoResponse，
    // 并将 EchoResponse.id 设置为 EchoRequest.id）
    example::EchoRequest req;
    example::EchoResponse resp;
    req.set_message("I'm a RPC to connect stream");
    req.set_id(1);
    stub.Echo(&cntl, &req, &resp, nullptr);
    if (cntl.Failed()) {
         LOG(ERROR) << "Failed to connect stream, " << cntl.ErrorText();
         return -1;
    }
    LOG(INFO) << "Stream accepted with response: " << resp.message();

    // 启动一个独立的发送线程，负责后续发送请求
    std::thread sender_thread([stream]() {
        while (!brpc::IsAskedToQuit()) {
            // 当发送与接收之间的差值超过10000时，等待一段时间
            if (g_sent_count.load(std::memory_order_relaxed) - 
                g_recv_count.load(std::memory_order_relaxed) >= POOL_SIZE) {
                // std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            // 发送新请求，msg_id 用当前时间记录
            if (send_request(stream, get_current_time_us()) == 0) {
                g_sent_count.fetch_add(1, std::memory_order_relaxed);
            } else {
                LOG(ERROR) << "Failed to send new request";
            }
        }
    });

    // 主线程等待退出信号
    while (!brpc::IsAskedToQuit()) {
         std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    if (brpc::StreamClose(stream) != 0) {
         LOG(ERROR) << "Failed to close stream";
    }
    LOG(INFO) << "Client is going to quit";
    
    // 等待发送线程结束
    if (sender_thread.joinable()) {
         sender_thread.join();
    }
    return 0;
}
