#include "CentralCache.h"
#include <thread>
#include "PageCache.h"
#include <iostream>
#include <set>

const std::chrono::milliseconds CentralCache::MAX_DELAY_INTERVAL{ 1000 };

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

void* CentralCache::fetchRange(size_t index,size_t batchNum) {
	assert(index>=0);

	//申请内存过大，应该直接向操作系统申请
	if(index>= FREE_LIST_NUM) {
		return nullptr;
	}

	//自旋等待获取锁
	while (m_centralFreeListLock[index].test_and_set()) {
		std::this_thread::yield(); //让出CPU时间片
	} 
	void* returnHead = nullptr;
	void* returnTail = nullptr;
	size_t returnBlockNum = 0;

	//尝试从中心缓存的自由链表中获取内存块
	try {
		void* head = m_centralFreeList[index].load();

		if(head){
			// 从链表头部最多取出 MAX_BATCH_BLOCK_NUM 个节点
			void* current = head;
			void* prev = nullptr;
			size_t blockCount = 0;

			while(current && blockCount < batchNum) {
				prev = current;
				//获取下一个节点
				current = *(reinterpret_cast<void**>(current)); 
				++blockCount;

				// 更新span的空闲计数
				//一次批量分给ThreadCache的内存块不一定在同一个Span中
				SpanTracker* spanTracker = getSpanTracker(prev);
				if (spanTracker) {
					//在该span中减少一个空闲内存块
					spanTracker->freeCount.fetch_sub(1);
				}
			}

			if(prev){
				//将链表断开
				*(reinterpret_cast<void**>(prev)) = nullptr;
			}
			m_centralFreeList[index].store(current);
			//设置返回的链表头和尾
			returnHead = head;
			returnTail = prev;
			returnBlockNum = blockCount;
		}

		//如果中心缓存的自由链表中没有足够的内存块，则从PageCache获取新的span
		if (!returnHead) {
			size_t size = (index + 1) * ALIGNMENT;
			returnHead = fetchFromPageCache(size);

			if(!returnHead) {
				//从PageCache获取span失败，释放锁并返回nullptr
				m_centralFreeListLock[index].clear();
				return nullptr;
			}

			// 计算实际分配的页数
			size_t numPages = (size <= SPAN_PAGES * PAGE_SIZE) ?
				SPAN_PAGES : (size + PAGE_SIZE - 1) / PAGE_SIZE;
			
			// 使用实际页数计算块数
			size_t totalBlockNum = (numPages * PAGE_SIZE) / size;

			//实际分配的内存块数量不一定等于batchNum
			size_t allocBlockNum = std::min(batchNum, totalBlockNum);

			//将从PageCache获取的内存块切分成小块
			char* start = static_cast<char*>(returnHead);

			// 构建返回给ThreadCache的内存块链表
			if (allocBlockNum > 1) {
				// 确保至少有两个块才构建链表
				// 构建链表
				for (size_t i = 1; i < allocBlockNum; ++i) {
					void* currentBlock = start + (i - 1) * size;
					void* nextBlock = start + i * size;
					*(reinterpret_cast<void**>(currentBlock)) = nextBlock;
				}
				void* lastReturned = start + (allocBlockNum - 1) * size;
				*(reinterpret_cast<void**>(lastReturned)) = nullptr;
			}

			// 构建保留在CentralCache的链表
			if(totalBlockNum> allocBlockNum) {
				void* centralHead = start + allocBlockNum * size;
				for (size_t i = allocBlockNum + 1; i < totalBlockNum; ++i) {
					void* currentBlock = start + (i - 1) * size;
					void* nextBlock = start + i * size;
					*(reinterpret_cast<void**>(currentBlock)) = nextBlock;
				}
				*(reinterpret_cast<void**>(start + (totalBlockNum - 1) * size)) = nullptr;
				//将多余的内存块插入到中心缓存的自由链表中
				m_centralFreeList[index].store(centralHead);
			}

			// 使用无锁方式记录span信息
			size_t trackerIndex = m_spanNum++;
			if (trackerIndex <m_spanTrackerArray.size())
			{
				m_spanTrackerArray[trackerIndex].spanAddr.store(start);
				m_spanTrackerArray[trackerIndex].numPages.store(numPages);
				m_spanTrackerArray[trackerIndex].blockCount.store(totalBlockNum); // 共分配了totalBlockNum个内存块
				m_spanTrackerArray[trackerIndex].freeCount.store(totalBlockNum-allocBlockNum); //已经分配出去了allocBlockNum个内存块
			}
		
		}
	}catch (...) {
		//释放锁
		m_centralFreeListLock[index].clear();
		throw; //重新抛出异常
	}

	//释放锁
	m_centralFreeListLock[index].clear();
	return returnHead;
}


SpanTracker* CentralCache::getSpanTracker(void* blockAddr) {
	//遍历spanTrackers_数组，找到内存块所属的span
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

void* CentralCache::fetchFromPageCache(size_t size) {
	//计算实际需要的页数
	size_t totalPages = (size + PAGE_SIZE - 1) / PAGE_SIZE; //向上取整

	if(totalPages < SPAN_PAGES) {
		// 小于等于32KB的请求，使用固定8页
		totalPages = SPAN_PAGES;
	}

	return PageCache::getInstance().allocateSpan(totalPages);
}


void CentralCache::returnRange(void* start, size_t size, size_t index) {
	if (!start || size<0 || index>FREE_LIST_NUM)
		return;
	//返回的单个内存块的大小
	size_t alignedBlockSize = (index + 1) * ALIGNMENT;

	//返回了多少个内存块
	size_t blockNum = size / alignedBlockSize;

	//自旋锁
	while (m_centralFreeListLock[index].test_and_set()) {
		std::this_thread::yield();
	}

	try {
		//将归还的链表链接到中心缓存的自由链表
		void* end = start;
		size_t count = 1;
		SpanTracker* spanTracker = nullptr;
		while (*(reinterpret_cast<void**>(end)) != nullptr && count < blockNum) {
			//更新归还内存块对应的span的空闲块数
			spanTracker = getSpanTracker(end);

			if (spanTracker) {
				++(spanTracker->freeCount);
			}
			else {
				std::cout << "spanTracker指针为空!" <<std::endl;
			}

			end = *(reinterpret_cast<void**>(end));
			++count;
		}

		//更新最后一个归还内存块对应的span的空闲块数
		spanTracker = getSpanTracker(end);
		if (spanTracker) {
			++(spanTracker->freeCount);
		}else {
			std::cout << "spanTracker指针为空!" << std::endl;
		}

		//头插法归还
		*(reinterpret_cast<void**>(end)) = m_centralFreeList[index].load();
		m_centralFreeList[index].store(start);

		// 2. 更新延迟计数(这儿的加1还有疑惑)
		size_t currentCount = m_delayCountsArray[index].fetch_add(1) +1;
		auto currentTime = std::chrono::steady_clock::now();

		// 3. 检查是否需要执行延迟归还
		if (shouldPerformDelayedReturn(index, currentCount, currentTime))
		{
			performDelayedReturn(index);
		}
	}
	catch (...) {
		m_centralFreeListLock[index].clear();
		throw;
	}
	m_centralFreeListLock[index].clear();

}

bool CentralCache::shouldPerformDelayedReturn(size_t index, size_t currentCount, std::chrono::steady_clock::time_point currentTime) {
	//
	if (currentCount >= MAX_DELAY_COUNT) {
		return true;
	}

	auto lastReturnTime = m_lastReturnTimeArray[index];
	return (currentTime - lastReturnTime) >= MAX_DELAY_INTERVAL;
}

void CentralCache::performDelayedReturn(size_t index) {
	// 重置延迟计数
	m_delayCountsArray[index].store(0);

	// 更新最后归还时间
	m_lastReturnTimeArray[index] = std::chrono::steady_clock::now();

	// 统计与归还块相关联的span
	std::set<SpanTracker*> spanFreeSet;
	void* currentBlock = m_centralFreeList[index].load();

	while (currentBlock)
	{
		SpanTracker* spanTracker = getSpanTracker(currentBlock);
		if (spanTracker)
		{
			//将spanTracker存放到set中
			spanFreeSet.insert(spanTracker);
		}
		currentBlock = *reinterpret_cast<void**>(currentBlock);
	}

	// 更新每个span的空闲计数并检查是否可以归还
	for (auto tracker : spanFreeSet)
	{
		if (tracker) {
			if ((tracker->blockCount.load()) <= (tracker->freeCount.load())) {
				returnSpanToPageCache(tracker,index);
			}
		}
	}
}

void CentralCache::returnSpanToPageCache(SpanTracker* spanTracker,size_t index) {
	assert(spanTracker);

	void* spanAddr = spanTracker->spanAddr.load();
	size_t pageNum = spanTracker->numPages.load();

	//从自由链表中移除这些块
	void* head = m_centralFreeList[index].load();
	void* newHead = nullptr;
	void* prev = nullptr;
	void* current = head;

	while (current) {
		void* next = *(reinterpret_cast<void**>(current));
		if (current >= spanAddr && current < (static_cast<char*>(spanAddr) + pageNum * PAGE_SIZE)) {
			if (prev) {
				*(reinterpret_cast<void**>(prev)) = next;
			}else {
				newHead = next;
			}
		}
		else {
			prev = current;
		}
		current = next;
	}

	//自由链表的第一个内存块被回收了
	if (newHead) {
		m_centralFreeList[index].store(newHead);
	}
	PageCache::getInstance().deallocateSpan(spanAddr, pageNum);
}