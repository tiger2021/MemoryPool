#pragma once
#include "Common.h"
#include <array>

class ThreadCache
{
public:
	static ThreadCache* getInstance();
	void* allocate(size_t size);
	void deallocate(void* ptr, size_t size);

private:
	ThreadCache();
	void* fetchFromCentralCache(size_t index);

	//批量获取内存块的数量
	size_t getBatchBlockNum(size_t size);
	
	// 判断是否需要归还内存给中心缓存
	bool shouldReturnToCentralCache(size_t index);

	// 归还内存到中心缓存
	void returnToCentralCache(void* start, size_t size);



private:
	//每个线程的自由链表数组
	std::array<void*, FREE_LIST_NUM> m_freeList;

	//自由链表中空闲内存块统计，当超过阈值时归还给中心缓存
	std::array<size_t, FREE_LIST_NUM> m_freeListBlockNumArray;  
};

