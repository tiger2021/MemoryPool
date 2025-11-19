#include "ThreadCache.h"

ThreadCache* ThreadCache::getInstance() {
	//表示每一个线程都拥有instance的一个实例拷贝
	static thread_local ThreadCache instance;

	return &instance;
}

ThreadCache::ThreadCache() {
	m_freeList.fill(nullptr);
	m_freeListSize.fill(0);
}

void* ThreadCache::allocate(size_t size) {
	assert(size > =0);

	size = size == 0 ? ALIGNMENT : size;

	if(size>MAX_BYTES) {
		//大于256KB，直接向操作系统申请
		return malloc(size);
	}
	size_t index = SizeClass::getFreeListIndex(size);
	void* ret = m_freeList[index];
	if (ret) {
		//线程本地自由链表命中
		//自由链表的头结点“出栈”：
		// 把当前块 ret 指向的下一个块读取出来，赋回为新的链表头 m_freeList[index]
		m_freeList[index] = *(reinterpret_cast<void**>(ret));

		//更新自由链表大小统计
		--m_freeListSize[index];
		return ret;
	} else {
		//线程本地自由链表未命中，从CentralCache获取
		return fetchFromCentralCache(size);
	}
}

void* ThreadCache::fetchFromCentralCache(size_t index) {
	return nullptr; //暂时不实现
}

void ThreadCache::deallocate(void* ptr, size_t size) {
	assert(ptr != nullptr);
	
	if (size > MAX_BYTES) {
		//大于256KB，直接向操作系统释放
		free(ptr);
		return;
	}
	size_t index = SizeClass::getFreeListIndex(size);
	//将内存块插入到对应的自由链表头部，“入栈”
	//void** 表示指向指针的指针
	*(reinterpret_cast<void**>(ptr)) = m_freeList[index];
	m_freeList[index] = ptr;
	//更新自由链表大小统计
	++m_freeListSize[index];
}
