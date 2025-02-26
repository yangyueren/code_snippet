#include <iostream>
#include <chrono>
#include <vector>
#include <string>
#include <iomanip>

using namespace std;
using namespace std::chrono;

// 防止优化掉计算结果
// 编译器屏障，告诉编译器“这里的 value 被使用了”，从而防止那些看起来没有副作用的计算被优化掉
// 并不会真正把数据写入内存，也不会显著影响测量延迟，而是确保你测量的那段代码不会被编译器剔除或重排序。
template <typename T>
inline void doNotOptimizeAway(T const& value) {
    asm volatile("" : : "r,m"(value) : "memory");
}

// 定义防止内联的宏
#if defined(_MSC_VER)
#define NOINLINE __declspec(noinline)
#else
#define NOINLINE __attribute__((noinline))
#endif

volatile int global_dummy = 0; // 用于防止编译器优化

// 测量单次加法操作的延迟（纳秒级）
void measureAdditionLatency() {
    volatile int a = 1, b = 2, c = 0;
    const int iterations = 100000000; // 1e8 次迭代
    auto start = high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        c = a + b;
        doNotOptimizeAway(c);
    }
    auto end = high_resolution_clock::now();
    // 将结果写入全局变量防止完全被优化掉
    global_dummy = c;
    auto duration = duration_cast<nanoseconds>(end - start).count();
    cout << "加法操作延迟: " << fixed << setprecision(3)
         << (double)duration / iterations << " ns/次" << endl;
}

// 普通函数（非虚函数）调用测试
NOINLINE int normalFunc(int x) {
    int result = x + 1;
    doNotOptimizeAway(result);
    return result;
}

void measureNormalFuncLatency() {
    const int iterations = 100000000;
    volatile int sum = 0;
    auto start = high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        sum = normalFunc(sum);
        doNotOptimizeAway(sum);
    }
    auto end = high_resolution_clock::now();
    global_dummy = sum;
    auto duration = duration_cast<nanoseconds>(end - start).count();
    cout << "普通函数调用延迟: " << fixed << setprecision(3)
         << (double)duration / iterations << " ns/次" << endl;
}

// 虚函数调用测试：定义基类与派生类，利用虚函数调用产生间接寻址开销
class Base {
public:
    virtual int virtualFunc(int x) { 
        int result = x + 1;
        doNotOptimizeAway(result);
        return result; 
    }
};

class Derived : public Base {
public:
    virtual int virtualFunc(int x) override { 
        int result = x + 1;
        doNotOptimizeAway(result);
        return result; 
    }
};

void measureVirtualFuncLatency() {
    const int iterations = 100000000;
    volatile int sum = 0;
    Derived d;
    Base* ptr = &d;
    auto start = high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        sum = ptr->virtualFunc(sum);
        doNotOptimizeAway(sum);
    }
    auto end = high_resolution_clock::now();
    global_dummy = sum;
    auto duration = duration_cast<nanoseconds>(end - start).count();
    cout << "虚函数调用延迟: " << fixed << setprecision(3)
         << (double)duration / iterations << " ns/次" << endl;
}

// 寄存器操作延迟测试（其实与简单加法类似，寄存器操作速度极快）
void measureRegisterLatency() {
    const size_t iterations = 1000000000;
    register int a = 1, b = 2, c = 0;
    auto start = high_resolution_clock::now();
    for (size_t i = 0; i < iterations; ++i) {
        c = a + b;
        doNotOptimizeAway(c);
        a = c;
    }
    auto end = high_resolution_clock::now();
    global_dummy = c;
    auto duration = duration_cast<nanoseconds>(end - start).count();
    cout << "寄存器操作延迟: " << fixed << setprecision(3)
         << (double)duration / iterations << " ns/次" << endl;
}

// 利用指针跳跃法测量不同内存层级的访问延迟
// 参数说明：
//   numElements - 数组中元素个数
//   stride      - 跳跃步长（单位：元素数）
//   levelName   - 内存层级名称（如 "L1 Cache"）
//   iterations  - 循环次数
void measureCacheLatency(size_t numElements, size_t stride, const string& levelName, int iterations) {
    vector<size_t> array(numElements);
    // 构建循环链表，避免预取优化
    for (size_t i = 0; i < numElements; ++i) {
        array[i] = (i + stride) % numElements;
    }
    size_t index = 0;
    auto start = high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        index = array[index];
        doNotOptimizeAway(index);
    }
    auto end = high_resolution_clock::now();
    auto duration = duration_cast<nanoseconds>(end - start).count();
    cout << levelName << " 访问延迟: " << fixed << setprecision(3)
         << (double)duration / iterations << " ns/次" << endl;
}

// 通过改变数组大小来估算缓存大小：当数组大小超过某一级缓存时，访问延迟会明显增大
void estimateCacheSize() {
    cout << "\n估算缓存大小（数组大小 vs 访问延迟）:" << endl;
    // 数组大小从 1KB 到 128MB，每次翻倍
    for (size_t size = 1024; size <= 128 * 1024 * 1024; size *= 2) {
        size_t numElements = size / sizeof(size_t);
        vector<size_t> array(numElements);
        size_t stride = 16; // 大致 16 * sizeof(size_t) 字节
        // 构建循环链表
        for (size_t i = 0; i < numElements; ++i) {
            array[i] = (i + stride) % numElements;
        }
        const int iterations = 100000000;
        size_t index = 0;
        auto start = high_resolution_clock::now();
        for (int i = 0; i < iterations; ++i) {
            index = array[index];
            doNotOptimizeAway(index);
        }
        auto end = high_resolution_clock::now();
        auto duration = duration_cast<nanoseconds>(end - start).count();
        double avgLatency = (double)duration / iterations;
        cout << "数组大小 " << size / 1024 << " KB: " << fixed << setprecision(3)
             << avgLatency << " ns/次" << endl;
    }
}

int main() {
    cout << "----- 性能测量 -----" << endl;
    measureAdditionLatency();
    measureNormalFuncLatency();
    measureVirtualFuncLatency();
    measureRegisterLatency();

    cout << "\n----- 缓存访问延迟测量 -----" << endl;
    // 这里假设 size_t 大小为 8 字节，根据实际情况也可以选择 int（4 字节）
    // 设定各层缓存对应的数组大小（单位：元素数）：
    // L1: 256KB, L2: 8MB, L3: 35MB, 主存: 1GB
    int pointerChaseIterations = 10000000;
    size_t l1_size = 256 * 1024 / sizeof(size_t);     // 256KB
    size_t l2_size = 8 * 1024 * 1024 / sizeof(size_t);  // 8MB
    size_t l3_size = 35 * 1024 * 1024 / sizeof(size_t); // 35MB
    size_t mem_size = 1024 * 1024 * 1024 / sizeof(size_t); // 1GB
    size_t stride = 16; // 跳跃步长

    measureCacheLatency(l1_size, stride, "L1 Cache", pointerChaseIterations);
    measureCacheLatency(l2_size, stride, "L2 Cache", pointerChaseIterations);
    measureCacheLatency(l3_size, stride, "L3 Cache", pointerChaseIterations);
    measureCacheLatency(mem_size, stride, "内存", pointerChaseIterations);

    estimateCacheSize();

    return 0;
}



/*
----- 性能测量 -----
加法操作延迟: 0.631 ns/次
普通函数调用延迟: 1.337 ns/次
虚函数调用延迟: 1.646 ns/次
寄存器操作延迟: 0.315 ns/次

----- 缓存访问延迟测量 -----
L1 Cache 访问延迟: 3.874 ns/次
L2 Cache 访问延迟: 7.290 ns/次
L3 Cache 访问延迟: 15.636 ns/次
内存 访问延迟: 24.584 ns/次

估算缓存大小（数组大小 vs 访问延迟）:
数组大小 1 KB: 1.577 ns/次
数组大小 2 KB: 1.577 ns/次
数组大小 4 KB: 1.576 ns/次
数组大小 8 KB: 1.576 ns/次
数组大小 16 KB: 1.577 ns/次
数组大小 32 KB: 1.578 ns/次
数组大小 64 KB: 3.953 ns/次
数组大小 128 KB: 3.953 ns/次
数组大小 256 KB: 3.890 ns/次
数组大小 512 KB: 3.988 ns/次
数组大小 1024 KB: 5.960 ns/次
数组大小 2048 KB: 7.661 ns/次
数组大小 4096 KB: 7.670 ns/次
数组大小 8192 KB: 8.303 ns/次
数组大小 16384 KB: 7.446 ns/次
数组大小 32768 KB: 12.339 ns/次
数组大小 65536 KB: 21.063 ns/次
数组大小 131072 KB: 23.958 ns/次
*/
