
use tokio::sync::mpsc;
use tokio::sync::oneshot;
use tokio::time::{sleep, Duration};
use tracing::{info, info_span, Instrument, Span};


async fn produce_jobs(tx: mpsc::Sender<(String, Span)>, producer_name: String) {
    let span = info_span!("produce_jobs", producer = %producer_name);
    for i in 1..=2 {
        let job_id = format!("job-{}-{}", producer_name, i);
        let job_span = info_span!(parent: &span, "job_created", job_id = %job_id);
        
        info!(parent: &job_span, "Sending job {}", job_id);

        tx.send((job_id.clone(), job_span)).await.expect("Failed to send");
        sleep(Duration::from_millis(100)).await;
    }
}

// ✅ Receiver 端恢复 `Span`
async fn receive_jobs(mut rx: mpsc::Receiver<(String, Span)>) {
    while let Some((job_id, parent_span)) = rx.recv().await {
        let child_span = info_span!(parent: &parent_span, "process_job", extra_info = "some_value");

        async move {
            info!("Processing job {}", job_id);
            sleep(Duration::from_millis(50)).await;
            info!("Job {} done", job_id);
        }
        .instrument(child_span) // ✅ 这里恢复 `Span`
        .await;
    }
}

#[tokio::main]
async fn main() -> anyhow::Result<()> {
    hugestream2_utils::logger::init_cli_terminal_logger();
    let (tx, rx) = mpsc::channel(100);

    // 启动多个 producer
    for i in 1..=3 {
        let tx = tx.clone();
        tokio::task::spawn(produce_jobs(tx, format!("producer-{}", i)));
    }

    // 启动 receiver
    receive_jobs(rx).await;
    Ok(())
}

/*
2025-02-27T02:45:38.791807Z  INFO produce_jobs:job_created: validator: components/validator/main.rs:31: Sending job job-producer-2-1 producer=producer-2 job_id=job-producer-2-1
2025-02-27T02:45:38.791805Z  INFO produce_jobs:job_created: validator: components/validator/main.rs:31: Sending job job-producer-3-1 producer=producer-3 job_id=job-producer-3-1
2025-02-27T02:45:38.791804Z  INFO produce_jobs:job_created: validator: components/validator/main.rs:31: Sending job job-producer-1-1 producer=producer-1 job_id=job-producer-1-1
2025-02-27T02:45:38.791951Z  INFO produce_jobs:job_created:process_job: validator: components/validator/main.rs:44: Processing job job-producer-2-1 producer=producer-2 job_id=job-producer-2-1 extra_info="some_value"
2025-02-27T02:45:38.843141Z  INFO produce_jobs:job_created:process_job: validator: components/validator/main.rs:46: Job job-producer-2-1 done producer=producer-2 job_id=job-producer-2-1 extra_info="some_value"
2025-02-27T02:45:38.843226Z  INFO produce_jobs:job_created:process_job: validator: components/validator/main.rs:44: Processing job job-producer-3-1 producer=producer-3 job_id=job-producer-3-1 extra_info="some_value"
2025-02-27T02:45:38.893293Z  INFO produce_jobs:job_created: validator: components/validator/main.rs:31: Sending job job-producer-3-2 producer=producer-3 job_id=job-producer-3-2
2025-02-27T02:45:38.893317Z  INFO produce_jobs:job_created: validator: components/validator/main.rs:31: Sending job job-producer-1-2 producer=producer-1 job_id=job-producer-1-2
2025-02-27T02:45:38.893307Z  INFO produce_jobs:job_created: validator: components/validator/main.rs:31: Sending job job-producer-2-2 producer=producer-2 job_id=job-producer-2-2
2025-02-27T02:45:38.894409Z  INFO produce_jobs:job_created:process_job: validator: components/validator/main.rs:46: Job job-producer-3-1 done producer=producer-3 job_id=job-producer-3-1 extra_info="some_value"
2025-02-27T02:45:38.894495Z  INFO produce_jobs:job_created:process_job: validator: components/validator/main.rs:44: Processing job job-producer-1-1 producer=producer-1 job_id=job-producer-1-1 extra_info="some_value"
2025-02-27T02:45:38.945759Z  INFO produce_jobs:job_created:process_job: validator: components/validator/main.rs:46: Job job-producer-1-1 done producer=producer-1 job_id=job-producer-1-1 extra_info="some_value"
2025-02-27T02:45:38.945836Z  INFO produce_jobs:job_created:process_job: validator: components/validator/main.rs:44: Processing job job-producer-3-2 producer=producer-3 job_id=job-producer-3-2 extra_info="some_value"
2025-02-27T02:45:38.997059Z  INFO produce_jobs:job_created:process_job: validator: components/validator/main.rs:46: Job job-producer-3-2 done producer=producer-3 job_id=job-producer-3-2 extra_info="some_value"
2025-02-27T02:45:38.997135Z  INFO produce_jobs:job_created:process_job: validator: components/validator/main.rs:44: Processing job job-producer-2-2 producer=producer-2 job_id=job-producer-2-2 extra_info="some_value"
2025-02-27T02:45:39.048384Z  INFO produce_jobs:job_created:process_job: validator: components/validator/main.rs:46: Job job-producer-2-2 done producer=producer-2 job_id=job-producer-2-2 extra_info="some_value"
2025-02-27T02:45:39.048463Z  INFO produce_jobs:job_created:process_job: validator: components/validator/main.rs:44: Processing job job-producer-1-2 producer=producer-1 job_id=job-producer-1-2 extra_info="some_value"
2025-02-27T02:45:39.098702Z  INFO produce_jobs:job_created:process_job: validator: components/validator/main.rs:46: Job job-producer-1-2 done producer=producer-1 job_id=job-producer-1-2 extra_info="some_value"
*/











/*
async fn send_job(tx: oneshot::Sender<(String, Span)>, span: Span) {
    let _entered = span.enter(); // ❌ 进入 `Span`
    info!("Sending job...");

    some_async_task().await; // ❌ `_entered` 不能跨 `await`

    let _ = tx.send(("Job Data".to_string(), span));
}

❌ 这个写法有问题
span.enter() 返回一个 同步作用域，不能跨 await。
some_async_task().await; 会在 await 之后丢失 Span 作用域。

✅ 解决方案
✔ 方法 1：用 .instrument(span) 让 Span 贯穿整个 async 任务

*/




