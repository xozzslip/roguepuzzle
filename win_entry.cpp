#include <Windows.h>
#include <stdint.h>
#include <tchar.h>
#include <stdio.h>
#include <strsafe.h>

static uint32_t* BitmapMemory;
static BITMAPINFO BitmapInfo;
static int WindowHeight;
static int WindowWidth;
static bool Running = true;

typedef struct {
    char* name;
    int width;
    int height;
    uint32_t* pixels;
} Image;

void DebugLog(const char* format, ...) {
    char s[256];
    va_list argptr;
    va_start(argptr, format);
	StringCchVPrintfA(s,
			256,
			format,
			argptr);

	va_end(argptr);
    OutputDebugString(s);
    return;
}

// s can be not terminated
bool StringEqualTo(char* s, const char* sample) {
    int i = 0;
    for (;;) {
        if (sample[i] == '\0') {
            break;
        }
        if (s[i] != sample[i]) {
            return false;
        }
        i += 1;
    }
    return true;
}

void FatalError(const char *text) {
    LPVOID lpMsgBuf;
    DWORD dw = GetLastError(); 
    FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | 
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        dw,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPTSTR) &lpMsgBuf,
        0, NULL );
    DebugLog("%s: %s", text, lpMsgBuf);
	ExitProcess(1);
}


void ParseBMP(uint8_t* bytes, int bytesCount, Image* image) {
	if (!StringEqualTo((char*)bytes, "BM")) {
		FatalError("invalid bmp header");
	}
	int headerSize = *((int32_t*)(bytes + 14));
	int bitsPerPixel = *((int16_t*)(bytes + 28));
	int bmpSize = *((int32_t*)(bytes + 2));
	int pixelsOffset = *((int32_t*)(bytes + 10));
	int width = *((int32_t*)(bytes + 18));
	int height = *((int32_t*)(bytes + 22));
	int compression = *((int32_t*)(bytes + 30));
	if (bytesCount!= bmpSize) {
		FatalError("size of bitmap on the disk and in header are not equal");
	}
	if (headerSize != 40) {
		FatalError("unsupported bmp header format, now only BITMAPINFOHEADER is supported");
	}
	if (pixelsOffset != headerSize + 14) {
		FatalError("bmp has invalid pixelsOffsset and headerSize");
	}
	if (compression != 0) {
		FatalError("compressed are not supported");
	}
	if ((bitsPerPixel % 8) != 0) {
		FatalError("unsupported bmp format, bits per pixel must be devidible by 8");
	}
	if (bitsPerPixel != 24) {
		FatalError("unsupported bmp format, only 24 bits per pixel are available");
	}
	int bytesPerPixel = bitsPerPixel / 8;
	uint32_t *pixels = (uint32_t *)VirtualAlloc(0, width * height * 4, MEM_COMMIT, PAGE_READWRITE);
    if (pixels == NULL) {
        FatalError("failed to allocate array for bmp pixels");
    }
	int oneRowSize = bytesPerPixel * width;
	int allignedRowSize = oneRowSize;
	if (oneRowSize % 4 != 0) {
		allignedRowSize = ((oneRowSize / 4) + 1) * 4;
	}
	if (allignedRowSize * height + pixelsOffset != bytesCount) {
		FatalError("bmp file size is not equal to estimated header size + pixels size");
	}
	// pixels stored bottom to top
    for (int i = 0; i < height; i++) {
		int offset = pixelsOffset + allignedRowSize * (height - i - 1);
		for (int j = 0; j < width; j++) {
            uint32_t pixel = *(uint32_t*)(bytes + offset + j * bytesPerPixel);
            
			uint8_t blue = *((uint8_t *)&pixel);
			uint8_t green = *(((uint8_t *)&pixel) + 1);
			uint8_t red = *(((uint8_t *)&pixel) + 2);
            uint8_t unused = *(((uint8_t*)&pixel) + 3);
            pixel = pixel & 0x00ffffff;
            DebugLog("pixel x=%d y=%d p=%x b=%x g=%x r=%x u=%x\n", j, i, pixel, blue, green, red, unused);
            pixels[i * width + j] = pixel;
		}
	}
	image->width = width;
	image->height = height;
    image->pixels = pixels;
    DebugLog("parsed bmp image width=%d height=%d headerSize=%d bitsPerPixel=%d pixelsOffset=%d allignedRowSize=%d\n", width, height, headerSize, bitsPerPixel, pixelsOffset, allignedRowSize);
    return;
}


LRESULT WindowProcA(
    HWND   hWnd,
    UINT   msg,
    WPARAM wParam,
    LPARAM lParam
) {
    LRESULT result = 0;
    switch (msg) {
    case WM_SIZE:
    {
        LONG width = (LONG) LOWORD(lParam);
        LONG height = (LONG) HIWORD(lParam);
        if (BitmapMemory) {
            VirtualFree(BitmapMemory, 0, MEM_RELEASE);
        }
        BitmapMemory = (uint32_t *) VirtualAlloc(0, 4 * width * height, MEM_COMMIT, PAGE_READWRITE);
        BitmapInfo.bmiHeader.biSize = sizeof(BitmapInfo.bmiHeader);
        BitmapInfo.bmiHeader.biWidth = width;
        BitmapInfo.bmiHeader.biHeight = -height;
        BitmapInfo.bmiHeader.biPlanes = 1;
        BitmapInfo.bmiHeader.biBitCount = 32;
        BitmapInfo.bmiHeader.biCompression = BI_RGB;
        BitmapInfo.bmiHeader.biSizeImage = 0;
        BitmapInfo.bmiHeader.biXPelsPerMeter = 0;
        BitmapInfo.bmiHeader.biYPelsPerMeter = 0;
        BitmapInfo.bmiHeader.biClrUsed = 0;
        BitmapInfo.bmiHeader.biClrImportant = 0;
        WindowHeight = height;
        WindowWidth = width;
    } break;
    case WM_QUIT: 
    {
        Running = false;
    } break;
    case WM_DESTROY:
    {
        Running = false;
    } break;
    default:
        result = DefWindowProc(hWnd, msg, wParam, lParam);
        break;
    }
    return result;
}




int WinMain(
    HINSTANCE hInstance,
    HINSTANCE hPrevInstance,
    LPSTR     lpCmdLine,
    int       nShowCmd)
{
    const int MAX_IMAGES = 1000;
    int imagesCount = 0;
    Image images[MAX_IMAGES] = {};
    WIN32_FIND_DATA fileMetadata = {};
    HANDLE dirHandle = FindFirstFile("assets\\*", &fileMetadata);
    if (INVALID_HANDLE_VALUE == dirHandle) {
        FatalError("failed to open assets directory\n");
    }
    do {
        char filePath[MAX_PATH]; 
        if (StringCchPrintfA(filePath, MAX_PATH, "assets\\%s", fileMetadata.cFileName) < 0) {
            FatalError("large assets are not supported\n");
        }   
        DebugLog("reading file with name=\"%s\" path=\"%s\"\n", fileMetadata.cFileName, (char *)filePath);
        if (imagesCount == MAX_IMAGES) {
            FatalError("maximum amount of assets exceeded\n");
        }
        if (fileMetadata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            continue;
        }
        if ((fileMetadata.nFileSizeHigh) != 0) {
            FatalError("large assets are not supported\n");
        }
        int fileSize = fileMetadata.nFileSizeLow;
        if (fileSize == 0) {
            FatalError("assets with size=0 bytes are not supported\n");
        }
        char* fileName = fileMetadata.cFileName;
        HANDLE fileHandle = CreateFile(filePath,               // file to open
            GENERIC_READ,          // open for reading
            FILE_SHARE_READ,       // share for reading
            NULL,                  // default security
            OPEN_EXISTING,         // existing file only
            FILE_ATTRIBUTE_NORMAL, // normal file
            NULL);                 // no attr. template
        if (fileHandle == 0 || fileHandle == INVALID_HANDLE_VALUE) {
            FatalError("failed to open asset file\n");
        }
        DWORD readBytes;
        uint8_t* assetContent = (uint8_t *) VirtualAlloc(0, fileSize, MEM_COMMIT, PAGE_READWRITE);
        if (ReadFile(fileHandle, assetContent, fileSize, &readBytes, NULL) <= 0) {
            FatalError("failed to read asset content to memory\n");
        }
        if (int(readBytes) != fileSize) {
            FatalError("failed to fully read asset content\n");
        }
        Image image = images[imagesCount];
        ParseBMP(assetContent, fileSize, &image);
        image.name = fileName;
        imagesCount += 1;
        VirtualFree(assetContent, 0, MEM_RELEASE);

    } while (FindNextFile(dirHandle, &fileMetadata) != 0);
    
    WNDCLASS windowClass = {};
    windowClass.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = WindowProcA;
    windowClass.hInstance = hInstance;
    windowClass.lpszClassName = "awesomeWindowClass";
    ATOM windowClassId = RegisterClass(&windowClass);
    HWND hWnd = CreateWindow(
        windowClass.lpszClassName,
        "Funny little window",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        NULL,
        NULL,
        hInstance,
        NULL);
    if (hWnd == 0) {
        FatalError("failed to create a window\n");
    }
    int timeframe = 0;
    Image image = images[0];
    while (Running) {
        MSG message = {};
        while (PeekMessage(&message, 0, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                Running = false;
            }   
            TranslateMessage(&message);
            DispatchMessage(&message);
        }
        timeframe++;

        for (int i = 0; i < WindowWidth * WindowHeight; i++) {
            int x = i % WindowWidth;
            int y = i / WindowHeight; 

			int32_t pixel = 0;
			int8_t* colors = (int8_t*)&pixel;
            colors[0] = y + timeframe;
		    BitmapMemory[i] = pixel;
        }

        StretchDIBits(
            GetDC(hWnd),
            0, 0, WindowWidth, WindowHeight,
            0, 0, WindowWidth, WindowHeight,
            BitmapMemory,
            &BitmapInfo,
            DIB_RGB_COLORS,
            SRCCOPY
        );
    } 
    return 0;
}

