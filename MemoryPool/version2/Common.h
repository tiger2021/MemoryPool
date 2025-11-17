#pragma once
#include <utility>

constexpr size_t ALIGNMENT = 8;   //对齐大小
constexpr size_t MAX_BYTES = 256 * 1024; //256KB
constexpr size_t FREE_LIST_NUM = MAX_BYTES / ALIGNMENT; //自由链表数量	

//内存块头部信息
struct BlockHeader {
	size_t size;          //内存块大小
	bool inUse;        //内存块是否被使用
	BlockHeader* next;   //指向下一个内存块
};

//大小类管理
class SizeClass {
public:
	static size_t roundUp(size_t bytes) {
		return (bytes + ALIGNMENT - 1) & ~(ALIGNMENT - 1);   //向上取整到对齐大小,~取反，二进制最后三位清0
	}

	static size_t getFreeListIndex(size_t bytes) {
		//确保bytes至少为ALIGNMENT
		bytes = std::max(bytes, ALIGNMENT);

		return (bytes + ALIGNMENT - 1) / ALIGNMENT - 1; //0对应8字节，1对应16字节
	}
}