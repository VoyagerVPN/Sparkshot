"""Проверка холодного запуска обычного EXE на отдельной оконной станции."""
import ctypes
from ctypes import wintypes
from pathlib import Path
import subprocess
import sys
import time

user32 = ctypes.WinDLL("user32", use_last_error=True)
user32.GetProcessWindowStation.restype = wintypes.HANDLE
name = ctypes.create_unicode_buffer(256)
needed = wintypes.DWORD()
assert user32.GetUserObjectInformationW(
    wintypes.HANDLE(user32.GetProcessWindowStation()), 2,
    name, ctypes.sizeof(name), ctypes.byref(needed)
) and name.value.startswith("SparkshotReview_"), "Запустите тест на изолированной станции"

exe = Path(sys.argv[1]).resolve()
children = []
try:
    for _ in range(8):
        children.append(subprocess.Popen([exe, "--autostart"], creationflags=subprocess.CREATE_NO_WINDOW))
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline and sum(p.poll() is None for p in children) > 1:
        time.sleep(.02)
    running = [p for p in children if p.poll() is None]
    assert len(running) == 1, f"Осталось процессов после восьми запусков: {len(running)}"
    assert all(p is running[0] or p.returncode == 0 for p in children)
    user32.FindWindowW.restype = wintypes.HWND
    deadline = time.monotonic() + 5
    while not (window := user32.FindWindowW("Sparkshot.Native", None)) and time.monotonic() < deadline:
        time.sleep(.02)
    pid = wintypes.DWORD()
    user32.GetWindowThreadProcessId(wintypes.HWND(window), ctypes.byref(pid))
    assert pid.value == running[0].pid
    assert not user32.IsWindowVisible(wintypes.HWND(window))
    print("8 concurrent starts, 1 resident process, editor hidden")
finally:
    for child in children:
        if child.poll() is None:
            child.terminate()
        child.wait(timeout=5)
