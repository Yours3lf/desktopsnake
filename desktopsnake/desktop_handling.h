#ifndef desktop_handling_h
#define desktop_handling_h

#include <iostream>
#include <vector>
#include <list>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <time.h>

using namespace std;

#define WIN32_LEAN_AND_MEAN //exclude useless garbage
#include <Windows.h>
#include <CommCtrl.h>
#include <Shlobj.h>

union LOHIWORD
{
  struct
  {
    WORD loword, hiword;
  };

  DWORD wholeword;
};

struct coord
{
  int x, y;

  coord( int xx = int(), int yy = int() ) : x( xx ), y( yy ) {}
};

HWND get_desktop_listview_handle()
{
  //get listview of desktop icons
  HWND handle = FindWindowA( "Progman", "Program Manager" );
  handle = FindWindowExA( handle, 0, "SHELLDLL_DefView", 0 );
  handle = FindWindowExA( handle, 0, "SysListView32", "FolderView" );
  return handle;
}

void get_original_icon_locations( HWND handle, vector<coord>& container )
{
  LRESULT num_icons = 0;
  DWORD process_id = 0;
  HANDLE proc_handle = 0;
  HANDLE proc_memory = 0;

  //get total count of the icons on the desktop
  num_icons = SendMessageA( handle, LVM_GETITEMCOUNT, 0, 0 );

  //get desktop icon thread id
  process_id = 0;
  GetWindowThreadProcessId( handle, &process_id );

  //get desktop process handle
  proc_handle = OpenProcess( PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE, false, process_id );
  //allocate memory to be able to manipulate the desktop
  proc_memory = VirtualAllocEx( proc_handle, 0, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE );

  SIZE_T num_bytes_read = 0;

  for( LRESULT c = 0; c < num_icons; ++c )
  {
    //get icon location
    POINT point;
    SendMessageA( handle, LVM_GETITEMPOSITION, c, (LPARAM)proc_memory );
    ReadProcessMemory( proc_handle, proc_memory, &point, sizeof(POINT), &num_bytes_read );

    container.push_back( coord( point.x, point.y ) );
  }

  //free process memory
  VirtualFreeEx( proc_handle, proc_memory, 0, MEM_RELEASE );
  CloseHandle( proc_handle );
}

void set_icon_position( HWND handle, int idx, coord pos )
{
  LOHIWORD tmp;
  tmp.loword = pos.x;
  tmp.hiword = pos.y;
  SendMessageA( handle, LVM_SETITEMPOSITION, idx, tmp.wholeword );
}

void get_icon_extents( HWND handle, coord& c )
{
  //get spacing (icon w/h)
  LRESULT spacing = SendMessageA( handle, LVM_GETITEMSPACING, FALSE, 0 );
  c.x = LOWORD(spacing);
  c.y = HIWORD(spacing);
}

void get_screen_size( coord& c )
{
  HWND tmp = GetDesktopWindow();
  RECT rect;
  GetWindowRect( tmp, &rect );
  c.x = rect.right;
  c.y = rect.bottom;
}

void set_snap_to_grid( HWND handle, bool tf )
{
  //set snap-to-grid
  SendMessageA( handle, LVM_SETEXTENDEDLISTVIEWSTYLE, LVS_EX_SNAPTOGRID, tf ? ~0 : 0 );
}

bool get_snap_to_grid( HWND handle )
{
  return SendMessageA( handle, LVM_GETEXTENDEDLISTVIEWSTYLE, 0, 0 ) & LVS_EX_SNAPTOGRID;
}

void set_console_visibility( bool sh )
{
  ShowWindow( GetConsoleWindow(), sh ? SW_RESTORE : SW_MINIMIZE );
}

string get_desktop_path()
{
  char path[MAX_PATH];
  SHGetSpecialFolderPathA(0, path, CSIDL_DESKTOP, false);
  return string((char*)path);
}

//magic numbers
#define MIN_ALL        419
#define MIN_ALL_UNDO   416

void set_windows_visibility( bool vis )
{
  HWND hwnd = FindWindowA( "Shell_TrayWnd", NULL );
  SendMessage( hwnd, WM_COMMAND, vis ? MIN_ALL_UNDO : MIN_ALL, 0 );
  Sleep(2000);
}

class timer
{
  FILETIME time;
public:
  void reset()
  {
    GetSystemTimeAsFileTime( &time );
  }

  //return the elapsed time in milliseconds
  unsigned get_elapsed_time()
  {
    timer tmr;

    ULONGLONG t;
    t = ((ULONGLONG)time.dwHighDateTime << 32) | (ULONGLONG)time.dwLowDateTime;
    t /= 10000;

    ULONGLONG t2;
    t2 = ((ULONGLONG)tmr.time.dwHighDateTime << 32) | (ULONGLONG)tmr.time.dwLowDateTime;
    t2 /= 10000;

    return t2 - t;
  }

  timer(){ reset(); }
};

string get_win32_error()
{
  DWORD error = GetLastError();

  if( error )
  {
    LPVOID msg_buf;
    DWORD buf_len = FormatMessage( 
      FORMAT_MESSAGE_ALLOCATE_BUFFER | 
      FORMAT_MESSAGE_FROM_SYSTEM |
      FORMAT_MESSAGE_IGNORE_INSERTS,
      NULL,
      error,
      MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
      (LPTSTR)&msg_buf, 0, NULL );

    if( buf_len )
    {
      LPCSTR msg_str = (LPCSTR)msg_buf;
      string result( msg_str, msg_str + buf_len );

      LocalFree( msg_buf );

      char buf[64] = {0};
      _itoa_s( error, buf, 10 );
      return result + "code: " + buf;
    }
  }

  return "";
}

#endif