#include <Windows.h>
#include <stdint.h>
#include <tchar.h>
#include <stdio.h>
#include <strsafe.h>


typedef struct {
    char name[MAX_PATH];
    int width;
    int height;
    uint32_t* pixels;
} Image;

typedef struct {
    int id;
    int x;
    int y;
    int scaledWidth; // scaling goes first
    int scaledHeight;
    float rotate; // then rotate the image
    int width; // resulting size of scaled and rotated
    int height;
    Image *image;
    uint32_t* pixels; // cache for scaled and rotated image
} Entity;

static uint32_t* BitmapMemory;
static BITMAPINFO BitmapInfo;
static int WindowHeight;
static int WindowWidth;
static bool Running = true;
static const int MAX_ENTITIES = 1000;
static Entity entities[MAX_ENTITIES];
static int entitiesCount;
static int entityId;
static const int MAX_IMAGES = 1000;
static int imagesCount;
static Image images[MAX_IMAGES];


bool StringEqualTo(char* s, const char* sample);

Entity* CreateEntity(const char* bmpName) {
    bool found = false;
    Image* image = NULL;
    for (int i = 0; i < imagesCount; i++) {
        image = &images[i];
        if (StringEqualTo(image->name, bmpName)) {
            found = true;
            break;
        }
    }
    if (!found) {
        return NULL;
    }
    Entity* entity = &entities[entitiesCount];
    entity->image = image;
    entity->id = entityId;
    entity->scaledWidth = image->width;
    entity->scaledHeight= image->height;
    entity->rotate = 0;
    entity->width = image->width;
    entity->height = image->height;
    entity->x = 0;
    entity->y = 0;
    entity->pixels = (uint32_t*)VirtualAlloc(0, image->width * image->height * 4, MEM_COMMIT, PAGE_READWRITE);
    for (int i = 0; i < image->width * image->height; i++) {
        entity->pixels[i] = image->pixels[i];
    }
    entityId++;
    entitiesCount++;
    return entity;
}

void MoveEntity(Entity* entity, int x, int y) {
    entity->x = x;
    entity->y = y;
}

void ResizeEntity(Entity* entity, int newWidth, int newHeight) {
    entity->pixels = (uint32_t*)VirtualAlloc(0, newWidth * newHeight * 4, MEM_COMMIT, PAGE_READWRITE);
    entity->width = newWidth;
    entity->height = newHeight;
    int originWidth = entity->image->width;
    int originHeight = entity->image->height;
    float scaleX = float(newWidth) / float(originWidth);
    float scaleY = float(newHeight) / float(originHeight);
    for (int i = 0; i < newWidth * newHeight; i++) {
        int newX = i % newWidth;
        int newY = i / newWidth;
        int originX = int(float(newX) / scaleX);
        int originY = int(float(newY) / scaleY);
        int originI = originX + originY * originWidth;
        entity->pixels[i] = entity->image->pixels[originI];
    }

}


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

void FatalError(const char* format, ...) {
    LPVOID lpMsgBuf;
    DWORD dw = GetLastError();
    FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        dw,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPTSTR)&lpMsgBuf,
        0, NULL);

    char s[256];
    va_list argptr;
    va_start(argptr, format);
    StringCchVPrintfA(s,
        256,
        format,
        argptr);

    va_end(argptr);
    DebugLog("%s: %s", s, lpMsgBuf);
    ExitProcess(1);
}


void UnpackBitmapBytes(uint8_t* bytes, int bytesCount, Image* image) {
    if (!StringEqualTo((char*)bytes, "BM")) {
        FatalError("invalid bmp header\n");
    }

    BITMAPFILEHEADER* header = (BITMAPFILEHEADER*)bytes;
    int bmpSize = header->bfSize;
    int pixelsOffset = header->bfOffBits;
    int headerSize = *((int32_t*)(bytes + 14));
    int bitsPerPixel;
    int width;
    int height;
    int compression;
    int i = BI_JPEG;
    if (headerSize == 124) {
        BITMAPV5HEADER* info = (BITMAPV5HEADER*)(bytes + sizeof(BITMAPFILEHEADER));
        bitsPerPixel = info->bV5BitCount;
        width = info->bV5Width;
        height = info->bV5Height;
        compression = info->bV5Compression;
    }
    else if (headerSize == 40) {
        BITMAPINFOHEADER* info = (BITMAPINFOHEADER*)(bytes + sizeof(BITMAPFILEHEADER));
        bitsPerPixel = info->biBitCount;
        width = info->biWidth;
        height = info->biHeight;
        compression = info->biCompression;
    }
    else {
        FatalError("unsupported bmp header format, now only BITMAPINFOHEADER/BITMAPV5HEADER is supported, but received %d bytes header\n");
    }
    if (bytesCount != bmpSize) {
        FatalError("size of bitmap on the disk and in header are not equal\n");
    }
    if (pixelsOffset != headerSize + 14) {
        FatalError("bmp has invalid pixelsOffsset and headerSize\n");
    }
    if ((bitsPerPixel % 8) != 0) {
        FatalError("unsupported bmp format, bits per pixel must be devidible by 8\n");
    }
    if (bitsPerPixel != 24 && bitsPerPixel != 32) {
        FatalError("unsupported bmp format, only 24/32 bits per pixel are available\n");
    }
    int bytesPerPixel = bitsPerPixel / 8;
    uint32_t* pixels = (uint32_t*)VirtualAlloc(0, width * height * 4, MEM_COMMIT, PAGE_READWRITE);
    if (pixels == NULL) {
        FatalError("failed to allocate array for bmp pixels\n");
    }
    int oneRowSize = bytesPerPixel * width;
    int allignedRowSize = oneRowSize;
    if (oneRowSize % 4 != 0) {
        allignedRowSize = ((oneRowSize / 4) + 1) * 4;
    }
    if (allignedRowSize * height + pixelsOffset != bytesCount) {
        FatalError("bmp file size is not equal to estimated header size + pixels size\n");
    }
    // pixels stored bottom to top
    for (int i = 0; i < height; i++) {
        int offset = pixelsOffset + allignedRowSize * (height - i - 1);
        for (int j = 0; j < width; j++) {
            uint32_t pixel = *(uint32_t*)(bytes + offset + j * bytesPerPixel);
            uint8_t blue = *((uint8_t*)&pixel);
            uint8_t green = *(((uint8_t*)&pixel) + 1);
            uint8_t red = *(((uint8_t*)&pixel) + 2);
            uint8_t alpha = *(((uint8_t*)&pixel) + 3);

            if (bitsPerPixel == 24) {
                pixel = pixel & 0x00ffffff;
            }
            //DebugLog("pixel x=%d y=%d p=%x b=%x g=%x r=%x a=%x\n", j, i, pixel, blue, green, red, alpha);
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
        Image *image = &images[imagesCount];
        char* fileName = fileMetadata.cFileName;
        UnpackBitmapBytes(assetContent, fileSize, image);
        StringCchCopy(image->name, MAX_PATH, fileName);
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
    if (CreateEntity("character.bmp") == NULL) {
        FatalError("failed to create character entity\n");
    }
    /*
    if (CreateEntity("curve.bmp") == NULL) {
        FatalError("failed to create character entity\n");
    }
    */
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
            BitmapMemory[i] = 0;
        }
        for (int i = 0; i < entitiesCount; i++) {
            Entity *entity = &entities[i];
            ResizeEntity(entity, WindowWidth, WindowHeight);
            for (int innerIndex = 0; innerIndex < entity->width * entity->height; innerIndex++) {
                int innerX = innerIndex % entity->width;
                int innerY = innerIndex / entity->width;

                int screenX = entity->x + innerX;
                int screenY = entity->y + innerY;
                int screenIndex = screenX + screenY * WindowWidth;
                BitmapMemory[screenIndex] = entity->pixels[innerIndex];
            }
        }
        
        /*
        Image image = images[0];
        for (int i = 0; i < WindowWidth * WindowHeight; i++) {
            int y = i / WindowWidth; 
            int x = i - y * WindowWidth; 
            int imageX = x % image.width;
            int imageY = y % image.height;
            int imageI = imageX + imageY * image.width;
            BitmapMemory[i] = image.pixels[imageI];
        }
        */


        

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

