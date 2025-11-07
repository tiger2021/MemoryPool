#pragma once
#include "HashBucket.h"

template<typename T, typename... Args>
T* newElement(Args&&... args) {
	T* p = nullptr;
	p = reinterpret_cast<T*>(HashBucket::useMemory(sizeof(T))));
	if (p != nullptr) {
		// 在分配的内存上构造对象
		//这是 C++ 中构造对象的低层写法之一，用于在已经分配好的原始内存地址上构造对象。
		//请在地址为 p 的地方构造一个类型为 T 的对象
		new(p) T(std::forward<Args>(args)...);
		return p;
	}
}


template<typename T>
void deleteElement(T* p) {
	if (p != nullptr) {
		p->~T(); // 显式调用析构函数
		HashBucket::freeMemory(reinterpret_cast<void*>(p), sizeof(T));
	}
}

void initializeMemoryPools() {
	HashBucket::initMemoryPool();
}
