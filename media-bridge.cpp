#define UNICODE
#define _UNICODE
#include <windows.h>
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
 if(m==WM_APP+1){
  auto button=(SystemMediaTransportControlsButton)a;
  if(!target)return 0;
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
  HWND next=Target();if(next!=target){target=next;controls.IsEnabled(target!=nullptr);fprintf(logFile,"Target=%p\n",target);fflush(logFile);}
  return 0;
 }
 if(m==WM_DESTROY){PostQuitMessage(0);return 0;}
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
 logFile=_wfsopen((std::wstring(path)+L"\\media-bridge.log").c_str(),L"w",_SH_DENYNO);if(!logFile)return 2;
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
  fprintf(logFile,"Windows media session ready. Target=%p\n",target);fflush(logFile);
  SetTimer(host,1,500,nullptr);MSG msg;while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
  controls.IsEnabled(false);controls.DisplayUpdater().ClearAll();
 }catch(hresult_error const&e){fprintf(logFile,"ERROR %08lx\n",(long)e.code());fflush(logFile);return 3;}
 fclose(logFile);return 0;
}
