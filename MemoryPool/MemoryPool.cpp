#include "MemoryPool.h"
#include <iostream>
#include <assert.h>

MemoryPool::MemoryPool(size_t BlockSize)
	:m_blockSize(BlockSize)
	,m_slotSize(0)
	, m_firstBlock(nullptr)
	, m_currentSlot(nullptr)
	, m_freeList(nullptr)
	, m_lastSlot(nullptr)
{}
MemoryPool::~MemoryPool()
{
	Slot* curr = m_firstBlock;
	while (curr)
	{
		Slot* next = curr->next;
		operator delete(reinterpret_cast<void*>(curr));
		curr = next;
	}
}

void MemoryPool::init(size_t slotSize)
{
	//assert(slotSize > 0);
	if (slotSize < sizeof(Slot*)) {
		std::cout<< "MemoryPool::init error: slotSize < sizeof(Slot*)" << std::endl;
		return;
	}

	m_slotSize = (slotSize < sizeof(Slot*)) ? sizeof(Slot*) : slotSize;
	m_firstBlock = nullptr;
	m_currentSlot = nullptr;
	m_freeList = nullptr;
	m_lastSlot = nullptr;
}

void* MemoryPool::allocate()
{
	//先尝试从空闲链表中分配内存槽
	Slot* slot = popFreeList();
	if (slot != nullptr) {
		return slot;
	}
	//空闲链表为空，从当前内存块中分配内存槽
	std::lock_guard<std::mutex> lock(m_mutexForBlock);
	if (m_currentSlot >= m_lastSlot) {
		//当前内存块已用完，申请新的内存块
		allocateNewBlock();
	}	
	slot = m_currentSlot;

	//C++ 的指针加法是按 元素个数（Slot） 移动，而不是按字节移动
	m_currentSlot += m_slotSize / sizeof(Slot);
	return slot;
}

Slot* MemoryPool::popFreeList()
{
	while (true)
	{
		//memory_order_acquire 表示该操作“获取”了共享内存的可见性保证：
		//其他线程在此之前对该内存块的修改，对当前线程是可见的。
		Slot* oldHead = m_freeList.load(std::memory_order_acquire);
		if (oldHead == nullptr)
			return nullptr;   // 空闲链表为空，返回 nullptr
		Slot* newHead = nullptr;
		try {
			newHead = oldHead->next.load(std::memory_order_relaxed);
		}
		catch (...) {
			continue; // 如果加载 next 指针时发生异常，重新尝试
		}

		//如果 m_freeList 当前值等于 oldHead，则把它改为 newHead 并返回 true；
		//否则不修改 m_freeList、返回 false，并把 oldHead 更新为 m_freeList 的当前值
		if (m_freeList.compare_exchange_weak(oldHead, newHead,
			std::memory_order_acquire,
			std::memory_order_relaxed)) {
			return oldHead;
		}	
	}
}

void MemoryPool::allocateNewBlock()
{
	//头插法插入新的内存块
	void* newBlock = operator new(m_blockSize);
	reinterpret_cast<Slot*>(newBlock)->next = m_firstBlock;
	m_firstBlock = reinterpret_cast<Slot*>(newBlock);

	//跳过 block 开头那部分用于“链表链接”的指针区域，
	//得到 block 的“正文部分”起始地址。
	//char* 是通用的“字节指针”
	//char 的大小是固定的：1 字节
	char* body = reinterpret_cast<char*>(newBlock) + sizeof(Slot*);

	//需要跳过多少字节才能对齐
	size_t paddingSize = padPointer(body, m_slotSize);

	//计算出第一个“可用 slot”的起始位置
	m_currentSlot = reinterpret_cast<Slot*>(body + paddingSize);

	//最后一个 slot 的起始地址
	m_lastSlot = reinterpret_cast<Slot*>(
		reinterpret_cast<char*>(newBlock) + m_blockSize - m_slotSize + 1);
}

//计算从当前地址 p 开始，到达“下一个对齐边界”所需要跳过的字节数。
size_t MemoryPool::padPointer(char* p, size_t alignment)
{
	//把指针 p 转换为整数类型（地址值）。
	size_t result = reinterpret_cast<size_t>(p);

	//离下一个对齐边界”还差多少字节,计算机中最小的可寻址存储单位(字节)
	return alignment - (result % alignment);
}


bool MemoryPool::pushFreeList(Slot* slot) {
	while (true) {
		Slot* oldHead = m_freeList.load(std::memory_order_relaxed);
		slot->next.store(oldHead, std::memory_order_relaxed);
		// 尝试将新节点设置为头节点
		if (m_freeList.compare_exchange_weak(oldHead, slot,
			std::memory_order_release, std::memory_order_relaxed)) {
			return true;
		}
		//失败则重新尝试
	}
}

void MemoryPool::deallocate(void* ptr) {
	if (!ptr) return;

	Slot* slot = reinterpret_cast<Slot*>(ptr);
	pushFreeList(slot);
}