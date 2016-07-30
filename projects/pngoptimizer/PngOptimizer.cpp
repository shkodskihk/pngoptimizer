/////////////////////////////////////////////////////////////////////////////////////
// This file is part of the PngOptimizer application
// Copyright (C) Hadrien Nilsson - psydk.org
// For conditions of distribution and use, see copyright notice in PngOptimizer.h
/////////////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "PngOptimizer.h"
#include "resource.h"

#include "POTraceCtl.h"
#include "DlgScreenshotsOptions.h"
#include "BmpClipboardDumper.h"
#include "MainWnd.h"
#include "DlgAskForFileName.h"

///////////////////////////////////////////////////////////////////////////////
// Main window title
static const char k_szTitle[] = "PngOptimizer";

// Welcome message
static const char k_szWelcomeMessage[] = "Drop PNG, GIF, BMP or TGA files here";

// Screenshot comments
static const char k_szCreating[] = "Creating ";
static const char k_szCreatingAndOptimizing[] = "Creating & Optimizing ";

///////////////////////////////////////////////////////////////////////////////
POApplication g_app;

POApplication& POApplication::GetInstance()
{
	return g_app;
}

bool POApplication::IsBmpAvailable() const
{
	return m_bmpcd.IsBmpAvailable();
}

POApplication::POApplication()
{
	m_hInstance = NULL;
	m_hAccel = NULL;
	m_hMutexPngOptimizer = NULL;
	m_pTaskbarList = NULL;
}

//////////////////////////////////////////////////////////////
int POApplication::ThreadProcStatic(void* pParameter)
{
	POApplication* pApp = (POApplication*) pParameter;
	pApp->ThreadProc();
	return 0;
}

void POApplication::ThreadProc()
{
	m_engine.OptimizeFiles(m_filePaths);

	// Notify the main window that the job is finished

	// A better theorical design would be to send a message to the main thread, independently of the main window.
	// But we cannot use PostThreadMessage because the main thread may not receive the message.
	// This can occur if the main thread is stuck in another message loop than the one we wrote here.
	// For example, when the user is dragging the scrollbar.
	BOOL bPostOk = ::PostMessage(POApplication::GetInstance().m_mainwnd.m_hWnd, WM_APP_THREADJOBDONE, 0, 0);
	bPostOk;
}

bool POApplication::IsJobRunning()
{
	return m_workingThread.IsStarted();
}

void POApplication::StartJob()
{
	if( m_filePaths.GetSize() == 0 )
		return;

	// Job already running ? Should never happen
	if( m_workingThread.IsStarted() )
	{
		// And we return here for release mode
		return;
	}

	// Bug me not with other file names while working
	m_mainwnd.AllowFileDropping(false);

	if( !m_workingThread.Start(ThreadProcStatic, this) )
	{
		m_mainwnd.WriteLine(L"Cannot create working thread", POEngine::TT_ErrorMsg);
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////
void POApplication::OnJobDone()
{
	m_workingThread.WaitForExit();
	m_mainwnd.AllowFileDropping(true);
}

//////////////////////////////////////////////////////////////
COLORREF POApplication::CrFromTc(POEngine::TextType tt)
{
	Color32 col = POEngine::ColorFromTextType(tt);
	return RGB(col.r, col.g, col.b);
}
//////////////////////////////////////////////////////////////

// Handle files given from the command line
void POApplication::ProcessCmdLineArgs()
{
	StringArray astrArgs = Process::CommandLineToArgv( Process::GetCommandLine());
	const int32 nArgCount = astrArgs.GetSize();
	if( nArgCount <= 1 )
		return;

	m_filePaths.SetSize(0);
	m_filePaths.EnsureCapacity(nArgCount - 1);
	for(int32 i = 1; i < nArgCount; ++i)
	{
		m_filePaths.Add(astrArgs[i]);
	}

	StartJob();
}

/////////////////////////////////////////////////////////////////////////////////////////////
// Change a rectangular zone to avoid full overlap
void POApplication::ChangeRectBecauseOfOverlap(RECT& rcWnd)
{
	// We divide the desktop screen into 4 zones, in order to decide
	// the direction of the offset to apply to the new rectangle
	
	int nScreenWidth = GetDeviceCaps(NULL, HORZRES);
	int nScreenHeight = GetDeviceCaps(NULL, VERTRES);
	
	const int nOffset = 16;
	if( rcWnd.left < nScreenWidth / 2 )
	{
		// On the left
		rcWnd.left += nOffset;
		rcWnd.right += nOffset;
	}
	else
	{
		// On the right
		if( (rcWnd.left - nOffset) > 0 )
		{
			rcWnd.left -= nOffset;
			rcWnd.right -= nOffset;
		}
	}

	if( rcWnd.top < nScreenHeight / 2 )
	{
		// Bottom
		rcWnd.top += nOffset;
		rcWnd.bottom += nOffset;
	}
	else
	{
		// Top
		if( (rcWnd.top - nOffset) > 0 )
		{
			rcWnd.top -= nOffset;
			rcWnd.bottom -= nOffset;
		}
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////
void POApplication::OnMainWndFilesDropped(const chustd::StringArray& filePaths)
{
	m_filePaths = filePaths;
	StartJob();	
}

/////////////////////////////////////////////////////////////////////////////////////////////
void POApplication::OnMainWndDestroying()
{
	HWND hWnd = m_mainwnd.GetHandle();

	WINDOWPLACEMENT wp = { 0 };
	wp.length = sizeof(WINDOWPLACEMENT);
	if( GetWindowPlacement(hWnd, &wp) )
	{
		AppSettings appSettings;
		appSettings.Write(*this, wp.rcNormalPosition, m_mainwnd.GetAlwaysOnTop()); // Save settings in the registry
	}
	
	// Suspend the thread so the application won't crash if resources allocated by the main
	// thread are deallocated while still in use by the working thread
	// For allocations made by the working thread, we let Windows do the job
	m_workingThread.Suspend();

	// Quit the main thread message loop
	PostQuitMessage(0);
}

/////////////////////////////////////////////////////////////////////////////////////////////
void POApplication::DumpScreenshot()
{
	if( !IsBmpAvailable() )
	{
		return;
	}

	if( !m_bmpcd.Dump(&m_engine) )
	{
		String strLastError = m_bmpcd.GetLastError();
		if( !strLastError.IsEmpty() )
		{
			m_mainwnd.WriteLine(L"Screenshot creation failure: " + strLastError, CrFromTc(POEngine::TT_ErrorMsg));
		}
		return;
	}

	String strCreating = m_bmpcd.m_settings.maximizeCompression ? k_szCreatingAndOptimizing : k_szCreating;

	String strScreenshotPath = m_bmpcd.GetFilePath();
	
	// Replace slashs with backslashs because some applications are lost when using something else than backslashs
	strScreenshotPath = strScreenshotPath.ReplaceAll(L"/", L"\\");

	// Stop redrawing to avoid ugly flashs
	m_mainwnd.SetTraceCtlRedraw(false);
	m_mainwnd.ClearLine();
	m_mainwnd.Write(strCreating, CrFromTc(POEngine::TT_RegularInfo));
	m_mainwnd.AddLink(m_bmpcd.GetFileName(), strScreenshotPath);
	m_mainwnd.Write(L"... ", CrFromTc(POEngine::TT_RegularInfo));
	m_mainwnd.Write(L" (OK)", CrFromTc(POEngine::TT_ActionOk));
	m_mainwnd.SetTraceCtlRedraw(true);

	// Force redraw here (so outside the SetRedraw(true/false) body)
	m_mainwnd.WriteLine(L"", CrFromTc(POEngine::TT_RegularInfo));
}

/////////////////////////////////////////////////////////////////////////////////////////////
void POApplication::OnBmpcdStateChanged(BmpClipboardDumper::DumpState eDumpState)
{
	String strCreating = m_bmpcd.m_settings.maximizeCompression ? k_szCreatingAndOptimizing : k_szCreating;

	switch(eDumpState)
	{
	case BmpClipboardDumper::DS_AskForFileName:
		{
			DlgAskForFileName dlg;
			dlg.m_strFileName = m_bmpcd.GetPreviousFileName();
			if( dlg.m_strFileName.IsEmpty() )
			{
				// Use the computed file name
				dlg.m_strFileName = m_bmpcd.GetFileName();
				dlg.m_bIncrementFileName = false;
			}
			else
			{
				dlg.m_bIncrementFileName = true;
			}

			dlg.m_strScreenshotDir = m_bmpcd.GetDir();

			if( dlg.DoModal( m_mainwnd.GetHandle()) == IDOK )
			{
				m_bmpcd.SetFileName(dlg.m_strFileName);
			}
			else
			{
				m_bmpcd.Abort();
			}
		}
		break;
	
	case BmpClipboardDumper::DS_Creating:
		m_mainwnd.Write(strCreating, CrFromTc(POEngine::TT_ActionVerb));
		m_mainwnd.Write(m_bmpcd.GetFileName(), CrFromTc(POEngine::TT_FilePath));
		m_mainwnd.Write(L"... ", CrFromTc(POEngine::TT_RegularInfo));
		m_mainwnd.Update();
		break;

	default:
		break;
	}
}

// This function is called from the working thread
void POApplication::OnEngineProgressing(const POEngine::ProgressingArg& arg)
{
	COLORREF cr = CrFromTc(arg.textType);
	int minWidthEx = 0;
	int justify = 0;
	if( arg.textType == POEngine::TT_SizeInfoNum )
	{
		// Pretty formatting for numbers
		minWidthEx = 0;
		justify = 1;
	}
	m_mainwnd.Write(arg.text, cr, minWidthEx, justify);
	m_mainwnd.Update();

	// Highlight the taskbar icon according to the progression
	if( m_pTaskbarList )
	{
		HWND hMainWnd = m_mainwnd.GetHandle();
		if( arg.textType == POEngine::TT_ActionVerb )
		{
			// Creating, Converting, Optimizing...
			m_pTaskbarList->SetProgressState(hMainWnd, TBPF_INDETERMINATE);
		}
		else if( arg.textType == POEngine::TT_BatchDoneOk )
		{
			if( ::GetForegroundWindow() != hMainWnd )
			{
				m_pTaskbarList->SetProgressValue(hMainWnd, 10, 10);
				m_pTaskbarList->SetProgressState(hMainWnd, TBPF_NORMAL);
			}
			else
			{
				// Clear the icon progress color if the Window is already activated
				m_pTaskbarList->SetProgressValue(hMainWnd, 0, 0);
				m_pTaskbarList->SetProgressState(hMainWnd, TBPF_NOPROGRESS);
			}
		}
		else if( arg.textType == POEngine::TT_BatchDoneFail )
		{
			if( ::GetForegroundWindow() != hMainWnd )
			{
				m_pTaskbarList->SetProgressValue(hMainWnd, 10, 10);
				m_pTaskbarList->SetProgressState(hMainWnd, TBPF_ERROR);
			}
			else
			{
				// Clear the icon progress color if the Window is already activated
				m_pTaskbarList->SetProgressValue(hMainWnd, 0, 0);
				m_pTaskbarList->SetProgressState(hMainWnd, TBPF_NOPROGRESS);
			}
		}
	}
}

void POApplication::OnMainWndListCleared()
{
	if( m_pTaskbarList )
	{
		HWND hMainWnd = m_mainwnd.GetHandle();
		m_pTaskbarList->SetProgressValue(hMainWnd, 0, 0);
		m_pTaskbarList->SetProgressState(hMainWnd, TBPF_NOPROGRESS);
	}
}

void POApplication::OnMainWndActivated()
{
	if( !m_workingThread.IsStarted() )
	{
		if( m_pTaskbarList )
		{
			HWND hMainWnd = m_mainwnd.GetHandle();
			m_pTaskbarList->SetProgressValue(hMainWnd, 0, 0);
			m_pTaskbarList->SetProgressState(hMainWnd, TBPF_NOPROGRESS);
		}
	}
}

void POApplication::DoConnections()
{
	m_mainwnd.FilesDropped.Connect(this, &POApplication::OnMainWndFilesDropped);
	m_mainwnd.ListCleared.Connect(this, &POApplication::OnMainWndListCleared);
	m_mainwnd.Destroying.Connect(this, &POApplication::OnMainWndDestroying);
	m_mainwnd.Activated.Connect(this, &POApplication::OnMainWndActivated);

	m_engine.Progressing.Connect(this, &POApplication::OnEngineProgressing);

	m_bmpcd.DumpStateChanged.Connect(this, &POApplication::OnBmpcdStateChanged);
}

/////////////////////////////////////////////////////////////////////////////////////////////
bool POApplication::Initialize(HINSTANCE hInstance)
{
	m_hInstance = hInstance;

	// Necessary to enable PngOptimizer as a drag-and-drop source :-p
	OleInitialize(NULL);

	InitCommonControls();

	///////////////////////////////////////////////////////////////////////
	// Get previous instance window rect, in order to avoid a total overlap with the new instance window
	static const wchar k_szMutexName[] = L"PngOptimizer mutex";

	m_hMutexPngOptimizer = CreateMutex(NULL, TRUE, k_szMutexName);
	int nLastError = ::GetLastError();

	bool bOneInstanceAlreadyRunning = false;
	if( m_hMutexPngOptimizer != 0 && nLastError == ERROR_ALREADY_EXISTS )
	{
		bOneInstanceAlreadyRunning = true;
	}
	///////////////////////////////////////////////////////////////////////

	///////////////////////////////////////////////////////////////////////
	// Read application settings
	RECT rcWnd;
	bool centerWindow;
	bool alwaysOnTop;
	AppSettings appSettings;
	appSettings.Read(*this, rcWnd, centerWindow, alwaysOnTop);

	if( bOneInstanceAlreadyRunning )
	{
		ChangeRectBecauseOfOverlap(rcWnd);
		
		// Write back the settings so the new window rectangle 
		// for the N+1-th instance will be updated
		appSettings.Write(*this, rcWnd, alwaysOnTop);
	}

	static const char szPngOptimizerError[] = "PngOptimizer error";

	if( !m_mainwnd.Create(k_szTitle, rcWnd, alwaysOnTop, k_szWelcomeMessage) )
	{
		String strErr = "Cannot create main gui window : " + m_mainwnd.GetLastError();
		MessageBoxW(NULL, strErr.GetBuffer(), String(szPngOptimizerError).GetBuffer(), MB_OK);
		return false;
	}

	if( centerWindow )
	{
		m_mainwnd.CenterWindow();
	}

	OnMainWndListCleared();
	m_mainwnd.Show(SW_SHOW);
	m_mainwnd.Update();

	m_pTaskbarList = NULL;
	HRESULT hres = CoCreateInstance(CLSID_TaskbarList, NULL, CLSCTX_ALL, IID_ITaskbarList3, (void**)&m_pTaskbarList);
	if( hres != S_OK )
	{
		m_pTaskbarList = NULL; // To be sure
	}

	DoConnections();
	
	// Perform some early resources creation to speedup first optimization
	if( !m_engine.WarmUp() )
	{
		String strErr = "POEngine warm-up failed";
		MessageBoxW(NULL, strErr.GetBuffer(), String(szPngOptimizerError).GetBuffer(), MB_OK);
		return false;
	}

	/////////////////////////////////////////////////////////////
	// Create application shortcuts
	//  - Ctrl+V or Shift+Inser to paste screenshot
	//  - Ctrl+Shift+Delete to clear the list

	const int accelCount = 3;
	static ACCEL aAccels[accelCount] =
	{
		{ FCONTROL | FVIRTKEY,          'V',       IDM_PASTEFROMCLIPBOARD },
		{ FSHIFT   | FVIRTKEY,          VK_INSERT, IDM_PASTEFROMCLIPBOARD },
		{ FCONTROL | FVIRTKEY,          VK_DELETE, IDM_CLEARLIST }
	};

	m_hAccel = CreateAcceleratorTable(aAccels, accelCount);

	/////////////////////////////////////////////////////////////
	// Handle files given through the command line
	ProcessCmdLineArgs();

	return true;
}

// Perform cleanup
POApplication::~POApplication()
{
	if( m_pTaskbarList != NULL )
	{
		m_pTaskbarList->Release();
		m_pTaskbarList = NULL;
	}
	if( m_hAccel )
	{
		DestroyAcceleratorTable(m_hAccel);
	}

	if( m_hMutexPngOptimizer )
	{
		CloseHandle(m_hMutexPngOptimizer);
	}

	OleUninitialize();
}

/////////////////////////////////////////////////////////////
// Main application loop

int32 POApplication::Run()
{	
	HWND hMainWnd = m_mainwnd.GetHandle();
	
	MSG msg;
	while( GetMessage(&msg, NULL, 0, 0)) 
	{
		// Keyboard shortcuts are targeted to the main window, so the main window handle is given
		// to TranslateAccelerator 
		if( !TranslateAccelerator(hMainWnd, m_hAccel, &msg) )
		{
			// Ok, no keyboard shortcut, process the message
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
	return 0;
}

/////////////////////////////////////////////////////////////
int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
	if( !g_app.Initialize(hInstance) )
	{
		return 1;
	}
	return g_app.Run();
}

