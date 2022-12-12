#include <Windows.h>
#include <intrin.h>
#include <stdint.h>
#include <tchar.h>
#include <stdio.h>
#include <strsafe.h>
#include <math.h>
#include <windowsx.h>


typedef struct {
    float x;
    float y;
} Vector;

inline Vector
operator-(Vector a, Vector b)
{
  Vector result;
  result.x = a.x - b.x;
  result.y = a.y - b.y;
  return result;
}

inline Vector
operator+(Vector a, Vector b)
{
  Vector result;
  result.x = a.x + b.x;
  result.y = a.y + b.y;
  return result;
}

typedef struct {
    char name[MAX_PATH];
    int width;
    int height;
    uint32_t* pixels;
} Image;


typedef struct {
    Vector center;
    float angle;
    float width;
    float height;
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
static int entitiesCount;
static const int MAX_IMAGES = 1000;
static int imagesCount;
static const float PI = double(3.141592653589793);
static Image allImages[MAX_IMAGES];
static UserInput Input;
static Vector DefaultOrientation = { 0, 1 };
static int QpcFrequency;


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


Vector RotateVector(Vector vector, float alpha) {
    double cosAlpha = cos(alpha);
    double sinAlpha = sin(alpha);
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

LARGE_INTEGER qpc() {
    LARGE_INTEGER startingTime;
    QueryPerformanceCounter(&startingTime);
    return startingTime;
}

float elapsedMs(LARGE_INTEGER start, LARGE_INTEGER end) {
    uint64_t elapsed = (end.QuadPart - start.QuadPart);
    return float(elapsed) * 1000.0f / QpcFrequency;
}


inline float Dot(Vector a, Vector b) {
    return a.x * b.x + a.y * b.y;
}

static inline __m128i muly(const __m128i &a, const __m128i &b)
{
#ifdef __SSE4_1__  // modern CPU - use SSE 4.1
    return _mm_mullo_epi32(a, b);
#else               // old CPU - use SSE 2
    __m128i tmp1 = _mm_mul_epu32(a,b); /* mul 2,0*/
    __m128i tmp2 = _mm_mul_epu32( _mm_srli_si128(a,4), _mm_srli_si128(b,4)); /* mul 3,1 */
    return _mm_unpacklo_epi32(_mm_shuffle_epi32(tmp1, _MM_SHUFFLE (0,0,2,0)), _mm_shuffle_epi32(tmp2, _MM_SHUFFLE (0,0,2,0))); /* shuffle results to [63..0] and pack */
#endif
}

void RenderRectangle(Vector center, float angle, float width, float height, Image* texture) {
    Vector corners[4] = {
        -width / 2, -height / 2,
         width / 2, -height / 2,
        -width / 2,  height / 2,
         width / 2,  height / 2,
    };
    for (int i = 0; i < 4; i++) {
        corners[i] = RotateVector(corners[i], angle) + center;
    }
    Vector origin = corners[0];
    Vector xAxis = corners[1] - origin;
    Vector yAxis = corners[2] - origin;
    __m128 originX = _mm_set_ps1(origin.x);
    __m128 xAxisX = _mm_set_ps1(xAxis.x);
    __m128 xAxisY = _mm_set_ps1(xAxis.y);
    __m128 yAxisX = _mm_set_ps1(yAxis.x);
    __m128 yAxisY = _mm_set_ps1(yAxis.y);
    __m128 xAxisSquareInv = _mm_set_ps1(1.0 / Dot(xAxis, xAxis));
    __m128 yAxisSquareInv = _mm_set_ps1(1.0 / Dot(yAxis, yAxis));
    __m128 textureWidth = _mm_set_ps1(texture->width);
    __m128 textureHeight = _mm_set_ps1(texture->height);
    __m128i textureWidthI = _mm_set1_epi32(texture->width);
    __m128i textureHeightI = _mm_set1_epi32(texture->height);
    for (int y = 0; y < WindowHeight; y++) {
		__m128 distanceY = _mm_set_ps1(y - origin.y);
        for (int x = 0; x < WindowWidth; x+=4){
            __m128 distanceX = _mm_sub_ps(_mm_set_ps(x, x + 1, x + 2, x + 3), originX);
            __m128 dotXAxis = _mm_add_ps(_mm_mul_ps(distanceX, xAxisX), _mm_mul_ps(distanceY, xAxisY));
            __m128 dotYAxis = _mm_add_ps(_mm_mul_ps(distanceX, yAxisX), _mm_mul_ps(distanceY, yAxisY));
            __m128 u = _mm_mul_ps(dotXAxis, xAxisSquareInv);
            __m128 v = _mm_mul_ps(dotYAxis, yAxisSquareInv);
            __m128i tx = _mm_cvtps_epi32(_mm_mul_ps(u, textureWidth));
            __m128i ty = _mm_cvtps_epi32(_mm_mul_ps(v, textureHeight));
            __m128i textureIndex = _mm_add_epi32(tx, muly(ty, textureWidthI));
            __m128i txInside = _mm_and_si128(_mm_cmpgt_epi32(tx, _mm_set1_epi32(-1)), _mm_cmplt_epi32(tx, textureWidthI));
            __m128i tyInside = _mm_and_si128(_mm_cmpgt_epi32(ty, _mm_set1_epi32(-1)), _mm_cmplt_epi32(ty, textureHeightI));
            __m128i inside = _mm_and_si128(txInside, tyInside);
            int32_t insideA = ((int32_t*)&inside)[0];
            int32_t insideB = ((int32_t*)&inside)[1];
            int32_t insideC = ((int32_t*)&inside)[2];
            int32_t insideD = ((int32_t*)&inside)[3];
            __m128i pixels = _mm_set1_epi32(0);
            if (insideA) {
                uint32_t pixel = texture->pixels[((uint32_t*)&textureIndex)[0]];
                uint32_t entityAlpha = uint32_t(pixel & 0xff000000);
                int pixelIndex = x + y * WindowWidth + 3;
                if (entityAlpha > 0) {
                    BitmapMemory[pixelIndex] = pixel;
                }
            }
            if (insideB) {
                uint32_t pixel = texture->pixels[((uint32_t*)&textureIndex)[1]];
                uint32_t entityAlpha = uint32_t(pixel & 0xff000000);
                int pixelIndex = x + y * WindowWidth + 2;
                if (entityAlpha > 0) {
                    BitmapMemory[pixelIndex] = pixel;
                }
            }
            if (insideC) {
                uint32_t pixel = texture->pixels[((uint32_t*)&textureIndex)[2]];
                uint32_t entityAlpha = uint32_t(pixel & 0xff000000);
                int pixelIndex = x + y * WindowWidth + 1;
                if (entityAlpha > 0) {
                    BitmapMemory[pixelIndex] = pixel;
                }
            }
            if (insideD) {
                uint32_t pixel = texture->pixels[((uint32_t*)&textureIndex)[3]];
                uint32_t entityAlpha = uint32_t(pixel & 0xff000000);
                int pixelIndex = x + y * WindowWidth + 0;
                if (entityAlpha > 0) {
                    BitmapMemory[pixelIndex] = pixel;
                }
            }

        }
    }
}

void RenderFromCamera(EntityID cam) {
    // BitmapMemory[screenIndex] = entityPixel;
    Transform camTransform = transforms[cam];
    Vector camOrigin = {};
    Vector camXAxis = {};
    Vector camYAxis = {};
    {
        Vector corners[4] = {
            -camTransform.width / 2, -camTransform.height / 2,
             camTransform.width / 2, -camTransform.height / 2,
            -camTransform.width / 2,  camTransform.height / 2,
             camTransform.width / 2,  camTransform.height / 2,
        };
        for (int i = 0; i < 4; i++) {
            corners[i] = RotateVector(corners[i], camTransform.angle) + camTransform.center;
        }
        camOrigin = corners[0];
        camXAxis = corners[1] - camOrigin;
        camYAxis = corners[2] - camOrigin;
    }
    float camXAxisSquareInv = 1.0 / Dot(camXAxis, camXAxis);
    float camYAxisSquareInv = 1.0 / Dot(camYAxis, camYAxis);
    float camXScale = WindowWidth / camTransform.width;
    float camYScale = WindowHeight / camTransform.height;

    for (EntityID entity = 0; entity < entitiesCount; entity++) {
        Transform entityTransform = transforms[entity];
        Image entityImage = images[entity];
        if (entityImage.width == 0 || entityImage.height == 0 || entityTransform.width == 0 || entityTransform.height == 0) {
            continue;
        }
        Vector d = entityTransform.center - camOrigin;
        float u = Dot(d, camXAxis) * camXAxisSquareInv;
        float v = Dot(d, camYAxis) * camYAxisSquareInv;
        float camX = u * WindowWidth;
        float camY = v * WindowHeight;
        float angle = entityTransform.angle - camTransform.angle;
        float width = entityTransform.width * camXScale;
        float height = entityTransform.height * camYScale;
        RenderRectangle({ camX, camY }, angle, width, height, &entityImage);
    }

}


int WinMain(
    HINSTANCE hInstance,
    HINSTANCE hPrevInstance,
    LPSTR     lpCmdLine,
    int       nShowCmd)
{
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency); 
    QpcFrequency = frequency.QuadPart;

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

    EntityID field = AddEntity();
    images[field] = GetImage("curve.bmp");
    transforms[field] = { {0, 0}, 0, float(WindowWidth), float(WindowHeight) };
    
    EntityID guy = AddEntity();
    images[guy] = GetImage("character.bmp");
    transforms[guy] = { 0, 0, PI / 4, 60, 60};

    EntityID fieldCam = AddEntity();
    transforms[fieldCam] = { {0, 0}, PI / 4, float(WindowWidth) / 1.5f , float(WindowHeight) / 1.5f};

    char fps[10] = {};
    char maxFps[15] = {};

    LARGE_INTEGER startMeasure = qpc();
    int passedFrames = 0;

    Image fieldImage = GetImage("highres.bmp");
    LARGE_INTEGER previousFrameRenderedAtCounter = LARGE_INTEGER{};
    float frameRate = 30;
    float frameDurationMs = 1000.0f / frameRate;
    while (Running) {
        LARGE_INTEGER frameCounter = qpc();
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
            double mouseDiff = double(Input.mouseX - WindowWidth / 2);
            POINT c = { WindowWidth / 2, WindowHeight / 2 };
            ClientToScreen(hWnd, &c);
            SetCursorPos(c.x, c.y);
            transforms[guy].angle -= mouseDiff * 0.001;
            transforms[fieldCam].angle -= mouseDiff * 0.001;
        }

		StringCchPrintf(maxFps, 15, "compute %.2fms", float(elapsedMs(frameCounter, qpc())));
        RenderFromCamera(fieldCam);
        while (true) {
            float elapsed = elapsedMs(previousFrameRenderedAtCounter, qpc());
            if (elapsed >= frameDurationMs) {
                break;
            }
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

        previousFrameRenderedAtCounter = qpc();
        
        uint64_t passedMs = elapsedMs(startMeasure, qpc());
        passedFrames += 1;
        if (passedMs > 1000) {
		    StringCchPrintf(fps, 10, "fps %d ", int(passedFrames / (double(passedMs) / 1000)));
            passedFrames = 0;
            startMeasure = qpc();
        }
    
		TextOutA(
          GetDC(hWnd),
		  0,
		  0,
          fps,
		  10
		);
		TextOutA(
          GetDC(hWnd),
		  0,
		  15,
          maxFps,
		  15
		);
    } 
    return 0;
}

