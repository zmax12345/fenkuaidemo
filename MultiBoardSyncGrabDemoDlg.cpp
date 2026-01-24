// MultiBoardSyncGrabDemoDlg.cpp : implementation file
//

#include "stdafx.h"
#include "MultiBoardSyncGrabDemo.h"
#include "MultiBoardSyncGrabDemoDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CAboutDlg dialog used for App About

class CAboutDlg : public CDialog
{
public:
	CAboutDlg();

// Dialog Data
	//{{AFX_DATA(CAboutDlg)
	enum { IDD = IDD_ABOUTBOX };
	//}}AFX_DATA

	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CAboutDlg)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:
	//{{AFX_MSG(CAboutDlg)
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

CAboutDlg::CAboutDlg() : CDialog(CAboutDlg::IDD)
{
	//{{AFX_DATA_INIT(CAboutDlg)
	//}}AFX_DATA_INIT
}

void CAboutDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CAboutDlg)
	//}}AFX_DATA_MAP
}

BEGIN_MESSAGE_MAP(CAboutDlg, CDialog)
	//{{AFX_MSG_MAP(CAboutDlg)
		// No message handlers
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CMultiBoardSyncGrabDemoDlg dialog

CMultiBoardSyncGrabDemoDlg::CMultiBoardSyncGrabDemoDlg(CWnd* pParent /*=NULL*/)
	: CDialog(CMultiBoardSyncGrabDemoDlg::IDD, pParent)
{
	//{{AFX_DATA_INIT(CMultiBoardSyncGrabDemoDlg)
		// NOTE: the ClassWizard will add member initialization here
	//}}AFX_DATA_INIT
	// Note that LoadIcon does not require a subsequent DestroyIcon in Win32
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);

	m_ImageWnd			= NULL;
	m_Acq[0]				= NULL;
   m_Acq[1]          = NULL;
	m_Buffers[0]	   = NULL;
   m_Buffers[1]	   = NULL;
	m_Xfer[0]	      = NULL;
   m_Xfer[1]          = NULL;
	m_View            = NULL;

   m_IsSignalDetected = TRUE;

   // 【新增初始化】
   m_bIsRecording = FALSE;
   for (int i = 0; i < 2; i++)
   {
	   m_hWorkerThread[i] = NULL;
	   m_hStopEvent[i] = NULL;
	   m_hDataAvailableEvent[i] = NULL;
	   m_pMemPool[i] = NULL;
	   m_hFileRaw[i] = INVALID_HANDLE_VALUE;
	   m_iHead[i] = 0;
	   m_iTail[i] = 0;
	   m_nPoolLoad[i] = 0;
	   m_nFramesRecorded[i] = 0;
	   InitializeCriticalSection(&m_csPool[i]); // 初始化锁
   }
}

void CMultiBoardSyncGrabDemoDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CMultiBoardSyncGrabDemoDlg)
	DDX_Control(pDX, IDC_STATUS, m_statusWnd);
	DDX_Control(pDX, IDC_VERT_SCROLLBAR, m_verticalScr);
	DDX_Control(pDX, IDC_HORZ_SCROLLBAR, m_horizontalScr);
	DDX_Control(pDX, IDC_VIEW_WND, m_viewWnd);
	//}}AFX_DATA_MAP
}

BEGIN_MESSAGE_MAP(CMultiBoardSyncGrabDemoDlg, CDialog)
	//{{AFX_MSG_MAP(CMultiBoardSyncGrabDemoDlg)
	ON_WM_SYSCOMMAND()
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
	ON_WM_DESTROY()
	ON_WM_MOUSEMOVE()
	ON_WM_HSCROLL()
	ON_WM_VSCROLL()
	ON_WM_MOVE()
	ON_WM_SIZE()
	ON_BN_CLICKED(IDC_SNAP, OnSnap)
	ON_BN_CLICKED(IDC_GRAB, OnGrab)
	ON_BN_CLICKED(IDC_FREEZE, OnFreeze)
	ON_BN_CLICKED(IDC_VIEW_OPTIONS, OnViewOptions)
	ON_BN_CLICKED(IDC_FILE_NEW, OnFileNew)
	ON_BN_CLICKED(IDC_EXIT, OnExit)
   ON_WM_ENDSESSION()
   ON_WM_QUERYENDSESSION()
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CMultiBoardSyncGrabDemoDlg message handlers

void CMultiBoardSyncGrabDemoDlg::XferCallback(SapXferCallbackInfo* pInfo)
{
	CMultiBoardSyncGrabDemoDlg* pDlg = (CMultiBoardSyncGrabDemoDlg*)pInfo->GetContext();

	// 1. 判断是哪个相机触发的回调
	int camIndex = -1;
	if (pInfo->GetTransfer() == pDlg->m_Xfer[0]) camIndex = 0;
	else if (pInfo->GetTransfer() == pDlg->m_Xfer[1]) camIndex = 1;
	else return; // 异常情况

	if (pInfo->IsTrash())
	{
		// 记录丢帧日志
		pDlg->WriteTrashLog(camIndex, -1, pDlg->m_nFramesRecorded[camIndex]);
		return;
	}

	// 2. 如果正在录制，将数据拷贝到对应的内存池
	if (pDlg->m_bIsRecording)
	{
		EnterCriticalSection(&pDlg->m_csPool[camIndex]);
		if (pDlg->m_nPoolLoad[camIndex] < POOL_FRAME_COUNT)
		{
			

			// 1. 获取参数
			int width = pDlg->m_Buffers[camIndex]->GetWidth();
			int height = pDlg->m_Buffers[camIndex]->GetHeight();
			int bpp = pDlg->m_Buffers[camIndex]->GetBytesPerPixel();

			// 2. 锁定源和目标
			void* pSrcStart = NULL;
			pDlg->m_Buffers[camIndex]->GetAddress(pDlg->m_Buffers[camIndex]->GetIndex(), &pSrcStart);
			BYTE* pSrcLine = (BYTE*)pSrcStart;
			BYTE* pDstLine = pDlg->m_pMemPool[camIndex][pDlg->m_iHead[camIndex]];

			// 3. 关键：计算源Buffer的跨度 (包含两个相机的总宽度)
			int copyBytes = width * bpp; // 这一行要拷多少字节 (有效数据)
			int srcPitch = pDlg->m_Buffer->GetWidth() * bpp; // 下一行在源内存里隔多远 (大Buffer总宽)

			// 4. 逐行拷贝 (修复拼图问题的核心)
			for (int h = 0; h < height; h++)
			{
				memcpy(pDstLine, pSrcLine, copyBytes); // 只拷有效数据
				pDstLine += copyBytes;                 // 目标指针紧凑移动
				pSrcLine += srcPitch;                  // 源指针跨大步 (跳过另一个相机的数据)
			}

			pDlg->m_iHead[camIndex] = (pDlg->m_iHead[camIndex] + 1) % POOL_FRAME_COUNT;
			pDlg->m_nPoolLoad[camIndex]++;

			// 唤醒对应的写盘线程
			SetEvent(pDlg->m_hDataAvailableEvent[camIndex]);
		}
		else
		{
			// 内存池满，软丢帧
			pDlg->WriteTrashLog(camIndex, -2, pDlg->m_nFramesRecorded[camIndex]);
		}
		LeaveCriticalSection(&pDlg->m_csPool[camIndex]);
	}

	// 3. 刷新界面 (可选：只刷新相机0的图像，或者交替刷新，避免界面卡顿)
	// if (camIndex == 0) 
	{
		pDlg->m_View->Show();
		// ...
	}
}

void CMultiBoardSyncGrabDemoDlg::SignalCallback(SapAcqCallbackInfo *pInfo)
{
	CMultiBoardSyncGrabDemoDlg *pDlg = (CMultiBoardSyncGrabDemoDlg *) pInfo->GetContext();
   pDlg->GetSignalStatus(pInfo->GetSignalStatus());
}

//***********************************************************************************
// Initialize Demo Dialog based application
//***********************************************************************************
BOOL CMultiBoardSyncGrabDemoDlg::OnInitDialog()
{
	CRect rect;


	CDialog::OnInitDialog();
	
	// Add "About..." menu item to system menu.

	// IDM_ABOUTBOX must be in the system command range.
	ASSERT((IDM_ABOUTBOX & 0xFFF0) == IDM_ABOUTBOX);
	ASSERT(IDM_ABOUTBOX < 0xF000);

	CMenu* pSysMenu = GetSystemMenu(FALSE);
	if (pSysMenu != NULL)
	{
		CString strAboutMenu;
		strAboutMenu.LoadString(IDS_ABOUTBOX);
		if (!strAboutMenu.IsEmpty())
		{
			pSysMenu->AppendMenu(MF_SEPARATOR);
			pSysMenu->AppendMenu(MF_STRING, IDM_ABOUTBOX, strAboutMenu);
		}

		pSysMenu->EnableMenuItem(SC_MAXIMIZE, MF_BYCOMMAND | MF_DISABLED | MF_GRAYED);
		pSysMenu->EnableMenuItem(SC_SIZE, MF_BYCOMMAND | MF_DISABLED | MF_GRAYED);
	}

	// Set the icon for this dialog.  The framework does this automatically
	//  when the application's main window is not a dialog
	SetIcon(m_hIcon, FALSE);	// Set small icon
	SetIcon(m_hIcon, TRUE);		// Set big icon
	
	// Initialize variables
	GetWindowText(m_appTitle);

   m_Buffer	      = new SapBufferWithTrash();

	// Are we operating on-line?
	CAcqConfigDlg dlg(this, NULL);
	if (dlg.DoModal() == IDOK)
	{
		// Define on-line objects
		m_Acq[0]			= new SapAcquisition(dlg.GetAcquisition());
      m_Buffers[0]	= new MySapBufferRoi(m_Buffer);
      m_Xfer[0]      = new SapAcqToBuf(m_Acq[0], m_Buffers[0], XferCallback, this);
      // Create acquisition object
	   if (m_Acq[0] && !*m_Acq[0] && !m_Acq[0]->Create())
      {
         DestroyObjects();
         EndDialog(TRUE);
         return FALSE;
      }
	}
   else
   {
      CString message;
      message.Format(_T("No board found or selected !"));
      MessageBox(message,_T("No Board"),MB_ICONEXCLAMATION);
      EndDialog(TRUE);
      return FALSE;
   }

   // Are we operating on-line?
	CAcqConfigDlg dlg2(this, NULL);
	if (dlg2.DoModal() == IDOK)
	{
		// Define on-line objects
		m_Acq[1]			= new SapAcquisition(dlg2.GetAcquisition());
      m_Buffers[1]	= new MySapBufferRoi(m_Buffer);
      m_Xfer[1]      = new SapAcqToBuf(m_Acq[1], m_Buffers[1], XferCallback, this);

      // Create acquisition object
	   if (m_Acq[1] && !*m_Acq[1] && !m_Acq[1]->Create())
      {
         DestroyObjects();
         EndDialog(TRUE);
         return FALSE;
      }
	}
   else
   {
      CString message;
      message.Format(_T("No board found or selected !"));
      MessageBox(message,_T("No Board"),MB_ICONEXCLAMATION);
      EndDialog(TRUE);
      return FALSE;
   }


   //check to see if both acquision devices support scatter gather.
   BOOL acq0SupportSG = SapBuffer::IsBufferTypeSupported(m_Acq[0]->GetLocation(),SapBuffer::TypeScatterGather);
   BOOL acq1SupportSG = SapBuffer::IsBufferTypeSupported(m_Acq[1]->GetLocation(),SapBuffer::TypeScatterGather);
   

   if (!acq0SupportSG || !acq1SupportSG)
   {
      // check if they support scatter gather physical
      BOOL acq0SupportSGP = SapBuffer::IsBufferTypeSupported(m_Acq[0]->GetLocation(),SapBuffer::TypeScatterGatherPhysical);
      BOOL acq1SupportSGP = SapBuffer::IsBufferTypeSupported(m_Acq[1]->GetLocation(),SapBuffer::TypeScatterGatherPhysical);

      if (!(!acq0SupportSG && !acq1SupportSG && acq0SupportSGP && acq1SupportSGP))
      {
         CString message;
         message.Format(_T("The chosen acquisition devices\n\n-%s\n-%s\n\ndo not support similar buffer types.\nThe demo will now close."),
                        (LPCTSTR)CString(m_Acq[0]->GetLocation().GetServerName()),
                        (LPCTSTR)CString(m_Acq[1]->GetLocation().GetServerName()));
         MessageBox(message,_T("Buffer Type Error"),MB_ICONEXCLAMATION);
         EndDialog(TRUE);
         return 0;
      }
   }


   
	// Define other objects
	m_View = new SapView( m_Buffer, m_viewWnd.GetSafeHwnd());

	// Create all objects
	if (!CreateObjects()) 
   { 
      EndDialog(TRUE); 
      return FALSE; 
   }

   m_View->SetScalingMode( SapView::ScalingNone, TRUE);

	// Create an image window object
	m_ImageWnd = new CImageWnd(m_View, &m_viewWnd, &m_horizontalScr, &m_verticalScr, this);
	UpdateMenu();

   // Get current input signal connection status
   GetSignalStatus();

   // 【新增】为两个相机申请内存池
   for (int i = 0; i < 2; i++)
   {
	   if (m_Buffers[i] && *m_Buffers[i])
	   {
		   int width = m_Buffers[i]->GetWidth();
		   int height = m_Buffers[i]->GetHeight();
		   int bytesPerPixel = m_Buffers[i]->GetBytesPerPixel();
		   int frameSize = width * height * bytesPerPixel;

		   m_pMemPool[i] = new BYTE * [POOL_FRAME_COUNT];
		   for (int j = 0; j < POOL_FRAME_COUNT; j++)
		   {
			   // 4KB 对齐，满足 SSD 直写要求
			   m_pMemPool[i][j] = (BYTE*)VirtualAlloc(NULL, frameSize, MEM_COMMIT, PAGE_READWRITE);
		   }
	   }
   }

	return TRUE;  // return TRUE  unless you set the focus to a control
}

BOOL CMultiBoardSyncGrabDemoDlg::CreateObjects()
{
	CWaitCursor wait;

	
   

	m_bufferWidth  = m_Acq[0]->GetXferParams().GetWidth();
   m_bufferHeight = m_Acq[0]->GetXferParams().GetHeight();
   m_bufferFormat = m_Acq[0]->GetXferParams().GetFormat();

   // Create buffer object
   if (SapBuffer::IsBufferTypeSupported(m_Acq[0]->GetLocation(),SapBuffer::TypeScatterGather))
      m_Buffer->SetParameters(1, 2 * m_bufferWidth, m_bufferHeight, m_bufferFormat, SapBuffer::TypeScatterGather);
   else if (SapBuffer::IsBufferTypeSupported(m_Acq[0]->GetLocation(),SapBuffer::TypeScatterGatherPhysical))
      m_Buffer->SetParameters(1, 2 * m_bufferWidth, m_bufferHeight, m_bufferFormat, SapBuffer::TypeScatterGatherPhysical);
   
   m_Buffer->SetPixelDepth( m_Acq[ 0]->GetXferParams().GetPixelDepth());

   if (m_Buffer && !*m_Buffer && !m_Buffer->Create()) 
   {
      DestroyObjects();
      return FALSE;
   }

   // Create buffer object
   m_Buffers[0]->SetRoi( 0, 0, m_bufferWidth, m_bufferHeight);
   if (m_Buffers[0] && !*m_Buffers[0] && !m_Buffers[0]->Create()) 
   {
      DestroyObjects();
      return FALSE;
   }
   
   m_Buffers[1]->SetRoi( m_bufferWidth, 0, m_bufferWidth, m_bufferHeight);
   if (m_Buffers[1] && !*m_Buffers[1] && !m_Buffers[1]->Create()) 
   {
      DestroyObjects();
      return FALSE;
   }
   
   // Create view object
   if (m_View && !*m_View && !m_View->Create()) 
   {
      DestroyObjects();
      return FALSE;
   }

   // Create transfer object
   if (m_Xfer[0] && !*m_Xfer[0] && !m_Xfer[0]->Create()) 
   {
      DestroyObjects();
      return FALSE;
   }

    if (m_Xfer[1] && !*m_Xfer[1] && !m_Xfer[1]->Create()) 
   {
      DestroyObjects();
      return FALSE;
   }


   return TRUE;
}

BOOL CMultiBoardSyncGrabDemoDlg::DestroyObjects()
{
	BOOL bResult= TRUE;

   if (m_Xfer[1] && *m_Xfer[1])
      bResult&= m_Xfer[1]->Destroy();

   if (m_Xfer[0] && *m_Xfer[0])
      bResult&= m_Xfer[0]->Destroy();

   // Destroy view object
   if (m_View && *m_View)
      bResult&= m_View->Destroy();

   // Destroy buffer object
   if (m_Buffers[0] && *m_Buffers[0])
      bResult&= m_Buffers[0]->Destroy();

   if (m_Buffers[1] && *m_Buffers[1])
      bResult&= m_Buffers[1]->Destroy();

   if (m_Buffer && *m_Buffer )
      bResult&=  m_Buffer->Destroy();

   // Destroy acquisition object
   if (m_Acq[0] && *m_Acq[0] )
      bResult&= m_Acq[0]->Destroy();

   if (m_Acq[1] && *m_Acq[1] )
      bResult&= m_Acq[1]->Destroy();

   return bResult;
}

//**********************************************************************************
//
//				Window related functions
//
//**********************************************************************************
void CMultiBoardSyncGrabDemoDlg::OnSysCommand(UINT nID, LPARAM lParam)
{
	if(( nID & 0xFFF0) == IDM_ABOUTBOX)
	{
		CAboutDlg dlgAbout;
		dlgAbout.DoModal();
	}
	else
	{
		CDialog::OnSysCommand(nID, lParam);
	}
}


// If you add a minimize button to your dialog, you will need the code below
//  to draw the icon.  For MFC applications using the document/view model,
//  this is automatically done for you by the framework.
void CMultiBoardSyncGrabDemoDlg::OnPaint() 
{
	if( IsIconic())
	{
		CPaintDC dc(this); // device context for painting

		SendMessage(WM_ICONERASEBKGND, (WPARAM) dc.GetSafeHdc(), 0);

		// Center icon in client rectangle
		INT32 cxIcon = GetSystemMetrics(SM_CXICON);
		INT32 cyIcon = GetSystemMetrics(SM_CYICON);
		CRect rect;
		GetClientRect(&rect);
		INT32 x = (rect.Width() - cxIcon + 1) / 2;
		INT32 y = (rect.Height() - cyIcon + 1) / 2;

		// Draw the icon
		dc.DrawIcon(x, y, m_hIcon);
	}
	else
	{
		CDialog::OnPaint();

		// Display last acquired image
      m_ImageWnd->OnPaint();
	}
}

void CMultiBoardSyncGrabDemoDlg::OnDestroy() 
{
	CDialog::OnDestroy();

	// Destroy all objects
	DestroyObjects();

	// Delete all objects
	if (m_ImageWnd)   delete m_ImageWnd;
   if (m_View)       delete m_View; 
   if (m_Xfer[0])    delete m_Xfer[0]; 
   if (m_Xfer[1])    delete m_Xfer[1];
   if (m_Buffers[0]) delete m_Buffers[0]; 
   if (m_Buffers[1]) delete m_Buffers[1]; 
   if (m_Buffer)	   delete m_Buffer; 
   if (m_Acq[0])		delete m_Acq[0]; 
   if (m_Acq[1])		delete m_Acq[1]; 
}

void CMultiBoardSyncGrabDemoDlg::OnMouseMove(UINT nFlags, CPoint point) 
{
   if (m_IsSignalDetected)
   {
	   CString str = m_appTitle;

	   if (!nFlags) 
		   str += "  " + m_ImageWnd->GetPixelString(point);

	   SetWindowText(str);
   }

	CDialog::OnMouseMove(nFlags, point);
}

void CMultiBoardSyncGrabDemoDlg::OnMove(int x, int y) 
{
	CDialog::OnMove(x, y);

	if (m_ImageWnd) m_ImageWnd->OnMove();
}


void CMultiBoardSyncGrabDemoDlg::OnSize(UINT nType, int cx, int cy) 
{
	CDialog::OnSize(nType, cx, cy);

	if (m_ImageWnd) m_ImageWnd->OnSize();
}


void CMultiBoardSyncGrabDemoDlg::OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar) 
{
	if (pScrollBar->GetDlgCtrlID() == IDC_HORZ_SCROLLBAR)
	{
		// Adjust source's horizontal origin
		m_ImageWnd->OnHScroll(nSBCode, nPos);
		OnPaint();
	}

	CDialog::OnHScroll(nSBCode, nPos, pScrollBar);
}

void CMultiBoardSyncGrabDemoDlg::OnVScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar) 
{
	if (pScrollBar->GetDlgCtrlID() == IDC_VERT_SCROLLBAR)
	{
		// Adjust source's vertical origin
		m_ImageWnd->OnVScroll(nSBCode, nPos);
		OnPaint();
	}

	CDialog::OnVScroll(nSBCode, nPos, pScrollBar);
}


// The system calls this to obtain the cursor to display while the user drags
//  the minimized window.
HCURSOR CMultiBoardSyncGrabDemoDlg::OnQueryDragIcon()
{
	return (HCURSOR) m_hIcon;
}


void CMultiBoardSyncGrabDemoDlg::OnExit() 
{
	EndDialog(TRUE);
}

void CMultiBoardSyncGrabDemoDlg::OnEndSession(BOOL bEnding)
{
   CDialog::OnEndSession(bEnding);

   if( bEnding)
   {
      // If ending the session, free the resources.
      OnDestroy(); 
   }
}

BOOL CMultiBoardSyncGrabDemoDlg::OnQueryEndSession()
{
   if (!CDialog::OnQueryEndSession())
      return FALSE;

   return TRUE;
}

//**************************************************************************************
// Updates the menu items enabling/disabling the proper items depending on the state
//  of the application
//**************************************************************************************
void CMultiBoardSyncGrabDemoDlg::UpdateMenu( void)
{
	BOOL bAcqNoGrab	= m_Xfer[0] && *m_Xfer[0] && !m_Xfer[0]->IsGrabbing();
	BOOL bAcqGrab		= m_Xfer[0] && *m_Xfer[0] && m_Xfer[0]->IsGrabbing();

	// Acquisition Control
	GetDlgItem(IDC_GRAB)->EnableWindow(bAcqNoGrab);
	GetDlgItem(IDC_SNAP)->EnableWindow(bAcqNoGrab);
	GetDlgItem(IDC_FREEZE)->EnableWindow(bAcqGrab);

	
	

	// File Options
	GetDlgItem(IDC_FILE_NEW)->EnableWindow(bAcqNoGrab);
	
	// If last control was disabled, set default focus
	if (!GetFocus())
		GetDlgItem(IDC_EXIT)->SetFocus();
}


void CMultiBoardSyncGrabDemoDlg::UpdateTitleBar()
{
   // Update pixel information
   CString str = m_appTitle;
   CPoint point;
   GetCursorPos(&point);
   ScreenToClient(&point);
   str += "  " + m_ImageWnd->GetPixelString(point);

   SetWindowText(str);
}


//*****************************************************************************************
//
//					Acquisition Control
//
//*****************************************************************************************

void CMultiBoardSyncGrabDemoDlg::OnFreeze()
{
	// -----------------------------------------------------------
	// 第一步：先让硬件停下来 (不再产生新数据)
	// -----------------------------------------------------------
	if (m_Xfer[0]->Freeze() && m_Xfer[1]->Freeze())
	{
		if (CAbortDlg(this, m_Xfer[0]).DoModal() != IDOK) m_Xfer[0]->Abort();
		if (CAbortDlg(this, m_Xfer[1]).DoModal() != IDOK) m_Xfer[1]->Abort();
		UpdateMenu();
	}

	// -----------------------------------------------------------
	// 【核心修复】让子弹飞一会儿 (关键！)
	// 硬件虽然停了，但驱动缓存里可能还积压着几十帧数据没传完。
	// 如果立刻关闸，这些尾部数据就会丢失。
	// 这里强行等待 1000 毫秒，保证所有余粮都能进仓。
	// -----------------------------------------------------------
	Sleep(1000);

	// -----------------------------------------------------------
	// 第二步：软件关闸 (停止存盘)
	// -----------------------------------------------------------
	if (m_bIsRecording)
	{
		m_bIsRecording = FALSE; // 真正关闸

		for (int i = 0; i < 2; i++)
		{
			// 1. 发送停止信号给后台线程
			if (m_hStopEvent[i]) SetEvent(m_hStopEvent[i]);

			// 2. 踹一脚(SetEvent)唤醒可能在睡觉的线程，防止它死等
			if (m_hDataAvailableEvent[i]) SetEvent(m_hDataAvailableEvent[i]);

			// 3. 等待写盘线程安全退出 (给它一点时间把最后的数据写进硬盘)
			if (m_hWorkerThread[i]) {
				WaitForSingleObject(m_hWorkerThread[i], 3000); // 最多等3秒
				CloseHandle(m_hWorkerThread[i]);
				m_hWorkerThread[i] = NULL;
			}

			// 4. 关闭文件句柄
			if (m_hFileRaw[i] != INVALID_HANDLE_VALUE) {
				CloseHandle(m_hFileRaw[i]);
				m_hFileRaw[i] = INVALID_HANDLE_VALUE;
			}

			// 5. 清理事件句柄
			if (m_hStopEvent[i]) { CloseHandle(m_hStopEvent[i]); m_hStopEvent[i] = NULL; }
			if (m_hDataAvailableEvent[i]) { CloseHandle(m_hDataAvailableEvent[i]); m_hDataAvailableEvent[i] = NULL; }
		}

		// 弹窗汇报最终结果
		CString strMsg;
		strMsg.Format(_T("录制完美结束！\n\n相机1: %d 帧\n相机2: %d 帧\n\n(已自动补全尾部数据)"),
			m_nFramesRecorded[0], m_nFramesRecorded[1]);
		AfxMessageBox(strMsg);
	}
}

void CMultiBoardSyncGrabDemoDlg::OnGrab()
{
	m_statusWnd.SetWindowText(_T(""));

	// 1. 弹出两次对话框，分别选择两个相机的保存路径
	CFileDialog dlg1(FALSE, _T(".raw"), _T("Cam1_Data.raw"), OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT, _T("Raw Files|*.raw||"));
	dlg1.m_ofn.lpstrTitle = _T("请选择【相机 1】保存路径 (建议 D盘)");
	if (dlg1.DoModal() != IDOK) return;
	m_strBasePath[0] = dlg1.GetPathName();
	// 去掉后缀，只保留前缀
	m_strBasePath[0] = m_strBasePath[0].Left(m_strBasePath[0].ReverseFind('.'));

	CFileDialog dlg2(FALSE, _T(".raw"), _T("Cam2_Data.raw"), OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT, _T("Raw Files|*.raw||"));
	dlg2.m_ofn.lpstrTitle = _T("请选择【相机 2】保存路径 (建议 E盘)");
	if (dlg2.DoModal() != IDOK) return;
	m_strBasePath[1] = dlg2.GetPathName();
	m_strBasePath[1] = m_strBasePath[1].Left(m_strBasePath[1].ReverseFind('.'));
	m_strLogPath = m_strBasePath[0] + _T("_Log.txt");

	// 2. 初始化所有状态
	m_bIsRecording = TRUE;
	for (int i = 0; i < 2; i++)
	{
		m_nChunkIndex[i] = 0;
		m_nFramesRecorded[i] = 0;
		m_nFramesInCurrentChunk[i] = 0;
		m_iHead[i] = 0; m_iTail[i] = 0; m_nPoolLoad[i] = 0;

		// 创建第一个文件
		CString fileName;
		fileName.Format(_T("%s_Part0000.raw"), m_strBasePath[i]);
		m_hFileRaw[i] = CreateFile(fileName, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
			FILE_ATTRIBUTE_NORMAL | FILE_FLAG_NO_BUFFERING, NULL);

		// 创建事件和线程
		m_hStopEvent[i] = CreateEvent(NULL, TRUE, FALSE, NULL);
		m_hDataAvailableEvent[i] = CreateEvent(NULL, FALSE, FALSE, NULL);

		if (i == 0) m_hWorkerThread[i] = CreateThread(NULL, 0, WriteThreadEntry0, this, 0, NULL);
		else        m_hWorkerThread[i] = CreateThread(NULL, 0, WriteThreadEntry1, this, 0, NULL);
	}

	// 3. 启动双相机采集
	if (m_Xfer[0]->Grab() && m_Xfer[1]->Grab())
	{
		UpdateMenu();
		m_statusWnd.SetWindowText(_T("双相机同步录制中..."));
	}
}

void CMultiBoardSyncGrabDemoDlg::OnSnap() 
{
   
   m_statusWnd.SetWindowText(_T(""));
   if (m_Xfer[0]->Snap() && m_Xfer[1]->Snap())
   {
      if (CAbortDlg(this, m_Xfer[0]).DoModal() != IDOK) 
      m_Xfer[0]->Abort();

      if (CAbortDlg(this, m_Xfer[1]).DoModal() != IDOK) 
      m_Xfer[1]->Abort();

      UpdateMenu();	
   }
}



void CMultiBoardSyncGrabDemoDlg::OnViewOptions() 
{
	CViewDlg dlg(this, m_View);
	if( dlg.DoModal() == IDOK)
	{
		m_ImageWnd->Invalidate();
		m_ImageWnd->OnSize();
	}
}

// 线程入口
DWORD WINAPI CMultiBoardSyncGrabDemoDlg::WriteThreadEntry0(LPVOID pParam) {
	((CMultiBoardSyncGrabDemoDlg*)pParam)->WriteThreadLoop(0); return 0;
}
DWORD WINAPI CMultiBoardSyncGrabDemoDlg::WriteThreadEntry1(LPVOID pParam) {
	((CMultiBoardSyncGrabDemoDlg*)pParam)->WriteThreadLoop(1); return 0;
}

// 通用写盘循环
void CMultiBoardSyncGrabDemoDlg::WriteThreadLoop(int camIndex)
{
	int width = m_Buffers[camIndex]->GetWidth();
	int height = m_Buffers[camIndex]->GetHeight();
	int bpp = m_Buffers[camIndex]->GetBytesPerPixel();
	int frameSize = width * height * bpp; // 这样写文件时才会写 2 倍大小
	DWORD dwWritten;

	while (WaitForSingleObject(m_hStopEvent[camIndex], 0) != WAIT_OBJECT_0)
	{
		// 等待数据
		WaitForSingleObject(m_hDataAvailableEvent[camIndex], 1000);

		while (true)
		{
			BYTE* pData = NULL;

			// 取数据
			EnterCriticalSection(&m_csPool[camIndex]);
			if (m_nPoolLoad[camIndex] > 0) {
				pData = m_pMemPool[camIndex][m_iTail[camIndex]];
			}
			LeaveCriticalSection(&m_csPool[camIndex]);

			if (pData == NULL) break;

			// 写盘 (直写)
			if (!WriteFile(m_hFileRaw[camIndex], pData, frameSize, &dwWritten, NULL)) {
				// 错误处理...
			}

			// 更新指针
			EnterCriticalSection(&m_csPool[camIndex]);
			m_iTail[camIndex] = (m_iTail[camIndex] + 1) % POOL_FRAME_COUNT;
			m_nPoolLoad[camIndex]--;
			m_nFramesRecorded[camIndex]++;
			m_nFramesInCurrentChunk[camIndex]++;
			LeaveCriticalSection(&m_csPool[camIndex]);

			// 分卷逻辑
			if (m_nFramesInCurrentChunk[camIndex] >= CHUNK_FRAME_LIMIT)
			{
				CloseHandle(m_hFileRaw[camIndex]);
				m_nChunkIndex[camIndex]++;
				m_nFramesInCurrentChunk[camIndex] = 0;

				CString nextFile;
				nextFile.Format(_T("%s_Part%04d.raw"), m_strBasePath[camIndex], m_nChunkIndex[camIndex]);

				m_hFileRaw[camIndex] = CreateFile(nextFile, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
					FILE_ATTRIBUTE_NORMAL | FILE_FLAG_NO_BUFFERING, NULL);
			}
		}
	}
}

//*****************************************************************************************
//
//					File Options
//
//*****************************************************************************************

void CMultiBoardSyncGrabDemoDlg::OnFileNew() 
{
	m_Buffers[0]->Clear();
   m_Buffers[1]->Clear();
	InvalidateRect(NULL, FALSE);
}


void CMultiBoardSyncGrabDemoDlg::GetSignalStatus()
{
   SapAcquisition::SignalStatus signalStatus;

   if (m_Acq[0] && m_Acq[0]->IsSignalStatusAvailable())
   {
      if (m_Acq[0]->GetSignalStatus(&signalStatus, SignalCallback, this))
         GetSignalStatus(signalStatus);
   }
}

void CMultiBoardSyncGrabDemoDlg::GetSignalStatus(SapAcquisition::SignalStatus signalStatus)
{
   m_IsSignalDetected = (signalStatus != SapAcquisition::SignalNone);

   if (m_IsSignalDetected)
      SetWindowText(m_appTitle);
   else
   {
      CString newTitle = m_appTitle;
      newTitle += " (No camera signal detected)";
      SetWindowText(newTitle);
   }
}

// =========================================================
// 【补全】双相机丢帧日志记录函数
// =========================================================
void CMultiBoardSyncGrabDemoDlg::WriteTrashLog(int camIndex, int trashCount, int currentFrame)
{
	// 如果没有设置日志路径，直接返回
	if (m_strLogPath.IsEmpty()) return;

	// 以追加模式(a+)打开日志文件
	FILE* fp = _tfopen(m_strLogPath, _T("a+"));
	if (fp)
	{
		// 获取当前时间
		SYSTEMTIME st;
		GetLocalTime(&st);

		// 区分是哪个相机报的警
		CString strCam = (camIndex == 0) ? _T("Camera_1") : _T("Camera_2");

		// 写入日志: [时间] [相机号] 丢帧信息
		_ftprintf(fp, _T("[%02d:%02d:%02d.%03d] [%s] 丢帧警告! 发生在第 %d 帧. Trash堆积: %d\n"),
			st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
			strCam, currentFrame, trashCount);

		fclose(fp);
	}
}
