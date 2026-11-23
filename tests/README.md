# Проверки Sparkshot

Проверки редактора и одновременных запусков используют отдельную оконную станцию Windows с собственным буфером обмена. Из корня проекта:

```powershell
.\build.cmd build
.\build.cmd build SparkshotTestHost
python -B tests/isolated-station.py python -B tests/editor-smoke.py .cache/build/Sparkshot.exe
python -B tests/isolated-station.py python -B tests/concurrent-start.py .cache/build/Sparkshot.exe
python -B tests/isolated-station.py python -B tests/native-ui.py .cache/build/Sparkshot.exe
```

Для проверки фокуса, системных сочетаний и возврата из захвата на рабочем столе запустите `python -B tests/native-ui.py .cache/build/Sparkshot.exe --interactive`. Проверка использует временную копию программы со своими настройками и папкой PNG.
