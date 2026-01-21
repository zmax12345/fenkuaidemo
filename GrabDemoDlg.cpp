// GrabDemoDlg.cpp : implementation file
//

#include "stdafx.h"
#include "GrabDemo.h"
#include "GrabDemoDlg.h"

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
// CGrabDemoDlg dialog

CGrabDemoDlg::CGrabDemoDlg(CWnd* pParent /*=NULL*/)
    : CDialog(CGrabDemoDlg::IDD, pParent)
{
    //{{AFX_DATA_INIT(CGrabDemoDlg)
    // NOTE: the ClassWizard will add member initialization here
    //}}AFX_DATA_INIT
    // Note that LoadIcon does not require a subsequent DestroyIcon in Win32
    m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);

    m_Acq = NULL;
    m_Buffers = NULL;
    m_Xfer = NULL;
    m_View = NULL;

    m_IsSignalDetected = TRUE;


    // 【补上这部分初始化】
    m_pMemPool = NULL;
    m_hWorkerThread = NULL;
    m_hFileRaw = INVALID_HANDLE_VALUE;
    InitializeCriticalSection(&m_csPool); // <--- 没这行，一进锁就闪退
    // 【新增初始化】
    m_fpRaw = NULL;
    m_bIsRecording = FALSE;
    m_nFramesRecorded = 0;
    m_strLogPath = _T("");
}

void CGrabDemoDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialog::DoDataExchange(pDX);
    //{{AFX_DATA_MAP(CGrabDemoDlg)
    DDX_Control(pDX, IDC_STATUS, m_statusWnd);
    DDX_Control(pDX, IDC_VIEW_WND, m_ImageWnd);
    //}}AFX_DATA_MAP
}

BEGIN_MESSAGE_MAP(CGrabDemoDlg, CDialog)
    //{{AFX_MSG_MAP(CGrabDemoDlg)
    ON_WM_SYSCOMMAND()
    ON_WM_PAINT()
    ON_WM_QUERYDRAGICON()
    ON_WM_DESTROY()
    ON_WM_SIZE()
    ON_BN_CLICKED(IDC_SNAP, OnSnap)
    ON_BN_CLICKED(IDC_GRAB, OnGrab)
    ON_BN_CLICKED(IDC_FREEZE, OnFreeze)
    ON_BN_CLICKED(IDC_GENERAL_OPTIONS, OnGeneralOptions)
    ON_BN_CLICKED(IDC_AREA_SCAN_OPTIONS, OnAreaScanOptions)
    ON_BN_CLICKED(IDC_LINE_SCAN_OPTIONS, OnLineScanOptions)
    ON_BN_CLICKED(IDC_COMPOSITE_OPTIONS, OnCompositeOptions)
    ON_BN_CLICKED(IDC_LOAD_ACQ_CONFIG, OnLoadAcqConfig)
    ON_BN_CLICKED(IDC_IMAGE_FILTER_OPTIONS, OnImageFilterOptions)
    ON_BN_CLICKED(IDC_BUFFER_OPTIONS, OnBufferOptions)
    ON_BN_CLICKED(IDC_VIEW_OPTIONS, OnViewOptions)
    ON_BN_CLICKED(IDC_FILE_LOAD, OnFileLoad)
    ON_BN_CLICKED(IDC_FILE_NEW, OnFileNew)
    ON_BN_CLICKED(IDC_FILE_SAVE, OnFileSave)
    ON_BN_CLICKED(IDC_EXIT, OnExit)
    ON_WM_ENDSESSION()
    ON_WM_QUERYENDSESSION()
    //}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CGrabDemoDlg message handlers

void CGrabDemoDlg::XferCallback(SapXferCallbackInfo* pInfo)
{
    CGrabDemoDlg* pDlg = (CGrabDemoDlg*)pInfo->GetContext();

    if (pInfo->IsTrash()) {
        // 记录丢帧日志...
        return;
    }

    if (!pDlg->m_bIsRecording) return;

    void* pDest = NULL; // 目标地址
    EnterCriticalSection(&pDlg->m_csPool);
    if (pDlg->m_nPoolLoad < POOL_FRAME_COUNT)
    {
        pDest = pDlg->m_pMemPool[pDlg->m_iHead]; // 拿到地址就跑
        pDlg->m_iHead = (pDlg->m_iHead + 1) % POOL_FRAME_COUNT;
        pDlg->m_nPoolLoad++;
    }
    else
    {
        pDlg->WriteTrashLog(-1, pDlg->m_nFramesRecorded);
    }
    LeaveCriticalSection(&pDlg->m_csPool); // <--- 立刻解锁！让后台线程能工作

    // 2. 在锁外面慢慢拷贝 (耗时操作)
    if (pDest != NULL)
    {
        void* pSrc = NULL;
        pDlg->m_Buffers->GetAddress(pDlg->m_Buffers->GetIndex(), &pSrc);
        int size = pDlg->m_Buffers->GetWidth() * pDlg->m_Buffers->GetHeight();

        // 放心拷，这块内存现在归我，后台线程还没读到它
        memcpy(pDest, pSrc, size);

        // 3. 拷完了再通知后台线程
        SetEvent(pDlg->m_hDataAvailableEvent);
    }
}

// =========================================================
// 【新增函数】写日志到 Log.txt
// =========================================================
void CGrabDemoDlg::WriteTrashLog(int trashCount, int currentFrame)
{
    // 如果没有日志路径，直接返回
    if (m_strLogPath.IsEmpty()) return;

    // 以追加模式(a+)打开
    FILE* fp = _tfopen(m_strLogPath, _T("a+"));
    if (fp)
    {
        // 获取当前毫秒级时间
        SYSTEMTIME st;
        GetLocalTime(&st);

        // 写入格式: [HH:MM:SS.mmm] 丢帧警告! 发生在第 X 帧. Trash总数: Y
        _ftprintf(fp, _T("[%02d:%02d:%02d.%03d] 丢帧警告! 发生在第 %d 帧. Trash总数: %d\n"),
            st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, currentFrame, trashCount);

        fclose(fp);
    }
}

DWORD WINAPI CGrabDemoDlg::WriteThreadEntry(LPVOID pParam)
{
    ((CGrabDemoDlg*)pParam)->WriteThreadLoop();
    return 0;
}

void CGrabDemoDlg::WriteThreadLoop()
{
    int frameSize = m_Buffers->GetWidth() * m_Buffers->GetHeight();
    DWORD dwWritten;

    while (WaitForSingleObject(m_hStopEvent, 0) != WAIT_OBJECT_0)
    {
        // 等待数据到来
        WaitForSingleObject(m_hDataAvailableEvent, 1000);

        while (true)
        {
            BYTE* pDataToWrite = NULL;

            // 1. 从环形缓冲取数据
            EnterCriticalSection(&m_csPool);
            if (m_nPoolLoad > 0) {
                pDataToWrite = m_pMemPool[m_iTail];
            }
            LeaveCriticalSection(&m_csPool);

            if (pDataToWrite == NULL) break; // 没数据了，继续等

            // 2. 【核心】直写硬盘 (绕过 Windows 缓存)
            // WriteFile 在 NO_BUFFERING 模式下要求 buffer 内存对齐，VirtualAlloc 已满足此要求
            if (!WriteFile(m_hFileRaw, pDataToWrite, frameSize, &dwWritten, NULL))
            {
                // 写入出错 (极少发生)
                // WriteTrashLog(-999, m_nFramesRecorded);
            }

            // 3. 更新指针和计数
            EnterCriticalSection(&m_csPool);
            m_iTail = (m_iTail + 1) % POOL_FRAME_COUNT;
            m_nPoolLoad--;
            m_nFramesRecorded++;
            m_nFramesInCurrentChunk++;
            LeaveCriticalSection(&m_csPool);

            // 4. 【分卷逻辑】检查是否写满
            if (m_nFramesInCurrentChunk >= CHUNK_FRAME_LIMIT)
            {
                // 关闭旧文件 (这会强制 SSD 刷新元数据)
                CloseHandle(m_hFileRaw);

                // 准备新文件名
                m_nChunkIndex++;
                m_nFramesInCurrentChunk = 0;
                CString nextFile;
                nextFile.Format(_T("%s_%04d%s"), m_strBasePrefix, m_nChunkIndex, m_strBaseExt);

                // 打开新文件 (继续直写)
                m_hFileRaw = CreateFile(nextFile, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_NO_BUFFERING, NULL);

                if (m_hFileRaw != INVALID_HANDLE_VALUE)
                {
                    // 【新增】预分配磁盘空间，防止 MFT 碎片化
                    LARGE_INTEGER liSize;
                    // 算好这个文件总共要多大：单帧大小 * 分卷帧数
                    liSize.QuadPart = (LONGLONG)frameSize * CHUNK_FRAME_LIMIT;

                    // 移动文件指针到末尾
                    if (SetFilePointerEx(m_hFileRaw, liSize, NULL, FILE_BEGIN))
                    {
                        // 标记这里是文件结尾 (这一步就占住了磁盘空间)
                        SetEndOfFile(m_hFileRaw);

                        // 移回文件开头，准备开始写
                        liSize.QuadPart = 0;
                        SetFilePointerEx(m_hFileRaw, liSize, NULL, FILE_BEGIN);
                    }
                }
            }
        }
    }
}

void CGrabDemoDlg::SignalCallback(SapAcqCallbackInfo* pInfo)
{
    CGrabDemoDlg* pDlg = (CGrabDemoDlg*)pInfo->GetContext();
    pDlg->GetSignalStatus(pInfo->GetSignalStatus());
}

void CGrabDemoDlg::PixelChanged(int x, int y)
{
    CString str = m_appTitle;
    str += "  " + m_ImageWnd.GetPixelString(CPoint(x, y));
    SetWindowText(str);
}

//***********************************************************************************
// Initialize Demo Dialog based application
//***********************************************************************************
BOOL CGrabDemoDlg::OnInitDialog()
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

    // Are we operating on-line?
    CAcqConfigDlg dlg(this, NULL);
    if (dlg.DoModal() == IDOK)
    {
        // Define on-line objects
        m_Acq = new SapAcquisition(dlg.GetAcquisition());

        // 【关键配置】申请2000个缓冲区，利用大内存抗住SSD抖动
        m_Buffers = new SapBufferWithTrash(2000, m_Acq);

        m_Xfer = new SapAcqToBuf(m_Acq, m_Buffers, XferCallback, this);


    }
    else
    {
        // Define off-line objects
        m_Buffers = new SapBuffer();
    }

    // Define other objects
    m_View = new SapView(m_Buffers);

    // Attach sapview to image viewer
    m_ImageWnd.AttachSapView(m_View);

    // Create all objects
    if (!CreateObjects()) { EndDialog(TRUE); return FALSE; }

    // =========================================================
    if (m_Buffers && *m_Buffers) // 确保 buffer 已经创建成功
    {
        int width = m_Buffers->GetWidth();
        int height = m_Buffers->GetHeight();
        // 这一步之前报错，是因为没 Create。现在 Create 了，就不会报错了。
        int bytesPerPixel = m_Buffers->GetBytesPerPixel();
        int frameSize = width * height * bytesPerPixel;

        // 申请指针数组
        m_pMemPool = new BYTE * [POOL_FRAME_COUNT];

        // 为每一帧申请 4KB 对齐的内存
        for (int i = 0; i < POOL_FRAME_COUNT; i++)
        {
            m_pMemPool[i] = (BYTE*)VirtualAlloc(NULL, frameSize, MEM_COMMIT, PAGE_READWRITE);
        }

        // 重置指针
        m_iHead = 0;
        m_iTail = 0;
        m_nPoolLoad = 0;
    }
    // =========================================================

    m_ImageWnd.AttachEventHandler(this);
    m_ImageWnd.CenterImage(true);
    m_ImageWnd.Reset();

    UpdateMenu();

    // Get current input signal connection status
    GetSignalStatus();

    return TRUE;  // return TRUE  unless you set the focus to a control
}

BOOL CGrabDemoDlg::CreateObjects()
{
    CWaitCursor wait;

    // Create acquisition object
    if (m_Acq && !*m_Acq && !m_Acq->Create())
    {
        DestroyObjects();
        return FALSE;
    }

    // Create buffer object
    if (m_Buffers && !*m_Buffers)
    {
        if (!m_Buffers->Create())
        {
            DestroyObjects();
            return FALSE;
        }
        // Clear all buffers
        m_Buffers->Clear();
    }

    // Create view object
    if (m_View && !*m_View && !m_View->Create())
    {
        DestroyObjects();
        return FALSE;
    }

    // Create transfer object
    if (m_Xfer && !*m_Xfer && !m_Xfer->Create())
    {
        DestroyObjects();
        return FALSE;
    }

    return TRUE;
}

BOOL CGrabDemoDlg::DestroyObjects()
{
    // Destroy transfer object
    if (m_Xfer && *m_Xfer) m_Xfer->Destroy();

    // Destroy view object
    if (m_View && *m_View) m_View->Destroy();

    // Destroy buffer object
    if (m_Buffers && *m_Buffers) m_Buffers->Destroy();

    // Destroy acquisition object
    if (m_Acq && *m_Acq) m_Acq->Destroy();

    return TRUE;
}

//**********************************************************************************
//
//				Window related functions
//
//**********************************************************************************
void CGrabDemoDlg::OnSysCommand(UINT nID, LPARAM lParam)
{
    if ((nID & 0xFFF0) == IDM_ABOUTBOX)
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
void CGrabDemoDlg::OnPaint()
{
    if (IsIconic())
    {
        CPaintDC dc(this); // device context for painting

        SendMessage(WM_ICONERASEBKGND, (WPARAM)dc.GetSafeHdc(), 0);

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
    }
}

void CGrabDemoDlg::OnDestroy()
{
    CDialog::OnDestroy();

    // 【新增：安全关闭文件】
    if (m_fpRaw)
    {
        fclose(m_fpRaw);
        m_fpRaw = NULL;
    }

    // 【补上：释放内存池】
    if (m_pMemPool)
    {
        for (int i = 0; i < POOL_FRAME_COUNT; i++)
        {
            if (m_pMemPool[i]) VirtualFree(m_pMemPool[i], 0, MEM_RELEASE);
        }
        delete[] m_pMemPool;
        m_pMemPool = NULL;
    }

    // 【补上：删除锁】
    DeleteCriticalSection(&m_csPool);

    // Destroy all objects
    DestroyObjects();

    // Delete all objects
    if (m_Xfer)			delete m_Xfer;
    if (m_View)			delete m_View;
    if (m_Buffers)		delete m_Buffers;
    if (m_Acq)			delete m_Acq;
}

void CGrabDemoDlg::OnSize(UINT nType, int cx, int cy)
{
    CDialog::OnSize(nType, cx, cy);

    CRect rClient;
    GetClientRect(rClient);

    // resize image viewer
    if (m_ImageWnd.GetSafeHwnd())
    {
        CRect rWnd;
        m_ImageWnd.GetWindowRect(rWnd);
        ScreenToClient(rWnd);
        rWnd.right = rClient.right - 5;
        rWnd.bottom = rClient.bottom - 5;
        m_ImageWnd.MoveWindow(rWnd);
    }
}


// The system calls this to obtain the cursor to display while the user drags
//  the minimized window.
HCURSOR CGrabDemoDlg::OnQueryDragIcon()
{
    return (HCURSOR)m_hIcon;
}


void CGrabDemoDlg::OnExit()
{
    EndDialog(TRUE);
}

void CGrabDemoDlg::OnEndSession(BOOL bEnding)
{
    CDialog::OnEndSession(bEnding);

    if (bEnding)
    {
        // If ending the session, free the resources.
        OnDestroy();
    }
}

BOOL CGrabDemoDlg::OnQueryEndSession()
{
    if (!CDialog::OnQueryEndSession())
        return FALSE;

    return TRUE;
}

//**************************************************************************************
// Updates the menu items enabling/disabling the proper items depending on the state
//  of the application
//**************************************************************************************
void CGrabDemoDlg::UpdateMenu(void)
{
    BOOL bAcqNoGrab = m_Xfer && *m_Xfer && !m_Xfer->IsGrabbing();
    BOOL bAcqGrab = m_Xfer && *m_Xfer && m_Xfer->IsGrabbing();
    BOOL bNoGrab = !m_Xfer || !m_Xfer->IsGrabbing();
    INT32	 scan = 0;
    BOOL bLineScan = m_Acq && m_Acq->GetParameter(CORACQ_PRM_SCAN, &scan) && (scan == CORACQ_VAL_SCAN_LINE);
    INT32 iInterface = CORACQ_VAL_INTERFACE_DIGITAL;
    if (m_Acq)
        m_Acq->GetCapability(CORACQ_CAP_INTERFACE, (void*)&iInterface);

    // Acquisition Control
    GetDlgItem(IDC_GRAB)->EnableWindow(bAcqNoGrab);
    GetDlgItem(IDC_SNAP)->EnableWindow(bAcqNoGrab);
    GetDlgItem(IDC_FREEZE)->EnableWindow(bAcqGrab);

    // Acquisition Options
    GetDlgItem(IDC_GENERAL_OPTIONS)->EnableWindow(bAcqNoGrab);
    GetDlgItem(IDC_AREA_SCAN_OPTIONS)->EnableWindow(bAcqNoGrab && !bLineScan);
    GetDlgItem(IDC_LINE_SCAN_OPTIONS)->EnableWindow(bAcqNoGrab && bLineScan);
    GetDlgItem(IDC_COMPOSITE_OPTIONS)->EnableWindow(bAcqNoGrab && (iInterface == CORACQ_VAL_INTERFACE_ANALOG));
    GetDlgItem(IDC_LOAD_ACQ_CONFIG)->EnableWindow(m_Xfer && !m_Xfer->IsGrabbing());

    // File Options
    GetDlgItem(IDC_FILE_NEW)->EnableWindow(bNoGrab);
    GetDlgItem(IDC_FILE_LOAD)->EnableWindow(bNoGrab);
    GetDlgItem(IDC_FILE_SAVE)->EnableWindow(bNoGrab);

    // Image filter Options
    GetDlgItem(IDC_IMAGE_FILTER_OPTIONS)->EnableWindow(bAcqNoGrab && m_Acq && *m_Acq && m_Acq->IsImageFilterAvailable());

    // General Options
    GetDlgItem(IDC_BUFFER_OPTIONS)->EnableWindow(bNoGrab);

    // If last control was disabled, set default focus
    if (!GetFocus())
        GetDlgItem(IDC_EXIT)->SetFocus();
}


//*****************************************************************************************
//
//					Acquisition Control
//
//*****************************************************************************************

void CGrabDemoDlg::OnFreeze()
{
    // 1. 先让采集卡停止传输 (这是硬件层面的停止)
    if (m_Xfer && m_Xfer->Freeze())
    {
        if (CAbortDlg(this, m_Xfer).DoModal() != IDOK)
            m_Xfer->Abort();
        UpdateMenu();
    }

    // 2. 处理录制逻辑停止 (这是软件层面的停止)
    if (m_bIsRecording)
    {
        m_bIsRecording = FALSE; // 第一步：关闸，阻止回调函数继续往内存池塞数据

        // 第二步：通知后台线程下班
        if (m_hStopEvent)
        {
            SetEvent(m_hStopEvent); // 发送停止信号
            SetEvent(m_hDataAvailableEvent); // 踹一脚线程，防止它卡在等待数据的 Sleep 里

            // 等待线程安全退出 (最多等2秒，防止死锁)
            if (m_hWorkerThread)
            {
                WaitForSingleObject(m_hWorkerThread, 2000);
                CloseHandle(m_hWorkerThread);
                m_hWorkerThread = NULL;
            }

            // 清理事件句柄
            CloseHandle(m_hStopEvent); m_hStopEvent = NULL;
            CloseHandle(m_hDataAvailableEvent); m_hDataAvailableEvent = NULL;
        }

        // 第三步：兜底关闭文件 (如果线程退出前没来得及关)
        if (m_hFileRaw != INVALID_HANDLE_VALUE)
        {
            CloseHandle(m_hFileRaw);
            m_hFileRaw = INVALID_HANDLE_VALUE;
        }

        // 第四步：清理旧变量 (防止逻辑混淆)
        if (m_fpRaw) { fclose(m_fpRaw); m_fpRaw = NULL; }

        // 第五步：弹窗汇报战果
        CString strMsg;
        strMsg.Format(_T("录制已停止！\n\n累计采集: %d 帧\n生成分卷: %d 个"),
            m_nFramesRecorded, m_nChunkIndex + 1);
        AfxMessageBox(strMsg);

        // 第六步：复位状态栏文字
        m_statusWnd.SetWindowText(_T("就绪"));
    }
}

void CGrabDemoDlg::OnGrab()
{
    m_statusWnd.SetWindowText(_T(""));

    CFileDialog dlg(FALSE, _T(".raw"), _T("StreamTest.raw"),
        OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT,
        _T("Raw Files (*.raw)|*.raw||"), this);

    if (dlg.DoModal() != IDOK) return;

    CString fullPath = dlg.GetPathName();

    // 1. 解析路径
    int dotPos = fullPath.ReverseFind('.');
    if (dotPos != -1) {
        m_strBasePrefix = fullPath.Left(dotPos);
        m_strBaseExt = fullPath.Mid(dotPos);
    }
    else {
        m_strBasePrefix = fullPath;
        m_strBaseExt = _T(".raw");
    }
    m_strLogPath = m_strBasePrefix + _T("_Log.txt");

    // 2. 初始化变量
    m_nChunkIndex = 0;
    m_nFramesInCurrentChunk = 0;
    m_nFramesRecorded = 0;
    m_iHead = 0; m_iTail = 0; m_nPoolLoad = 0;

    // 3. 【核心】创建第一个文件 (开启 NO_BUFFERING 直写模式)
    CString firstFile;
    firstFile.Format(_T("%s_%04d%s"), m_strBasePrefix, m_nChunkIndex, m_strBaseExt);

    m_hFileRaw = CreateFile(firstFile, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_NO_BUFFERING, NULL); // <--- 关键！

    if (m_hFileRaw == INVALID_HANDLE_VALUE) {
        AfxMessageBox(_T("创建文件失败！请检查路径。"));
        return;
    }

    // 4. 启动后台线程
    m_hStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    m_hDataAvailableEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    m_hWorkerThread = CreateThread(NULL, 0, WriteThreadEntry, this, 0, NULL);

    m_bIsRecording = TRUE;

    // 写入开始日志
    WriteTrashLog(0, 0); // 借用日志函数写个头，或者你可以单独写

    m_Xfer->Grab();
    m_statusWnd.SetWindowText(_T("正在录制 (直写+分卷模式)..."));

    UpdateMenu();
}

void CGrabDemoDlg::OnSnap()
{
    m_statusWnd.SetWindowText(_T(""));

    if (m_Xfer->Snap())
    {
        if (CAbortDlg(this, m_Xfer).DoModal() != IDOK)
            m_Xfer->Abort();

        UpdateMenu();
    }
}


//*****************************************************************************************
//
//					Acquisition Options
//
//*****************************************************************************************

void CGrabDemoDlg::OnGeneralOptions()
{
    CAcqDlg dlg(this, m_Acq);
    dlg.DoModal();
}

void CGrabDemoDlg::OnAreaScanOptions()
{
    CAScanDlg dlg(this, m_Acq);
    dlg.DoModal();
}

void CGrabDemoDlg::OnLineScanOptions()
{
    CLScanDlg dlg(this, m_Acq);
    dlg.DoModal();
}

void CGrabDemoDlg::OnCompositeOptions()
{
    if (m_Xfer->Snap())
    {
        CCompDlg dlg(this, m_Acq, m_Xfer);
        dlg.DoModal();

        UpdateMenu();
    }
}

void CGrabDemoDlg::OnLoadAcqConfig()
{
    // Set acquisition parameters
    CAcqConfigDlg dlg(this, m_Acq);
    if (dlg.DoModal() == IDOK)
    {
        // Destroy objects
        DestroyObjects();

        // Update acquisition object
        SapAcquisition acq = *m_Acq;
        *m_Acq = dlg.GetAcquisition();

        // Recreate objects
        if (!CreateObjects())
        {
            *m_Acq = acq;
            CreateObjects();
        }

        GetSignalStatus();

        m_ImageWnd.Reset();
        InvalidateRect(NULL);
        UpdateWindow();
        UpdateMenu();
    }
}

void CGrabDemoDlg::OnImageFilterOptions()
{
    CImageFilterEditorDlg dlg(m_Acq);
    dlg.DoModal();

}

//*****************************************************************************************
//
//					General Options
//
//*****************************************************************************************

void CGrabDemoDlg::OnBufferOptions()
{
    CBufDlg dlg(this, m_Buffers, m_View->GetDisplay());
    if (dlg.DoModal() == IDOK)
    {
        // Destroy objects
        DestroyObjects();

        // Update buffer object
        SapBuffer buf = *m_Buffers;
        *m_Buffers = dlg.GetBuffer();

        // Recreate objects
        if (!CreateObjects())
        {
            *m_Buffers = buf;
            CreateObjects();
        }

        m_ImageWnd.Reset();
        InvalidateRect(NULL);
        UpdateWindow();
        UpdateMenu();
    }
}

void CGrabDemoDlg::OnViewOptions()
{
    CViewDlg dlg(this, m_View);
    if (dlg.DoModal() == IDOK)
        m_ImageWnd.Refresh();
}

//*****************************************************************************************
//
//					File Options
//
//*****************************************************************************************

void CGrabDemoDlg::OnFileNew()
{
    m_Buffers->Clear();
    InvalidateRect(NULL, FALSE);
}

void CGrabDemoDlg::OnFileLoad()
{
    CLoadSaveDlg dlg(this, m_Buffers, TRUE);
    if (dlg.DoModal() == IDOK)
    {
        InvalidateRect(NULL);
        UpdateWindow();
    }
}

void CGrabDemoDlg::OnFileSave()
{
    CLoadSaveDlg dlg(this, m_Buffers, FALSE);
    dlg.DoModal();
}

void CGrabDemoDlg::GetSignalStatus()
{
    SapAcquisition::SignalStatus signalStatus;

    if (m_Acq && m_Acq->IsSignalStatusAvailable())
    {
        if (m_Acq->GetSignalStatus(&signalStatus, SignalCallback, this))
            GetSignalStatus(signalStatus);
    }
}

void CGrabDemoDlg::GetSignalStatus(SapAcquisition::SignalStatus signalStatus)
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

