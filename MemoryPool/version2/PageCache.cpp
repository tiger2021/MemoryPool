#include "PageCache.h"

PageCache& PageCache::getInstance() {
	static PageCache instance;
	return instance;
}

void* PageCache::allocateSpan(size_t pageNum) {
	return nullptr; //占位实现
}
