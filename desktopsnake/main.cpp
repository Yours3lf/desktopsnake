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

//desktop icons listview handle
HWND win_handle = 0;

//original icon locations (to restore icons later)
vector<coord> icon_pos;
//icon extents
coord icon_extents;
//screen size
coord screen_size;
//did we originally have snap to grid?
bool has_snaptogrid;

string desktop_path;

const coord grid_correction( 17, 2 );

HWND get_desktop_listview_handle()
{
  //get listview of desktop icons
  HWND handle = FindWindowA( "Progman", "Program Manager" );
  handle = FindWindowExA( handle, 0, "SHELLDLL_DefView", 0 );
  handle = FindWindowExA( handle, 0, "SysListView32", "FolderView" );
  return handle;
}

void get_original_icon_locations( vector<coord>& container )
{
  LRESULT num_icons = 0;
  DWORD process_id = 0;
  HANDLE proc_handle = 0;
  HANDLE proc_memory = 0;

  //get total count of the icons on the desktop
  num_icons = SendMessageA( win_handle, LVM_GETITEMCOUNT, 0, 0 );

  //get desktop icon thread id
  process_id = 0;
  GetWindowThreadProcessId( win_handle, &process_id );

  //get desktop process handle
  proc_handle = OpenProcess( PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE, false, process_id );
  //allocate memory to be able to manipulate the desktop
  proc_memory = VirtualAllocEx( proc_handle, 0, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE );

  SIZE_T num_bytes_read = 0;

  for( LRESULT c = 0; c < num_icons; ++c )
  {
    //get icon location
    POINT point;
    SendMessageA( win_handle, LVM_GETITEMPOSITION, c, (LPARAM)proc_memory );
    ReadProcessMemory( proc_handle, proc_memory, &point, sizeof(POINT), &num_bytes_read );

    container.push_back( coord( point.x, point.y ) );
  }

  //free process memory
  VirtualFreeEx( proc_handle, proc_memory, 0, MEM_RELEASE );
  CloseHandle( proc_handle );
}

void set_icon_position( int idx, coord pos )
{
  LOHIWORD tmp;
  tmp.loword = pos.x;
  tmp.hiword = pos.y;
  SendMessageA( win_handle, LVM_SETITEMPOSITION, idx, tmp.wholeword );
}

void get_icon_extents( coord& c )
{
  //get spacing (icon w/h)
  LRESULT spacing = SendMessageA( win_handle, LVM_GETITEMSPACING, FALSE, 0 );
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

void set_snap_to_grid( bool tf )
{
  //set snap-to-grid
  SendMessageA( win_handle, LVM_SETEXTENDEDLISTVIEWSTYLE, LVS_EX_SNAPTOGRID, tf ? ~0 : 0 );
}

bool get_snap_to_grid()
{
  return SendMessageA( win_handle, LVM_GETEXTENDEDLISTVIEWSTYLE, 0, 0 ) & LVS_EX_SNAPTOGRID;
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
  HWND lHwnd = FindWindowA( "Shell_TrayWnd", NULL );
  SendMessage( lHwnd, WM_COMMAND, vis ? MIN_ALL_UNDO : MIN_ALL, 0 );
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

//-----------------------------------------------
//gamecode
//-----------------------------------------------

enum blocktype
{
  SNAKE, FOOD, EMPTY
};

enum direction
{
  LEFT, RIGHT, UP, DOWN, ESCAPE
};

bool gameover = false;

vector<string> deletables;
vector<vector<blocktype> > snakemap;
list<coord> snake;

unsigned map_width = 0, map_height = 0;

unsigned food_idx = 0;
coord food_pos;

coord dummy;
coord& snakehead_pos = dummy; //u so stupid hoe

direction dir = RIGHT;
direction last_dir = RIGHT;

bool inv_dir = false;
unsigned food_counter = 0;
float gamespeed = 4;
float initial_gamespeed = gamespeed;
float inc_amount = 16;

//settings
bool die_on_wall = true,
     snake_gets_faster = true,
     inverse_movement = true;

void register_hotkeys()
{
  RegisterHotKey( 0, LEFT, 0, VK_LEFT );
  RegisterHotKey( 0, RIGHT, 0, VK_RIGHT );
  RegisterHotKey( 0, UP, 0, VK_UP );
  RegisterHotKey( 0, DOWN, 0, VK_DOWN );
  RegisterHotKey( 0, ESCAPE, 0, VK_ESCAPE );
}

void unregister_hotkeys()
{
  UnregisterHotKey( 0, LEFT );
  UnregisterHotKey( 0, RIGHT );
  UnregisterHotKey( 0, UP );
  UnregisterHotKey( 0, DOWN );
  UnregisterHotKey( 0, ESCAPE );
}

coord to_screenpos( coord snakemappos )
{
  return coord( snakemappos.x * icon_extents.x + grid_correction.x, snakemappos.y * icon_extents.y + grid_correction.y );
}

//ideas
//-inverse controls --- done
//-no passing through walls --- done
//-display score
//-display record break event
//-display inverse gameplay
//-game features question?
//-logo in at startup

void place_food()
{
  while( true )
  {
    coord newcoord = coord( rand() % map_width, rand() % map_height );

    blocktype& newpos = snakemap[newcoord.x][newcoord.y];
    if( newpos == EMPTY )
    {
      newpos = FOOD;
      food_pos = newcoord;
      return;
    }
  }
}

void step_game()
{
  last_dir = dir;

  //move dat snake
  if( dir == UP )
    --snakehead_pos.y;
  else if( dir == DOWN )
    ++snakehead_pos.y;
  else if( dir == LEFT )
    --snakehead_pos.x;
  else if( dir == RIGHT )
    ++snakehead_pos.x;

  bool wall = false;

  //check if snake is offscreen
  if( snakehead_pos.x < 0 )
  {
    snakehead_pos.x = map_width - 1;
    wall = true;
  }
  
  if( snakehead_pos.y < 0 )
  {
    snakehead_pos.y = map_height - 1;
    wall = true;
  }

  if( snakehead_pos.x >= map_width )
  {
    snakehead_pos.x = 0;
    wall = true;
  }

  if( snakehead_pos.y >= map_height )
  {
    snakehead_pos.y = 0;
    wall = true;
  }

  if( die_on_wall && wall )
  {
    gameover = true;
    return;
  }

  //game logic
  blocktype nextblock = snakemap[snakehead_pos.x][snakehead_pos.y];

  if( nextblock == FOOD )
  {
    //dont remove last snake block
    place_food();
    set_icon_position( food_idx, to_screenpos( food_pos ) );
    
    if( snake_gets_faster )
    {
      gamespeed += inc_amount * 0.01;
    }

    ++food_counter;

    if( food_counter % 10 == 0 )
    {
      inv_dir = !inv_dir;
    }
  }
  else if( nextblock == SNAKE ) 
  {
    //gameover
    gameover = true;
    return;
  }
  else
  {
    //empty, remove last
    coord lastpos = *(--snake.end());
    snakemap[lastpos.x][lastpos.y] = EMPTY;
    snake.erase( --snake.end() );
  }

  snake.insert( snake.begin(), snakehead_pos );
  snakemap[snakehead_pos.x][snakehead_pos.y] = SNAKE;

  int cntr = 0;
  for( auto c : snake )
    set_icon_position( cntr++, to_screenpos( c )  );
}

void init_game()
{
  for( auto& c : snakemap )
  {
    c.clear();
    c.assign( map_height, EMPTY );
  }

  gamespeed = initial_gamespeed;
  food_counter = 0;
  inv_dir = false;
  dir = RIGHT;

  //move all icons off-sceen
  int cntr = 0;
  for( auto c : icon_pos )
    set_icon_position( cntr++, coord( -100, 0 ) );

  set_console_visibility( false );

  //set snake head
  snake.clear();
  snake.push_back( coord() );
  snakehead_pos = *snake.begin();
  snakemap[snakehead_pos.x][snakehead_pos.y] = SNAKE;

  //set up food
  place_food();
  set_icon_position( food_idx, to_screenpos( food_pos ) );
}

struct score
{
  string name;
  unsigned num;

  bool operator<( const score& a )
  {
    return a.num < num;
  }
  
  score( string n = string(), unsigned s = unsigned() ) : name( n ), num( s ) {}
};

void write_settings()
{
  ofstream of( "settings.dat" );

  if( !of.is_open() )
    return;

  of.write( (char*)&die_on_wall, sizeof(die_on_wall) );
  of.write( (char*)&inverse_movement, sizeof(inverse_movement) );
  of.write( (char*)&snake_gets_faster, sizeof(snake_gets_faster) );

  of.close();
}

void read_settings()
{
  ifstream f( "settings.dat" );

  if( !f.is_open() )
    return;

  f.read( (char*)&die_on_wall, sizeof(die_on_wall) );
  f.read( (char*)&inverse_movement, sizeof(inverse_movement) );
  f.read( (char*)&snake_gets_faster, sizeof(snake_gets_faster) );

  f.close();
}

void display_highscores()
{
  cout << endl;

  ifstream f( "highscore.txt" );

  if( !f.is_open() )
    return;

  vector<string> lines;
  while( !f.eof() )
  {
    lines.resize( lines.size() + 1 );
    getline( f, lines[lines.size() - 1] );
  }
  f.close();

  vector<score> scores;

  for( auto& c : lines )
  {
    string scorestr = c.substr( c.find( " " ) + 1, string::npos );
    unsigned scoreint = atoi( scorestr.c_str() );
    scores.push_back( score( c.substr( 0, c.find( " " ) ), scoreint ) );
  }

  sort( scores.begin(), scores.end() );

  cout << "Highscores: " << endl;

  int counter = 1;
  for( auto c : scores )
    if( !c.name.empty() && counter < 11 )
      cout << "#" << counter++ << " name: " << c.name << " score: " << c.num << endl;

  cout << endl;
}

bool ask_question()
{
  while( true )
  {
    char ret;
    cin >> ret;

    if( ret == 'n' )
    {
      Sleep(250);
      return false;
    }
    else if( ret == 'y' )
    {
      Sleep(250);
      cin.clear();
      return true;
    }
    else
      cout << "Come on, y or n?" << endl;
  }
}

int main( int argc, char** args )
{
  cout << "Desktop Snake Game" << endl
       << "Move with arrows" << endl
       << "Exit with escape" << endl
       << "Created by Marton Tamas in 2013" << endl;

  display_highscores();

  read_settings();

  cout << "Die when collide with wall? " << (die_on_wall ? "yes" : "no") << endl;
  cout << "Snake gets faster when eating food? " << (snake_gets_faster ? "yes" : "no") << endl;
  cout << "Inverse movement after eating 10 food? " << (inverse_movement ? "yes" : "no") << endl;
  cout << endl << "Set gameplay options?" << endl;
  bool set_gameplay_options = ask_question();
       
  if( set_gameplay_options )
  {
    cout << "Die when collide with wall?" << endl;
    die_on_wall = ask_question();

    cout << "Snake gets faster when eating food?" << endl;
    snake_gets_faster = ask_question();

    cout << "Inverse movement after eating 10 food?" << endl;
    inverse_movement = ask_question();

    write_settings();
  }

  cout << "Press enter to start playing." << endl;
  cin.get();

  cout << "Initializing, please wait..." << endl << endl;

  //init
  srand( time(0) ); //init randomizer
  desktop_path = get_desktop_path() + "/";
  win_handle = get_desktop_listview_handle();
  has_snaptogrid = get_snap_to_grid();
  get_original_icon_locations( icon_pos );
  get_icon_extents( icon_extents );
  get_screen_size( screen_size );
  set_snap_to_grid( false );
  register_hotkeys();

  //--------
  //GAMEPLAY
  //--------
  bool run = true;
  timer clock;

  //get map size
  unsigned w = screen_size.x / icon_extents.x;
  unsigned h = screen_size.y / icon_extents.y;

  snakemap.resize( w );
  for( auto& c : snakemap )
    c.assign( h, EMPTY );

  unsigned block_count = w * h;

  map_width = w;
  map_height = h;

  //insert remaining desktop icons (if desktop is not full of icons)
  for( int c = icon_pos.size(); c <= block_count; ++c )
  {
    stringstream ss;
    ss << "snakeblock " << c << ".snk";

    string path = desktop_path + ss.str();

    ofstream of(path.c_str());
    of.close();

    deletables.push_back(path);
  }

  //wait for desktop to update
  Sleep(3000); 

  //reacquire icon locations
  icon_pos.clear();
  get_original_icon_locations( icon_pos );

  food_idx = icon_pos.size() - 1;

  init_game();

  set_windows_visibility( false );

  while( run )
  {
    timer sleeper;

    //input handling
    MSG msg = {0};
    while( PeekMessageA( &msg, 0, 0, 0, PM_REMOVE ) != 0 )
    {
      if( msg.message == WM_HOTKEY )
      {
        if( inverse_movement && inv_dir )
        {
          if( msg.wParam == LEFT )
            msg.wParam = RIGHT;
          else if( msg.wParam == RIGHT )
            msg.wParam = LEFT;
          else if( msg.wParam == UP )
            msg.wParam = DOWN;
          else if( msg.wParam == DOWN )
            msg.wParam = UP;
        }

        if( msg.wParam == LEFT && last_dir != RIGHT )
          dir = LEFT;
        else if( msg.wParam == RIGHT && last_dir != LEFT )
          dir = RIGHT;
        else if( msg.wParam == UP && last_dir != DOWN )
          dir = UP;
        else if( msg.wParam == DOWN && last_dir != UP )
          dir = DOWN;
        else if( msg.wParam == ESCAPE )
          run = false;
      }
    }

    if( clock.get_elapsed_time() > 1000 / gamespeed )
    {
      step_game();

      if( gameover )
      {
        set_console_visibility( true );

        cout << "Score: " << snake.size() << endl;
        cout << "Enter name:" << endl;
        string name = "";
        cin >> name;

        ofstream of( "highscore.txt", ios::out | ios::app );
        of << name << " " << snake.size() << endl;
        of.close();

        display_highscores();

        cout << "GAMEOVER. Wanna play some more? y/n" << endl;

        if( ask_question() )
        {
          gameover = false;
          init_game();
        }

        //exit the game if still gameover
        if( gameover )
          break;
      }

      clock.reset();
    }

    unsigned elapsed = sleeper.get_elapsed_time();
    if( elapsed < 17 )
      Sleep( 17 - elapsed ); //make sure it is not too fast
  }

  //------------
  //END GAMEPLAY
  //------------

  //delete created files
  for( auto c : deletables )
    if( remove( c.c_str() ) )
      cerr << "couldn't delete temp file: " << c << endl;

  Sleep(3000);

  //reset icons at exit
  set_snap_to_grid( has_snaptogrid );

  unregister_hotkeys();

  int cnt = 0;
  for( auto c : icon_pos )
    set_icon_position( cnt++, c );

  set_windows_visibility( true );

  set_console_visibility( true ); //show console
  cout << "Press any button to exit." << endl;
  cin.get();

  return 0;
}