#include "CentralCache.h"
#include <thread>
#include "PageCache.h"

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
	assert(index> =0);

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

			//将从PageCache获取的内存块切分成小块
			char* start = static_cast<char*>(returnHead);
			//实际分配的内存块数量不一定等于batchNum
			size_t totalBlockNum = (SPAN_PAGES*PAGE_SIZE) / size; 
			size_t allocBlockNum = std::min(batchNum, totalBlockNum);

			// 构建返回给ThreadCache的内存块链表
			if (allocBlockNum > 1) {
				// 确保至少有两个块才构建链表
				// 构建链表
				for (size_t i = 1; i < allocBlockNum; ++i) {
					void* currentBlock = start + (i - 1) * size;
					void* nextBlock = start + i * size;
					*(reinterpret_cast<void**>(currentBlock)) = nextBlock;
				}
				*(reinterpret_cast<void**>(start + (allocBlockNum - 1) * size)) = nullptr;
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


void CentralCache::returnRange(void* start, size_t size, size_t bytes) {
	
}
