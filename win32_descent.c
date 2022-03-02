#include <stdio.h>
#include <windows.h>
#include "descent.h"

/**
 * MY_WS_FLAGS - Flags of main window when not fullscreen
 */
#define MY_WS_FLAGS (WS_VISIBLE | WS_SYSMENU | WS_CAPTION) 

/**
 * winmm_func - Function type of timeBeginPeriod and timeEndPeriod
 */
typedef MMRESULT WINAPI winmm_func(UINT);

/**
 * Values globals to WinMain
 * g_DIBInfo: Bitmap information for main pixel buffer
 * g_GameState: Holds platform independent game state
 */
static const BITMAPINFO g_DIBInfo = {
    .bmiHeader = {
        .biSize = sizeof(g_DIBInfo.bmiHeader),
        .biWidth = DIB_WIDTH,
        .biHeight = DIB_HEIGHT,
        .biPlanes = 1,
        .biBitCount = 32,
        .biCompression = BI_RGB
    }
};

static struct game_state *g_GameState;

/** 
 * LogError() - Logs error to error.log with timestamp 
 * @Format: printf style format string
 * @...: printf style format args 
 * 
 * Note: Do NOT pass dynamic string into format
 *       Instead pass as an argument where the format string is %s.
 *       Example:
 *           Bad: LogError(SomeError); 
 *           Good: LogError("%s", SomeError);
 *
 * Return: Returns zero on failure and nonzero on success
 */
__attribute__((format(printf, 1, 2)))
static BOOL LogError(const char *Format, ...) {
    /*OpenErrorHandle*/
    static HANDLE s_ErrorHandle = INVALID_HANDLE_VALUE;
    if(s_ErrorHandle == INVALID_HANDLE_VALUE) {
        s_ErrorHandle = CreateFile(
            "error.log",
            FILE_APPEND_DATA,
            FILE_SHARE_READ, 
            NULL, 
            OPEN_ALWAYS, 
            FILE_ATTRIBUTE_NORMAL, 
            NULL
        );
        if(s_ErrorHandle == INVALID_HANDLE_VALUE) {
            return FALSE;
        }
    }

    /*GetTimeString*/
    SYSTEMTIME Time;
    GetLocalTime(&Time);

    char Error[1024];
    int DateLength = sprintf_s(
        Error, 
        _countof(Error),
        "%04hu-%02hu-%02huT%02hu:%02hu:%02hu.%03hu ", 
        Time.wYear, 
        Time.wMonth, 
        Time.wDay, 
        Time.wHour, 
        Time.wMinute, 
        Time.wSecond, 
        Time.wMilliseconds
    );
    if(DateLength < 0) {
        return FALSE; 
    }

    /*AppendFormatString*/
    va_list FormatArgs;
    va_start(FormatArgs, Format);

    char *ErrorPtr = &Error[DateLength];
    int CharsLeft = (int) _countof(Error) - DateLength;
    int FormatLength = vsprintf_s(ErrorPtr, CharsLeft, Format, FormatArgs); 

    va_end(FormatArgs);
    if(FormatLength < 0) {
        return FALSE;
    }

    /*WriteError*/
    ErrorPtr[FormatLength] = '\n';
    int ToWrite = DateLength + FormatLength + 1;
    DWORD Written;
    return (
        WriteFile(s_ErrorHandle, Error, ToWrite, &Written, NULL) &&
        ToWrite == (int) Written 
    );
}

/**
 * MessageError() - Displays an error message box and logs an error 
 *
 * @Error: Error to display
 *
 * Context: Only to be used before successful window creation as
 *          message box is not a parentless.
 *
 * Note: On failure to log error, displays error message box
 */
static void MessageError(const char *Error) {
    MessageBox(NULL, Error, "Error", MB_ICONERROR); 
    if(!LogError(Error)) { 
        MessageBox(NULL, "LogError failed", "Error", MB_ICONERROR); 
    }
}

/**
 * LoadProcs() - Loads a library and some functions from that library
 *
 * @LibName: Library name 
 * @ProcCount: Number of functions
 * @ProcNames: The names of functions 
 * @Procs: The functions found
 *
 * Return: If successful, module handle to library, elsewise NULL
 */
[[nodiscard]]
static HMODULE LoadProcs(
    const char *LibName, 
    size_t ProcCount, 
    const char *ProcNames[static ProcCount],
    FARPROC Procs[static ProcCount]
) {
    HMODULE Library = LoadLibrary(LibName);
    if(!Library) {
        return NULL;
    }
    for(size_t I = 0; I < ProcCount; I++) {
        Procs[I] = GetProcAddress(Library, ProcNames[I]);
        if(!Procs[I]) {
            FreeLibrary(Library);
            return NULL; 
        } 
    }
    return Library;
} 

/**
 * GetPerfFreq() - Get processor performance frequency
 *
 * The performance frequency is the value of counter cycles per second. 
 * For example: If the performance frequency is 1000 and the 
 * the diffrence between two counters is 10000, than 10 s have passed. 
 *
 * Return: The performance frequency, always returns same value
 */
static int64_t GetPerfFreq(void) {
    LARGE_INTEGER PerfFreq;
    QueryPerformanceFrequency(&PerfFreq);
    return PerfFreq.QuadPart;
}

/**
 * GetPerfCounter() - Gets performance counter 
 *
 * Performance counters are meaningingless by themselves however the
 * difference between two counters obtained at two diffrent times 
 * gives the change in time. See GetPerfFreq() for details
 *
 * Return: Returns performance counter
 */ 
static int64_t GetPerfCounter(void) {
    LARGE_INTEGER PerfCounter;
    QueryPerformanceCounter(&PerfCounter);
    return PerfCounter.QuadPart;
}

/**
 * GetDeltaCounter() - Gets the change in counter 
 * @BeginCounter: Point in past to be measured from 
 *
 * Return: Returns the diffrence between current counter and counter passed in 
 */
static int64_t GetDeltaCounter(int64_t BeginCounter) {
    return GetPerfCounter() - BeginCounter;
}

/**
 * SetWindowState() - Function to change window style, position, and size
 * @Window: Window handle
 * @Style: Window style to be set to
 * @X: New left most coordinate of window
 * @Y: New top most coordiante of window 
 * @Width: New width of window
 * @Height: New height of window
 */
static void SetWindowState(
    HWND Window, 
    DWORD Style, 
    int X,
    int Y,
    int Width,
    int Height
) {
    SetWindowLongPtr(Window, GWL_STYLE, Style);
    SetWindowPos(
        Window, 
        HWND_TOP, 
        X, 
        Y, 
        Width, 
        Height, 
        SWP_FRAMECHANGED
    );
}

/**
 * FindButtonFromKey() - Finds the "button" for virtual key, if any
 * @VKey: Virtual key 
 *
 * Return: If "button" found returns pointer to button, elsewise NULL
 */
static uint32_t *FindButtonFromKey(size_t VKey) {
    static const uint8_t s_KeyUsed[COUNTOF_BT] = {
        VK_LEFT,
        VK_UP,
        VK_RIGHT,
        VK_DOWN,
    }; 

    for(int I = 0; I < COUNTOF_BT; I++) {
        if(VKey == s_KeyUsed[I]) {
            return &g_GameState->Buttons[I];
        } 
    }
    return NULL;
}

/**
 * ToggleFullscreen() - Toggles whether the program is in fullscreen 
 * @Window: Main window handle
 *
 * Return: Reports if change was successful
 */
static BOOL ToggleFullscreen(HWND Window) {
    static RECT RestoreRect = {0}; 
    static BOOL IsFullscreen = FALSE;
    static DEVMODE PrevDevMode = {
        .dmSize = sizeof(PrevDevMode)
    };

    /*RestoreFromFullscreen*/
    if(IsFullscreen) { 
        LONG DispState = ChangeDisplaySettings(&PrevDevMode, CDS_FULLSCREEN);
        if(DispState != DISP_CHANGE_SUCCESSFUL) {
            return FALSE;
        }
        SetWindowState(
            Window, 
            MY_WS_FLAGS, 
            RestoreRect.left, 
            RestoreRect.top,
            DIB_WIDTH,
            DIB_HEIGHT
        ); 
        IsFullscreen = FALSE;
        return TRUE;
    } 

    /*MakeFullscreen*/
    if(!EnumDisplaySettings(NULL, ENUM_CURRENT_SETTINGS, &PrevDevMode)) {
        return FALSE;
    }
    GetWindowRect(Window, &RestoreRect);

    DEVMODE DevMode = {
        .dmSize = sizeof(DevMode),
        .dmFields = DM_PELSWIDTH | DM_PELSHEIGHT,
        .dmPelsWidth = DIB_WIDTH,
        .dmPelsHeight = DIB_HEIGHT
    };
    LONG DispState = ChangeDisplaySettings(&DevMode, CDS_FULLSCREEN);
    SetWindowState(Window, WS_POPUP | WS_VISIBLE, 0, 0, DIB_WIDTH, DIB_HEIGHT); 

    IsFullscreen = TRUE;
    return DispState == DISP_CHANGE_SUCCESSFUL;
}

/**
 * WndProc() - Handles messages that cannot be processed by ProcessMessages
 * @Window: The main window handle
 * @Message: Message to process 
 * @WParam: Generic integer value that changes meaning depending on message
 * @LParam: Generic integer value that changes meaning depending on message
 *
 * Use the search feature on https://docs.microsoft.com/en-us/windows/win32/ 
 * to find more about specific messages. 
 *
 * Return: Result of function
 *
 * For most messages processed zero should be returned.
 * For messages not processed DefWindowProc should be called.
 */
static LRESULT WndProc(
    HWND Window, 
    UINT Message, 
    WPARAM WParam, 
    LPARAM LParam
) {
    switch(Message) {
    case WM_DESTROY:
        {
            PostQuitMessage(EXIT_SUCCESS);
        } return 0;
    case WM_PAINT:
        {
            PAINTSTRUCT Paint;
            HDC DeviceContext = BeginPaint(Window, &Paint);
            SetDIBitsToDevice(
                DeviceContext,
                0,
                0,
                DIB_WIDTH,
                DIB_HEIGHT,
                0,
                0,
                0U,
                DIB_HEIGHT,
                g_GameState->Pixels,
                &g_DIBInfo,
                DIB_RGB_COLORS
            );
            EndPaint(Window, &Paint);
        } return 0;
    }
    return DefWindowProc(Window, Message, WParam, LParam);
}

/**
 * WinMain() - Entry point to windows program 
 * @Instance: A unique handle that identifies the process
 * @PrevInstance: A holdover from Win16, always NULL
 * @CmdLine: Full command line string (ex: .\descent.exe -mute) 
 * @CmdShow: Holds value for default visibility of window, ignored 
 *
 * For more information on @PrevInstance visit:
 * https://devblogs.microsoft.com/oldnewthing/20040615-00/?p=38873
 *
 * Return: Process exit code; either EXIT_FAILURE or EXIT_SUCCESS
 */
int WINAPI WinMain(
    HINSTANCE Instance, 
    [[maybe_unused]] HINSTANCE PrevInstance, 
    [[maybe_unused]] LPSTR CmdLine, 
    [[maybe_unused]] int CmdShow
) {
    /*SetUpTiming*/
    const int64_t PerfFreq = GetPerfFreq(); 
    const int64_t FinalDeltaCounter = PerfFreq / 60LL;

    /*AllocGameState*/
    g_GameState = VirtualAlloc( 
        NULL, 
        sizeof(*g_GameState), 
        MEM_COMMIT | MEM_RESERVE,
        PAGE_READWRITE
    );
    if(!g_GameState) {
        MessageError("VirtualAlloc failed");
        return EXIT_FAILURE;
    }

    /*InitWindowClass*/
    WNDCLASS WindowClass = {
        .style = CS_HREDRAW | CS_VREDRAW,
        .lpfnWndProc = WndProc,
        .hInstance = Instance,
        .hCursor = LoadCursor(NULL, IDI_APPLICATION),
        .lpszClassName = "GameWindowClass"
    }; 
    if(!RegisterClass(&WindowClass)) {
        MessageError("RegisterClass failed");
        return EXIT_FAILURE;
    }

    /*InitWindow*/
    RECT WindowRect = {
        .right = DIB_WIDTH, 
        .bottom = DIB_HEIGHT 
    };
    AdjustWindowRect(&WindowRect, MY_WS_FLAGS, FALSE);

    HWND Window = CreateWindow(
        WindowClass.lpszClassName, 
        "Game", 
        MY_WS_FLAGS,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        WindowRect.right - WindowRect.left,
        WindowRect.bottom - WindowRect.top,
        NULL,
        NULL,
        Instance,
        NULL
    );
    if(!Window) {
        MessageError("CreateWindow failed"); 
        return EXIT_FAILURE;
    }

    /*LoadWinmm*/
    union {
        FARPROC Procs[2];
        struct {
            winmm_func *TimeBeginPeriod;
            winmm_func *TimeEndPeriod;
        };
    } WinmmProcs;

    BOOL IsGranular = FALSE;
    HMODULE WinmmLibrary = LoadProcs(
        "winmm.dll",
        2,
        (const char *[]) {
            "timeBeginPeriod",
            "timeEndPeriod"
        },
        WinmmProcs.Procs
    );
    if(WinmmLibrary) {
        IsGranular = (WinmmProcs.TimeBeginPeriod(1) == TIMERR_NOERROR); 
        if(!IsGranular) {
            FreeLibrary(WinmmLibrary);
            WinmmLibrary = NULL;
            LogError("timeBeginPeriod failed"); 
        }
    } else {
        LogError("LoadProcs failed: winmm.dll"); 
    }

    /*LoadGameCode*/
    union {
        FARPROC Proc; 
        game_update *Update; 
    } GameProcs;

    HMODULE GameLibrary = LoadProcs(
        "descent.dll", 
        1, 
        (const char *[]) {
            "GameUpdate"
        },
        &GameProcs.Proc
    );
    if(!GameLibrary) {
        LogError("LoadProcs failed: descent.dll"); 
    }

    /*MainLoop*/
    bool IsRunning = 1;
    while(IsRunning) {
        int64_t BeginCounter = GetPerfCounter();

        /*ProcessMessages*/
        MSG Message;
        while(PeekMessage(&Message, NULL, 0U, 0U, PM_REMOVE)) {
            switch(Message.message) {
            case WM_KEYDOWN:
                {
                    size_t KeyI = Message.wParam;
                    if(KeyI == VK_F11) { 
                        if(!ToggleFullscreen(Window)) {
                            MessageError("ToggleFullscreen failed"); 
                        }
                    } else {
                        uint32_t *Key = FindButtonFromKey(Message.wParam);
                        if(Key && *Key < INT32_MAX) {
                            (*Key)++;
                        }
                    }
                } break;
            case WM_KEYUP:
                {
                    uint32_t *Key = FindButtonFromKey(Message.wParam);
                    if(Key) {
                        *Key = 0;
                    }
                } break;
            case WM_QUIT:
                {
                    IsRunning = 0;
                } break;
            default:
                {
                    TranslateMessage(&Message);
                    DispatchMessage(&Message);
                } break;
            }
        }

        /*UpdateGameCode*/
        if(GameLibrary) {
            GameProcs.Update(g_GameState);
        }

        /*UpdateFrame*/
        InvalidateRect(Window, NULL, FALSE);

        /*SleepTillNextFrame*/
        int64_t DeltaCounter = GetDeltaCounter(BeginCounter);
        if(DeltaCounter < FinalDeltaCounter) {
            if(IsGranular) {
                int64_t RemainCounter = FinalDeltaCounter - DeltaCounter;
                DWORD SleepMS = (DWORD) (1000LL * RemainCounter / PerfFreq);
                if(SleepMS > 0UL) {
                    Sleep(SleepMS);
                }
            }
            while(GetDeltaCounter(BeginCounter) < FinalDeltaCounter);
        }
    }

    /*CleanUp*/
    if(WinmmLibrary) {
        WinmmProcs.TimeEndPeriod(1);
    }
    return EXIT_SUCCESS;
}
