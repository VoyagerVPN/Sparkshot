"""Проверка редактора через сообщения Win32 и WM_PRINTCLIENT, без физического ввода."""
import ctypes as c
from ctypes import wintypes as w
import json
import sys
import time
import threading
import os
import struct
import zlib
from pathlib import Path

u=c.WinDLL('user32',use_last_error=True)
g=c.WinDLL('gdi32',use_last_error=True)
u.SendMessageTimeoutW.argtypes=[w.HWND,w.UINT,w.WPARAM,w.LPARAM,w.UINT,w.UINT,c.POINTER(c.c_size_t)]
u.SendMessageTimeoutW.restype=w.LPARAM
u.GetWindowThreadProcessId.argtypes=[w.HWND,c.POINTER(w.DWORD)]
u.GetWindowThreadProcessId.restype=w.DWORD
u.IsWindowVisible.argtypes=[w.HWND]
u.GetForegroundWindow.restype=w.HWND
u.GetWindowTextW.argtypes=[w.HWND,w.LPWSTR,c.c_int]
u.GetClassNameW.argtypes=[w.HWND,w.LPWSTR,c.c_int]
u.GetWindowRect.argtypes=[w.HWND,c.POINTER(w.RECT)]
u.FindWindowExW.argtypes=[w.HWND,w.HWND,w.LPCWSTR,w.LPCWSTR];u.FindWindowExW.restype=w.HWND
u.SetWindowTextW.argtypes=[w.HWND,w.LPCWSTR];u.SetWindowTextW.restype=w.BOOL
u.IsWindow.argtypes=[w.HWND];u.IsWindow.restype=w.BOOL
u.OpenClipboard.argtypes=[w.HWND];u.OpenClipboard.restype=w.BOOL
u.EmptyClipboard.argtypes=[];u.EmptyClipboard.restype=w.BOOL
u.CloseClipboard.argtypes=[];u.CloseClipboard.restype=w.BOOL
u.IsClipboardFormatAvailable.argtypes=[w.UINT];u.IsClipboardFormatAvailable.restype=w.BOOL
u.GetClipboardData.argtypes=[w.UINT];u.GetClipboardData.restype=w.HANDLE
u.RegisterClipboardFormatW.argtypes=[w.LPCWSTR];u.RegisterClipboardFormatW.restype=w.UINT
k=c.WinDLL('kernel32',use_last_error=True)
k.GlobalLock.argtypes=[w.HGLOBAL];k.GlobalLock.restype=c.c_void_p
k.GlobalUnlock.argtypes=[w.HGLOBAL];k.GlobalUnlock.restype=w.BOOL
g.CreateCompatibleDC.argtypes=[w.HDC];g.CreateCompatibleDC.restype=w.HDC
g.CreateDIBSection.argtypes=[w.HDC,c.c_void_p,w.UINT,c.POINTER(c.c_void_p),w.HANDLE,w.DWORD];g.CreateDIBSection.restype=w.HBITMAP
g.SelectObject.argtypes=[w.HDC,w.HGDIOBJ];g.SelectObject.restype=w.HGDIOBJ
g.DeleteObject.argtypes=[w.HGDIOBJ];g.DeleteDC.argtypes=[w.HDC]
callback=c.WINFUNCTYPE(w.BOOL,w.HWND,w.LPARAM)
u.EnumWindows.argtypes=[callback,w.LPARAM]
class Header(c.Structure):
    _fields_=[('size',w.DWORD),('width',w.LONG),('height',w.LONG),('planes',w.WORD),('bpp',w.WORD),('compression',w.DWORD),('bytes',w.DWORD),('xppm',w.LONG),('yppm',w.LONG),('used',w.DWORD),('important',w.DWORD)]

exe=Path(sys.argv[1]).resolve()
assert '--headless' not in str(exe)
host=c.WinDLL(str(exe.with_name('SparkshotTestHost.dll')))
host.RunIsolatedEditor.argtypes=[w.LPCWSTR];host.RunIsolatedEditor.restype=c.c_int
thread=threading.Thread(target=host.RunIsolatedEditor,args=(' '.join(sys.argv[2:]),),daemon=True);thread.start()
hwnd=None
checks=[]
opening=[]
def send(message,wp=0,lp=0,target=None):
    result=c.c_size_t()
    if not u.SendMessageTimeoutW(target or hwnd,message,wp,lp,2,10000,c.byref(result)):
        raise OSError(f'Win32 message timed out: {message:#x}')
    return result.value
def point(x,y):return (y<<16)|(x&0xffff)
def open_clipboard(deadline_seconds=2):
    # Буфер может быть коротко занят другим процессом; ждём освобождения.
    deadline=time.monotonic()+deadline_seconds
    while not u.OpenClipboard(None):
        if time.monotonic()>=deadline:raise OSError('clipboard is busy')
        time.sleep(.02)
def move(x,y):send(0x200,0,point(x,y))
def click(x,y):move(x,y);send(0x201,1,point(x,y));send(0x202,0,point(x,y))
def drag(x1,y1,x2,y2):move(x1,y1);send(0x201,1,point(x1,y1));move(x2,y2);send(0x202,0,point(x2,y2))
def capture(delay=True):
    # Ждём обычный цикл вывода, WM_PRINTCLIENT читает последний выведенный кадр и не форсирует новый.
    if delay:time.sleep(.03)
    width,height=1200,780
    header=Header(c.sizeof(Header),width,-height,1,32,0,width*height*4,0,0,0,0)
    dc=g.CreateCompatibleDC(None);bits=c.c_void_p();bitmap=g.CreateDIBSection(dc,c.byref(header),0,c.byref(bits),None,0);old=g.SelectObject(dc,bitmap)
    try:
        send(0x318,dc,4)
        g.GdiFlush()
        return list((c.c_uint32*(width*height)).from_address(bits.value))
    finally:g.SelectObject(dc,old);g.DeleteObject(bitmap);g.DeleteDC(dc)
def pixel(frame,x,y):return frame[y*1200+x]&0xffffff
def check(name,condition):
    if not condition:raise AssertionError(name)
    checks.append(name)
def check_frame(name,predicate):
    # Ожидаем наблюдаемое состояние, а не предполагаем одинаковое время GPU и WARP.
    deadline=time.monotonic()+2
    while True:
        frame=capture()
        if predicate(frame):
            checks.append(name)
            return frame
        if time.monotonic()>=deadline:raise AssertionError(name)
def capture_at_restart(timings=None):
    # Наблюдаем кадр при возврате команды захвата, до следующего прохода цикла рисования.
    class ReturnedMessage(c.Structure):
        _fields_=[('result',c.c_ssize_t),('lparam',w.LPARAM),('wparam',w.WPARAM),('message',w.UINT),('hwnd',w.HWND)]
    hook_callback=c.WINFUNCTYPE(c.c_ssize_t,c.c_int,w.WPARAM,w.LPARAM)
    u.SetWindowsHookExW.argtypes=[c.c_int,hook_callback,w.HINSTANCE,w.DWORD];u.SetWindowsHookExW.restype=w.HANDLE
    u.CallNextHookEx.argtypes=[w.HANDLE,c.c_int,w.WPARAM,w.LPARAM];u.CallNextHookEx.restype=c.c_ssize_t
    u.UnhookWindowsHookEx.argtypes=[w.HANDLE]
    observed=[]
    @hook_callback
    def returned(code,wp,lp):
        if code>=0:
            message=c.cast(lp,c.POINTER(ReturnedMessage)).contents
            if message.hwnd==hwnd and message.message==0x8001:
                if timings is not None:timings.append((time.monotonic()-begin)*1000)
                observed.append(capture(False))
        return u.CallNextHookEx(None,code,wp,lp)
    hook=u.SetWindowsHookExW(12,returned,None,u.GetWindowThreadProcessId(hwnd,None))
    if not hook:raise c.WinError(c.get_last_error())
    begin=time.monotonic()
    try:send(0x8001)
    finally:u.UnhookWindowsHookEx(hook)
    assert len(observed)==1,'Missing first capture frame'
    return observed[0]
try:
    deadline=time.monotonic()+8
    while time.monotonic()<deadline:
        candidates=[]
        @callback
        def find(window,_):
            pid=w.DWORD();u.GetWindowThreadProcessId(window,c.byref(pid))
            cls=c.create_unicode_buffer(256);u.GetClassNameW(window,cls,256)
            if pid.value==os.getpid() and cls.value=='Sparkshot.Native':candidates.append(window)
            return True
        u.EnumWindows(find,0)
        if candidates:hwnd=candidates[0];break
        if not thread.is_alive():raise RuntimeError('Isolated editor exited')
        time.sleep(.02)
    assert hwnd,'Missing isolated app window'
    check('headless window is hidden',not u.IsWindowVisible(hwnd))
    if '--interactions' in sys.argv:
        send(0x8001)
        drag(200,220,850,620)
        move(430,400)
        plain=capture()
        click(885,482)
        move(430,400)
        check_frame('number preview marks its placement before click',lambda frame:pixel(frame,430,400)!=pixel(plain,430,400))
        click(885,578)
        move(500,400)
        check_frame('fill reticle marks its target pixel',lambda frame:pixel(frame,500,400)!=pixel(plain,500,400))
        send(0x20a,(120<<16)|4)
        check_frame('Shift wheel changes the selected tool',lambda frame:pixel(frame,510,400)==pixel(plain,510,400))
        send(0x20a,(1200<<16)|8)
        check_frame('Ctrl wheel enlarges the active shape preview',lambda frame:pixel(frame,506,400)!=pixel(plain,506,400))
        click(885,418)
        click(350,350)
        editor=u.FindWindowExW(hwnd,None,'EDIT',None)
        check('text editor opens',bool(editor))
        check('text can be entered',u.SetWindowTextW(editor,'Move me'))
        check_frame('active text starts at the click point',lambda frame:pixel(frame,347,347)!=pixel(plain,347,347))
        send(0x104,18)
        drag(355,355,455,405)
        send(0x105,18)
        check_frame('Alt drag moves active text without committing it',lambda frame:pixel(frame,447,397)!=pixel(plain,447,397) and pixel(frame,347,347)==pixel(plain,347,347))
        small_text=capture()
        send(0x20a,(600<<16)|8,target=editor)
        check_frame('Ctrl wheel changes active text size',lambda frame:sum(pixel(frame,x,y)!=pixel(small_text,x,y) for x in range(450,540,2) for y in range(405,455,2))>50)
        open_clipboard();u.EmptyClipboard();u.CloseClipboard()
        send(0xb1,0,4,editor)
        send(0x301,target=editor)
        open_clipboard()
        try:
            text_handle=u.GetClipboardData(13)
            assert text_handle,'Selected text was not copied'
            text_bits=k.GlobalLock(text_handle)
            try:check('selected letters copy as text',c.wstring_at(text_bits)=='Move')
            finally:k.GlobalUnlock(text_handle)
        finally:u.CloseClipboard()
        check('text editor stays open after copying selected letters',u.IsWindow(editor))
        send(0xb1,-1,-1,editor)
        send(0x301,target=editor)
        open_clipboard()
        try:
            check('copy without selected letters places screenshot on clipboard',u.IsClipboardFormatAvailable(8) and u.IsClipboardFormatAvailable(17))
            dib_handle=u.GetClipboardData(8)
            dib_bits=k.GlobalLock(dib_handle)
            try:
                dib=Header.from_address(dib_bits)
                copied=(c.c_uint32*(dib.width*dib.height)).from_address(dib_bits+c.sizeof(Header))
                check('copied screenshot includes moved text',any(copied[(dib.height-1-y)*dib.width+x]&0xffffff==0xff5757 for y in range(180,215) for x in range(250,365)))
            finally:k.GlobalUnlock(dib_handle)
        finally:u.CloseClipboard()
        check('copying the screenshot closes the editor',not u.IsWindow(editor))
    elif '--vary-demo' in sys.argv:
        first=capture_at_restart()
        second=capture_at_restart()
        check('different screenshot replaces the first frame immediately',pixel(second,425,400)!=pixel(first,425,400) and pixel(second,425,400)==pixel(second,180,250))
        bounds=w.RECT();u.GetWindowRect(hwnd,c.byref(bounds))
        check('second screenshot adopts smaller dimensions',bounds.right-bounds.left==1000 and bounds.bottom-bounds.top==700)
        third=capture_at_restart()
        check('third screenshot restores its own pixels without the previous frame',pixel(third,425,400)==pixel(first,425,400) and pixel(third,425,400)!=pixel(second,425,400))
        u.GetWindowRect(hwnd,c.byref(bounds))
        check('third screenshot restores larger dimensions',bounds.right-bounds.left==1200 and bounds.bottom-bounds.top==780)
    else:
        send(0x8001)
        drag(200,220,850,620)
        before=check_frame('selected screenshot is rendered',lambda frame:pixel(frame,400,400)==0x3b63ab)
        drag(400,400,430,425)
        check_frame('default tool moves selection',lambda frame:pixel(frame,215,300)!=0x3b63ab and pixel(before,215,300)==0x3b63ab)
        check_frame('default drag does not draw',lambda frame:pixel(frame,420,415)==0x3b63ab)
        move(500,450);time.sleep(.04)
        click(915,553)
        click(569,350)
        check_frame('picker selects left neighbor',lambda frame:pixel(frame,915,715)==0x3b63ab)
        click(570,350)
        check_frame('picker selects right neighbor',lambda frame:pixel(frame,915,715)==0x222e41)
        check_frame('picker has exact white sample pixel',lambda frame:pixel(frame,570,350)==0xffffff)
        move(700,350)
        check_frame('old picker pointer is restored',lambda frame:pixel(frame,570,350)==0x222e41)
        check_frame('new picker pointer reaches latest position',lambda frame:pixel(frame,700,350)==0xffffff)
        for x in range(300,601):move(x,400)
        check_frame('burst of input renders final state',lambda frame:pixel(frame,600,400)==0xffffff and pixel(frame,300,400)!=0xffffff)
        send(0x2a3)
        check_frame('pointer disappears on mouse leave',lambda frame:pixel(frame,600,400)==0x222e41)
        click(915,298)
        drag(350,400,500,400)
        send(0x2a3)
        check_frame('brush annotation is visible',lambda frame:pixel(frame,425,400)==0x222e41)
        click(915,657);send(0x2a3)
        check_frame('undo restores original screenshot',lambda frame:pixel(frame,425,400)==0x3b63ab)
        click(915,687);send(0x2a3)
        check_frame('redo restores annotation',lambda frame:pixel(frame,425,400)==0x222e41)
        click(915,425);drag(350,400,500,400);send(0x2a3)
        check_frame('eraser restores original screenshot',lambda frame:pixel(frame,425,400)==0x3b63ab)
        old=capture();first=capture_at_restart()
        check('new capture starts with a fresh frame before the next paint loop',pixel(first,425,400)==pixel(old,180,250) and pixel(first,425,400)!=pixel(old,425,400))
        drag(200,220,850,620);drag(400,400,430,425)
        check_frame('new capture resets selection tool and annotations',lambda frame:pixel(frame,215,300)!=0x3b63ab and pixel(frame,420,415)==0x3b63ab)
        time.sleep(.05);cpu=time.process_time();time.sleep(.25)
        check('idle editor does not spin',time.process_time()-cpu<.05)
        output=exe.parent/'test-output';previous=set(output.glob('Screenshot_*.png'))
        click(435,679);deadline=time.monotonic()+3
        while time.monotonic()<deadline:
            created=set(output.glob('Screenshot_*.png'))-previous
            if created:break
            time.sleep(.02)
        check('save produces PNG in isolated output',len(created)==1)
        raw=next(iter(created)).read_bytes();offset=8;packed=b'';width=height=0
        while offset<len(raw):
            size=struct.unpack('>I',raw[offset:offset+4])[0];kind=raw[offset+4:offset+8];data=raw[offset+8:offset+8+size];offset+=size+12
            if kind==b'IHDR':width,height,depth,color_type=struct.unpack('>IIBB',data[:10])
            if kind==b'IDAT':packed+=data
        check('PNG preserves selection dimensions',width==650 and height==400)
        channels={2:3,6:4}[color_type];scan=zlib.decompress(packed);stride=width*channels;prior=bytearray(stride);rows=[]
        for y in range(height):
            mode=scan[y*(stride+1)];row=bytearray(scan[y*(stride+1)+1:(y+1)*(stride+1)])
            for x in range(stride):
                a=row[x-channels] if x>=channels else 0;b=prior[x];d=prior[x-channels] if x>=channels else 0
                if mode==1:value=a
                elif mode==2:value=b
                elif mode==3:value=(a+b)//2
                elif mode==4:
                    estimate=a+b-d;pa,pb,pd=abs(estimate-a),abs(estimate-b),abs(estimate-d);value=a if pa<=pb and pa<=pd else b if pb<=pd else d
                else:value=0
                row[x]=(row[x]+value)&255
            rows.append(row);prior=row
        check('export excludes editor overlays',tuple(rows[155][195*channels:195*channels+3])==(0x3b,0x63,0xab))
        first=capture_at_restart()
        check('new capture after closing starts with a fresh frame',pixel(first,425,400)==pixel(old,180,250) and pixel(first,425,400)!=pixel(old,425,400))
        drag(200,220,850,620)
        open_clipboard();u.EmptyClipboard();u.CloseClipboard()
        click(300,654)
        open_clipboard()
        try:
            # Копирование размещает CF_DIB (8), CF_DIBV5 (17) и зарегистрированный PNG.
            png_format=u.RegisterClipboardFormatW('PNG')
            check('copy places screenshot DIB on the clipboard',u.IsClipboardFormatAvailable(8) and u.IsClipboardFormatAvailable(17) and u.IsClipboardFormatAvailable(png_format))
            handle=u.GetClipboardData(8)
            assert handle,'GetClipboardData returned no DIB'
            bits=k.GlobalLock(handle)
            assert bits,'GlobalLock returned no DIB bits'
            try:
                dib=Header.from_address(bits)
                # Нижний порядок строк: положительная высота, как у GDI/Lightshot.
                check('clipboard DIB preserves selection dimensions',dib.width==650 and dib.height==400 and dib.bpp==32 and dib.compression==0)
                pixels=(c.c_uint32*(dib.width*dib.height)).from_address(bits+c.sizeof(Header))
                # Строка y верхнего порядка хранится в строке height-1-y.
                check('clipboard DIB preserves selection pixels',pixels[(400-1-155)*650+195]==0xff3b63ab and pixels[(400-1-10)*650+10]==0xff222e41)
            finally:k.GlobalUnlock(handle)
            v5=u.GetClipboardData(17)
            assert v5,'GetClipboardData returned no DIBV5'
            bits=k.GlobalLock(v5)
            assert bits,'GlobalLock returned no DIBV5 bits'
            try:
                # Первые 40 байт BITMAPV5HEADER совместимы с BITMAPINFOHEADER.
                v5header=Header.from_address(bits)
                check('clipboard DIBV5 carries explicit header',v5header.size==124 and v5header.width==650 and v5header.height==400 and v5header.bpp==32)
            finally:k.GlobalUnlock(v5)
            png_handle=u.GetClipboardData(png_format)
            assert png_handle,'GetClipboardData returned no PNG'
            png_bits=k.GlobalLock(png_handle)
            assert png_bits,'GlobalLock returned no PNG bits'
            try:
                check('clipboard PNG block starts with file signature',c.string_at(png_bits,8)==b'\x89PNG\r\n\x1a\n')
            finally:k.GlobalUnlock(png_handle)
        finally:u.CloseClipboard()
        opening=[]
        for _ in range(5):
            capture_at_restart(opening)
        check('warm capture command completes within 100 ms median',sorted(opening)[2]<100)
finally:
    if hwnd:
        try:send(0x10)
        except OSError:pass
    thread.join(timeout=3)
check('worker exits cleanly',not thread.is_alive())
print(json.dumps({'backend':'warp' if '--warp' in sys.argv else 'hardware','passed':checks,'warm_capture_median_ms':sorted(opening)[2] if opening else None},ensure_ascii=False))
