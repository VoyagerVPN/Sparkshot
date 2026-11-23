"""Запуск команды на отдельной оконной станции с собственным буфером обмена."""
import ctypes as c
from ctypes import wintypes as w
from pathlib import Path
import os, subprocess, sys, uuid
u=c.WinDLL('user32',use_last_error=True);k=c.WinDLL('kernel32',use_last_error=True)
u.GetProcessWindowStation.restype=w.HANDLE
u.CreateWindowStationW.argtypes=[w.LPCWSTR,w.DWORD,w.DWORD,c.c_void_p];u.CreateWindowStationW.restype=w.HANDLE
u.SetProcessWindowStation.argtypes=[w.HANDLE];u.SetProcessWindowStation.restype=w.BOOL
u.CreateDesktopW.argtypes=[w.LPCWSTR,w.LPCWSTR,c.c_void_p,w.DWORD,w.DWORD,c.c_void_p];u.CreateDesktopW.restype=w.HANDLE
u.CloseDesktop.argtypes=[w.HANDLE];u.CloseWindowStation.argtypes=[w.HANDLE]
k.CreateFileW.argtypes=[w.LPCWSTR,w.DWORD,w.DWORD,c.c_void_p,w.DWORD,w.DWORD,w.HANDLE];k.CreateFileW.restype=w.HANDLE
k.CloseHandle.argtypes=[w.HANDLE]
class SECURITY(c.Structure):_fields_=[('length',w.DWORD),('descriptor',c.c_void_p),('inherit',w.BOOL)]
class STARTUP(c.Structure):_fields_=[('cb',w.DWORD),('reserved',w.LPWSTR),('desktop',w.LPWSTR),('title',w.LPWSTR),('x',w.DWORD),('y',w.DWORD),('xs',w.DWORD),('ys',w.DWORD),('xc',w.DWORD),('yc',w.DWORD),('fill',w.DWORD),('flags',w.DWORD),('show',w.WORD),('reserved2',w.WORD),('reservedptr',c.c_void_p),('stdin',w.HANDLE),('stdout',w.HANDLE),('stderr',w.HANDLE)]
class PROCESS(c.Structure):_fields_=[('process',w.HANDLE),('thread',w.HANDLE),('pid',w.DWORD),('tid',w.DWORD)]
k.CreateProcessW.argtypes=[w.LPCWSTR,w.LPWSTR,c.c_void_p,c.c_void_p,w.BOOL,w.DWORD,c.c_void_p,w.LPCWSTR,c.POINTER(STARTUP),c.POINTER(PROCESS)];k.CreateProcessW.restype=w.BOOL
k.WaitForSingleObject.argtypes=[w.HANDLE,w.DWORD];k.GetExitCodeProcess.argtypes=[w.HANDLE,c.POINTER(w.DWORD)]
station_name='SparkshotReview_'+uuid.uuid4().hex
original=u.GetProcessWindowStation();station=u.CreateWindowStationW(station_name,0,0xF037F,None)
if not station:raise c.WinError(c.get_last_error())
if not u.SetProcessWindowStation(station):raise c.WinError(c.get_last_error())
desktop=u.CreateDesktopW('Review',None,None,0,0xF01FF,None)
if not desktop:raise c.WinError(c.get_last_error())
if not u.SetProcessWindowStation(original):raise c.WinError(c.get_last_error())
temp=Path('.cache/temp').resolve()
temp.mkdir(parents=True,exist_ok=True)
log=temp/('private-'+uuid.uuid4().hex+'.log');security=SECURITY(c.sizeof(SECURITY),None,True)
output=k.CreateFileW(str(log.resolve()),0x40000000,3,c.byref(security),2,0x80,None)
null=k.CreateFileW('NUL',0x80000000,3,c.byref(security),3,0x80,None)
if output==w.HANDLE(-1).value or null==w.HANDLE(-1).value:raise c.WinError(c.get_last_error())
startup=STARTUP();startup.cb=c.sizeof(startup);startup.desktop=station_name+'\\Review';startup.flags=0x100;startup.stdin=null;startup.stdout=output;startup.stderr=output
process=PROCESS();command=c.create_unicode_buffer(subprocess.list2cmdline(sys.argv[1:]))
try:
    os.environ['TEMP']=os.environ['TMP']=str(temp);os.environ['PYTHONDONTWRITEBYTECODE']='1'
    if not k.CreateProcessW(None,command,None,None,True,0x08000000,None,os.getcwd(),c.byref(startup),c.byref(process)):raise c.WinError(c.get_last_error())
    k.WaitForSingleObject(process.process,0xFFFFFFFF);code=w.DWORD();k.GetExitCodeProcess(process.process,c.byref(code))
    k.CloseHandle(process.thread);k.CloseHandle(process.process);k.CloseHandle(output);output=None
    sys.stdout.buffer.write(log.read_bytes());sys.exit(code.value)
finally:
    if output:k.CloseHandle(output)
    k.CloseHandle(null);u.CloseDesktop(desktop);u.CloseWindowStation(station)
