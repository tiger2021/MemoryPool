#pragma once
#include <iostream>
#include "MemoryPool.h"
#include "MemoryPoolInterface.h"

//   512 / 8 = 64
#define MEMORY_POOL_NUM 64
#define SLOT_BASE_SIZE 8  
#define MAX_SLOT_SIZE 512

//HashBucket 就充当了一个 “自动选择合适内存池” 的调度层。
class HashBucket
{
private:
	HashBucket() {}
	~HashBucket() {}

	static void initMemoryPool() {
		for (int i = 0; i < MEMORY_POOL_NUM; ++i) {
			getMemoryPool(i).init((i + 1) * SLOT_BASE_SIZE);
		}

	}

	//index = 0 → 8字节的内存池
	//index = 1 → 16字节的内存池
	//index = 2 → 24字节的内存池
	static MemoryPool& getMemoryPool(int index) {
		//静态局部变量 只在第一次执行时创建一次，之后全局共享；
		//并且它会在程序结束时自动销毁。
		static MemoryPool memoryPool[MEMORY_POOL_NUM];
		return memoryPool[index];
	}

	static void* useMemory(size_t size) {
		if (size < 0) {
			std::cout << "HashBucket::useMemory error: size < 0" << std::endl;
		}
		if (size > MAX_SLOT_SIZE) {
			//超过最大内存池管理范围，直接从堆申请
			return operator new(size);
		}

		return getMemoryPool(((size + 7) / SLOT_BASE_SIZE) - 1).allocate();
	}

	static void freeMemory(void* ptr, size_t size) {
		if (!ptr) {
			std::cout << "HashBucket::freeMemory error: ptr is nullptr" << std::endl;
			return;
		}
		if (size < SLOT_BASE_SIZE) {
			std::cout << "HashBucket::freeMemory error: size < SLOT_BASE_SIZE" << std::endl;
			return;
		}
		if (size > MAX_SLOT_SIZE) {
			//超过最大内存池管理范围，直接释放堆内存
			operator delete(ptr);
			return;
		}
		getMemoryPool(((size + 7) / SLOT_BASE_SIZE) - 1).deallocate(ptr);
	}

	template<typename T, typename... Args>
	friend T* newElement(Args&&... args);

	template<typename T>
	friend void deleteElement(T* p);

	friend void initializeMemoryPools();

};