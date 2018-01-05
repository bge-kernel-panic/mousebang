#define _UNICODE
#define UNICODE
#define _WIN32_WINNT _WIN32_WINNT_WINXP
#include <windows.h>
#include <tchar.h>
#include <stdarg.h>
#include <stdio.h>
#include <wchar.h>
#include <stdlib.h>

#define THRESHOLD 50.0

const TCHAR initial_message[] = TEXT("Press space to start capture\r\n");

LARGE_INTEGER freq;

TCHAR* buffer = NULL;
int buffer_size = -1;

typedef enum {
	CAPTURE_START,
	MOUSE_A_CAPTURE,
	MOUSE_B_CAPTURE,
	CAPTURING,
	PAUSED
} status_t;


typedef struct {
	HANDLE device;
	USHORT buttons;
} event_t;

typedef struct {
	HANDLE device;
	DWORD id;
	USHORT button_mask;
	LARGE_INTEGER last_event;
	TCHAR* name;
} device_info_t;

void grow(int sz) {
	buffer_size += sz;
	buffer = (TCHAR*) realloc(buffer, buffer_size * sizeof(TCHAR));
}

void reset() {
	if(buffer != NULL)
		free(buffer);
	buffer = (TCHAR*) malloc(sizeof(TCHAR));
	*buffer = TEXT('\0');
	buffer_size = 1;
}

void prepend(const TCHAR* data, int cnt) {
	
	if(cnt > 0 && data[cnt - 1] == TEXT('\0'))
		--cnt;
	if(cnt == 0)
		return;
	
	int old_size = buffer_size;
	grow(cnt);
	// ensure we don't include NUL at end when moving, we want it to stay in place
	if(old_size > 0) {
		memmove(((BYTE*) buffer) + cnt * sizeof(TCHAR),
			buffer,
			old_size * sizeof(TCHAR));
	}
	memcpy(buffer, data, cnt * sizeof(TCHAR));
}

void bprintf(const TCHAR* format, ...) {
	TCHAR l_buffer[1024];
	va_list args;
	va_start(args, format);
	// grow buffer by format characters
	// we don't count the NUL because we already have it in there
	int sz = vsnwprintf(l_buffer, 1023, format, args);
	l_buffer[1023] = '\0';
	if(sz > 1023)
		sz = 1023;
	prepend(l_buffer, sz);
	va_end(args);
}

#define BUTTON_DOWN_MASK (RI_MOUSE_BUTTON_1_DOWN | RI_MOUSE_BUTTON_2_DOWN | RI_MOUSE_BUTTON_3_DOWN | RI_MOUSE_BUTTON_4_DOWN | RI_MOUSE_BUTTON_5_DOWN)
int extract_event(LPARAM wm_input_lParam, event_t* out) {
	RAWINPUT* raw_input_data;
	UINT raw_input_size = 0;
	HRAWINPUT hraw = (HRAWINPUT) wm_input_lParam;
	
	GetRawInputData(hraw, RID_INPUT, NULL, &raw_input_size, sizeof(RAWINPUTHEADER));
	raw_input_data = (RAWINPUT*) calloc(raw_input_size, 1);
	GetRawInputData(hraw, RID_INPUT, raw_input_data, &raw_input_size, sizeof(RAWINPUTHEADER));
	
	
	/* extract what we need here */
	if(raw_input_data->header.dwType != RIM_TYPEMOUSE) {
		free(raw_input_data);
		return 0;
	}
	
	/* mouse event; we only check events that have mouse buttons 1-5 down */
	if((raw_input_data->data.mouse.usButtonFlags & BUTTON_DOWN_MASK) == 0) {
		free(raw_input_data);
		return 0;
	}
	
	/* OK we're interested now.  Grab the handle, find the device information */
	out->device = raw_input_data->header.hDevice;
	out->buttons = raw_input_data->data.mouse.usButtonFlags & BUTTON_DOWN_MASK;
	free(raw_input_data);
	return 1;
}

void get_full_event_info(const event_t* event, device_info_t* out) {
	static TCHAR NUL[] = {TEXT('\0')};
	
	RID_DEVICE_INFO device_info = {sizeof(RID_DEVICE_INFO), 0};
	UINT device_name_size = 0;
	UINT device_info_size = sizeof(RID_DEVICE_INFO);
	
	GetRawInputDeviceInfo(event->device,
			      RIDI_DEVICEINFO,
			      &device_info,
			      &device_info_size);
	out->device = event->device;
	out->id = device_info.mouse.dwId;
	out->button_mask = event->buttons & BUTTON_DOWN_MASK;
	out->name = NUL;
	GetRawInputDeviceInfo(event->device,
			      RIDI_DEVICENAME,
			      NULL,
			      &device_name_size);
	out->name = (TCHAR*) calloc((device_name_size + 1) * sizeof(TCHAR), 1);
	GetRawInputDeviceInfo(event->device,
			      RIDI_DEVICENAME,
			      out->name,
			      &device_name_size);
}

int match_event_single(const event_t* event, const device_info_t* device_info_candidate) {
	RID_DEVICE_INFO device_info = {sizeof(RID_DEVICE_INFO), 0};
	UINT device_info_size = sizeof(RID_DEVICE_INFO);
	GetRawInputDeviceInfo(event->device,
			      RIDI_DEVICEINFO,
			      &device_info,
			      &device_info_size);
	if(device_info.dwType != RIM_TYPEMOUSE) {
		return -1;
	}
	return device_info.mouse.dwId == device_info_candidate->id &&
	   event->device == device_info_candidate->device &&
	   (event->buttons & device_info_candidate->button_mask) != 0;
}

int match_event(const event_t* event, device_info_t** device_infos) {
	RID_DEVICE_INFO device_info = {sizeof(RID_DEVICE_INFO), 0};
	UINT device_info_size = sizeof(RID_DEVICE_INFO);
	/* extract device ID from the device handle */
	GetRawInputDeviceInfo(event->device,
			      RIDI_DEVICEINFO,
			      &device_info,
			      &device_info_size);
	if(device_info.dwType != RIM_TYPEMOUSE) {
		bprintf(TEXT("Error getting device info for handle %p; please close\r\n"), (void*) event->device);
		return -2;
	}
	
	for(int i = 0; device_infos[i] != NULL; ++i) {
		if(device_infos[i]->id == device_info.mouse.dwId && 
		   device_infos[i]->device == event->device &&
		   event->buttons == device_infos[i]->button_mask) {
			QueryPerformanceCounter(&device_infos[i]->last_event);
			return i;
		}
	}
	return -1;
}

double delta_ms(const LARGE_INTEGER* start, const LARGE_INTEGER* end) {
	return ((start->QuadPart - end->QuadPart) * 1000.0) / freq.QuadPart;
}

void reset_events(device_info_t** device_infos) {
	for(int i = 0; device_infos[i] != NULL; ++i) {
		device_infos[i]->last_event.QuadPart = 0;
	}
}

void swap(TCHAR* lhs, TCHAR* rhs) {
	if(lhs == rhs || *lhs == *rhs)
		return;
	*lhs ^= *rhs;
	*rhs ^= *lhs;
	*lhs ^= *rhs;
}

void reverse(TCHAR* buffer, TCHAR* last) {
	while(buffer < last) {
		swap(buffer, last);
		++buffer;
		--last;
	}
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	static status_t current_status = CAPTURE_START;
	static device_info_t device_a, device_b;
	static device_info_t* device_data[] = { &device_a, &device_b, NULL };
	static TCHAR AB = TEXT('A');
	static device_info_t null_device = {0};
	
	int default_process = 1;
	int invalidate = 0;
	
	if(msg == WM_CLOSE || (msg == WM_KEYDOWN && wParam == VK_ESCAPE)) {
		PostQuitMessage(0);
		return 0L;
	}
	if(msg == WM_PAINT) {
		HDC hdc;
		PAINTSTRUCT ps;
		RECT rect;
		
		hdc = BeginPaint(hWnd, &ps);
		GetClientRect(hWnd, &rect);
		FillRect(hdc, &rect, (HBRUSH) GetStockObject(WHITE_BRUSH));
		DrawText(hdc,
			 buffer,
			 -1,
			 &rect,
			 DT_TOP | DT_LEFT | DT_WORDBREAK);
		EndPaint(hWnd, &ps);
		return 0L;
	}
	
	switch(current_status) {
		case CAPTURE_START:
			if(msg == WM_KEYDOWN && wParam == VK_SPACE) {
				reset();
				AB = 'A';
				device_a = null_device;
				device_b = null_device;
				bprintf(TEXT("Press mouse A button\r\n"));
				current_status = MOUSE_A_CAPTURE;
				default_process = 0;
				invalidate = 1;
			} else if(msg == WM_KEYDOWN && wParam == VK_RETURN) {
				HGLOBAL data;
				TCHAR* data_locked;
				OpenClipboard(hWnd);
				data = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, buffer_size * sizeof(TCHAR));
				data_locked =  (TCHAR*) GlobalLock(data);
				if(!data_locked) {
					GlobalFree(data);
				} else {
					if(EmptyClipboard()) {
						TCHAR* last = data_locked;
						memcpy(data_locked, buffer, buffer_size * sizeof(TCHAR));
						/* reverse lines */
						/* step 1 each line's characters */
						do {
							TCHAR* prev = last;
							last = _tcschr(prev, '\n');
							reverse(prev, last == NULL ? data_locked + buffer_size - 2 : last);
							if(last != NULL) ++last;
						} while(last && last < data_locked + buffer_size);
						/* step 2 reverse whole buffer */
						reverse(data_locked, data_locked + buffer_size - 2);
						GlobalUnlock(data);
						// send to clipboard
						SetClipboardData(CF_UNICODETEXT, data);
					} else {
						GlobalUnlock(data);
						GlobalFree(data);
					}
				}
				
				CloseClipboard();
			}
			break;
		case MOUSE_A_CAPTURE:
		case MOUSE_B_CAPTURE:
			if(msg == WM_INPUT) {
				event_t event;
				device_info_t* ptr = device_data[current_status - MOUSE_A_CAPTURE];
				default_process = 0;
				if(extract_event(lParam, &event)) {
					if(match_event(&event, device_data) == -1) {
						get_full_event_info(&event, ptr);
						bprintf(TEXT("Button %lc: mask 0x%x, handle 0x%p, id 0x%x, name %s\r\n"),
							AB,
							(int) ptr->button_mask,
							(void *) ptr->device,
							(int) ptr->id,
							ptr->name);
						++current_status;
						++AB;
						if(current_status < CAPTURING) {
							bprintf(TEXT("Press mouse %c button\r\n"), AB);
						} else {
							bprintf(TEXT("Capturing, press space to stop\r\n"));
						}
						invalidate = 1;
				 	}
				}
			}
			break;
		case CAPTURING:
			if(msg == WM_KEYDOWN && wParam == VK_SPACE) {
				bprintf(TEXT("Finished.  Press space again to restart, enter to copy to clipboard\r\n"));
				current_status = CAPTURE_START;
				default_process = 0;
				invalidate = 1;
			} else if(msg == WM_INPUT) {
				event_t event;
				if(extract_event(lParam, &event)) {
					/* find matching item */
					int pressed = 0;
					int i = match_event(&event, device_data);
					if(i >= 0) {
						double delta = delta_ms(&device_a.last_event, &device_b.last_event);
						if(device_a.last_event.QuadPart != 0 && device_b.last_event.QuadPart != 0) {
							if(-THRESHOLD < delta && delta < THRESHOLD) {
								bprintf(TEXT("A - B = %f\r\n"), delta);
								invalidate = 1;
							}
							reset_events(device_data);
						}
					}
				}
			}
			break;
	}

	if(invalidate)
		InvalidateRect(hWnd, NULL, TRUE);
	if(default_process) {
		return DefWindowProc(hWnd, msg, wParam, lParam);
	}
	return 0L;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
		   LPSTR cmdLine, int cmdShow) {
	MSG msg = {0};
	WNDCLASS wc = {0};
	RAWINPUTDEVICE rid;
	
	
	wc.lpfnWndProc = WndProc;
	wc.lpszClassName = TEXT("ButtonLatencyTester");
	wc.hInstance = hInstance;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	wc.hbrBackground = (HBRUSH) GetStockObject(WHITE_BRUSH);
	RegisterClass(&wc);
	
	HWND windowHandle = CreateWindow(wc.lpszClassName, TEXT("MouseBang! Press ESC to exit (window must have focus, might need to alt-tab)"), WS_OVERLAPPEDWINDOW, 
				CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
				NULL, NULL, hInstance, NULL);
	
	
	/* ready heap */
	reset();
	prepend(initial_message, sizeof(initial_message));
	CopyMemory(buffer, initial_message, sizeof(initial_message));
	
	QueryPerformanceFrequency(&freq);
	
	rid.usUsagePage = 0x01;  /* see https://msdn.microsoft.com/en-us/library/ff543440.aspx */
	rid.usUsage = 0x02;
	rid.dwFlags = RIDEV_INPUTSINK;
	rid.hwndTarget = windowHandle;
	
	
	RegisterRawInputDevices(&rid, 1, sizeof(rid));
	ShowWindow(windowHandle, SW_SHOW);
	while(GetMessage(&msg, NULL, 0, 0))
		DispatchMessage(&msg);
	
	DestroyWindow(windowHandle);
	
	free(buffer);
	return msg.wParam;
}