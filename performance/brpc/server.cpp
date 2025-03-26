#include <gflags/gflags.h>
#include <butil/logging.h>
#include <brpc/server.h>
#include "echo.pb.h"
#include <brpc/stream.h>
#include <sstream>

DEFINE_bool(send_attachment, true, "Carry attachment along with response");
DEFINE_int32(port, 8001, "TCP Port of this server");
DEFINE_int32(idle_timeout_s, -1, "Connection will be closed if there is no "
             "read/write operations during the last `idle_timeout_s'");

// StreamReceiver 实现：每当接收到消息时，将请求反序列化，构造带有相同 id 的响应后返回
class StreamReceiver : public brpc::StreamInputHandler {
public:
    virtual int on_received_messages(brpc::StreamId id, 
                                     butil::IOBuf *const messages[], 
                                     size_t size) {
        for (size_t i = 0; i < size; ++i) {
            std::string req_str;
            req_str.resize(messages[i]->size());
            messages[i]->copy_to(&req_str[0], req_str.size());


            // 解析 EchoRequest
            example::EchoRequest req;
            if (!req.ParseFromString(req_str)) {
                LOG(ERROR) << "Failed to parse EchoRequest";
                continue;
            }
            // LOG(INFO) << "Received request: " << req.id();
            // 构造 EchoResponse，复制 id 并设置响应消息
            example::EchoResponse resp;
            resp.set_message("Reply from server");
            resp.set_id(req.id());

            std::string resp_str;
            if (!resp.SerializeToString(&resp_str)) {
                LOG(ERROR) << "Failed to serialize EchoResponse";
                continue;
            }
            butil::IOBuf reply;
            reply.append(resp_str);
            if (brpc::StreamWrite(id, reply) != 0) {
                LOG(ERROR) << "Failed to write reply on stream " << id;
            }
        }
        return 0;
    }
    virtual void on_idle_timeout(brpc::StreamId id) {
        LOG(INFO) << "Stream=" << id << " has no data transmission for a while";
    }
    virtual void on_closed(brpc::StreamId id) {
        LOG(INFO) << "Stream=" << id << " is closed";
    }
};

// EchoService 服务实现：在 Echo 接口中接受 stream 并设置 StreamReceiver
class StreamingEchoService : public example::EchoService {
public:
    StreamingEchoService() : _sd(brpc::INVALID_STREAM_ID) {}
    virtual ~StreamingEchoService() {
        brpc::StreamClose(_sd);
    }
    virtual void Echo(google::protobuf::RpcController* controller,
                      const example::EchoRequest* /*request*/,
                      example::EchoResponse* response,
                      google::protobuf::Closure* done) {
        brpc::ClosureGuard done_guard(done);
        brpc::Controller* cntl = static_cast<brpc::Controller*>(controller);
        brpc::StreamOptions stream_options;
        stream_options.handler = &_receiver;
        if (brpc::StreamAccept(&_sd, *cntl, &stream_options) != 0) {
            cntl->SetFailed("Fail to accept stream");
            return;
        }
        response->set_message("Accepted stream");
        response->set_id(1);
    }
private:
    StreamReceiver _receiver;
    brpc::StreamId _sd;
};

int main(int argc, char* argv[]) {
    GFLAGS_NAMESPACE::ParseCommandLineFlags(&argc, &argv, true);
    brpc::Server server;
    StreamingEchoService echo_service_impl;
    if (server.AddService(&echo_service_impl, brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        LOG(ERROR) << "Fail to add service";
        return -1;
    }
    brpc::ServerOptions options;
    options.idle_timeout_sec = FLAGS_idle_timeout_s;
    if (server.Start(FLAGS_port, &options) != 0) {
        LOG(ERROR) << "Fail to start EchoServer";
        return -1;
    }
    server.RunUntilAskedToQuit();
    return 0;
}
