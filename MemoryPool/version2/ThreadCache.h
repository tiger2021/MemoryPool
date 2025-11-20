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
	//bool shouldReturnToCentralCache(size_t size);



private:
	std::array<void*, FREE_LIST_NUM> m_freeList;   //每个线程的自由链表数组
	std::array<size_t, FREE_LIST_NUM> m_freeListSize;  //自由链表大小统计
};

