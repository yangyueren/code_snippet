syntax = "proto3";

package bench.api.benchrpc.protos;

service Benchrpc {
  /// Client API
  rpc TestStream(stream PublishRequest) returns (stream PublishReply);
}

message PublishRequest {
  int64 id = 1;
  string message = 2;
}

message PublishReply {
  int64 id = 1;
  string message = 2;
}






use std::pin::Pin;
use std::time::{Duration, Instant};
use std::hint::spin_loop;
use async_stream::try_stream;
use futures::Stream;
use futures::StreamExt;
use tokio::sync::mpsc;
use tokio_stream::wrappers::ReceiverStream;
use tonic::{transport::Server, Request, Response, Status};

use bench_pb::bench_benchrpc::{
    benchrpc_server::{Benchrpc, BenchrpcServer},
    benchrpc_client::BenchrpcClient,
    PublishRequest, PublishReply,
};

/// 服务端实现：收到请求后立即回复带有相同 id 的 pong 消息
#[derive(Default)]
pub struct MyBenchrpc {}

#[tonic::async_trait]
impl Benchrpc for MyBenchrpc {
    type TestStreamStream =
        Pin<Box<dyn Stream<Item = Result<PublishReply, Status>> + Send + 'static>>;

    async fn test_stream(
        &self,
        request: Request<tonic::Streaming<PublishRequest>>,
    ) -> Result<Response<Self::TestStreamStream>, Status> {
        println!(
            "服务端: 收到来自 {:?} 的流式请求",
            request.remote_addr()
        );
        let mut stream = request.into_inner();

        let output = try_stream! {
            while let Some(req) = stream.next().await {
                let req = req?;
                let reply = PublishReply {
                    id: req.id,
                    message: format!("pong: {}", req.message),
                };
                yield reply;
            }
        };

        Ok(Response::new(Box::pin(output) as Self::TestStreamStream))
    }
}

fn busy_sleep(duration: Duration) {
    let start = Instant::now();
    while start.elapsed() < duration {
        spin_loop(); // CPU busy loop
    }
}

/// 直方图结构，用于统计延迟（单位：μs）
struct LatencyHistogram {
    boundaries: Vec<u64>, // bucket 上界（单位：μs）
    counts: Vec<u64>,     // 每个 bucket 内计数
    total: u64,
    max: u64,
}

impl LatencyHistogram {
    /// 构造直方图，bucket 范围从 1μs 到 10s，采用等比划分（因子 1.2）
    fn new() -> Self {
        let mut boundaries = Vec::new();
        let mut cur = 1u64; // 1μs
        let max_bound = 10_000_000_000u64; // 10s = 10_000_000_000μs
        while cur < max_bound {
            boundaries.push(cur);
            let next = (cur as f64 * 1.2).ceil() as u64;
            if next <= cur {
                break;
            }
            cur = next;
        }
        boundaries.push(max_bound);
        let counts = vec![0u64; boundaries.len()];
        Self {
            boundaries,
            counts,
            total: 0,
            max: 0,
        }
    }

    /// 根据延迟记录到相应 bucket 中，duration 单位转换为 μs
    fn record(&mut self, d: Duration) {
        let micros = d.as_micros() as u64;
        let idx = self
            .boundaries
            .iter()
            .position(|&b| micros <= b)
            .unwrap_or(self.counts.len() - 1);
        self.counts[idx] += 1;
        self.total += 1;
        if micros > self.max {
            self.max = micros;
        }
    }

    /// 根据累计分布计算百分位，q 取值 0.0 ~ 1.0
    /// 返回估算延迟（μs），取 bucket 上界作为估算值
    fn quantile(&self, q: f64) -> u64 {
        if self.total == 0 {
            return 0;
        }
        let target = (self.total as f64 * q).ceil() as u64;
        let mut sum = 0;
        for (i, &count) in self.counts.iter().enumerate() {
            sum += count;
            if sum >= target {
                return self.boundaries[i];
            }
        }
        *self.boundaries.last().unwrap_or(&0)
    }
}

/// 客户端压测程序
///
/// 实现思路：
/// 1. 使用 tokio::sync::mpsc 构建无锁等待队列，容量设置为 1_000_000。当发送数据前，将 (PublishRequest, Instant)
///    写入等待队列（队列满时 send().await 会等待）。
/// 2. 另通过 mpsc channel 将请求数据发送到服务端。
/// 3. 接收任务收到 reply 后，从等待队列中依次获取发送记录，将响应延迟记录到直方图中。
/// 4. 记录整体运行时间，计算 QPS（每秒完成的操作数）。
async fn run_client(addr: String, total_requests: usize) -> Result<(), Box<dyn std::error::Error>> {
    let mut client = BenchrpcClient::connect(addr).await?;

    // 通过 mpsc channel 将请求数据发送给服务端
    let (tx, rx) = mpsc::channel(128);
    let outbound = ReceiverStream::new(rx);
    let mut response_stream = client.test_stream(outbound).await?.into_inner();

    // 构建无锁等待队列，容量 1_000_000，用于记录 (PublishRequest, Instant)
    let (wait_tx, mut wait_rx) = mpsc::channel::<(PublishRequest, Instant)>(1_000_000);

    // 记录客户端启动时间
    let start_time = Instant::now();

    // 发送任务：先将 (request, Instant) 放入等待队列，再发送到服务端
    let send_task = tokio::spawn(async move {
        for i in 0..total_requests as i64 {
            // busy_sleep(Duration::from_nanos(1000));
            // println!("发送请求: {}", i);
            let req = PublishRequest {
                id: i,
                message: "x".repeat(2048).to_string(),
            };
            if let Err(e) = wait_tx.send((req.clone(), Instant::now())).await {
                eprintln!("等待队列发送失败: {}", e);
                break;
            }
            if let Err(e) = tx.send(req).await {
                eprintln!("发送请求失败: {}", e);
                break;
            }
            // 可根据需要调整发送速率
            // tokio::time::sleep(Duration::from_micros(10)).await; // tokio 底层需要 1ms 进行调度
        }
    });

    // 接收任务：每收到 reply 时，从等待队列中获取对应记录，将响应延迟记录到直方图中
    let stats_task = tokio::spawn(async move {
        let mut hist = LatencyHistogram::new();
        let mut received = 0;
        while let Some(reply) = response_stream.next().await {
            match reply {
                Ok(reply) => {
                    if let Some((req, sent_time)) = wait_rx.recv().await {
                        if req.id != reply.id {
                            eprintln!(
                                "队列中请求 id {} 与收到 reply id {} 不匹配！",
                                req.id, reply.id
                            );
                        }
                        let elapsed = sent_time.elapsed();
                        hist.record(elapsed);
                        received += 1;
                    } else {
                        eprintln!("等待队列为空，但收到 reply id: {}", reply.id);
                    }
                }
                Err(e) => {
                    eprintln!("接收 reply 失败: {}", e);
                    break;
                }
            }
        }
        (received, hist)
    });

    send_task.await?;
    let (received, hist) = stats_task.await?;
    let elapsed_total = start_time.elapsed();
    let p50 = hist.quantile(0.50);
    let p99 = hist.quantile(0.99);
    let max = hist.max;
    let qps = if elapsed_total.as_secs_f64() > 0.0 {
        received as f64 / elapsed_total.as_secs_f64()
    } else {
        0.0
    };

    println!("总请求: {}, 成功响应: {}", total_requests, received);
    println!(
        "Latency stats: p50: {}μs, p99: {}μs, max: {}μs",
        p50, p99, max
    );
    println!("总耗时: {:.3}s, QPS: {:.2}", elapsed_total.as_secs_f64(), qps);

    Ok(())
}

/// 入口，根据命令行参数启动服务端或客户端
#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let args: Vec<String> = std::env::args().collect();
    if args.len() < 2 {
        eprintln!("请指定模式: server 或 client");
        std::process::exit(1);
    }

    if args[1] == "server" {
        let addr = "[::1]:50051".parse()?;
        let benchrpc_service = MyBenchrpc::default();
        println!("服务端启动，监听地址 {}", addr);
        Server::builder()
            .add_service(BenchrpcServer::new(benchrpc_service))
            .serve(addr)
            .await?;
    } else if args[1] == "client" {
        let total_requests = if args.len() >= 3 {
            args[2].parse::<usize>().unwrap_or(1000)
        } else {
            1000
        };
        let addr = "http://[::1]:50051".to_string();
        println!("客户端启动，连接到 {}，发送 {} 个请求", addr, total_requests);
        run_client(addr, total_requests).await?;
    } else {
        eprintln!("未知模式: {}", args[1]);
    }

    Ok(())
}

/*

cpp
(py38) (base) ➜  yyr2 ✗ ../bazel-bin/yyr2/cpp/benchrpc client 1000000 
总请求: 1000000, 成功响应: 1000000
Latency stats: p50: 84μs, p99: 122μs, max: 3265μs
总耗时: 23.1861s, QPS: 43129.2

rust
(py38) (base) ➜  yyr2 ✗ ./target/release-with-debug/benchrpc client 10000000
客户端启动，连接到 http://[::1]:50051，发送 10000000 个请求
总请求: 10000000, 成功响应: 10000000
Latency stats: p50: 2297μs, p99: 3309μs, max: 7882μs
总耗时: 34.847s, QPS: 286969.87

*/
