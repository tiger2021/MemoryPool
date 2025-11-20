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

			//调整原span的页数, 因为已经分出去了一部分
			span->pageNum = pageNum;
		}
		// 记录span信息用于回收
		m_pageToSpanMap[span->pageAddr] = span;
		return span->pageAddr;
	}
	//没有合适的span，向系统申请内存
	void* memory = systemAlloc(pageNum);
	if(!memory) {
		return nullptr; //系统内存申请失败
	}

	//创建新的span
	Span* memNewSpan = new Span;
	memNewSpan->pageAddr = memory;
	memNewSpan->pageNum = pageNum;
	memNewSpan->next = nullptr;

	// 记录span信息用于回收
	m_pageToSpanMap[memNewSpan->pageAddr] = memNewSpan;
	return memNewSpan->pageAddr;
}


void* PageCache::systemAlloc(size_t numPages) {
	size_t size = numPages * PAGE_SIZE;
	void* ptr = malloc(size);
	return ptr;
}