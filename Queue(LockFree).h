#pragma once
#include "stdafx.h"
#include "MemoryPool(LockFree).h"

template <typename DATA>
class CQueue
{
private:
	LONG64 m_lSize;


	struct st_NODE
	{
		DATA data;
		st_NODE* NextNode;
	};

	struct st_TOP_NODE
	{
		st_NODE* pTopNode;
		LONG64 lCount;
	};

	st_TOP_NODE* m_pHead;
	st_TOP_NODE* m_pTail;

	CLFFreeList<st_NODE>* m_MemoryPool;

	st_NODE* m_pDummyNode;
	LONG64 m_EnLogCount;

	LONG64 m_lHeadCount;
	LONG64 m_lTailCount;

public:
	inline CQueue()
	{
		m_EnLogCount = 0;
		m_lSize = m_lHeadCount = m_lTailCount= 0;
		m_MemoryPool = new CLFFreeList<st_NODE>(0, FALSE);

		m_pDummyNode = m_MemoryPool->Alloc();
		m_pDummyNode->data = 0;
		m_pDummyNode->NextNode = nullptr;

		m_pHead = (st_TOP_NODE*)_aligned_malloc(sizeof(st_TOP_NODE), 16);
		m_pHead->lCount = 0;
		m_pHead->pTopNode = m_pDummyNode;

		m_pTail = (st_TOP_NODE*)_aligned_malloc(sizeof(st_TOP_NODE), 16);
		m_pTail->lCount = 0;
		m_pTail->pTopNode = m_pDummyNode;
	}

	inline ~CQueue()
	{
		st_NODE* temp;
		while (m_pHead->pTopNode != nullptr)
		{
			temp = m_pHead->pTopNode;
			m_pHead->pTopNode = m_pHead->pTopNode->NextNode;
			m_MemoryPool->Free(temp);
		}

		delete m_MemoryPool;

		_aligned_free(m_pHead);
		_aligned_free(m_pTail);

	}

	inline BOOL Enqueue(DATA data)
	{
		st_NODE* newNode = m_MemoryPool->Alloc();
		newNode->data = data;
		newNode->NextNode = nullptr;

		st_TOP_NODE stCloneNode;
		st_NODE* nextNode;
		LONG64 Logcount = InterlockedIncrement64(&m_EnLogCount);

		while(true)
		{
			LONG64 newCount = InterlockedIncrement64(&m_lTailCount);

			stCloneNode.pTopNode = m_pTail->pTopNode;
			stCloneNode.lCount = m_pTail->lCount;
			nextNode = m_pTail->pTopNode->NextNode;

			if (nextNode == nullptr)
			{
				// tail의 next가 null이고 현재 미리 얻어논 tail을 복사한 노드가 가리키는 다음 노드 주소값이 tail이 가리키고 있는 노드의 다음 노드 주소값과 같고 이게 nullptr이면.
				if (_InterlockedCompareExchangePointer((PVOID*)&stCloneNode.pTopNode->NextNode, newNode, nextNode) == nullptr)
				{
					InterlockedIncrement64(&m_lSize);

					InterlockedCompareExchange128((LONG64*)m_pTail, newCount, (LONG64)stCloneNode.pTopNode->NextNode, (LONG64*)&stCloneNode);
					break;
				}
			}
			// 다른 스레드에서 enqeueue중 tail에 노드 삽입 단계를 성공했다면.
			// 그냥 테일을 옮겨주고 새로 시도 하자.
			else
			{

				InterlockedCompareExchange128((LONG64*)m_pTail, newCount, (LONG64)stCloneNode.pTopNode->NextNode, (LONG64*)&stCloneNode);
			}
		}
		return TRUE;
	}

	inline BOOL Dequeue(DATA& data)
	{
		st_TOP_NODE stCloneHeadNode, stCloneTailNode;
		st_NODE* nextNode;

		LONG64 newHead = InterlockedIncrement64(&m_lHeadCount);

		LONG64 Logcount = InterlockedIncrement64(&m_EnLogCount);
		while (true)
		{
			// 헤드 저장
			stCloneHeadNode.pTopNode = m_pHead->pTopNode;
			stCloneHeadNode.lCount = m_pHead->lCount;

			// 테일 저장
			stCloneTailNode.pTopNode = m_pTail->pTopNode;
			stCloneTailNode.lCount = m_pTail->lCount;

			// 헤드의 next저장
			nextNode = stCloneHeadNode.pTopNode->NextNode;

			// 비었다면 이곳도 수정해야 한다.
			if (m_lSize == 0 && (m_pHead->pTopNode->NextNode == nullptr))
			{
				data = nullptr;
				return FALSE;
			}
			else
			{
				// head와 tail이 같은 노드를 가리키고 있는 상황 => 비어있는 상황 but
				// enq와 deq가 동시에 발생 => enq에서 tail을 검사하고 node를 이었다.(1st CAS)
				// 이후 2번째 CAS(tail 전진)을 하지 못한채 deq가 실행되면 head확인, head의 next가 nullptr이 아님.
				// deq성공 이후 다시 enq의 2번째 CAS시도. 하지만 이미 tail이 가리키고 있던 노드는 free된 상황. => 프로그램 깨짐.
				if (stCloneTailNode.pTopNode->NextNode != nullptr)
				{
					LONG64 newCount = InterlockedIncrement64(&m_lTailCount);
					InterlockedCompareExchange128((LONG64*)m_pTail, newCount, (LONG64)stCloneTailNode.pTopNode->NextNode, (LONG64*)&stCloneTailNode);
				}

				// 헤드의 next에 노드가 존재한다면
				if (nextNode != nullptr)
				{
					data = nextNode->data;
					if (InterlockedCompareExchange128((LONG64*)m_pHead, newHead, (LONG64)stCloneHeadNode.pTopNode->NextNode, (LONG64*)&stCloneHeadNode))
					{
						m_MemoryPool->Free(stCloneHeadNode.pTopNode);
						InterlockedDecrement64(&m_lSize);
						break;
					}

				}
		
			}
		}
		return TRUE;
	}

	inline bool Peek(DATA& outData, int iPeekPos)
	{
		if (m_lSize <= 0 || m_lSize < iPeekPos)
			return false;

		st_NODE* targetNode = m_pHead->pTopNode->NextNode;
		int iPos = 0;
		while (true)
		{
			if (targetNode == nullptr)
				return false;

			if (iPos == iPeekPos)
			{
				outData = targetNode->data;
				return true;
			}
			targetNode = targetNode->NextNode;
			++iPos;
		}
	}

	inline void Clear()
	{
		if (m_lSize > 0)
		{
			st_TOP_NODE* pTop = m_pHead;
			st_NODE* pNode;
			while (m_pHead->pTopNode->NextNode != nullptr)
			{
				pTop->pTopNode = pTop->pTopNode->NextNode;
				pNode = pTop->pTopNode;
				m_MemoryPool->Free(pNode);
				m_lSize--;
			}
		}
	}

	LONG64 GetUsingCount(void)
	{
		return m_lSize;
	}

	LONG64 GetAllocCount(void)
	{
		return m_MemoryPool->GetAllocCount();
	}
};




// Enqeueue
// 삽입의 단계는 2단계
// tail뒤에 새 노드 연결
// tail을 새 노드로 변경

// tail뒤에 새 노드를 연결하는 와중에 다른 스레드에서 enqeueue를 시도. tail의 next노드는 새 노드가 있는 상태.
// 다른 스레드는 tail의 next를 확인하지만 tail을 옮기는 2번째 cas를 실행하지 않아서 tail에 새 노드를 연결 할 수 없음
// 그러면 다른 스레드가 tail을 한칸 옮기고 다시 tail의 next를 다시 확인하자.
// 기존 스레드는 2번째 cas가 성공하던 실패하던 상관없이 실행하고 빠져나온다.
// tail의 위치는 어짜피 다른 스레드에서 옮겨줄 것이기 때문.