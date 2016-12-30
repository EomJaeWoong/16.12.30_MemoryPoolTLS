/*---------------------------------------------------------------

	procademy MemoryPool.

	메모리 풀 클래스.
	특정 데이타(구조체,클래스,변수)를 일정량 할당 후 나눠쓴다.

	- 사용법.

	procademy::CMemoryPool<DATA> MemPool(300, FALSE);
	DATA *pData = MemPool.Alloc();

	pData 사용

	MemPool.Free(pData);


	!.	아주 자주 사용되어 속도에 영향을 줄 메모리라면 생성자에서
		Lock 플래그를 주어 페이징 파일로 복사를 막을 수 있다.
		아주 중요한 경우가 아닌이상 사용 금지.

		
		
		주의사항 :	단순히 메모리 사이즈로 계산하여 메모리를 할당후 메모리 블록을 리턴하여 준다.
					클래스를 사용하는 경우 클래스의 생성자 호출 및 클래스정보 할당을 받지 못한다.
					클래스의 가상함수, 상속관계가 전혀 이뤄지지 않는다.
					VirtualAlloc 으로 메모리 할당 후 memset 으로 초기화를 하므로 클래스정보는 전혀 없다.
		
				
----------------------------------------------------------------*/
#ifndef  __MEMORYPOOLTLS__H__
#define  __MEMORYPOOLTLS__H__
#include <assert.h>
#include <new.h>

template <class DATA>
class CLockfreeStack
{
public:

	struct st_NODE
	{
		DATA	Data;
		st_NODE *pNext;
	};

	struct st_TOP_NODE
	{
		st_NODE *pTopNode;
		__int64 iUniqueNum;
	};

public:

	/////////////////////////////////////////////////////////////////////////
	// 생성자
	//
	// Parameters: 없음.
	// Return: 없음.
	/////////////////////////////////////////////////////////////////////////
	CLockfreeStack()
	{
		_lUseSize = 0;
		_iUniqueNum = 0;

		_pTop = (st_TOP_NODE *)_aligned_malloc(sizeof(st_TOP_NODE), 16);
		_pTop->pTopNode = NULL;
		_pTop->iUniqueNum = 0;
	}

	/////////////////////////////////////////////////////////////////////////
	// 생성자
	//
	// Parameters: 없음.
	// Return: 없음.
	/////////////////////////////////////////////////////////////////////////
	virtual ~CLockfreeStack()
	{
		st_NODE *pNode;
		while (_pTop->pTopNode != NULL)
		{
			pNode = _pTop->pTopNode;
			_pTop->pTopNode = _pTop->pTopNode->pNext;
			free(pNode);
		}
	}

	/////////////////////////////////////////////////////////////////////////
	// 현재 사용중인 용량 얻기.
	//
	// Parameters: 없음.
	// Return: (int)사용중인 용량.
	/////////////////////////////////////////////////////////////////////////
	long	GetUseSize(void);

	/////////////////////////////////////////////////////////////////////////
	// 데이터가 비었는가 ?
	//
	// Parameters: 없음.
	// Return: (bool)true, false
	/////////////////////////////////////////////////////////////////////////
	bool	isEmpty(void)
	{
		if (_pTop->pTopNode == NULL)
			return true;

		return false;
	}


	/////////////////////////////////////////////////////////////////////////
	// CPacket 포인터 데이타 넣음.
	//
	// Parameters: (DATA)데이타.
	// Return: (bool) true, false
	/////////////////////////////////////////////////////////////////////////
	bool	Push(DATA Data)
	{
		st_NODE *pNode = new st_NODE;
		st_TOP_NODE pPreTopNode;
		__int64 iUniqueNum = InterlockedIncrement64(&_iUniqueNum);

		do {
			pPreTopNode.pTopNode = _pTop->pTopNode;
			pPreTopNode.iUniqueNum = _pTop->iUniqueNum;

			pNode->Data = Data;
			pNode->pNext = _pTop->pTopNode;
		} while (!InterlockedCompareExchange128((volatile LONG64 *)_pTop, iUniqueNum, (LONG64)pNode, (LONG64 *)&pPreTopNode));
		_lUseSize += sizeof(pNode);

		return true;
	}

	/////////////////////////////////////////////////////////////////////////
	// 데이타 빼서 가져옴.
	//
	// Parameters: (DATA *) 뽑은 데이터 넣어줄 포인터
	// Return: (bool) true, false
	/////////////////////////////////////////////////////////////////////////
	bool	Pop(DATA *pOutData)
	{
		st_TOP_NODE pPreTopNode;
		st_NODE *pNode;
		__int64 iUniqueNum = InterlockedIncrement64(&_iUniqueNum);

		do
		{
			pPreTopNode.pTopNode = _pTop->pTopNode;
			pPreTopNode.iUniqueNum = _pTop->iUniqueNum;

			pNode = _pTop->pTopNode;
		} while (!InterlockedCompareExchange128((volatile LONG64 *)_pTop, iUniqueNum, (LONG64)_pTop->pTopNode->pNext, (LONG64 *)&pPreTopNode));
		*pOutData = pPreTopNode.pTopNode->Data;
		delete pNode;
		return true;
	}

private:

	long			_lUseSize;

	st_TOP_NODE	*_pTop;
	__int64			_iUniqueNum;
};

template <class DATA>
class CMemoryPool
{
private:

	/* **************************************************************** */
	// 각 블럭 앞에 사용될 노드 구조체.
	/* **************************************************************** */
	struct st_BLOCK_NODE
	{
		st_BLOCK_NODE()
		{
			stpNextBlock = NULL;
		}
		st_BLOCK_NODE *stpNextBlock;
	};

public:

	//////////////////////////////////////////////////////////////////////////
	// 생성자, 파괴자.
	//
	// Parameters:	(int) 최대 블럭 개수.
	//				(bool) 메모리 Lock 플래그 - 중요하게 속도를 필요로 한다면 Lock.
	// Return:
	//////////////////////////////////////////////////////////////////////////
	CMemoryPool(int iBlockNum, bool bLockFlag = false)
	{
		Initial(iBlockNum, bLockFlag);
	}

	virtual	~CMemoryPool()
	{
		Release();
	}

	void Initial(int iBlockNum, bool bLockFlag = false)
	{
		st_BLOCK_NODE *pNode, *pPreNode;

		////////////////////////////////////////////////////////////////
		// 메모리 풀 크기 설정
		////////////////////////////////////////////////////////////////
		m_iBlockCount = iBlockNum;

		if (iBlockNum < 0)	return;

		else if (iBlockNum == 0)
		{
			m_bStoreFlag = true;
			m_stBlockHeader = NULL;
		}

		else
		{
			m_bStoreFlag = false;

			pNode = (st_BLOCK_NODE *)malloc(sizeof(DATA) + sizeof(st_BLOCK_NODE));
			m_stBlockHeader = pNode;
			pPreNode = pNode;

			m_LockfreeStack.Push(pNode);

			for (int iCnt = 1; iCnt < iBlockNum; iCnt++)
			{
				pNode = (st_BLOCK_NODE *)malloc(sizeof(DATA) + sizeof(st_BLOCK_NODE));
				pPreNode->stpNextBlock = pNode;
				pPreNode = pNode;

				m_LockfreeStack.Push(pNode);
			}
		}
	}

	void Release()
	{
		delete[] m_stBlockHeader;
	}

	//////////////////////////////////////////////////////////////////////////
	// 블럭 하나를 할당받는다.
	//
	// Parameters: 없음.
	// Return: (DATA *) 데이타 블럭 포인터.
	//////////////////////////////////////////////////////////////////////////
	DATA	*Alloc(bool bPlacementNew = true)
	{
		st_BLOCK_NODE *stpBlock;

		if (m_iBlockCount <= m_iAllocCount)
		{
			stpBlock = (st_BLOCK_NODE *)malloc(sizeof(DATA) + sizeof(st_BLOCK_NODE));
			InterlockedIncrement64((LONG64 *)&m_iBlockCount);
			InterlockedIncrement64((LONG64 *)&m_iAllocCount);
		}

		else
		{
			if (!m_LockfreeStack.Pop(&stpBlock))
				return NULL;
		}

		if (bPlacementNew)	new ((DATA *)(stpBlock + 1)) DATA;

		return (DATA *)(stpBlock + 1);
	}

	//////////////////////////////////////////////////////////////////////////
	// 사용중이던 블럭을 해제한다.
	//
	// Parameters: (DATA *) 블럭 포인터.
	// Return: (BOOL) TRUE, FALSE.
	//////////////////////////////////////////////////////////////////////////
	bool	Free(DATA *pData)
	{
		if (!m_LockfreeStack.Push((st_BLOCK_NODE *)pData - 1))
			return false;

		InterlockedIncrement64((LONG64 *)&m_iAllocCount);
		return true;
	}


	//////////////////////////////////////////////////////////////////////////
	// 현재 사용중인 블럭 개수를 얻는다.
	//
	// Parameters: 없음.
	// Return: (int) 사용중인 블럭 개수.
	//////////////////////////////////////////////////////////////////////////
	int		GetAllocCount(void) { return m_iAllocCount; }

private :
	CLockfreeStack<st_BLOCK_NODE *> m_LockfreeStack;

	//////////////////////////////////////////////////////////////////////////
	// 노드 구조체 헤더
	//////////////////////////////////////////////////////////////////////////
	st_BLOCK_NODE *m_stBlockHeader;

	//////////////////////////////////////////////////////////////////////////
	// 메모리 Lock 플래그
	//////////////////////////////////////////////////////////////////////////
	bool m_bLockFlag;

	//////////////////////////////////////////////////////////////////////////
	// 메모리 동적 플래그, true면 필요할떄마다 새로 동적으로 생성
	//////////////////////////////////////////////////////////////////////////
	bool m_bStoreFlag;

	//////////////////////////////////////////////////////////////////////////
	// 현재 사용중인 블럭 개수
	//////////////////////////////////////////////////////////////////////////
	int m_iAllocCount;

	//////////////////////////////////////////////////////////////////////////
	// 전체 블럭 개수
	//////////////////////////////////////////////////////////////////////////
	int m_iBlockCount;

	template <class DATA>
	class CMemoryPoolTLS
	{
	private :
		template <class DATA>
		class CChunkBlock
		{
		private :
			struct stDATA_BLOCK
			{
				unsigned __int64 _iBlockCheck;

				CChunkBlock *pChunkBlock;
				DATA Data;
				//stDATA_BLOCK *pNext; -> 이거 스택으로 쓰려면 이렇게, 배열로 만드셈
			}

		public :
			CChunkBlock();
			virtual ~CChunkBlock();

		private :
			CMemoryPoolTLS *_pTLSptr;

			long _lMaxCnt;
			long _lAllocRef;
			
			stDATA_BLOCK *_pDataBlock;
		};

	public :
		CChunkBlock *ChunkAlloc();
		bool ChunkFree(DATA *Data);

	private :
		CMemoryPool<CChunkBlock> *p_ChunkMemPool;
	};
};

#endif