"""Меню на отдельной станции. Полный тест фокуса Windows запускается с --interactive."""
import ctypes as c
from ctypes import wintypes as w
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

u = c.WinDLL("user32", use_last_error=True)
u.GetProcessWindowStation.restype = w.HANDLE
station = c.create_unicode_buffer(256)
needed = w.DWORD()
assert u.GetUserObjectInformationW(w.HANDLE(u.GetProcessWindowStation()), 2, station,
                                   c.sizeof(station), c.byref(needed)) and (station.value.startswith("SparkshotReview_") or "--interactive" in sys.argv)
u.FindWindowExW.argtypes = [w.HWND, w.HWND, w.LPCWSTR, w.LPCWSTR]
u.FindWindowExW.restype = w.HWND
u.SendMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
u.SendMessageW.restype = c.c_ssize_t
u.PostMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
u.IsWindowVisible.argtypes = [w.HWND]
u.GetMenuItemCount.argtypes = [w.HMENU]
u.GetWindowThreadProcessId.argtypes = [w.HWND,c.POINTER(w.DWORD)]

def wait(predicate, description):
    limit = time.monotonic() + 4
    while time.monotonic() < limit:
        result = predicate()
        if result:
            return result
        time.sleep(.02)
    raise AssertionError(description)

def find(kind, pid):
    hwnd = 0
    while hwnd := u.FindWindowExW(None, hwnd, kind, None):
        owner = w.DWORD()
        u.GetWindowThreadProcessId(hwnd,c.byref(owner))
        if owner.value == pid:
            return hwnd

with tempfile.TemporaryDirectory(dir=Path(sys.argv[1]).resolve().parent) as directory:
    exe = Path(directory) / "Sparkshot.exe"
    shutil.copy2(sys.argv[1], exe)
    ini = exe.parent / "data" / "settings.ini"
    ini.parent.mkdir()
    ini.write_bytes(b"[Sparkshot]\r\nSaveFolder=shots\r\nUserValue=preserved\r\n")
    process = subprocess.Popen([exe, "--demo", "--autostart"], creationflags=subprocess.CREATE_NO_WINDOW)
    try:
        editor = wait(lambda: find("Sparkshot.Native", process.pid), "Нет окна приложения")
        def menu():
            u.PostMessageW(editor, 0x8002, 1, 0x205)
            return wait(lambda: find("#32768", process.pid), "Не появилось системное меню Win32")
        popup = menu()
        handle = u.SendMessageW(popup, 0x01E1, 0, 0)
        assert u.GetMenuItemCount(handle) == 4
        u.PostMessageW(editor, 0x100, 27, 0)
        wait(lambda: not find("#32768", process.pid), "Esc не закрыл меню")
        assert not u.IsWindowVisible(editor)
        print("Native menu: four commands, Esc returns to tray")
        def choose(index):
            menu()
            for key in [0x28]*(index+1)+[0x0d]:
                u.PostMessageW(editor,0x100,key,0)
        choose(1)
        dialog=wait(lambda: find("#32770",process.pid),"Настройки не открылись отдельным диалогом")
        assert not u.IsWindowVisible(editor)
        if "--interactive" not in sys.argv:
            assert u.FindWindowExW(dialog,None,"Button","English")
            print("Modeless settings dialog created on the isolated station")
            sys.exit(0)
        wait(lambda: u.IsWindowVisible(dialog),"Диалог не виден")
        english=u.FindWindowExW(dialog,None,"Button","English")
        assert english
        russian=u.FindWindowExW(dialog,None,"Button","Русский")
        assert russian
        u.SendMessageW(english,0x00F5,0,0)
        u.SendMessageW(dialog,0x28,english,1)
        u.PostMessageW(english,0x100,9,0)
        class GuiInfo(c.Structure):
            _fields_=[("size",w.DWORD),("flags",w.DWORD),("active",w.HWND),("focus",w.HWND),
                      ("capture",w.HWND),("menuOwner",w.HWND),("moveSize",w.HWND),
                      ("caret",w.HWND),("rect",w.RECT)]
        def focus():
            state=GuiInfo()
            state.size=c.sizeof(state)
            u.GetGUIThreadInfo(u.GetWindowThreadProcessId(dialog,None),c.byref(state))
            return state.focus
        wait(lambda: focus()==russian,"Tab не переместил фокус на следующую кнопку")
        u.PostMessageW(russian,0x100,13,0)
        wait(lambda: b"Language=ru" in ini.read_bytes(),"Enter не выбрал русский язык")
        print("Tab moves focus, Enter activates the focused native button")
        u.GetDlgItem.argtypes=[w.HWND,c.c_int]
        u.GetDlgItem.restype=w.HWND
        change=u.GetDlgItem(dialog,208)
        u.SendMessageW(change,0x00F5,0,0)
        u.PostMessageW(change,0x100,27,0)
        u.SendMessageW(dialog,0,0,0)
        assert u.IsWindowVisible(dialog) and b"CaptureKey=" not in ini.read_bytes()
        u.SendMessageW(change,0x00F5,0,0)
        u.PostMessageW(change,0x100,0x86,0)
        wait(lambda: b"CaptureKey=134" in ini.read_bytes(),"Новая клавиша F23 не сохранилась")
        assert b"UserValue=preserved" in ini.read_bytes()
        u.PostMessageW(change,0x100,0x71,0)
        u.GetWindow.argtypes=[w.HWND,w.UINT]
        u.GetWindow.restype=w.HWND
        def folder_picker():
            hwnd=0
            while hwnd:=u.FindWindowExW(None,hwnd,"#32770",None):
                if u.GetWindow(hwnd,4)==dialog:
                    return hwnd
        picker=wait(folder_picker,"F2 не открыл выбор папки поверх настроек")
        u.PostMessageW(picker,0x10,0,0)
        wait(lambda: not folder_picker(),"Выбор папки не закрылся")
        assert u.IsWindowVisible(dialog)
        print("Hotkey can be cancelled or saved, folder picker is owned by settings")
        bounds=w.RECT()
        u.GetWindowRect(w.HWND(dialog),c.byref(bounds))
        def capture():
            u.keybd_event(0x86,0,0,0)
            u.keybd_event(0x86,0,2,0)
        capture()
        wait(lambda: not u.IsWindowVisible(dialog),"Захват не скрыл настройки")
        wait(lambda: u.IsWindowVisible(editor),"Редактор не открылся из настроек")
        u.PostMessageW(editor,0x100,27,0)
        wait(lambda: u.IsWindowVisible(dialog),"Закрытие захвата не вернуло исходный диалог")
        restored=w.RECT()
        u.GetWindowRect(w.HWND(dialog),c.byref(restored))
        assert bytes(bounds)==bytes(restored) and not u.IsWindowVisible(editor)
        capture()
        wait(lambda: u.IsWindowVisible(editor),"Повторный захват не открылся")
        wait(lambda: u.GetForegroundWindow()==editor,"Редактор не получил фокус")
        for msg,buttons,x,y in [(0x201,1,200,220),(0x200,1,850,620),(0x202,0,850,620)]:
            u.SendMessageW(editor,msg,buttons,(y<<16)|x)
        for vk,flags in [(0x11,0),(ord("S"),0),(ord("S"),2),(0x11,2)]:
            u.keybd_event(vk,0,flags,0)
        wait(lambda: u.IsWindowVisible(dialog),"Сохранение не вернуло исходный диалог")
        saved=list((exe.parent/"shots").glob("*.png"))
        assert len(saved)==1 and saved[0].read_bytes().startswith(b"\x89PNG\r\n\x1a\n")
        print("Ctrl+S writes PNG and returns to settings")
        u.PostMessageW(dialog,0x10,0,0)
        wait(lambda: not u.IsWindowVisible(dialog),"Настройки не закрылись")
        choose(0)
        wait(lambda: not find("#32768", process.pid),"Команда захвата не закрыла меню")
        wait(lambda: u.IsWindowVisible(editor),"Команда меню не открыла захват")
        u.PostMessageW(editor,0x100,27,0)
        wait(lambda: not u.IsWindowVisible(editor),"Редактор не закрылся")
        assert not u.IsWindowVisible(dialog) and not find("#32768",process.pid)
        choose(3)
        assert process.wait(timeout=4)==0
        print("Capture returns to the same settings window and bounds, tray capture finishes in tray, Exit stops process")
    finally:
        if process.poll() is None:
            process.terminate()
        process.wait(timeout=5)
