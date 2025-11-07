#pragma once
#include <atomic>
#include <mutex>

//一个 Slot 占用的内存
//	┌──────────────────────────────────────────────┐
//	│ atomic<Slot*> next  │   用户数据（对象内容） │
//	└──────────────────────────────────────────────┘
struct Slot {
	std::atomic<Slot*> next;
};


class MemoryPool
{
	public:
	MemoryPool(size_t BlockSize=4096);
	~MemoryPool();
	void init(size_t slotSize);
	void* allocate();
	Slot* popFreeList();

	void allocateNewBlock();
	size_t padPointer(char* p, size_t alignment);

	// 使用CAS操作进行无锁入队和出队
	bool pushFreeList(Slot* slot);
	void deallocate(void* ptr);
	

private:
	size_t m_blockSize;   //从操作系统/堆一次性申请的大块连续内存的字节数
	size_t m_slotSize;    // MemoryPool 管理的每个分配单元的大小（固定、对齐后的大小）。
	Slot* m_firstBlock=nullptr; //指向内存池管理的首个实际内存块
	Slot* m_currentSlot=nullptr;  //指向当前未被使用过的槽
	std::atomic<Slot*> m_freeList=nullptr; //空闲槽链表的头指针（已使用过但释放的槽）
	Slot* m_lastSlot = nullptr;   //作为当前内存块中最后能够存放元素的位置标识(超过该位置需申请新的内存块)
	std::mutex m_mutexForBlock; //保证多线程情况下避免不必要的重复开辟内存导致的浪费行为
};





