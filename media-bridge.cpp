#define UNICODE
#define _UNICODE
#include <windows.h>
#include <shellapi.h>
#include <SystemMediaTransportControlsInterop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.h>
#include <winrt/Windows.Media.Control.h>
#include <cstdio>
#include <share.h>
#include <string>
using namespace winrt;
using namespace Windows::Media;
static HWND target=nullptr,last=nullptr,host=nullptr;
static SystemMediaTransportControls controls{nullptr};
static FILE* logFile;
static bool playing=true;
static HANDLE stopEvent;
static bool enabled=true;
static UINT taskbarCreated;
static constexpr UINT trayMessage=WM_APP+2;
static constexpr UINT toggleCommand=40001,logCommand=40002,exitCommand=40003;
static NOTIFYICONDATAW tray={};
static HICON bridgeIcon=nullptr;
static std::wstring logPath;
static void UpdateTray(bool add=false){
 tray.cbSize=sizeof(tray);tray.hWnd=host;tray.uID=1;
 tray.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP|NIF_SHOWTIP;
 tray.uCallbackMessage=trayMessage;
 if(!bridgeIcon)bridgeIcon=(HICON)LoadImageW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(101),IMAGE_ICON,
   GetSystemMetrics(SM_CXSMICON),GetSystemMetrics(SM_CYSMICON),LR_DEFAULTCOLOR);
 tray.hIcon=bridgeIcon;
 std::wstring tip=L"Bluetooth Media Bridge - ";
 if(!enabled)tip+=L"disabled";
 else if(!target)tip+=L"waiting for VLC / MPC-HC";
 else {wchar_t cls[128]={};GetClassNameW(target,cls,128);tip+=wcsstr(cls,L"MediaPlayerClassic")?L"MPC-HC":L"VLC";}
 wcsncpy_s(tray.szTip,tip.c_str(),_TRUNCATE);
 if(!Shell_NotifyIconW(add?NIM_ADD:NIM_MODIFY,&tray)){
  fprintf(logFile,"Tray icon update failed: %lu\n",GetLastError());fflush(logFile);
 } else if(add){fprintf(logFile,"Tray icon registered\n");fflush(logFile);
 }
 if(add){tray.uVersion=NOTIFYICON_VERSION_4;Shell_NotifyIconW(NIM_SETVERSION,&tray);}
}
static void TrayMenu(){
 HMENU menu=CreatePopupMenu();if(!menu)return;
 AppendMenuW(menu,MF_STRING|MF_DISABLED,0,!enabled?L"Bridge disabled":target?L"Player connected":L"Waiting for VLC / MPC-HC");
 AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
 AppendMenuW(menu,MF_STRING|(enabled?MF_CHECKED:MF_UNCHECKED),toggleCommand,L"Enable bridge");
 AppendMenuW(menu,MF_STRING,logCommand,L"Open log");
 AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
 AppendMenuW(menu,MF_STRING,exitCommand,L"Exit");
 POINT pt;GetCursorPos(&pt);SetForegroundWindow(host);
 UINT command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY|TPM_RIGHTBUTTON,pt.x,pt.y,0,host,nullptr);
 DestroyMenu(menu);PostMessageW(host,WM_NULL,0,0);
 if(command)PostMessageW(host,WM_COMMAND,command,0);
}
static const wchar_t* stopName=L"Local\\HeadsetMediaBridge.Stop.v3";
static bool IsPlayer(HWND w){
 if(!w || !IsWindowVisible(w))return false;
 DWORD pid;GetWindowThreadProcessId(w,&pid);HANDLE p=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);if(!p)return false;
 wchar_t path[32768];DWORD n=32768;BOOL ok=QueryFullProcessImageNameW(p,0,path,&n);CloseHandle(p);if(!ok)return false;
 const wchar_t* base=wcsrchr(path,L'\\');base=base?base+1:path;
 return !_wcsicmp(base,L"vlc.exe") || !_wcsicmp(base,L"mpc-hc64.exe") || !_wcsicmp(base,L"mpc-hc.exe");
}
struct Candidates{HWND first=nullptr;int count=0;};
static BOOL CALLBACK Find(HWND w,LPARAM p){if(IsPlayer(w)&&!GetWindow(w,GW_OWNER)){auto c=(Candidates*)p;c->count++;if(!c->first)c->first=w;}return TRUE;}
static HWND Target(){HWND fg=GetForegroundWindow();if(IsPlayer(fg)){last=fg;return fg;}if(IsPlayer(last))return last;Candidates c;EnumWindows(Find,(LPARAM)&c);return c.count==1?c.first:nullptr;}
static LRESULT CALLBACK Window(HWND w,UINT m,WPARAM a,LPARAM b){
 if(taskbarCreated && m==taskbarCreated){UpdateTray(true);return 0;}
 if(m==trayMessage){
  UINT event=LOWORD(b);
  if(event==WM_CONTEXTMENU || event==NIN_SELECT || event==NIN_KEYSELECT)TrayMenu();
  return 0;
 }
 if(m==WM_COMMAND){
  if(LOWORD(a)==toggleCommand){enabled=!enabled;controls.IsEnabled(enabled&&target!=nullptr);UpdateTray();fprintf(logFile,"Bridge enabled=%d\n",enabled);fflush(logFile);return 0;}
  if(LOWORD(a)==logCommand){ShellExecuteW(w,L"open",logPath.c_str(),nullptr,nullptr,SW_SHOWNORMAL);return 0;}
  if(LOWORD(a)==exitCommand){DestroyWindow(w);return 0;}
 }
 if(m==WM_APP+1){
  auto button=(SystemMediaTransportControlsButton)a;
  if(!enabled || !target)return 0;
  wchar_t cls[128]={};GetClassNameW(target,cls,128);
  BOOL ok=FALSE;
  if(button==SystemMediaTransportControlsButton::Play || button==SystemMediaTransportControlsButton::Pause){
   ok=wcsstr(cls,L"MediaPlayerClassic")?PostMessageW(target,WM_COMMAND,889,0):PostMessageW(target,WM_APPCOMMAND,(WPARAM)target,LPARAM(14)<<16);
   if(ok){playing=!playing;controls.PlaybackStatus(playing?MediaPlaybackStatus::Playing:MediaPlaybackStatus::Paused);}
  } else if(button==SystemMediaTransportControlsButton::Next || button==SystemMediaTransportControlsButton::Previous){
   int command=button==SystemMediaTransportControlsButton::Next?11:12;
   ok=PostMessageW(target,WM_APPCOMMAND,(WPARAM)target,LPARAM(command)<<16);
  }
  fprintf(logFile,"Button=%d Delivered=%d Error=%lu\n",(int)button,ok,ok?0:GetLastError());fflush(logFile);return 0;
 }
 if(m==WM_TIMER){
  if(WaitForSingleObject(stopEvent,0)==WAIT_OBJECT_0){DestroyWindow(w);return 0;}
  HWND next=Target();if(next!=target){target=next;controls.IsEnabled(enabled&&target!=nullptr);UpdateTray();fprintf(logFile,"Target=%p\n",target);fflush(logFile);}
  return 0;
 }
 if(m==WM_DESTROY){KillTimer(w,1);Shell_NotifyIconW(NIM_DELETE,&tray);if(bridgeIcon){DestroyIcon(bridgeIcon);bridgeIcon=nullptr;}PostQuitMessage(0);return 0;}
 return DefWindowProcW(w,m,a,b);
}
int wmain(int argc,wchar_t**argv){
 if(argc>1 && !wcscmp(argv[1],L"--inspect")){
  init_apartment(apartment_type::multi_threaded);
  try{
   auto manager=Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
   for(auto const& session:manager.GetSessions()){
    auto props=session.TryGetMediaPropertiesAsync().get();
    if(props.Title()==L"VLC / MPC-HC headset controls"){
     printf("Bridge visible in Windows media sessions. Status=%d\n",(int)session.GetPlaybackInfo().PlaybackStatus());return 0;
    }
   }
   puts("Bridge session not found");return 4;
  }catch(hresult_error const&e){printf("Inspect error %08lx\n",(long)e.code());return 3;}
 }
 if(argc>1 && !wcscmp(argv[1],L"--stop")){HANDLE e=OpenEventW(EVENT_MODIFY_STATE,FALSE,stopName);if(e){SetEvent(e);CloseHandle(e);}return 0;}
 HANDLE mutex=CreateMutexW(nullptr,FALSE,L"Local\\HeadsetMediaBridge.Singleton.v3");if(GetLastError()==ERROR_ALREADY_EXISTS)return 0;
 wchar_t path[32768];GetModuleFileNameW(nullptr,path,32768);*wcsrchr(path,L'\\')=0;
 logPath=std::wstring(path)+L"\\media-bridge.log";
 logFile=_wfsopen(logPath.c_str(),L"w",_SH_DENYNO);if(!logFile)return 2;
 try{
  init_apartment(apartment_type::single_threaded);
  stopEvent=CreateEventW(nullptr,TRUE,FALSE,stopName);
  WNDCLASSW c={};c.lpfnWndProc=Window;c.hInstance=GetModuleHandleW(nullptr);c.lpszClassName=L"HeadsetMediaBridge";RegisterClassW(&c);
  host=CreateWindowW(c.lpszClassName,L"VLC / MPC-HC headset controls",WS_OVERLAPPED,0,0,0,0,nullptr,nullptr,c.hInstance,nullptr);
  auto interop=get_activation_factory<SystemMediaTransportControls,ISystemMediaTransportControlsInterop>();
  check_hresult(interop->GetForWindow(host,guid_of<SystemMediaTransportControls>(),put_abi(controls)));
  controls.IsPlayEnabled(true);controls.IsPauseEnabled(true);controls.IsNextEnabled(true);controls.IsPreviousEnabled(true);
  controls.ButtonPressed([](auto const&,SystemMediaTransportControlsButtonPressedEventArgs const&args){PostMessageW(host,WM_APP+1,(WPARAM)args.Button(),0);});
  auto display=controls.DisplayUpdater();display.Type(MediaPlaybackType::Music);display.MusicProperties().Title(L"VLC / MPC-HC headset controls");display.MusicProperties().Artist(L"Bluetooth media bridge");display.Update();
  target=Target();controls.IsEnabled(target!=nullptr);controls.PlaybackStatus(MediaPlaybackStatus::Playing);
  taskbarCreated=RegisterWindowMessageW(L"TaskbarCreated");UpdateTray(true);
  fprintf(logFile,"Windows media session ready. Target=%p\n",target);fflush(logFile);
  SetTimer(host,1,500,nullptr);MSG msg;while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
  controls.IsEnabled(false);controls.DisplayUpdater().ClearAll();
 }catch(hresult_error const&e){if(host&&IsWindow(host))DestroyWindow(host);fprintf(logFile,"ERROR %08lx\n",(long)e.code());fflush(logFile);return 3;}
 fclose(logFile);return 0;
}
