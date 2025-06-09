/////////////////////////////////////////////////////////////////////////////
// iocp.h - IOCP�T�|�[�g�N���X
//
#pragma once

#include "thread.h"
#include <malloc.h>

#define MIN_KEEPALIVE 30000
#define DIFF_TIME(now, tm) (now >= tm ? now - (tm) : ((DWORD)(~0) - (tm)) + now)
#define TRUNC_WAIT_OBJECTS(n) (n < MAXIMUM_WAIT_OBJECTS ? n : MAXIMUM_WAIT_OBJECTS)

// IO�����C�x���g��`�N���X
template <class THRD>	// THRD: �X���b�h�����N���X
class CIocpOverlapped
{
	template <class, int> friend class CIocpThread;
	friend THRD;
protected:
	OVERLAPPED m_ovl;
	DWORD m_dwLastTime;	// �C�x���g���������ŏI����(ms)
	THRD* m_pThread;	// �C�x���g�����X���b�h
	
	CIocpOverlapped()
	{
		::ZeroMemory(&m_ovl, sizeof(m_ovl));
		
		m_dwLastTime = ::GetTickCount();
		m_pThread = NULL;
	}
public:
	virtual BOOL OnCompletionIO(LPOVERLAPPED_ENTRY lpCPEntry) = 0;	// IO�����C�x���g
	virtual operator HANDLE() = 0;	// IOCP�p�̃n���h���擾
};

// IOCP�p�X���b�h�����N���X
template <class THRD, int NEnt = 1>	// NEnt: �����C�x���g1��Ŏ擾�ł���ő�G���g����
//template <class THRD, class TTPL>
class CIocpThread : public CAPCThread
{
	template <class> friend class CIocpThreadPool;
protected:
	typedef CIocpOverlapped<THRD> TOVL;	// �C�x���g�N���X
	typedef CIocpThreadPool<THRD> TTPL;	// �X���b�h�v�[���N���X
	
	TTPL* m_pIocp;
	
	// �e�X���b�h�C�x���g�̃f�t�H���g����
	VOID OnCancelIO(TOVL* pIO)	// IO������
	{
	}
	VOID OnTimeout(DWORD dwCurrentTime)	// �^�C���A�E�g
	{
	}
	
	// IO�����C�x���g�ҋ@�p���C���X���b�h
	static DWORD WINAPI MainProc(LPVOID pParam)
	{
		THRD* pThis = static_cast<THRD*>(pParam);
		TTPL* pIocp = pThis->m_pIocp;	// IOCP�X���b�h�v�[��
		OVERLAPPED_ENTRY CPEntries[NEnt] = { 0 };	// (�X���b�h������=>�G���g��������:16 / m_nThreadCnt + 1)
		INT iNumEntries;
		DWORD dwLastTime = ::GetTickCount();	// �ŏI�X�V����
		
		while ((iNumEntries = pIocp->WaitForIocp(CPEntries, NEnt)) >= 0)	// �����C�x���g�ҋ@
		{
			DWORD dwCurrentTime = ::GetTickCount();
			LPOVERLAPPED_ENTRY lpCPEntry = CPEntries + iNumEntries;
			
			while (--lpCPEntry >= CPEntries)	// �S�G���g���`�F�b�N
			{
				if (!lpCPEntry->lpOverlapped)
				{
					lpCPEntry->Internal = reinterpret_cast<ULONG_PTR>(pThis);
					pIocp->OnPostEvent(lpCPEntry);	// ���[�U�C�x���g���s
				}
				else if (lpCPEntry->lpCompletionKey)
				{	// �C�x���g�n���h���擾
					TOVL* pIO = reinterpret_cast<TOVL*>(lpCPEntry->lpCompletionKey);
					
					pIO->m_dwLastTime = dwCurrentTime;
					pIO->m_pThread = pThis;
					if (!pIO->OnCompletionIO(lpCPEntry))	// IO�����C�x���g���s
						pThis->OnCancelIO(pIO);		// IO�G���[���̃C�x���g���s
				}
			}
			
			if (DIFF_TIME(dwCurrentTime, dwLastTime) >= pIocp->m_dwMinKeepAlive)	// �^�C���A�E�g����
			{
				pThis->OnTimeout(dwCurrentTime);	// �^�C���A�E�g�C�x���g���s
				dwLastTime = dwCurrentTime;
			}
		}
		return 0;	// (�G���[�I������THRD�I�u�W�F�N�g���������邽�߃A�N�Z�X�֎~)
	}
public:
	CIocpThread(TTPL* pIocp = NULL)
	{
		m_pIocp = pIocp;
	}
	
	TTPL* GetIocp()	// IOCP�X���b�h�v�[���擾
	{
		return m_pIocp;
	}
	
	// IOCP���C���X���b�h�J�n
	BOOL BeginThread(TTPL* pIocp, BOOL bSync = FALSE)
	{
		m_pIocp = pIocp;
		
		return (CreateThread(MainProc, this, bSync) != -1);
	}
};

// IOCP�p�X���b�h�v�[���N���X
template <class THRD>
//template <class THRD, TOVL>
class CIocpThreadPool
{
	template <class, int> friend class CIocpThread;
	friend THRD;
protected:
	typedef typename THRD::TOVL TOVL;
	
	HANDLE m_hIocp;			// IOCP�n���h��
	DWORD m_dwMinKeepAlive;	// �ŏ��ڑ��ێ�����(ms)
	DWORD m_nThreadNum;		// �X���b�h��
	LONG  m_nThreadCnt;		// �X���b�h�I���J�E���^
	THRD* m_pThreads;		// �X���b�h�z��
	
	// ���[�U�C�x���g�̃f�t�H���g����
	virtual VOID OnPostEvent(LPOVERLAPPED_ENTRY lpCPEntry)
	{
	}
	
	// IO�����C�x���g�ҋ@(�߂�l: 0�ȏ�=���s�p��/-1=�G���[�I��)
	INT WaitForIocp(LPOVERLAPPED_ENTRY CPEntries, ULONG ulNumEntries)
	{
		if (!::GetQueuedCompletionStatusEx(
				m_hIocp,
				CPEntries, ulNumEntries, &ulNumEntries,
				m_dwMinKeepAlive, TRUE))	// APC�L����(Alert���)�őҋ@
		{
			switch (::GetLastError())	// �G���[����
			{
			case WAIT_IO_COMPLETION:	// APC queued
			case WAIT_TIMEOUT:			// Timeout
				return 0;
//			case ERROR_INVALID_HANDLE:	// Invalid Handle
//			case ERROR_ABANDONED_WAIT_0:// IOCP closed
			default:
				// IOCP�n���h������ɂ��G���[���A�X���b�h�����J�E���g�_�E��
				THRD* pThreads = m_pThreads;
				if (pThreads && ::InterlockedDecrement(&m_nThreadCnt) == 0)
					delete[] pThreads;	// �S�I���ŃX���b�h�z������
				
				return -1;
			}
		}
		return ulNumEntries;
	}
	
	// �X���b�h�v�[���I���ҋ@
	BOOL JoinThread(DWORD nThreadNum)
	{
		Stop();	// �X���b�h��~�A�I������
		if (nThreadNum > m_nThreadNum || ::InterlockedExchange(&m_nThreadCnt, 0) <= 0)
			return FALSE;
		
		if (nThreadNum > 0)	// �N���G���[���͑ҋ@��������̂�
		{
			DWORD nThreadCnt, nTplCnt;
			DWORD dwCrtThreadId = ::GetCurrentThreadId();
			LPHANDLE phThreadPool = (LPHANDLE)alloca(sizeof(HANDLE) * nThreadNum);
			
			// �ҋ@�p�X���b�h�n���h���̔z��쐬
			for (nThreadCnt = nTplCnt = 0; nThreadCnt < nThreadNum ; ++nThreadCnt)
				if (dwCrtThreadId != m_pThreads[nThreadCnt].m_dwThreadID)	// ���X���b�h�͏��O
					phThreadPool[nTplCnt++] = m_pThreads[nThreadCnt].m_hThread;
			
			// �X���b�h�v�[��(���X���b�h������)�̏I���ҋ@
			if (nTplCnt > 0)
				::WaitForMultipleObjects(nTplCnt, phThreadPool, TRUE, INFINITE);
		}
		delete[] m_pThreads;	// �S�I���ŃX���b�h�z������
		
		return TRUE;
	}
public:
	CIocpThreadPool(DWORD nThreadNum = 0)
	{
		m_hIocp = NULL;
		m_dwMinKeepAlive = INFINITE;
		
		m_nThreadNum = TRUNC_WAIT_OBJECTS(nThreadNum);
		m_nThreadCnt = 0;
		m_pThreads = NULL;
	}
	~CIocpThreadPool()
	{
		JoinThread(m_nThreadNum);	// �X���b�h�v�[���I���ҋ@
	}
	
	BOOL IsRunning()	// �X���b�h���s��:TRUE
	{
		return (m_nThreadCnt > 0);
	}
	
	// IOCP�̍쐬��IO�n���h���֘A�t��
	BOOL CreateIocp(TOVL* pIO = NULL, DWORD nThreadNum = 0)
	{
		HANDLE hIo, *phIocp;
		
		if (!m_hIocp)	// ���񎞂�IOCP�쐬
		{
			hIo = (pIO ? *pIO : INVALID_HANDLE_VALUE);	// IO�n���h��������Ύw��
			phIocp = &m_hIocp;
			if (nThreadNum > 0)	// 1�ȏ�Ȃ�X���b�h���Z�b�g(�ő�64�܂�)
				m_nThreadNum = TRUNC_WAIT_OBJECTS(nThreadNum);
		}
		else if (pIO)	// 2��ڈȍ~�̓n���h���w��K�{
			phIocp = &(hIo = *pIO);
		else
			return TRUE;	// �n���h���ȗ����͖���
		
		*phIocp = ::CreateIoCompletionPort(hIo, m_hIocp, reinterpret_cast<ULONG_PTR>(pIO), m_nThreadNum);
		return (*phIocp != NULL);
	}
	
	// ���[�U�C�x���g���s
	BOOL PostEvent(ULONG_PTR ulCompletionKey, DWORD dwIoSize = 0)
	{
		return ::PostQueuedCompletionStatus(m_hIocp, dwIoSize, ulCompletionKey, NULL);
	}
	
	// �X���b�h�v�[���̎��s�J�n(bSync: ����/�񓯊�)
	BOOL Start(DWORD nKeepAlive = MIN_KEEPALIVE, BOOL bSync = TRUE)
	{
		if (IsRunning() || !CreateIocp())	// IOCP���쐬�Ȃ�쐬
			return FALSE;	// �X���b�h�v�[�����s��
		
		if (m_nThreadNum == 0)	// �X���b�h�������ݒ�Ȃ�CPU�����Z�b�g
		{
			SYSTEM_INFO systemInfo = { 0 };
			::GetSystemInfo(&systemInfo);
			m_nThreadNum = systemInfo.dwNumberOfProcessors;
		}
		
		// �X���b�h�v�[���쐬(������Suspend����X���b�h�̍ő吔�����Z: m_nThreadNum += N)
		m_pThreads = new THRD[m_nThreadNum];
		if (!m_pThreads)
			return FALSE;
		
		// �^�C���A�E�g���Ԑݒ�(�ő�^�C���A�E�g�l/2�ȏ�͖���)
		m_dwMinKeepAlive = (nKeepAlive <= INFINITE / 2 ? nKeepAlive : INFINITE);
		m_nThreadCnt = m_nThreadNum;	// �X���b�h�J�E���^������
		
		DWORD nThreadCnt, nThreadNum = m_nThreadNum - bSync;	// �ŏI�X���b�h�͓���(�I���ҋ@)�Ɋ���
		for (nThreadCnt = 0; nThreadCnt < nThreadNum; ++nThreadCnt)
			if (!m_pThreads[nThreadCnt].BeginThread(this))	// �e�X���b�h��񓯊����s
			{	// �N���G���[��
				JoinThread(nThreadCnt);	// ���s�X���b�h������ΏI���ҋ@
				return FALSE;
			}
		
		return (bSync ? m_pThreads[nThreadNum].BeginThread(this, TRUE) : TRUE);	// �������͏I���܂őҋ@
	}
	
	// �X���b�h�v�[���̎��s�I��
	BOOL Stop()
	{
		HANDLE hIocp = ::InterlockedExchangePointer(&m_hIocp, NULL);	// IOCP�n���h���̃N���A����ɂ��ē��h�~
		
		return (hIocp ? ::CloseHandle(hIocp) : FALSE);	// IOCP����ɂ��X���b�h�I��
	}
};
