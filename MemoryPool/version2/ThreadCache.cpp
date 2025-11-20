#include "ThreadCache.h"
#include "CentralCache.h"

ThreadCache* ThreadCache::getInstance() {
	//表示每一个线程都拥有instance的一个实例拷贝
	static thread_local ThreadCache instance;

	return &instance;
}

ThreadCache::ThreadCache() {
	m_freeList.fill(nullptr);
	m_freeListBlockNumArray.fill(0);
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
		--m_freeListBlockNumArray[index];
		return ret;
	} else {
		//线程本地自由链表未命中，从CentralCache获取
		return fetchFromCentralCache(size);
	}
}

void* ThreadCache::fetchFromCentralCache(size_t index) {

	size_t batchNum = getBatchBlockNum((index + 1) * ALIGNMENT);
	
	//从中心缓存批量获取内存块
	void* ret = CentralCache::getInstance().fetchRange(index, batchNum);

	//获取失败
	if(!ret) {
		return nullptr;
	}

	//第一个内存块返回给调用者，剩余的插入到线程本地自由链表
	void* current = *(reinterpret_cast<void**>(ret));
	*(reinterpret_cast<void**>(ret)) = nullptr;

	//更新自由链表头部
	m_freeList[index] = current;

	//计算当前自由链表中该大小的内存块有多少
	size_t blockNum = 0;
	while (current) {
		++blockNum;
		current = *(reinterpret_cast<void**>(current));
	}

	//更新自由链表大小统计
	m_freeListBlockNumArray[index] += blockNum;

	return ret; 
}

size_t ThreadCache::getBatchBlockNum(size_t size) {
	// 基准：每次批量获取不超过4KB内存
	constexpr size_t MAX_BATCH_BLOCK_SIZE = 4*1024; //4KB

	size_t baseNum;
	if (size <= 32) baseNum = 64;   //64*32=2048B  2KB
	else if (size <= 64) baseNum = 32;  //32*64=2048B 2KB
	else if (size <= 128) baseNum = 16; //16*128=2048B  2KB
	else if (size <= 256) baseNum = 8;  //8*256=2048B  2KB
	else if (size <= 512) baseNum = 4;  //4*512=2048B   2KB
	else if (size <= 1024) baseNum = 2; //2*1KB=2048B  2KB
	else baseNum = 1;

	return baseNum;
	

}

void ThreadCache::deallocate(void* ptr, size_t size) {
	assert(ptr != nullptr && size >= 0);

	if (size > MAX_BYTES) {
		free(ptr);
		return;
	}

	size_t index = SizeClass::getFreeListIndex(size);
	//将内存块插入到线程本地自由链表头部
	*(reinterpret_cast<void**>(ptr)) = m_freeList[index];
	m_freeList[index] = ptr;
	//更新自由链表大小统计
	++m_freeListBlockNumArray[index];
	if(shouldReturnToCentralCache(index)) {
		returnToCentralCache(m_freeList[index], size);
	}


}

bool ThreadCache::shouldReturnToCentralCache(size_t index) {

	//简单策略：当某个自由链表中的内存块数量超过40个时，归还一半给中心缓存
	return m_freeListBlockNumArray[index] > THREAD_FREE_BLOCK_THRESHOLD;
}

void ThreadCache::returnToCentralCache(void* start, size_t size) {
	//根据大小计算下标
	size_t index = SizeClass::getFreeListIndex(size);

	//对齐后内存块的实际大小
	size_t alignedSize = SizeClass::roundUp(size);

	//计算需要归还的内存块数量
	size_t totalBlockNum = m_freeListBlockNumArray[index];

	if (totalBlockNum <= 1) {
		return; //至少保留一个内存块
	}

	//保留1/4的内存块在ThreadCache中
	size_t blocksToKeep = totalBlockNum / 4;
	size_t blocksToReturn = totalBlockNum - blocksToKeep;

	//将内存块串成链表                             
	char* splitNode = static_cast<char*>(start);
	//遍历到最后一个要保留的内存块                                                                                   
	for (size_t i = 0; i < blocksToReturn - 1; ++i) {
		splitNode = static_cast<char*>(*(reinterpret_cast<void**>(splitNode)));

		//链表提前结束，更新实际的返回数量
		if (splitNode == nullptr) {
			blocksToKeep = i + 1;
			blocksToReturn = totalBlockNum - blocksToKeep;
			break;
		}
	}

	if (splitNode) {
		//将要保留的部分和要归还的部分断开
		void* returnStart = *(reinterpret_cast<void**>(splitNode));
		*(reinterpret_cast<void**>(splitNode)) = nullptr;

		//更新ThreadCache中的自由链表头部
		m_freeList[index] = start;
		//更新ThreadCache中的自由链表块数量统计
		m_freeListBlockNumArray[index] = blocksToKeep;

		//归还内存块给中心缓存
		if (blocksToKeep > 0 && returnStart != nullptr) {
			//暂时先不写
		}
	}
}
