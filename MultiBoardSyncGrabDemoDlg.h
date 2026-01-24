// MultiBoardSyncGrabDemoDlg.h : header file
//

#if !defined(AFX_MULTIBOARDSYNCGRABDEMODLG_H__82BFE149_F01E_11D1_AF74_00A0C91AC0FB__INCLUDED_)
#define AFX_MULTIBOARDSYNCGRABDEMODLG_H__82BFE149_F01E_11D1_AF74_00A0C91AC0FB__INCLUDED_

#if _MSC_VER >= 1000
#pragma once
#endif // _MSC_VER >= 1000

#include "SapClassBasic.h"
#include "SapClassGui.h"

/////////////////////////////////////////////////////////////////////////////
// CMultiBoardSyncGrabDemoDlg dialog
/////////////////////////////////////////////////////////////////////////////
// C2Acq1xFerDlg dialog
// Derived class allows us to correctly retrieve the trash buffer handle
// for a SapBufferRoi object
class MySapBufferRoi : public SapBufferRoi
{
public:
   MySapBufferRoi(SapBuffer *pParent, int xmin=0, int ymin=0, int width=-1, int height=-1)
      : SapBufferRoi(pParent, xmin, ymin, width, height) {}
   virtual CORBUFFER GetTrash() const { return m_hTrashChild; }
};


class CMultiBoardSyncGrabDemoDlg : public CDialog
{
// Construction
public:
	CMultiBoardSyncGrabDemoDlg(CWnd* pParent = NULL);	// standard constructor

	BOOL CreateObjects();
	BOOL DestroyObjects();
	void UpdateMenu();
   void UpdateTitleBar();
	static void XferCallback(SapXferCallbackInfo *pInfo);
	static void SignalCallback(SapAcqCallbackInfo *pInfo);
   void GetSignalStatus();
   void GetSignalStatus(SapAcquisition::SignalStatus signalStatus);

// Dialog Data
	//{{AFX_DATA(CMultiBoardSyncGrabDemoDlg)
	enum { IDD = IDD_MULTIBOARDSYNCGRABDEMO_DIALOG };
	CStatic	m_statusWnd;
	CScrollBar	m_verticalScr;
	CScrollBar	m_horizontalScr;
	CStatic	m_viewWnd;
	//}}AFX_DATA

	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CMultiBoardSyncGrabDemoDlg)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);	// DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:
   HICON		m_hIcon;
   CString	m_appTitle;
   UINT32   m_minWidth;
   UINT32   m_minHeight;
   UINT32   m_bufferWidth;
   UINT32   m_bufferHeight;
   UINT32   m_bufferFormat;

   CImageWnd*        m_ImageWnd;
   SapAcquisition*   m_Acq[2];
   
   MySapBufferRoi*   m_Buffers[2];
   SapBuffer*        m_Buffer ; 
      
   SapTransfer*      m_Xfer[2];
   SapView*          m_View;

   // 线程同步与控制
   HANDLE      m_hWorkerThread[2];       // 两个后台写盘线程
   HANDLE      m_hStopEvent[2];          // 两个停止信号
   HANDLE      m_hDataAvailableEvent[2]; // 两个数据就绪信号
   CRITICAL_SECTION m_csPool[2];         // 两把锁，分别保护各自的内存池

   // 内存池
   BYTE** m_pMemPool[2];            // 两个指针数组，指向各自的内存块
   int         m_iHead[2];               // 生产者指针
   int         m_iTail[2];               // 消费者指针
   int         m_nPoolLoad[2];           // 当前积压量

   // 文件操作
   HANDLE      m_hFileRaw[2];            // 两个文件句柄 (直写模式)
   BOOL        m_bIsRecording;           // 全局录制状态
   int         m_nFramesRecorded[2];     // 各自已录制的帧数
   int         m_nChunkIndex[2];         // 各自的分卷索引
   int         m_nFramesInCurrentChunk[2];// 当前分卷已写帧数

   // 路径管理
   CString     m_strBasePath[2];         // 两个相机的基础路径 (例如 "D:\Cam1" 和 "E:\Cam2")
   CString     m_strLogPath;             // 日志路径

   // 常量定义
   static const int POOL_FRAME_COUNT = 500; // 每个相机 500 帧缓冲
   static const int CHUNK_FRAME_LIMIT = 4000; // 分卷大小

   // 线程函数
   static DWORD WINAPI WriteThreadEntry0(LPVOID pParam);
   static DWORD WINAPI WriteThreadEntry1(LPVOID pParam);
   void WriteThreadLoop(int camIndex); // 通用写盘逻辑

   // 辅助函数
   void WriteTrashLog(int camIndex, int trashCount, int currentFrame);
   void CleanupResources(); // 统一清理

	
   BOOL m_IsSignalDetected;   // TRUE if camera signal is detected

	// Generated message map functions
	//{{AFX_MSG(CMultiBoardSyncGrabDemoDlg)
	virtual BOOL OnInitDialog();
	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	afx_msg void OnPaint();
	afx_msg HCURSOR OnQueryDragIcon();
	afx_msg void OnDestroy();
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
	afx_msg void OnVScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
	afx_msg void OnMove(int x, int y);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnSnap();
	afx_msg void OnGrab();
	afx_msg void OnFreeze();
	afx_msg void OnViewOptions();
	afx_msg void OnFileNew();
	afx_msg void OnExit();
   afx_msg void OnEndSession(BOOL bEnding);
   afx_msg BOOL OnQueryEndSession();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Developer Studio will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_MULTIBOARDSYNCGRABDEMODLG_H__82BFE149_F01E_11D1_AF74_00A0C91AC0FB__INCLUDED_)
