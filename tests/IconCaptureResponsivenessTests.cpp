// Exercise the production message loop without hooks, Explorer, settings, or effects.
#define wWinMain PenumbraApplicationMain
#include "../src/Nocturne.Renderer/main.cpp"
#undef wWinMain

namespace {
constexpr UINT PingMessage=WM_APP+99;
unsigned pings=0;
HANDLE workerEntered{},releaseWorker{};

LRESULT CALLBACK FixtureProc(HWND window,UINT message,WPARAM wParam,LPARAM lParam) {
    if(message==PingMessage) { ++pings; return 0; }
    return DefWindowProcW(window,message,wParam,lParam);
}

bool CaptureFixture(IconMaskCapture& capture) {
    capture.result.rect=capture.rect;
    capture.result.width=capture.rect.right-capture.rect.left;
    capture.result.height=capture.rect.bottom-capture.rect.top;
    capture.result.bits.assign((capture.result.width*capture.result.height+7)/8,1);
    return true;
}

bool BlockedCapture(IconMaskCapture& capture) {
    SetEvent(workerEntered);
    return WaitForSingleObject(releaseWorker,5000)==WAIT_OBJECT_0&&CaptureFixture(capture);
}

int Check(bool success,const char* message) {
    std::printf("%s: %s\n",success?"PASS":"FAIL",message);
    return success?0:1;
}
}

int main() {
    stopEvent=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    reloadEvent=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    workerEntered=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    releaseWorker=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    if(!stopEvent||!reloadEvent||!workerEntered||!releaseWorker) return 1;
    iconMaskShareName=L"Local\\Penumbra.IconCaptureResponsiveness.Test";
    instance=GetModuleHandleW(nullptr);
    WNDCLASSW cls{};
    cls.hInstance=instance;
    cls.lpfnWndProc=FixtureProc;
    cls.lpszClassName=L"Penumbra.IconCaptureFixture";
    if(!RegisterClassW(&cls)) return 2;
    controller=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,cls.lpszClassName,L"",WS_POPUP,
        0,0,1,1,nullptr,nullptr,instance,nullptr);
    iconList=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,cls.lpszClassName,L"",WS_POPUP|WS_VISIBLE,
        -10000,-10000,16,16,nullptr,nullptr,instance,nullptr);
    if(!controller||!iconList) return 3;
    // Keep the test focused on the mask and message loop, with no production IPC.
    const auto savedIconList=iconList;
    iconList=nullptr;
    int failures=Check(!StartIconCapture(CaptureFixture),"Missing desktop icon view never starts a worker");
    iconList=savedIconList;
    params.opacity=0;
    if(!StartIconCapture(BlockedCapture)||WaitForSingleObject(workerEntered,1000)!=WAIT_OBJECT_0) return 4;
    for(int i=0;i<1000;++i) if(StartIconCapture(BlockedCapture)) return 5;
    failures+=Check(true,"Repeated requests do not create more capture workers");
    waitingForInitialIconMask=true;
    PostMessageW(controller,PingMessage,0,0);
    const ULONGLONG start=GetTickCount64();
    while(!pings&&GetTickCount64()-start<300) if(!DispatchWork(10)) return 6;
    failures+=Check(pings==1&&WaitForSingleObject(iconCaptureThread,0)==WAIT_TIMEOUT,
        "Controller handles messages while the capture is blocked");
    while(iconCaptureDeadline&&GetTickCount64()-start<2200) if(!DispatchWork(25)) return 7;
    failures+=Check(!waitingForInitialIconMask&&iconMaskUnavailable&&iconCapture->cancelled.load(),
        "Initial capture times out and releases the vignette without waiting for the worker");
    SetEvent(stopEvent);
    failures+=Check(!DispatchWork(100)&&WaitForSingleObject(iconCaptureThread,0)==WAIT_TIMEOUT,
        "Stop is processed with the worker still blocked");
    SetEvent(releaseWorker);
    if(WaitForSingleObject(iconCaptureThread,1000)!=WAIT_OBJECT_0||!DispatchWork(50)) return 8;
    failures+=Check(!iconCaptureThread&&!iconMaskWindow&&iconMask.bits.empty(),"Late results are discarded");

    // A normal result can follow a timed-out capture once the worker completes.
    if(!StartIconCapture(CaptureFixture)||WaitForSingleObject(iconCaptureThread,1000)!=WAIT_OBJECT_0) return 9;
    animationTimerArmed=true;
    animationTick=CreateWaitableTimerW(nullptr,FALSE,nullptr);
    if(!animationTick||!DispatchWork(50)) return 10;
    failures+=Check(readyIconCapture&&iconMask.bits.empty(),"Mask upload waits for the animation to finish");
    StopAnimation();
    // Disable protection before applying the queued result. It must be discarded.
    params.darkenIcons=true;
    ApplyCapturedIcons();
    failures+=Check(!readyIconCapture&&iconMask.bits.empty(),"Changing icon settings invalidates a queued result");
    params.darkenIcons=false;
    if(!StartIconCapture(CaptureFixture)||WaitForSingleObject(iconCaptureThread,1000)!=WAIT_OBJECT_0||!DispatchWork(50)) return 11;
    failures+=Check(iconMaskWindow==iconList&&iconMask.bits.size()==32&&!iconMaskUnavailable,
        "A fresh capture restores the protected mask");
    CancelIconCapture();
    ReleaseIconMaskShare();
    DestroyWindow(iconList);
    DestroyWindow(controller);
    CloseHandle(workerEntered);
    CloseHandle(releaseWorker);
    CloseHandle(reloadEvent);
    CloseHandle(stopEvent);
    return failures?1:0;
}
