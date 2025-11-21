#include "PageCache.h"

PageCache& PageCache::getInstance() {
	static PageCache instance;
	return instance;
}

void* PageCache::allocateSpan(size_t pageNum) {
	std::lock_guard<std::mutex> lock(m_mutex);

	// 查找合适的空闲span
	// lower_bound函数返回第一个大于等于numPages的元素的迭代器
	auto it = m_freeSpansMap.lower_bound(pageNum);
	if(it!=m_freeSpansMap.end()) {
		Span* span = it->second;

		if(span->next){
			//从空闲链表中移除该span
			m_freeSpansMap[it->first] = span->next;
		}
		else {
			//空闲链表中该页数的span已经没有了，从map中彻底移除该页数的span
			m_freeSpansMap.erase(it);
		}

		// 如果span大于需要的numPages则进行分割
		if (span->pageNum > pageNum) {
			Span* newSpan = new Span;
			newSpan->pageAddr = static_cast<char*>(span->pageAddr) + pageNum * PAGE_SIZE;
			newSpan->pageNum = span->pageNum - pageNum;
			newSpan->next = nullptr;

			// 将超出部分放回空闲Span*列表头部
			auto& spanList = m_freeSpansMap[newSpan->pageNum];
			newSpan->next = spanList;
			spanList = newSpan;

			//调整原span的页数，因为一部分内存页生成了一个新的span
			span->pageNum = pageNum;
		}
		// 记录span信息用于回收
		m_pageAddrToSpanMap[span->pageAddr] = span;
		return span->pageAddr;
	}
	//没有合适的span，向系统申请内存,得到一大块连续的内存
	void* memory = systemAlloc(pageNum);
	if(!memory) {
		return nullptr; //系统内存申请失败
	}

	//一个Span中的内存页是连续的
	//创建新的span
	Span* memNewSpan = new Span;
	memNewSpan->pageAddr = memory;
	memNewSpan->pageNum = pageNum;
	memNewSpan->next = nullptr;

	// 记录span信息用于回收
	m_pageAddrToSpanMap[memNewSpan->pageAddr] = memNewSpan;
	return memNewSpan->pageAddr;
}


void* PageCache::systemAlloc(size_t numPages) {
	size_t size = numPages * PAGE_SIZE;
	void* ptr = malloc(size);
	return ptr;
}


// 释放span
void PageCache::deallocateSpan(void* ptr, size_t pageNum) {
	std::lock_guard<std::mutex> lock(m_mutex);

	// 查找对应的span，没找到代表不是PageCache分配的内存，直接返回
	auto it = m_pageAddrToSpanMap.find(ptr);
	if (it == m_pageAddrToSpanMap.end())
		return;
	Span* span = it->second;

	//尝试合并相邻的span
	void* nextAddr = static_cast<char*>(ptr) + pageNum * PAGE_SIZE;
	auto nextIt = m_pageAddrToSpanMap.find(nextAddr);

	if (nextIt != m_pageAddrToSpanMap.end()) {
		Span* nextSpan = nextIt->second;

		// 1. 首先检查nextSpan是否在空闲链表中
		bool found = false;
		auto& nextList = m_freeSpansMap[nextSpan->pageNum];

		//检查是否是头节点
		if (nextList == nextSpan) {
			nextList = nextSpan->next;
			found = true;
		}else if (nextList)
		{
			Span* prev = nextList;
			while (prev->next) {
				if (prev->next == nextSpan) {
					// 将nextSpan从空闲链表中移除
					prev->next = nextSpan->next;
					found = true;
					break;
				}
				prev = prev->next;
			}
		}

		// 2. 只有在找到nextSpan的情况下才进行合并
		if (found) {
			//合并span
			span->pageNum += nextSpan->pageNum;
			m_pageAddrToSpanMap.erase(nextAddr);
			delete nextSpan;
		}
	}
	// 将合并后的span通过头插法插入空闲列表
	auto& list = m_freeSpansMap[span->pageNum];
	span->next = list;
	list = span;
}