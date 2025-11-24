#include "MemoryPool.h"
#include <iostream>
#include <vector>
#include <thread>
#include <cassert>
#include <cstring>
#include <random>
#include <algorithm>
#include <atomic>

//std::cout << "" << std::endl;

// 基础分配测试
void testBasicAllocation() 
{
    std::cout << "Running basic allocation test..." << std::endl;
    
    // 测试小内存分配
    void* ptr1 = MemoryPool::allocate(8);
    assert(ptr1 != nullptr);
    MemoryPool::deallocate(ptr1, 8);

    // 测试中等大小内存分配
    void* ptr2 = MemoryPool::allocate(1024);
    assert(ptr2 != nullptr);
    MemoryPool::deallocate(ptr2, 1024);

    // 测试大内存分配（超过MAX_BYTES）
    void* ptr3 = MemoryPool::allocate(1024 * 1024);
    assert(ptr3 != nullptr);
    MemoryPool::deallocate(ptr3, 1024 * 1024);

    std::cout << "Basic allocation test passed!" << std::endl;
}

// 内存写入测试
void testMemoryWriting() 
{
    std::cout << "Running memory writing test..." << std::endl;

    // 分配并写入数据
    const size_t size = 128;
    char* ptr = static_cast<char*>(MemoryPool::allocate(size));
    assert(ptr != nullptr);

    // 写入数据
    for (size_t i = 0; i < size; ++i) 
    {
        ptr[i] = static_cast<char>(i % 256);
    }

    // 验证数据
    for (size_t i = 0; i < size; ++i) 
    {
        assert(ptr[i] == static_cast<char>(i % 256));
    }

    MemoryPool::deallocate(ptr, size);
    std::cout << "Memory writing test passed!" << std::endl;
}

// 多线程测试
void testMultiThreading() 
{
    std::cout << "Running multi-threading test..." << std::endl;

    const int NUM_THREADS = 5000;

    // 每个线程进行 1000 次 allocate/deallocate
    const int ALLOCS_PER_THREAD = 1000;
     std::atomic<bool> has_error{false};
    
    auto threadFunc = [&has_error]() 
    {
        try 
        {
            // 保存当前线程 allocate 得到的内存块与对应大小
            std::vector<std::pair<void*, size_t>> allocations;
            allocations.reserve(ALLOCS_PER_THREAD);
            
            // 主循环：每个线程执行 1000 次内存申请或释放
            for (int i = 0; i < ALLOCS_PER_THREAD && !has_error; ++i) 
            {
                size_t size = (rand() % 256 + 1) * 8;
                void* ptr = MemoryPool::allocate(size);
                
                if (!ptr) 
                {
                    std::cerr << "Allocation failed for size: " << size << std::endl;
                    has_error = true;
                    break;
                }
                
                allocations.push_back({ptr, size});
                
                // 随机释放某些已分配的块（50% 概率）
                if (rand() % 2 && !allocations.empty()) 
                {
                    size_t index = rand() % allocations.size();
                    MemoryPool::deallocate(allocations[index].first, 
                                         allocations[index].second);
                    allocations.erase(allocations.begin() + index);
                }
            }
            

            // 在线程结束前，把剩余未释放的块全部释放
            for (const auto& alloc : allocations) 
            {
                MemoryPool::deallocate(alloc.first, alloc.second);
            }
        }
        catch (const std::exception& e) 
        {
            std::cerr << "Thread exception: " << e.what() << std::endl;
            has_error = true;
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_THREADS; ++i) 
    {
        threads.emplace_back(threadFunc);
    }

    for (auto& thread : threads) 
    {
        thread.join();
    }

    std::cout << "Multi-threading test passed!" << std::endl;
}

// 边界测试
void testEdgeCases() 
{
    std::cout << "Running edge cases test..." << std::endl;
    
    // 测试0大小分配
    void* ptr1 = MemoryPool::allocate(0);
    assert(ptr1 != nullptr);
    MemoryPool::deallocate(ptr1, 0);
    
    // 测试最小对齐大小
    void* ptr2 = MemoryPool::allocate(1);
    assert(ptr2 != nullptr);
    assert((reinterpret_cast<uintptr_t>(ptr2) & (ALIGNMENT - 1)) == 0);
    MemoryPool::deallocate(ptr2, 1);
    
    // 测试最大大小边界
    void* ptr3 = MemoryPool::allocate(MAX_BYTES);
    assert(ptr3 != nullptr);
    MemoryPool::deallocate(ptr3, MAX_BYTES);
    
    // 测试超过最大大小
    void* ptr4 = MemoryPool::allocate(MAX_BYTES + 1);
    assert(ptr4 != nullptr);
    MemoryPool::deallocate(ptr4, MAX_BYTES + 1);
    
    std::cout << "Edge cases test passed!" << std::endl;
}

// // 压力测试：连续大量地分配内存、乱序释放，检测内存池是否稳定
void testStress() 
{
    std::cout << "Running stress test..." << std::endl;

    const int NUM_ITERATIONS = 10000;

    // 保存每次分配的地址 + 大小，用于之后的随机释放
    std::vector<std::pair<void*, size_t>> allocations;
    allocations.reserve(NUM_ITERATIONS);

    // 大量重复分配内存
    for (int i = 0; i < NUM_ITERATIONS; ++i) 
    {
        size_t size = (rand() % 1024 + 1) * 8;
        void* ptr = MemoryPool::allocate(size);
        assert(ptr != nullptr);
        allocations.push_back({ptr, size});
    }

    // 随机顺序释放
    std::random_device rd;
    std::mt19937 g(rd());

    //将序列中的所有元素随机打乱，生成一个均匀分布的随机排列
    std::shuffle(allocations.begin(), allocations.end(), g);
    for (const auto& alloc : allocations) 
    {
        MemoryPool::deallocate(alloc.first, alloc.second);
    }

    std::cout << "Stress test passed!" << std::endl;
}

int main() 
{
    try 
    {
        std::cout << "Starting memory pool tests..." << std::endl;

        testBasicAllocation();
        testMemoryWriting();
        testMultiThreading();
        testEdgeCases();
        testStress();

        std::cout << "All tests passed successfully!" << std::endl;
        return 0;
    }
    catch (const std::exception& e) 
    {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}