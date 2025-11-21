#pragma once
#include "Common.h"
#include <atomic>
#include <array>
#include <chrono>

//在CentralCache中的一个span中存放的内存块的大小是一样的
struct SpanTracker
{
	std::atomic<void*> spanAddr{ nullptr };  //span起始地址
	std::atomic<size_t> numPages{ 0 };   //占多少页
	std::atomic<size_t> blockCount{ 0 }; //总块数
	std::atomic<size_t> freeCount{ 0 };  //空闲块数量
};

class CentralCache
{
public:
	static CentralCache& getInstance();

	//向ThreadCache提供分配内存块接口
	void* fetchRange(size_t index,size_t batchNum);

	//向ThreadCache提供归还内存块接口
	void returnRange(void* start, size_t size, size_t index);

private:
	CentralCache();

	// 获取span信息
	SpanTracker* getSpanTracker(void* blockAddr);

	//从PageCache获取span
	void* fetchFromPageCache(size_t size);

	// 检查是否需要执行延迟归还
	bool shouldPerformDelayedReturn(size_t index, size_t currentCount, std::chrono::steady_clock::time_point currentTime);

	//执行延迟归还
	void performDelayedReturn(size_t index);

	void returnSpanToPageCache(SpanTracker* spanTracker, size_t index);

private:
	//中心缓存的自由链表
	std::array<std::atomic<void*>, FREE_LIST_NUM> m_centralFreeList;

	//中心缓存自由链表的自旋锁
	std::array<std::atomic_flag, FREE_LIST_NUM> m_centralFreeListLock;

	//使用数组存储span信息
	std::array<SpanTracker, MAX_SPAN_NUM> m_spanTrackerArray;

	//当前span数量
	std::atomic<size_t> m_spanNum{ 0 }; 

	// 延迟归还相关的成员变量
	//最大延迟计数
	static const size_t MAX_DELAY_COUNT = 48;

	//每个大小类的延迟计数
	std::array<std::atomic<size_t>, FREE_LIST_NUM> m_delayCountsArray;
	
	// 上次归还时间
	std::array<std::chrono::steady_clock::time_point, FREE_LIST_NUM> m_lastReturnTimeArray;
	
	//延迟间隔
	static const std::chrono::milliseconds MAX_DELAY_INTERVAL;
};

