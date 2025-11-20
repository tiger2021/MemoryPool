#pragma once
#include "Common.h"
#include <map>
#include <mutex>

struct Span
{
	void* pageAddr; //span起始页地址
	size_t pageNum; //span占用页数
	Span* next;     //指向下一个span

};

class PageCache
{
public:
	static PageCache& getInstance();

	//向CentralCache提供分配span接口
	void* allocateSpan(size_t pageNum);
	

private:
	// 向系统申请内存
	void* systemAlloc(size_t numPages);

private:

	// 按页数管理空闲span，不同页数对应不同Span链表
	std::map<size_t, Span*> m_freeSpansMap;

	// 页号到span的映射，用于回收
	std::map<void*, Span*> m_pageToSpanMap;
	std::mutex m_mutex; 

};

