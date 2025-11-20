#pragma once
#include "Common.h"
class PageCache
{
public:
	static PageCache& getInstance();

	//向CentralCache提供分配span接口
	void* allocateSpan(size_t pageNum);
	

private:

private:

};

