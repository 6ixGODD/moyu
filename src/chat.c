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
#include <uxtheme.h>
#include <wchar.h>

#define TRAY_WM (WM_APP + 120)
#define TRAY_UID 1
#define PANEL_WM_HIDE (WM_APP + 121)
#define MENU_CHAT 2001
#define MENU_RECENT 2002
#define MENU_EXIT 2003
#define PANEL_SPEAK 3001
#define PANEL_RECENT 3002
#define PANEL_QUIT 3003

struct chat_ui {
  moyu_app* app;
  HANDLE chat_process;
  HWND tray_hwnd;
  HWND panel_hwnd;
  HWND input_hwnd;
  HWND input_edit;
  HICON tray_icon;
  NOTIFYICONDATAW tray;
  HFONT title_font;
  HFONT body_font;
  RECT buttons[3];
  int hover_button;
  RECT input_send_rect;
  bool input_send_hover;
  int input_lines;
  WNDPROC input_edit_proc;
  HBRUSH input_edit_brush;
};

static const wchar_t* TRAY_CLASS = L"moyu_tray_window";
static const wchar_t* PANEL_CLASS = L"moyu_panel_window";
static const wchar_t* INPUT_CLASS = L"moyu_input_window";
static LRESULT CALLBACK tray_wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
static LRESULT CALLBACK panel_wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
static LRESULT CALLBACK input_wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
static LRESULT CALLBACK input_edit_wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
static void perform_action(chat_ui* ui,int cmd);
static void show_input(chat_ui* ui);
static void submit_input(chat_ui* ui);

typedef enum {
  APP_MODE_DEFAULT,
  APP_MODE_ALLOW_DARK,
  APP_MODE_FORCE_DARK,
  APP_MODE_FORCE_LIGHT
} preferred_app_mode;

static void enable_system_menu_theme(HWND owner) {
  static bool initialized = false;
  if (!initialized) {
    HMODULE ux = LoadLibraryW(L"uxtheme.dll");
    if (ux) {
      typedef preferred_app_mode(WINAPI * set_mode_fn)(preferred_app_mode);
      typedef void(WINAPI * flush_menu_fn)(void);
      set_mode_fn set_mode = (set_mode_fn)GetProcAddress(ux, MAKEINTRESOURCEA(135));
      flush_menu_fn flush_menu =
          (flush_menu_fn)GetProcAddress(ux, MAKEINTRESOURCEA(136));
      if (set_mode) set_mode(APP_MODE_ALLOW_DARK);
      if (flush_menu) flush_menu();
    }
    initialized = true;
  }
  if (owner) {
    HMODULE ux = GetModuleHandleW(L"uxtheme.dll");
    if (ux) {
      typedef BOOL(WINAPI * allow_dark_fn)(HWND, BOOL);
      allow_dark_fn allow_dark =
          (allow_dark_fn)GetProcAddress(ux, MAKEINTRESOURCEA(133));
      if (allow_dark) allow_dark(owner, TRUE);
    }
  }
}

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
  ZeroMemory(&wc, sizeof(wc));
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = input_wnd_proc;
  wc.hInstance = GetModuleHandleW(NULL);
  wc.hCursor = LoadCursorW(NULL, MAKEINTRESOURCEW(32513));
  wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
  wc.lpszClassName = INPUT_CLASS;
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
  enable_system_menu_theme(ui->tray_hwnd);
  HMENU m=CreatePopupMenu();
  AppendMenuW(m, MF_STRING, MENU_CHAT, L"Open Terminal Chat");
  AppendMenuW(m, MF_STRING, MENU_RECENT, L"Open Collections");
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
  else if(cmd==MENU_EXIT)PostQuitMessage(0);
  else if(cmd==PANEL_SPEAK)show_input(ui);
  else if(cmd==PANEL_RECENT)open_home(ui,"collections");
  else if(cmd==PANEL_QUIT)PostQuitMessage(0);
}

static void layout_buttons(chat_ui* ui){
  int x=14,y=86,w=112,h=28,col_gap=8,row_gap=8;
  for(int i=0;i<3;i++){
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
  RoundRect(dc,rc.left,rc.top,rc.right,rc.bottom,12,12);
  SetBkMode(dc,TRANSPARENT);SetTextColor(dc,rgb(43,37,31));
  DrawTextW(dc,label,-1,&rc,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
  SelectObject(dc,oldf);SelectObject(dc,oldp);SelectObject(dc,oldb);
  DeleteObject(fill);DeleteObject(pen);
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
  RECT tr={14,12,120,34};DrawTextW(dc,L"MOYU",-1,&tr,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
  SelectObject(dc,ui->body_font);
  RECT mood={14,34,150,50};
  wchar_t mood_line[96];
  swprintf(mood_line, 96, L"mood  %ls", mood_text(ui->app));
  DrawTextW(dc,mood_line,-1,&mood,DT_LEFT|DT_TOP|DT_SINGLELINE);

  RECT auto_rc={14,52,170,68};
  DrawTextW(dc,
            ui->app->agent->autonomous_enabled ? L"mode  autonomous"
                                                : L"mode  paused",
            -1,
            &auto_rc,
            DT_LEFT|DT_TOP|DT_SINGLELINE);

  const char* last = ui->app->last_collection_title ? ui->app->last_collection_title : "Nothing kept yet.";
  wchar_t* wlast = to_wide(last);
  SetTextColor(dc, rgb(95, 86, 78));
  RECT last_label={14,70,110,84};
  RECT last_value={72,70,238,84};
  DrawTextW(dc,L"last",-1,&last_label,DT_LEFT|DT_TOP|DT_SINGLELINE);
  DrawTextW(dc,wlast,-1,&last_value,DT_LEFT|DT_TOP|DT_SINGLELINE|DT_END_ELLIPSIS);
  moyu_free(wlast);
  SetTextColor(dc,rgb(40,34,29));

  static const int ids[3]={PANEL_SPEAK,PANEL_RECENT,PANEL_QUIT};
  static const wchar_t* labels[3]={L"Speak",L"Keepsakes",L"Quit"};
  for(int i=0;i<3;i++){
    const wchar_t* label=labels[i];
    draw_button(dc,ui->buttons[i],label,ui->hover_button==i,ui->body_font);
  }
  SetTextColor(dc, rgb(119, 107, 95));
  SelectObject(dc, ui->body_font);
  RECT hint = {14, 154, 238, 174};
  DrawTextW(dc,
            L"Double-click to talk. Drag files to feed.",
            -1,
            &hint,
            DT_LEFT | DT_TOP | DT_WORDBREAK);
  SelectObject(dc,oldf);SelectObject(dc,oldp);DeleteObject(border);
}

static void show_panel(chat_ui* ui, int x, int y) {
  if(!ui||!ui->panel_hwnd)return;
  layout_buttons(ui);
  ui->hover_button = -1;
  SetWindowPos(ui->panel_hwnd,HWND_TOPMOST,x-126,y-10,252,182,SWP_SHOWWINDOW);
  ShowWindow(ui->panel_hwnd,SW_SHOWNORMAL);
  SetForegroundWindow(ui->panel_hwnd);
  SetFocus(ui->panel_hwnd);
  InvalidateRect(ui->panel_hwnd,NULL,TRUE);
}

static void show_input(chat_ui* ui) {
  if (!ui || !ui->input_hwnd) return;
  const int width = 306, height = 44;
  RECT pet = {ui->app->pet_x, ui->app->pet_y,
              ui->app->pet_x + ui->app->win_w,
              ui->app->pet_y + ui->app->win_h};
  HMONITOR monitor = MonitorFromRect(&pet, MONITOR_DEFAULTTONEAREST);
  MONITORINFO mi = {sizeof(mi)};
  GetMonitorInfoW(monitor, &mi);
  int x = pet.right + 8;
  if (x + width > mi.rcWork.right) x = pet.left - width - 8;
  int y = pet.bottom - height - 10;
  if (x < mi.rcWork.left) x = mi.rcWork.left;
  if (x + width > mi.rcWork.right) x = mi.rcWork.right - width;
  if (y < mi.rcWork.top) y = mi.rcWork.top;
  if (y + height > mi.rcWork.bottom) y = mi.rcWork.bottom - height;
  ui->input_lines = 1;
  SetWindowPos(ui->input_hwnd, HWND_TOPMOST, x, y, width, height,
               SWP_SHOWWINDOW);
  HRGN rgn = CreateRoundRectRgn(0, 0, width + 1, height + 1, 12, 12);
  SetWindowRgn(ui->input_hwnd, rgn, TRUE);
  SetWindowPos(ui->input_edit, NULL, 10, 10, width - 54, 23,
               SWP_NOZORDER | SWP_NOACTIVATE);
  ui->input_send_rect = (RECT){width - 38, 8, width - 10, 36};
  ShowScrollBar(ui->input_edit, SB_VERT, FALSE);
  ShowWindow(ui->input_hwnd, SW_SHOWNORMAL);
  SetForegroundWindow(ui->input_hwnd);
  SetFocus(ui->input_edit ? ui->input_edit : ui->input_hwnd);
  if (ui->input_edit) SendMessageW(ui->input_edit, EM_SETSEL, 0, -1);
}

static void resize_input_for_content(chat_ui* ui) {
  if (!ui || !ui->input_hwnd || !ui->input_edit ||
      !IsWindowVisible(ui->input_hwnd)) return;
  wchar_t text[2048];
  GetWindowTextW(ui->input_edit, text, 2048);
  HDC dc = GetDC(ui->input_edit);
  HGDIOBJ old = SelectObject(dc, ui->body_font);
  RECT calc = {0, 0, 252, 0};
  DrawTextW(dc, text[0] ? text : L" ", -1, &calc,
            DT_CALCRECT | DT_WORDBREAK | DT_EDITCONTROL | DT_NOPREFIX);
  TEXTMETRICW tm;
  GetTextMetricsW(dc, &tm);
  SelectObject(dc, old);
  ReleaseDC(ui->input_edit, dc);
  int line_h = tm.tmHeight > 0 ? tm.tmHeight : 17;
  int lines = (calc.bottom + line_h - 1) / line_h;
  if (lines < 1) lines = 1;
  int shown_lines = lines > 5 ? 5 : lines;
  if (shown_lines == ui->input_lines && lines <= 5) return;

  RECT wr;
  GetWindowRect(ui->input_hwnd, &wr);
  int width = wr.right - wr.left;
  int old_height = wr.bottom - wr.top;
  int edit_h = shown_lines * line_h + 6;
  int height = edit_h + 20;
  HMONITOR monitor = MonitorFromWindow(ui->input_hwnd, MONITOR_DEFAULTTONEAREST);
  MONITORINFO mi = {sizeof(mi)};
  GetMonitorInfoW(monitor, &mi);
  int y = wr.top - (height - old_height);
  if (y < mi.rcWork.top) y = mi.rcWork.top;
  if (y + height > mi.rcWork.bottom) y = mi.rcWork.bottom - height;
  SetWindowPos(ui->input_hwnd, HWND_TOPMOST, wr.left, y, width, height,
               SWP_NOACTIVATE);
  HRGN rgn = CreateRoundRectRgn(0, 0, width + 1, height + 1, 12, 12);
  SetWindowRgn(ui->input_hwnd, rgn, TRUE);
  SetWindowPos(ui->input_edit, NULL, 10, 10, width - 54, edit_h,
               SWP_NOZORDER | SWP_NOACTIVATE);
  ui->input_send_rect =
      (RECT){width - 38, height - 36, width - 10, height - 8};
  ShowScrollBar(ui->input_edit, SB_VERT, lines > 5);
  ui->input_lines = shown_lines;
  InvalidateRect(ui->input_hwnd, NULL, TRUE);
}

static void submit_input(chat_ui* ui) {
  if (!ui || !ui->input_edit) return;
  wchar_t buf[512];
  GetWindowTextW(ui->input_edit, buf, (int)(sizeof(buf) / sizeof(buf[0])));
  if (!buf[0]) return;
  char* text = to_utf8(buf);
  if (moyu_app_send_chat(ui->app, text)) {
    SetWindowTextW(ui->input_edit, L"");
    ShowWindow(ui->input_hwnd, SW_HIDE);
  }
  moyu_free(text);
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
      return 0;
    }
    if (lp == WM_RBUTTONUP || lp == WM_CONTEXTMENU) {
      POINT p;GetCursorPos(&p);show_native_menu(ui,p.x,p.y);
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
  for(int i=0;i<3;i++)if(PtInRect(&ui->buttons[i],pt))return i;
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
      static const int ids[3]={PANEL_SPEAK,PANEL_RECENT,PANEL_QUIT};
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

static LRESULT CALLBACK input_edit_wnd_proc(HWND hwnd,
                                            UINT msg,
                                            WPARAM wp,
                                            LPARAM lp) {
  chat_ui* ui = (chat_ui*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
  if (!ui || !ui->input_edit_proc) return DefWindowProcW(hwnd, msg, wp, lp);
  if (msg == WM_GETDLGCODE) return DLGC_WANTALLKEYS;
  if (msg == WM_KEYDOWN) {
    if (wp == VK_ESCAPE) {
      ShowWindow(ui->input_hwnd, SW_HIDE);
      return 0;
    }
    if (wp == VK_RETURN && !(GetKeyState(VK_SHIFT) & 0x8000)) {
      submit_input(ui);
      return 0;
    }
  }
  if (msg == WM_CHAR && wp == VK_RETURN &&
      !(GetKeyState(VK_SHIFT) & 0x8000))
    return 0;
  return CallWindowProcW(ui->input_edit_proc, hwnd, msg, wp, lp);
}

static LRESULT CALLBACK input_wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  chat_ui* ui=(chat_ui*)GetWindowLongPtrW(hwnd,GWLP_USERDATA);
  if(msg==WM_NCCREATE){
    CREATESTRUCTW* cs=(CREATESTRUCTW*)lp;
    SetWindowLongPtrW(hwnd,GWLP_USERDATA,(LONG_PTR)cs->lpCreateParams);
    return DefWindowProcW(hwnd,msg,wp,lp);
  }
  switch(msg){
    case WM_ACTIVATE:
      if(LOWORD(wp)==WA_INACTIVE)ShowWindow(hwnd,SW_HIDE);
      return 0;
    case WM_COMMAND:
      if (LOWORD(wp) == 1001 && HIWORD(wp) == EN_CHANGE)
        resize_input_for_content(ui);
      break;
    case WM_KEYDOWN:
      if (wp == VK_ESCAPE) { ShowWindow(hwnd, SW_HIDE); return 0; }
      break;
    case WM_MOUSEMOVE: {
      POINT p = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
      bool hover = PtInRect(&ui->input_send_rect, p) != 0;
      if (hover != ui->input_send_hover) {
        ui->input_send_hover = hover;
        InvalidateRect(hwnd, &ui->input_send_rect, FALSE);
      }
      return 0;
    }
    case WM_LBUTTONUP: {
      POINT p = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
      if (PtInRect(&ui->input_send_rect, p)) submit_input(ui);
      return 0;
    }
    case WM_SETCURSOR:
      if (LOWORD(lp) == HTCLIENT) {
        POINT p; GetCursorPos(&p); ScreenToClient(hwnd, &p);
        if (PtInRect(&ui->input_send_rect, p)) {
          SetCursor(LoadCursorW(NULL, MAKEINTRESOURCEW(32649)));
          return TRUE;
        }
      }
      break;
    case WM_CTLCOLOREDIT: {
      HDC edit_dc = (HDC)wp;
      SetTextColor(edit_dc, rgb(48, 42, 38));
      SetBkColor(edit_dc, rgb(255, 249, 238));
      return (LRESULT)ui->input_edit_brush;
    }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
      PAINTSTRUCT ps;HDC dc=BeginPaint(hwnd,&ps);
      RECT rc;GetClientRect(hwnd,&rc);
      HBRUSH bg=CreateSolidBrush(rgb(252,249,244));
      FillRect(dc,&rc,bg);DeleteObject(bg);
      HPEN pen=CreatePen(PS_SOLID,1,rgb(181,170,156));
      HGDIOBJ oldp=SelectObject(dc,pen);
      HBRUSH fill=CreateSolidBrush(rgb(252,249,244));
      HGDIOBJ oldb=SelectObject(dc,fill);
      RoundRect(dc,2,2,rc.right-2,rc.bottom-2,20,20);
      SelectObject(dc,oldb);DeleteObject(fill);
      SelectObject(dc,oldp);DeleteObject(pen);
      RECT edit_rc;
      GetWindowRect(ui->input_edit, &edit_rc);
      MapWindowPoints(NULL, hwnd, (POINT*)&edit_rc, 2);
      HPEN edit_pen = CreatePen(PS_SOLID, 1, rgb(151, 137, 122));
      HGDIOBJ old_edit_pen = SelectObject(dc, edit_pen);
      HGDIOBJ old_edit_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
      Rectangle(dc, edit_rc.left - 2, edit_rc.top - 2,
                edit_rc.right + 2, edit_rc.bottom + 2);
      SelectObject(dc, old_edit_brush);
      SelectObject(dc, old_edit_pen);
      DeleteObject(edit_pen);

      RECT b = ui->input_send_rect;
      int cx = (b.left + b.right) / 2, cy = (b.top + b.bottom) / 2;
      HBRUSH button_fill = CreateSolidBrush(ui->input_send_hover
          ? rgb(91, 116, 94) : rgb(111, 134, 111));
      HPEN button_pen = CreatePen(PS_SOLID, 1, rgb(64, 79, 65));
      HGDIOBJ old_button_fill = SelectObject(dc, button_fill);
      HGDIOBJ old_button_pen = SelectObject(dc, button_pen);
      Ellipse(dc, b.left, b.top, b.right, b.bottom);
      HPEN check_pen = CreatePen(PS_SOLID, 2, rgb(255, 250, 239));
      SelectObject(dc, check_pen);
      MoveToEx(dc, cx - 6, cy, NULL);
      LineTo(dc, cx - 1, cy + 5);
      LineTo(dc, cx + 7, cy - 5);
      SelectObject(dc, old_button_pen);
      SelectObject(dc, old_button_fill);
      DeleteObject(check_pen);
      DeleteObject(button_pen);
      DeleteObject(button_fill);
      EndPaint(hwnd,&ps);
      return 0;
    }
  }
  return DefWindowProcW(hwnd,msg,wp,lp);
}

chat_ui* chat_ui_create(moyu_app* app) {
  ensure_tray_class();
  chat_ui* ui=(chat_ui*)moyu_alloc(sizeof(*ui));
  ZeroMemory(ui, sizeof(*ui));
  ui->app=app;
  ui->hover_button=-1;
  ui->input_lines=1;
  ui->input_edit_brush=CreateSolidBrush(rgb(255,249,238));
  ui->tray_icon=create_tray_icon();
  ui->title_font=CreateFontW(18,0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
  ui->body_font=CreateFontW(13,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
  ui->tray_hwnd = CreateWindowExW(0, TRAY_CLASS, L"moyu-tray", WS_OVERLAPPED, 0, 0, 0, 0, NULL, NULL, GetModuleHandleW(NULL), ui);
  ui->panel_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST,
                                   PANEL_CLASS,
                                   L"moyu-panel",
                                   WS_POPUP,
                                   0,0,252,182,
                                   NULL,NULL,GetModuleHandleW(NULL),ui);
  if(ui->panel_hwnd){
    HRGN rgn=CreateRoundRectRgn(0,0,252,182,22,22);
    SetWindowRgn(ui->panel_hwnd,rgn,TRUE);
  }
  ui->input_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST,
                                   INPUT_CLASS,
                                   L"moyu-input",
                                   WS_POPUP,
                                   0,0,322,70,
                                   NULL,NULL,GetModuleHandleW(NULL),ui);
  if (ui->input_hwnd) {
    HRGN rgn=CreateRoundRectRgn(0,0,322,70,24,24);
    SetWindowRgn(ui->input_hwnd,rgn,TRUE);
    ui->input_edit = CreateWindowExW(0, L"EDIT", L"",
                                     WS_CHILD|WS_VISIBLE|ES_MULTILINE|
                                         ES_AUTOVSCROLL|WS_VSCROLL,
                                     10, 10, 252, 23,
                                     ui->input_hwnd, (HMENU)1001, GetModuleHandleW(NULL), NULL);
    SendMessageW(ui->input_edit, WM_SETFONT, (WPARAM)ui->body_font, TRUE);
    SendMessageW(ui->input_edit, EM_SETMARGINS, EC_LEFTMARGIN|EC_RIGHTMARGIN,
                 MAKELPARAM(3,3));
    SetWindowLongPtrW(ui->input_edit, GWLP_USERDATA, (LONG_PTR)ui);
    ui->input_edit_proc = (WNDPROC)SetWindowLongPtrW(
        ui->input_edit, GWLP_WNDPROC, (LONG_PTR)input_edit_wnd_proc);
    ShowScrollBar(ui->input_edit, SB_VERT, FALSE);
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
  }
  return ui;
}

void chat_ui_destroy(chat_ui* ui) {
  if(!ui)return;
  if(ui->tray.hWnd)Shell_NotifyIconW(NIM_DELETE,&ui->tray);
  if(ui->chat_process)CloseHandle(ui->chat_process);
  if(ui->input_hwnd)DestroyWindow(ui->input_hwnd);
  if(ui->panel_hwnd)DestroyWindow(ui->panel_hwnd);
  if(ui->tray_hwnd)DestroyWindow(ui->tray_hwnd);
  if(ui->tray_icon)DestroyIcon(ui->tray_icon);
  if(ui->title_font)DeleteObject(ui->title_font);
  if(ui->body_font)DeleteObject(ui->body_font);
  if(ui->input_edit_brush)DeleteObject(ui->input_edit_brush);
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

void chat_ui_show_quick_chat(chat_ui* ui) {
  show_input(ui);
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
