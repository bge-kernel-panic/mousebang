mousebang.exe: mousebang.c
	gcc -o mousebang.exe -O2 mousebang.c -lgdi32 -Wl,-subsystem,windows
	strip mousebang.exe