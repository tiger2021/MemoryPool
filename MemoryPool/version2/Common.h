#pragma once
#include <utility>
#include <assert.h>

constexpr size_t ALIGNMENT = 8;   //对齐大小
constexpr size_t MAX_BYTES = 256 * 1024; //256KB
constexpr size_t FREE_LIST_NUM = MAX_BYTES / ALIGNMENT; //自由链表数量	
constexpr size_t MAX_SPAN_NUM = 1024;   
constexpr size_t PAGE_SIZE =4096;  // 4K 页大小

// 线程本地缓存中单个大小类内存块的最大数量阈值，超过则归还给中心缓存
constexpr size_t THREAD_FREE_BLOCK_THRESHOLD = 64;  

// 每次从PageCache获取span大小（以页为单位）
constexpr size_t SPAN_PAGES = 8;

//内存块头部信息
struct BlockHeader {
	size_t size;          //内存块大小
	bool inUse;        //内存块是否被使用
	BlockHeader* next;   //指向下一个内存块
};

//大小类管理
class SizeClass {
public:
	//先将申请内存的大小转换为8的倍数向上取整
	static size_t roundUp(size_t bytes) {
		return (bytes + ALIGNMENT - 1) & ~(ALIGNMENT - 1);   //向上取整到对齐大小,~取反，二进制最后三位清0
	}

	//计算申请的内存从ThreadCache自由链表的哈希桶的下标
	static size_t getFreeListIndex(size_t bytes) {

		assert(bytes >= 0);

		//确保bytes至少为ALIGNMENT
		bytes = std::max(bytes, ALIGNMENT);

		return (bytes + ALIGNMENT - 1) / ALIGNMENT - 1; //0对应8字节，1对应16字节
	}
};