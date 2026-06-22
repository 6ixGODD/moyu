#include "chat.h"

#include "agent.h"
#include "loop.h"
#include "mem.h"
#include "state.h"
#include "workdir.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wchar.h>

#define TRAY_WM (WM_APP + 120)
#define TRAY_UID 1
#define PANEL_WM_HIDE (WM_APP + 121)
#define MENU_CHAT 2001
#define MENU_RECENT 2002
#define MENU_PAUSE 2003
#define MENU_HOME 2004
#define MENU_DIAG 2005
#define MENU_EXIT 2006

struct chat_ui {
  moyu_app* app;
  HANDLE chat_process;
  HWND tray_hwnd;
  HWND panel_hwnd;
  HICON tray_icon;
  NOTIFYICONDATAW tray;
  HFONT title_font;
  HFONT body_font;
  HFONT mono_font;
  RECT buttons[6];
  int hover_button;
};

static const wchar_t* TRAY_CLASS = L"moyu_tray_window";
static const wchar_t* PANEL_CLASS = L"moyu_panel_window";
static LRESULT CALLBACK tray_wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
static LRESULT CALLBACK panel_wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
static void perform_action(chat_ui* ui,int cmd);

static HICON create_tray_icon(void) {
  const int w = 32, h = 32;
  LPCWSTR stock_icon = MAKEINTRESOURCEW(32512);
  BITMAPV5HEADER bi;
  ZeroMemory(&bi, sizeof(bi));
  bi.bV5Size = sizeof(bi);
  bi.bV5Width = w;
  bi.bV5Height = -h;
  bi.bV5Planes = 1;
  bi.bV5BitCount = 32;
  bi.bV5Compression = BI_BITFIELDS;
  bi.bV5RedMask = 0x00FF0000;
  bi.bV5GreenMask = 0x0000FF00;
  bi.bV5BlueMask = 0x000000FF;
  bi.bV5AlphaMask = 0xFF000000;

  void* bits = NULL;
  HDC dc = GetDC(NULL);
  HBITMAP color = CreateDIBSection(dc, (BITMAPINFO*)&bi, DIB_RGB_COLORS, &bits, NULL, 0);
  ReleaseDC(NULL, dc);
  if (!color || !bits) return LoadIconW(NULL, stock_icon);

  DWORD* px = (DWORD*)bits;
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      px[y * w + x] = 0x00000000;
    }
  }

  const int cx = 16, cy = 16, r = 12;
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      int dx = x - cx;
      int dy = y - cy;
      int d2 = dx * dx + dy * dy;
      if (d2 <= r * r) px[y * w + x] = 0xFFF6F1E7;
      if (d2 >= (r - 1) * (r - 1) && d2 <= r * r) px[y * w + x] = 0xFF6A5D50;
    }
  }
  for (int y = 10; y <= 12; y++) {
    for (int x = 10; x <= 12; x++) px[y * w + x] = 0xFF2A241F;
    for (int x = 20; x <= 22; x++) px[y * w + x] = 0xFF2A241F;
  }
  for (int x = 11; x <= 21; x++) px[21 * w + x] = 0xFF2A241F;
  px[20 * w + 12] = px[20 * w + 13] = px[20 * w + 19] = px[20 * w + 20] = 0xFF2A241F;

  HBITMAP mask = CreateBitmap(w, h, 1, 1, NULL);
  ICONINFO ii;
  ZeroMemory(&ii, sizeof(ii));
  ii.fIcon = TRUE;
  ii.hbmColor = color;
  ii.hbmMask = mask;
  HICON icon = CreateIconIndirect(&ii);
  DeleteObject(mask);
  DeleteObject(color);
  return icon ? icon : LoadIconW(NULL, stock_icon);
}

static void ensure_tray_class(void) {
  static bool registered = false;
  if (registered) return;
  WNDCLASSEXW wc;
  ZeroMemory(&wc, sizeof(wc));
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = tray_wnd_proc;
  wc.hInstance = GetModuleHandleW(NULL);
  wc.lpszClassName = TRAY_CLASS;
  RegisterClassExW(&wc);
  ZeroMemory(&wc, sizeof(wc));
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = panel_wnd_proc;
  wc.hInstance = GetModuleHandleW(NULL);
  wc.hCursor = LoadCursorW(NULL, MAKEINTRESOURCEW(32649));
  wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
  wc.lpszClassName = PANEL_CLASS;
  RegisterClassExW(&wc);
  registered = true;
}

static wchar_t* to_wide(const char* s) {
  int n=MultiByteToWideChar(CP_UTF8,0,s?s:"",-1,NULL,0);
  wchar_t* w=(wchar_t*)moyu_alloc((size_t)n*sizeof(wchar_t));
  MultiByteToWideChar(CP_UTF8,0,s?s:"",-1,w,n);return w;
}

static char* to_utf8(const wchar_t* w) {
  int n=WideCharToMultiByte(CP_UTF8,0,w?w:L"",-1,NULL,0,NULL,NULL);
  char* s=(char*)moyu_alloc((size_t)n);WideCharToMultiByte(CP_UTF8,0,w?w:L"",-1,s,n,NULL,NULL);return s;
}

static void open_home(chat_ui* ui,const char* child) {
  char* path=child?platform_join_path(ui->app->workdir->root,child):moyu_strdup(ui->app->workdir->root);
  wchar_t* w=to_wide(path);ShellExecuteW(NULL,L"open",w,NULL,NULL,SW_SHOWNORMAL);moyu_free(w);moyu_free(path);
}

static void show_native_menu(chat_ui* ui, int x, int y) {
  if(!ui)return;
  HMENU m=CreatePopupMenu();
  AppendMenuW(m, MF_STRING, MENU_CHAT, L"Open Terminal Chat");
  AppendMenuW(m, MF_STRING, MENU_RECENT, L"Open Collections");
  AppendMenuW(m, MF_STRING, MENU_HOME, L"Open MOYU Home");
  AppendMenuW(m, MF_STRING, MENU_DIAG, L"Runtime Status");
  AppendMenuW(m, MF_SEPARATOR, 0, NULL);
  AppendMenuW(m,
              MF_STRING,
              MENU_PAUSE,
              ui->app->agent->autonomous_enabled ? L"Pause Autonomy"
                                                 : L"Resume Autonomy");
  AppendMenuW(m, MF_SEPARATOR, 0, NULL);
  AppendMenuW(m, MF_STRING, MENU_EXIT, L"Quit MOYU");
  SetForegroundWindow(ui->tray_hwnd ? ui->tray_hwnd : ui->panel_hwnd);
  int cmd = TrackPopupMenu(
      m, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, x, y, 0,
      ui->tray_hwnd ? ui->tray_hwnd : NULL, NULL);
  DestroyMenu(m);
  if (cmd) perform_action(ui, cmd);
  PostMessageW(ui->tray_hwnd ? ui->tray_hwnd : ui->panel_hwnd, WM_NULL, 0, 0);
}

static COLORREF rgb(int r,int g,int b){return RGB(r,g,b);}

static const wchar_t* mood_text(moyu_app* app){
  if(app->pet_dragging)return L"being carried";
  if(app->emotion.arousal < -0.25f)return L"sleepy";
  if(app->emotion.valence > 0.35f)return L"bright";
  if(app->mouse_near || app->current_anim == ANIM_OBSERVE)return L"curious";
  return L"quiet";
}

static void perform_action(chat_ui* ui,int cmd){
  if(cmd==MENU_CHAT)chat_ui_show(ui);
  else if(cmd==MENU_RECENT)open_home(ui,"collections");
  else if(cmd==MENU_PAUSE){ui->app->agent->autonomous_enabled=!ui->app->agent->autonomous_enabled;state_meta_set(ui->app->state,"autonomy_enabled",ui->app->agent->autonomous_enabled?"1":"0");}
  else if(cmd==MENU_HOME)open_home(ui,NULL);
  else if(cmd==MENU_DIAG){char* e=agent_explain(ui->app->agent);wchar_t* w=to_wide(e);MessageBoxW(NULL,w,L"MOYU runtime",MB_OK|MB_ICONINFORMATION);moyu_free(w);moyu_free(e);}
  else if(cmd==MENU_EXIT)PostQuitMessage(0);
}

static void layout_buttons(chat_ui* ui){
  int x=18,y=250,w=148,h=38,col_gap=12,row_gap=10;
  for(int i=0;i<6;i++){
    int col=i%2,row=i/2;
    ui->buttons[i].left=x+col*(w+col_gap);
    ui->buttons[i].top=y+row*(h+row_gap);
    ui->buttons[i].right=ui->buttons[i].left+w;
    ui->buttons[i].bottom=ui->buttons[i].top+h;
  }
}

static void draw_button(HDC dc, RECT rc, const wchar_t* label, bool hover, HFONT font){
  HBRUSH fill=CreateSolidBrush(hover?rgb(229,238,255):rgb(244,239,233));
  HPEN pen=CreatePen(PS_SOLID,1,hover?rgb(102,131,189):rgb(160,146,129));
  HGDIOBJ oldb=SelectObject(dc,fill), oldp=SelectObject(dc,pen), oldf=SelectObject(dc,font);
  RoundRect(dc,rc.left,rc.top,rc.right,rc.bottom,14,14);
  SetBkMode(dc,TRANSPARENT);SetTextColor(dc,rgb(43,37,31));
  DrawTextW(dc,label,-1,&rc,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
  SelectObject(dc,oldf);SelectObject(dc,oldp);SelectObject(dc,oldb);
  DeleteObject(fill);DeleteObject(pen);
}

static void draw_chip(HDC dc,
                      int x,
                      int y,
                      int w,
                      int h,
                      COLORREF fill_color,
                      COLORREF line_color,
                      const wchar_t* text,
                      HFONT font) {
  RECT rc = {x, y, x + w, y + h};
  HBRUSH fill = CreateSolidBrush(fill_color);
  HPEN pen = CreatePen(PS_SOLID, 1, line_color);
  HGDIOBJ oldb = SelectObject(dc, fill), oldp = SelectObject(dc, pen),
          oldf = SelectObject(dc, font);
  RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, 14, 14);
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, rgb(42, 35, 31));
  DrawTextW(dc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  SelectObject(dc, oldf);
  SelectObject(dc, oldp);
  SelectObject(dc, oldb);
  DeleteObject(fill);
  DeleteObject(pen);
}

static void fill_round_rect(HDC dc,
                            RECT rc,
                            COLORREF fill_color,
                            COLORREF line_color,
                            int radius) {
  HBRUSH fill = CreateSolidBrush(fill_color);
  HPEN pen = CreatePen(PS_SOLID, 1, line_color);
  HGDIOBJ oldb = SelectObject(dc, fill), oldp = SelectObject(dc, pen);
  RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, radius, radius);
  SelectObject(dc, oldp);
  SelectObject(dc, oldb);
  DeleteObject(fill);
  DeleteObject(pen);
}

static void draw_section(HDC dc,
                         RECT rc,
                         const wchar_t* title,
                         const wchar_t* body,
                         HFONT title_font,
                         HFONT body_font) {
  fill_round_rect(dc, rc, rgb(246, 242, 236), rgb(226, 216, 205), 18);
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, rgb(70, 60, 53));
  HFONT oldf = (HFONT)SelectObject(dc, title_font);
  RECT tr = {rc.left + 12, rc.top + 10, rc.right - 12, rc.top + 34};
  DrawTextW(dc, title, -1, &tr, DT_LEFT | DT_TOP | DT_SINGLELINE);
  SelectObject(dc, body_font);
  RECT br = {rc.left + 12, rc.top + 32, rc.right - 12, rc.bottom - 12};
  DrawTextW(dc, body, -1, &br, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS);
  SelectObject(dc, oldf);
}

static void paint_panel(chat_ui* ui, HDC dc){
  RECT rc;GetClientRect(ui->panel_hwnd,&rc);
  HBRUSH bg=CreateSolidBrush(rgb(248,245,240));
  FillRect(dc,&rc,bg);DeleteObject(bg);
  HPEN border=CreatePen(PS_SOLID,1,rgb(181,170,156));
  HGDIOBJ oldp=SelectObject(dc,border);
  HBRUSH card=CreateSolidBrush(rgb(252,249,244));
  HGDIOBJ oldb=SelectObject(dc,card);
  RoundRect(dc,4,4,rc.right-4,rc.bottom-4,24,24);
  SelectObject(dc,oldb);DeleteObject(card);

  SetBkMode(dc,TRANSPARENT);SetTextColor(dc,rgb(40,34,29));
  HFONT oldf=(HFONT)SelectObject(dc,ui->title_font);
  RECT tr={20,18,230,48};DrawTextW(dc,L"MOYU",-1,&tr,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
  SelectObject(dc,ui->body_font);
  draw_chip(dc,
            250,
            18,
            88,
            28,
            ui->app->agent->autonomous_enabled ? rgb(225, 239, 225)
                                                : rgb(240, 228, 220),
            ui->app->agent->autonomous_enabled ? rgb(115, 149, 111)
                                                : rgb(167, 129, 113),
            ui->app->agent->autonomous_enabled ? L"Autonomy on"
                                                : L"Autonomy off",
            ui->body_font);

  wchar_t mood_line[96];
  swprintf(mood_line, 96, L"mood: %ls", mood_text(ui->app));
  draw_chip(dc, 20, 56, 112, 28, rgb(232, 238, 248), rgb(154, 170, 202), mood_line, ui->body_font);
  draw_chip(dc,
            140,
            56,
            114,
            28,
            ui->app->mouse_near ? rgb(232, 244, 234) : rgb(241, 236, 230),
            ui->app->mouse_near ? rgb(126, 167, 132) : rgb(185, 171, 152),
            ui->app->mouse_near ? L"you are close" : L"watching the room",
            ui->body_font);
  draw_chip(dc,
            262,
            56,
            76,
            28,
            ui->app->pet_dragging ? rgb(255, 236, 214) : rgb(238, 237, 245),
            ui->app->pet_dragging ? rgb(190, 142, 96) : rgb(165, 161, 190),
            ui->app->pet_dragging ? L"dragged" : L"settled",
            ui->body_font);

  const char* last = ui->app->last_collection_title ? ui->app->last_collection_title : "Nothing kept yet.";
  const char* last_body = ui->app->last_collection_body ? ui->app->last_collection_body : "Drop a file or folder here and MOYU will react to it.";
  const char* speech = ui->app->say_text ? ui->app->say_text : "Waiting for a nudge.";
  wchar_t* wlast = to_wide(last);
  wchar_t* wlast_body = to_wide(last_body);
  wchar_t* wspeech = to_wide(speech);
  RECT sec1 = {20, 98, 338, 172};
  RECT sec2 = {20, 180, 338, 238};
  draw_section(dc, sec1, L"Latest keepsake", wlast, ui->body_font, ui->body_font);
  draw_section(dc, sec2, L"Current thought", wspeech, ui->body_font, ui->body_font);
  moyu_free(wspeech);
  moyu_free(wlast);

  HFONT old_body = (HFONT)SelectObject(dc, ui->mono_font ? ui->mono_font : ui->body_font);
  SetTextColor(dc, rgb(101, 89, 78));
  RECT foot = {32, 148, 326, 166};
  DrawTextW(dc, wlast_body, -1, &foot, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
  moyu_free(wlast_body);
  SelectObject(dc, old_body);

  static const int ids[6]={MENU_CHAT,MENU_RECENT,MENU_PAUSE,MENU_HOME,MENU_DIAG,MENU_EXIT};
  static const wchar_t* labels_on[6]={L"Terminal Chat",L"Collections",L"Pause Runtime",L"MOYU Home",L"Runtime Status",L"Quit"};
  static const wchar_t* labels_off[6]={L"Terminal Chat",L"Collections",L"Resume Runtime",L"MOYU Home",L"Runtime Status",L"Quit"};
  for(int i=0;i<6;i++){
    const wchar_t* label=(ids[i]==MENU_PAUSE&& !ui->app->agent->autonomous_enabled)?labels_off[i]:labels_on[i];
    draw_button(dc,ui->buttons[i],label,ui->hover_button==i,ui->body_font);
  }
  SetTextColor(dc, rgb(119, 107, 95));
  SelectObject(dc, ui->body_font);
  RECT hint = {22, 380, 338, 408};
  DrawTextW(dc,
            L"Drag files onto MOYU. Left double-click opens chat. Right click here for the companion panel.",
            -1,
            &hint,
            DT_LEFT | DT_TOP | DT_WORDBREAK);
  SelectObject(dc,oldf);SelectObject(dc,oldp);DeleteObject(border);
}

static void show_panel(chat_ui* ui, int x, int y) {
  if(!ui||!ui->panel_hwnd)return;
  layout_buttons(ui);
  ui->hover_button = -1;
  SetWindowPos(ui->panel_hwnd,HWND_TOPMOST,x-178,y-18,358,420,SWP_SHOWWINDOW);
  ShowWindow(ui->panel_hwnd,SW_SHOWNORMAL);
  SetForegroundWindow(ui->panel_hwnd);
  SetFocus(ui->panel_hwnd);
  InvalidateRect(ui->panel_hwnd,NULL,TRUE);
}

static LRESULT CALLBACK tray_wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  chat_ui* ui = (chat_ui*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
  if (msg == WM_NCCREATE) {
    CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
    return DefWindowProcW(hwnd, msg, wp, lp);
  }
  if (!ui) return DefWindowProcW(hwnd, msg, wp, lp);
  if (msg == TRAY_WM) {
    if (lp == WM_LBUTTONDBLCLK) {
      chat_ui_show(ui);
      return 0;
    }
    if (lp == WM_RBUTTONUP || lp == WM_CONTEXTMENU) {
      POINT p;GetCursorPos(&p);show_native_menu(ui,p.x,p.y);
      return 0;
    }
    if (lp == WM_LBUTTONUP) {
      POINT p;GetCursorPos(&p);show_panel(ui,p.x,p.y);
      return 0;
    }
  }
  if (msg == PANEL_WM_HIDE && ui->panel_hwnd) {
    ShowWindow(ui->panel_hwnd, SW_HIDE);
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

static int hit_button(chat_ui* ui, int x, int y){
  POINT pt={x,y};
  for(int i=0;i<6;i++)if(PtInRect(&ui->buttons[i],pt))return i;
  return -1;
}

static LRESULT CALLBACK panel_wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  chat_ui* ui=(chat_ui*)GetWindowLongPtrW(hwnd,GWLP_USERDATA);
  if(msg==WM_NCCREATE){
    CREATESTRUCTW* cs=(CREATESTRUCTW*)lp;
    SetWindowLongPtrW(hwnd,GWLP_USERDATA,(LONG_PTR)cs->lpCreateParams);
    return DefWindowProcW(hwnd,msg,wp,lp);
  }
  if(!ui)return DefWindowProcW(hwnd,msg,wp,lp);
  switch(msg){
    case WM_ACTIVATE:
      if(LOWORD(wp)==WA_INACTIVE)ShowWindow(hwnd,SW_HIDE);
      return 0;
    case WM_KEYDOWN:
      if (wp == VK_ESCAPE) { ShowWindow(hwnd, SW_HIDE); return 0; }
      return 0;
    case WM_MOUSEMOVE: {
      int hover=hit_button(ui,GET_X_LPARAM(lp),GET_Y_LPARAM(lp));
      if(hover!=ui->hover_button){ui->hover_button=hover;InvalidateRect(hwnd,NULL,TRUE);}
      return 0;
    }
    case WM_LBUTTONUP: {
      int hover=hit_button(ui,GET_X_LPARAM(lp),GET_Y_LPARAM(lp));
      static const int ids[6]={MENU_CHAT,MENU_RECENT,MENU_PAUSE,MENU_HOME,MENU_DIAG,MENU_EXIT};
      if(hover>=0){ShowWindow(hwnd,SW_HIDE);perform_action(ui,ids[hover]);}
      return 0;
    }
    case WM_PAINT: {
      PAINTSTRUCT ps;HDC dc=BeginPaint(hwnd,&ps);paint_panel(ui,dc);EndPaint(hwnd,&ps);return 0;
    }
    case WM_ERASEBKGND: return 1;
  }
  return DefWindowProcW(hwnd,msg,wp,lp);
}

chat_ui* chat_ui_create(moyu_app* app) {
  ensure_tray_class();
  chat_ui* ui=(chat_ui*)moyu_alloc(sizeof(*ui));
  ZeroMemory(ui, sizeof(*ui));
  ui->app=app;
  ui->hover_button=-1;
  ui->tray_icon=create_tray_icon();
  ui->title_font=CreateFontW(24,0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
  ui->body_font=CreateFontW(16,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
  ui->mono_font=CreateFontW(14,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,FIXED_PITCH,L"Consolas");
  ui->tray_hwnd = CreateWindowExW(0, TRAY_CLASS, L"moyu-tray", WS_OVERLAPPED, 0, 0, 0, 0, NULL, NULL, GetModuleHandleW(NULL), ui);
  ui->panel_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST,
                                   PANEL_CLASS,
                                   L"moyu-panel",
                                   WS_POPUP,
                                   0,0,358,420,
                                   NULL,NULL,GetModuleHandleW(NULL),ui);
  if(ui->panel_hwnd){
    HRGN rgn=CreateRoundRectRgn(0,0,358,420,26,26);
    SetWindowRgn(ui->panel_hwnd,rgn,TRUE);
  }
  if (ui->tray_hwnd) {
    ZeroMemory(&ui->tray, sizeof(ui->tray));
    ui->tray.cbSize = sizeof(ui->tray);
    ui->tray.hWnd = ui->tray_hwnd;
    ui->tray.uID = TRAY_UID;
    ui->tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    ui->tray.uCallbackMessage = TRAY_WM;
    ui->tray.hIcon = ui->tray_icon;
    wcscpy_s(ui->tray.szTip, 128, L"MOYU");
    Shell_NotifyIconW(NIM_ADD, &ui->tray);
    ui->tray.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &ui->tray);
  }
  return ui;
}

void chat_ui_destroy(chat_ui* ui) {
  if(!ui)return;
  if(ui->tray.hWnd)Shell_NotifyIconW(NIM_DELETE,&ui->tray);
  if(ui->chat_process)CloseHandle(ui->chat_process);
  if(ui->panel_hwnd)DestroyWindow(ui->panel_hwnd);
  if(ui->tray_hwnd)DestroyWindow(ui->tray_hwnd);
  if(ui->tray_icon)DestroyIcon(ui->tray_icon);
  if(ui->title_font)DeleteObject(ui->title_font);
  if(ui->body_font)DeleteObject(ui->body_font);
  if(ui->mono_font)DeleteObject(ui->mono_font);
  moyu_free(ui);
}

void chat_ui_show(chat_ui* ui) {
  if(!ui)return;
  if(ui->chat_process&&WaitForSingleObject(ui->chat_process,0)==WAIT_TIMEOUT)return;
  if(ui->chat_process){CloseHandle(ui->chat_process);ui->chat_process=NULL;}
  wchar_t exe[MAX_PATH];GetModuleFileNameW(NULL,exe,MAX_PATH);wchar_t* slash=wcsrchr(exe,L'\\');
  if(slash)wcscpy_s(slash+1,(size_t)(MAX_PATH-(slash+1-exe)),L"moyu-chat.exe");
  STARTUPINFOW si={sizeof(si)};PROCESS_INFORMATION pi={0};
  if(CreateProcessW(exe,NULL,NULL,NULL,FALSE,CREATE_NEW_CONSOLE,NULL,NULL,&si,&pi)){
    ui->chat_process=pi.hProcess;CloseHandle(pi.hThread);
  }else MessageBoxW(NULL,L"Cannot start moyu-chat.exe. Rebuild the project first.",L"MOYU",MB_OK|MB_ICONERROR);
}

void chat_ui_append(chat_ui* ui,const char* role,const char* text){(void)ui;(void)role;(void)text;}
bool chat_ui_visible(chat_ui* ui){return ui&&ui->chat_process&&WaitForSingleObject(ui->chat_process,0)==WAIT_TIMEOUT;}

void chat_ui_context_menu(chat_ui* ui) {
  POINT p;GetCursorPos(&p);show_panel(ui,p.x,p.y);
}

void chat_ui_onboarding(chat_ui* ui) {
  if(!ui)return;char* done=state_meta_get(ui->app->state,"onboarding_version");if(done){moyu_free(done);return;}
  int answer=MessageBoxW(NULL,L"Choose one folder MOYU may inspect read-only? Choose No to keep it inside ~/.moyu.",L"Welcome to MOYU",MB_YESNO|MB_ICONINFORMATION);
  if(answer==IDYES){BROWSEINFOW bi={0};bi.lpszTitle=L"Choose a read-only observation folder";bi.ulFlags=BIF_RETURNONLYFSDIRS|BIF_NEWDIALOGSTYLE;PIDLIST_ABSOLUTE id=SHBrowseForFolderW(&bi);if(id){wchar_t path[MAX_PATH];if(SHGetPathFromIDListW(id,path)){char* root=to_utf8(path);if(ui->app->observe_root)moyu_free(ui->app->observe_root);ui->app->observe_root=root;state_meta_set(ui->app->state,"observe_root",root);state_permission_set(ui->app->state,"filesystem.observe",root,"allow",true);}CoTaskMemFree((void*)id);}}
  int autonomy=MessageBoxW(NULL,L"Allow occasional read-only intentions while you are idle?",L"MOYU autonomy",MB_YESNO|MB_ICONQUESTION);ui->app->agent->autonomous_enabled=autonomy==IDYES;state_meta_set(ui->app->state,"autonomy_enabled",autonomy==IDYES?"1":"0");state_meta_set(ui->app->state,"onboarding_version","2");
}
#else
struct chat_ui{int unused;};
chat_ui* chat_ui_create(struct moyu_app* a){(void)a;return NULL;}void chat_ui_destroy(chat_ui* u){(void)u;}void chat_ui_show(chat_ui* u){(void)u;}void chat_ui_append(chat_ui* u,const char* r,const char* t){(void)u;(void)r;(void)t;}void chat_ui_context_menu(chat_ui* u){(void)u;}void chat_ui_onboarding(chat_ui* u){(void)u;}bool chat_ui_visible(chat_ui* u){(void)u;return false;}
#endif
