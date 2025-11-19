#include "CentralCache.h"
#include <thread>

CentralCache& CentralCache::getInstance() {
	static CentralCache instance;
	return instance;
}

CentralCache::CentralCache() {
	//初始化中心缓存的自由链表和自旋锁
	for(auto & freeList : m_centralFreeList) {
		freeList.store(nullptr);
	}

	for(auto & lock : m_centralFreeListLock) {
		//“释放”自旋锁，原子标志位重置为 false
		lock.clear();
	}
	m_spanNum.store(0);
}

void* CentralCache::fetchRange(size_t index) {
	assert(index << FREE_LIST_NUM > =0);

	//申请内存过大，应该直接向操作系统申请
	if(index>= FREE_LIST_NUM) {
		return nullptr;
	}

	//自旋等待获取锁
	while (m_centralFreeListLock[index].test_and_set()) {
		std::this_thread::yield(); //让出CPU时间片
	} 
	void* res = nullptr;

	//尝试从中心缓存的自由链表中获取内存块
	try {
		res = m_centralFreeList[index].load();

		if(!res) {
			//中心缓存自由链表为空，尝试从PageCache中获取内存块
			return nullptr; //暂时不实现
		}
		else {
			//从中心缓存自由链表中取出一个内存块
			void* nextBlock = *(reinterpret_cast<void**>(res));
			m_centralFreeList[index].store(nextBlock);

			//将res与中心缓存自由链表断开连接
			*(reinterpret_cast<void**>(res)) = nullptr;

			//更新span的空闲计数
			SpanTracker* spanTracker = getSpanTracker(res);
			if (spanTracker) {
				spanTracker->freeCount.fetch_sub(1);
			}

		}
	}catch (...) {
		//释放锁
		m_centralFreeListLock[index].clear();
		throw; //重新抛出异常
	}

	//释放锁
	m_centralFreeListLock[index].clear();
	return res;
}


SpanTracker* CentralCache::getSpanTracker(void* blockAddr) {
	
	for(size_t i=0;i<m_spanNum.load(); ++i) {
		void* spanAddr = m_spanTrackerArray[i].spanAddr.load();
		size_t numPages = m_spanTrackerArray[i].numPages.load();
		if(blockAddr >= spanAddr && blockAddr < static_cast<char*>(spanAddr) + numPages * PAGE_SIZE) {
			return &m_spanTrackerArray[i];
		}
	}
	//未找到对应的SpanTracker
	return nullptr;
}
