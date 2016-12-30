/*---------------------------------------------------------------

	procademy MemoryPool.

	�޸� Ǯ Ŭ����.
	Ư�� ����Ÿ(����ü,Ŭ����,����)�� ������ �Ҵ� �� ��������.

	- ����.

	procademy::CMemoryPool<DATA> MemPool(300, FALSE);
	DATA *pData = MemPool.Alloc();

	pData ���

	MemPool.Free(pData);


	!.	���� ���� ���Ǿ� �ӵ��� ������ �� �޸𸮶�� �����ڿ���
		Lock �÷��׸� �־� ����¡ ���Ϸ� ���縦 ���� �� �ִ�.
		���� �߿��� ��찡 �ƴ��̻� ��� ����.

		
		
		���ǻ��� :	�ܼ��� �޸� ������� ����Ͽ� �޸𸮸� �Ҵ��� �޸� ����� �����Ͽ� �ش�.
					Ŭ������ ����ϴ� ��� Ŭ������ ������ ȣ�� �� Ŭ�������� �Ҵ��� ���� ���Ѵ�.
					Ŭ������ �����Լ�, ��Ӱ��谡 ���� �̷����� �ʴ´�.
					VirtualAlloc ���� �޸� �Ҵ� �� memset ���� �ʱ�ȭ�� �ϹǷ� Ŭ���������� ���� ����.
		
				
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
	// ������
	//
	// Parameters: ����.
	// Return: ����.
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
	// ������
	//
	// Parameters: ����.
	// Return: ����.
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
	// ���� ������� �뷮 ���.
	//
	// Parameters: ����.
	// Return: (int)������� �뷮.
	/////////////////////////////////////////////////////////////////////////
	long	GetUseSize(void);

	/////////////////////////////////////////////////////////////////////////
	// �����Ͱ� ����°� ?
	//
	// Parameters: ����.
	// Return: (bool)true, false
	/////////////////////////////////////////////////////////////////////////
	bool	isEmpty(void)
	{
		if (_pTop->pTopNode == NULL)
			return true;

		return false;
	}


	/////////////////////////////////////////////////////////////////////////
	// CPacket ������ ����Ÿ ����.
	//
	// Parameters: (DATA)����Ÿ.
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
	// ����Ÿ ���� ������.
	//
	// Parameters: (DATA *) ���� ������ �־��� ������
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
	// �� �� �տ� ���� ��� ����ü.
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
	// ������, �ı���.
	//
	// Parameters:	(int) �ִ� �� ����.
	//				(bool) �޸� Lock �÷��� - �߿��ϰ� �ӵ��� �ʿ�� �Ѵٸ� Lock.
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
		// �޸� Ǯ ũ�� ����
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
	// �� �ϳ��� �Ҵ�޴´�.
	//
	// Parameters: ����.
	// Return: (DATA *) ����Ÿ �� ������.
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
	// ������̴� ���� �����Ѵ�.
	//
	// Parameters: (DATA *) �� ������.
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
	// ���� ������� �� ������ ��´�.
	//
	// Parameters: ����.
	// Return: (int) ������� �� ����.
	//////////////////////////////////////////////////////////////////////////
	int		GetAllocCount(void) { return m_iAllocCount; }

private :
	CLockfreeStack<st_BLOCK_NODE *> m_LockfreeStack;

	//////////////////////////////////////////////////////////////////////////
	// ��� ����ü ���
	//////////////////////////////////////////////////////////////////////////
	st_BLOCK_NODE *m_stBlockHeader;

	//////////////////////////////////////////////////////////////////////////
	// �޸� Lock �÷���
	//////////////////////////////////////////////////////////////////////////
	bool m_bLockFlag;

	//////////////////////////////////////////////////////////////////////////
	// �޸� ���� �÷���, true�� �ʿ��ҋ����� ���� �������� ����
	//////////////////////////////////////////////////////////////////////////
	bool m_bStoreFlag;

	//////////////////////////////////////////////////////////////////////////
	// ���� ������� �� ����
	//////////////////////////////////////////////////////////////////////////
	int m_iAllocCount;

	//////////////////////////////////////////////////////////////////////////
	// ��ü �� ����
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
				//stDATA_BLOCK *pNext; -> �̰� �������� ������ �̷���, �迭�� �����
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