#pragma once
#include "Common.h"
#include <atomic>
#include <array>

struct SpanTracker
{
	std::atomic<void*> spanAddr{ nullptr }; //span起始地址
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
	void returnRange(void* start, size_t size, size_t bytes);
private:
	CentralCache();

	// 获取span信息
	SpanTracker* getSpanTracker(void* blockAddr);

	//从PageCache获取span
	void* fetchFromPageCache(size_t size);

private:
	//中心缓存的自由链表
	std::array<std::atomic<void*>, FREE_LIST_NUM> m_centralFreeList;

	//中心缓存自由链表的自旋锁
	std::array<std::atomic_flag, FREE_LIST_NUM> m_centralFreeListLock;

	//使用数组存储span信息
	std::array<SpanTracker, MAX_SPAN_NUM> m_spanTrackerArray;

	//当前span数量
	std::atomic<size_t> m_spanNum{ 0 }; 
};

