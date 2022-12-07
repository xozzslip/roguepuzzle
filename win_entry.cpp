#include <Windows.h>
#include <stdint.h>
#include <tchar.h>
#include <stdio.h>
#include <strsafe.h>
#include <math.h>
#include <windowsx.h>


typedef struct {
    int x;
    int y;
} Vector;

typedef struct {
    int cornerX;
    int cornerY;
    int effectiveWidth;
    int effectiveHeight;
    double angleSin;
    double angleCos;
} RenderDetails;


typedef struct {
    char name[MAX_PATH];
    int width;
    int height;
    uint32_t* pixels;
} Image;


typedef struct {
    int centerX;
    int centerY;
    double angle;
    int width;
    int height;
} Transform;


typedef struct {
    bool up;
    bool down;
    bool left;
    bool right;
    int mouseX; 
    int mouseY;
} UserInput;

typedef int EntityID;

bool StringEqualTo(char* s, const char* sample);
void DebugLog(const char* format, ...);
void FatalError(const char* format, ...);

static uint32_t* BitmapMemory;
static BITMAPINFO BitmapInfo;
static int WindowHeight;
static int WindowWidth;
static bool Running = true;
static const int MAX_ENTITIES = 1000;
static Transform transforms[MAX_ENTITIES];
static Image images[MAX_ENTITIES];
static RenderDetails renderCache[MAX_ENTITIES];
static int entitiesCount;
static const int MAX_IMAGES = 1000;
static int imagesCount;
static const double PI = double(3.141592653589793);
static Image allImages[MAX_IMAGES];
static UserInput Input;
static Vector DefaultOrientation = { 0, 1 };


Image GetImage(const char* bmpName) {
    bool found = false;
    Image* image = NULL;
    for (int i = 0; i < imagesCount; i++) {
        image = &allImages[i];
        if (StringEqualTo(image->name, bmpName)) {
            found = true;
            break;
        }
    }
    if (!found) {
        FatalError("failed to create entity %s", bmpName);
    }
    return *image;
}


Vector RotateVector(Vector vector, double sinAlpha, double cosAlpha) {
    // signs are specific for our coordinate system
    int newX = int(double(vector.x) * cosAlpha + double(vector.y) * sinAlpha);
    int newY = int(-double(vector.x) * sinAlpha + double(vector.y) * cosAlpha);
    Vector result = { newX, newY };
    return result;
}


double VectorLength(int x, int y) {
    return sqrt(double(x) * double(x) + double(y) * double(y));
}

double AngleBetween(int fromX, int fromY, int toX, int toY){
    double lengthFrom = VectorLength(fromX, fromY);
    double lengthTo = VectorLength(toX, toY);
    double cosAlpha = double(fromX) * double(toX) + double(fromY) * double(toY) / lengthFrom / lengthTo;
    double alpha = acos(cosAlpha);
    if (double(fromX) * double(toY) > double(fromY) * double(toX)) {
        alpha = -alpha;
    }
    return alpha;
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
    case WM_KEYDOWN:
    {
        if (wParam == VK_ESCAPE) {
            Running = false;
        }
        else if (wParam == 'W') {
            Input.up = true;
        }
        else if (wParam == 'D') {
            Input.right = true;
        }
        else if (wParam == 'S') {
            Input.down = true;
        }
        else if (wParam == 'A') {
            Input.left = true;
        }
    } break;
    case WM_KEYUP:
    {
        if (wParam == 'W') {
            Input.up = false;
        }
        else if (wParam == 'D') {
            Input.right = false;
        }
        else if (wParam == 'S') {
            Input.down = false;
        }
        else if (wParam == 'A') {
            Input.left = false;
        }
    } break;
    case WM_MOUSEMOVE:
    {
		Input.mouseX = GET_X_LPARAM(lParam); 
		Input.mouseY = GET_Y_LPARAM(lParam);
    } break;
    default:
        result = DefWindowProc(hWnd, msg, wParam, lParam);
        break;
    }
    return result;
}


EntityID AddEntity() {
    EntityID id = entitiesCount;
    entitiesCount++;
    return id;
}

void RenderToMemory(EntityID cam) {
    // BitmapMemory[screenIndex] = entityPixel;
    Transform camTransform = transforms[cam];
	double cosCamAngle = cos(camTransform.angle);
	double sinCamAngle = sin(camTransform.angle);
    for (EntityID entity = 0; entity < entitiesCount; entity++) {
        Transform entityTransform = transforms[entity];
        Image entityImage = images[entity];
        if (entityImage.width == 0 || entityImage.height == 0 || entityTransform.width == 0 || entityTransform.height == 0) {
            continue;
        }
        double entityToCamAngle = camTransform.angle - entityTransform.angle;
		double cosEntityToCamAngle = cos(entityToCamAngle);
		double sinEntityToCamAngle = sin(entityToCamAngle);
        double entityToCameraRotatedX = camTransform.centerX - entityTransform.centerX;
        double entityToCameraRotatedY =  camTransform.centerY - entityTransform.centerY;
        double entityToCamX = entityToCameraRotatedX * cosCamAngle + entityToCameraRotatedY * (-1) * sinCamAngle;
        double entityToCamY = -entityToCameraRotatedX * (-1) * sinCamAngle + entityToCameraRotatedY * cosCamAngle;
        double camScaleX = double(camTransform.width) / double(WindowWidth);
        double camScaleY = double(camTransform.height) / double(WindowHeight);
        double imageScaleX = double(entityImage.width) / double(entityTransform.width);
        double imageScaleY = double(entityImage.height) / double(entityTransform.height);
        for (int windowX = 0; windowX < WindowWidth; windowX ++) {
            for (int windowY = 0; windowY < WindowHeight; windowY++) {
                double pixelX = (double(windowX) - double(WindowWidth) / 2) * camScaleX;
                double pixelY = (double(windowY) - double(WindowHeight) / 2) * camScaleY;
                double entityToPixelX = entityToCamX + pixelX;
                double entityToPixelY = entityToCamY + pixelY;
				double entityPixelX = entityToPixelX * cosEntityToCamAngle + entityToPixelY * (-1) * sinEntityToCamAngle;
				double entityPixelY = -entityToPixelX * (-1) * sinEntityToCamAngle + entityToPixelY * cosEntityToCamAngle;
                double imageXd = entityPixelX * imageScaleX;
                double imageYd = entityPixelY * imageScaleY;
                imageXd += double(entityImage.width) / 2;
                imageYd += double(entityImage.height) / 2;
                int imageX = int(imageXd);
                int imageY = int(imageYd);
				if (imageX >= entityImage.width || imageY >= entityImage.height || imageX < 0 || imageY < 0) {
					continue;
				}
                int imageIndex = imageX + imageY * entityImage.width;
                int windowIndex = windowX + windowY * WindowWidth;
                uint32_t pixel = entityImage.pixels[imageIndex];
                uint32_t entityAlpha = uint32_t(pixel & 0xff000000);
                if (entityAlpha > 0) {
                    BitmapMemory[windowIndex] = pixel;
                }



            }
        }        
    }
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
        Image *image = &allImages[imagesCount];
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
    ShowWindow(hWnd, SW_MAXIMIZE);
    ShowCursor(0);
    int frame = 0;

    /*
    Cam cam = {};
    
    Entity* field = CreateEntity("curve.bmp");
    field->width *= 10;
    field->height *= 10;
    field->visible = false;

    Entity* character = CreateEntity("character.bmp");
	character->x = 20;
    character->y = 250;
    character->width *= 2;
    character->height *= 2;
    character->visible = false;

    Entity* testEntity = CreateEntity("test3.bmp");
    testEntity->x = 200;
    testEntity->y = 300;
    testEntity->rotation = PI / 4;
    testEntity->width *= 50;
    testEntity->height *= 50;
    testEntity->visible = false;

    Entity* cursor = CreateEntity("cursor.bmp");
    cursor->width *= 5;
    cursor->height *= 5;
    cursor->visible = false;
    */


    EntityID field = AddEntity();
    images[field] = GetImage("curve.bmp");
    transforms[field] = { 0, 0, 0, images[field].width * 3, images[field].height * 3};

    EntityID guy = AddEntity();
    images[guy] = GetImage("character.bmp");
    transforms[guy] = { 10, 10, PI / 4, 20, 20};


    EntityID guyCam = AddEntity();
    transforms[guyCam] = { 15, 15, PI / 4 , 80, 80 };

    EntityID fieldCam = AddEntity();
    transforms[fieldCam] = { 0, 0, 0, WindowWidth / 5, WindowHeight / 5};

    uint64_t startMs = GetTickCount64(); 
    uint64_t frame30Ms = GetTickCount64(); 
    char fps[10] = {};
    while (Running) {
        frame++;
        MSG message = {};
        while (PeekMessage(&message, 0, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                Running = false;
            }
            TranslateMessage(&message);
            DispatchMessage(&message);
        }
        for (int i = 0; i < WindowWidth * WindowHeight; i++) {
            BitmapMemory[i] = 0;
        }

        {
            /* move mice to the right = > mouseDiff>0 => clockwise rotation */
            double mouseDiff = double(Input.mouseX - WindowWidth / 2);
            POINT c = { WindowWidth / 2, WindowHeight / 2 };
            ClientToScreen(hWnd, &c);
            SetCursorPos(c.x, c.y);
            // transforms[guy].angle -= mouseDiff;
        }
        RenderToMemory(fieldCam);
        StretchDIBits(
            GetDC(hWnd),
            0, 0, WindowWidth, WindowHeight,
            0, 0, WindowWidth, WindowHeight,
            BitmapMemory,
            &BitmapInfo,
            DIB_RGB_COLORS,
            SRCCOPY
        );

        int measureFrame = 10;
        if (frame % measureFrame == 0) {
			uint64_t time = uint64_t(GetTickCount64());
			uint64_t passedMs = time - frame30Ms;
			frame30Ms = time;
			StringCchPrintf(fps, 10, "fps %d ", int(measureFrame / (double(passedMs) / 1000)));
        }
		TextOutA(
          GetDC(hWnd),
		  0,
		  0,
          fps,
		  10
		);
    } 
    return 0;
}

