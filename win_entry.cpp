#include <Windows.h>
#include <intrin.h>
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
    float x;
    float y;
} VectorF;

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

inline VectorF
operator-(VectorF a, VectorF b)
{
  VectorF result;
  result.x = a.x - b.x;
  result.y = a.y - b.y;
  return result;
}

inline VectorF
operator+(VectorF a, VectorF b)
{
  VectorF result;
  result.x = a.x + b.x;
  result.y = a.y + b.y;
  return result;
}

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


Vector RotateVector(Vector vector, float alpha) {
    double cosAlpha = cos(alpha);
    double sinAlpha = sin(alpha);
    // signs are specific for our coordinate system
    int newX = int(double(vector.x) * cosAlpha + double(vector.y) * sinAlpha);
    int newY = int(-double(vector.x) * sinAlpha + double(vector.y) * cosAlpha);
    Vector result = { newX, newY };
    return result;
}

VectorF RotateVectorF(VectorF vector, float alpha) {
    double cosAlpha = cos(alpha);
    double sinAlpha = sin(alpha);
    // signs are specific for our coordinate system
    int newX = int(double(vector.x) * cosAlpha + double(vector.y) * sinAlpha);
    int newY = int(-double(vector.x) * sinAlpha + double(vector.y) * cosAlpha);
    VectorF result = { newX, newY };
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

uint64_t msSinceQpc(LARGE_INTEGER start) {
    LARGE_INTEGER frequency, endingTime, elapsed;
    QueryPerformanceFrequency(&frequency); 
	QueryPerformanceCounter(&endingTime);
	elapsed.QuadPart = endingTime.QuadPart - start.QuadPart;
	elapsed.QuadPart *= 1000;
	elapsed.QuadPart /= frequency.QuadPart;
    return elapsed.QuadPart;
}


inline int Dot(Vector a, Vector b) {
    return a.x * b.x + a.y * b.y;
}

inline int DotF(VectorF a, VectorF b) {
    return a.x * b.x + a.y * b.y;
}

void RenderRectangleFast(VectorF center, float angle, float width, float height, Image* texture) {
    LARGE_INTEGER time = qpc();
    VectorF corners[4] = {
        -width / 2, -height / 2,
         width / 2, -height / 2,
        -width / 2,  height / 2,
         width / 2,  height / 2,
    };
    for (int i = 0; i < 4; i++) {
        corners[i] = RotateVectorF(corners[i], angle) + center;
    }
    VectorF origin = corners[0];
    VectorF xAxis = corners[1] - origin;
    VectorF yAxis = corners[2] - origin;
    __m128 originX = _mm_set_ps1(origin.x);
    __m128 xAxisX = _mm_set_ps1(xAxis.x);
    __m128 xAxisY = _mm_set_ps1(xAxis.y);
    __m128 yAxisX = _mm_set_ps1(yAxis.x);
    __m128 yAxisY = _mm_set_ps1(yAxis.y);
    __m128 xAxisSquareInv = _mm_set_ps1(1.0 / DotF(xAxis, xAxis));
    __m128 yAxisSquareInv = _mm_set_ps1(1.0 / DotF(yAxis, yAxis));
    __m128 textureWidth = _mm_set_ps1(texture->width);
    __m128 textureHeight = _mm_set_ps1(texture->height);
    for (int y = 0; y < WindowHeight; y++) {
		__m128 distanceY = _mm_set_ps1(y - origin.y);
        for (int x = 0; x < WindowWidth; x+=4){
            __m128 distanceX = _mm_sub_ps(_mm_set_ps(x, x + 1, x + 2, x+ 3), originX);
            __m128 dotXAxis = _mm_add_ps(_mm_mul_ps(distanceX, xAxisX), _mm_mul_ps(distanceY, xAxisY));
            __m128 dotYAxis = _mm_add_ps(_mm_mul_ps(distanceX, yAxisX), _mm_mul_ps(distanceY, yAxisY));
            __m128 u = _mm_mul_ps(dotXAxis, xAxisSquareInv);
            __m128 v = _mm_mul_ps(dotYAxis, yAxisSquareInv);
            __m128 uInside = _mm_and_ps(_mm_cmpge_ps(u, _mm_set_ps1(0)), _mm_cmple_ps(u, _mm_set_ps1(1)));
            __m128 vInside = _mm_and_ps(_mm_cmpge_ps(v, _mm_set_ps1(0)), _mm_cmple_ps(v, _mm_set_ps1(1)));
            __m128i inside = _mm_cvtps_epi32(_mm_and_ps(uInside, vInside));
            __m128i textureX = _mm_cvtps_epi32(_mm_mul_ps(u, textureWidth));
            __m128i textureY = _mm_cvtps_epi32(_mm_mul_ps(v, textureHeight));
            __m128i textureIndex = _mm_add_epi32(textureX, _mm_mullo_epi16(textureY, _mm_set1_epi32(texture->width)));
            int32_t insideA = ((int32_t*)&inside)[0];
            int32_t insideB = ((int32_t*)&inside)[1];
            int32_t insideC = ((int32_t*)&inside)[2];
            int32_t insideD = ((int32_t*)&inside)[3];
            __m128i pixels = _mm_set1_epi32(0);
            if (insideA) {
                ((int32_t*)&pixels)[3] = texture->pixels[((uint32_t*)&textureIndex)[0]];
            }
            if (insideB) {
                ((int32_t*)&pixels)[2] = texture->pixels[((uint32_t*)&textureIndex)[1]];
            }
            if (insideC) {
                ((int32_t*)&pixels)[1] = texture->pixels[((uint32_t*)&textureIndex)[2]];
            }
            if (insideD) {
                ((int32_t*)&pixels)[0] = texture->pixels[((uint32_t*)&textureIndex)[3]];
            }

            int pixelIndex = x + y * WindowWidth;
            _mm_store_si128((__m128i*)(BitmapMemory + pixelIndex), pixels);
        }
    }
    DebugLog("rendered rectangle %dms\n", msSinceQpc(time));


}


void RenderRectangle(Vector center, float angle, int width, int height, Image* texture) {
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
    float xAxisSquareInv = 1.0 / Dot(xAxis, xAxis);
    float yAxisSquareInv = 1.0 / Dot(yAxis, yAxis);

    for (int y = 0; y < WindowHeight; y++) {
        for (int x = 0; x < WindowWidth; x++){
            Vector d = Vector{ x, y } - origin;
            float u = float(Dot(xAxis, d)) * xAxisSquareInv;
            float v = float(Dot(yAxis, d)) * yAxisSquareInv;
            if (u >= 0 && u <= 1 && v >= 0 && v <= 1) {
                int pixelIndex = x + y * WindowWidth;
                int textureX = int(u * float(texture->width));
                int textureY = int(v * float(texture->height));
                int textureIndex = textureX + textureY * texture->width;
				BitmapMemory[pixelIndex] = texture->pixels[textureIndex];
            }
        }
    }
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
        int yMin = 0;
        int yMax = WindowHeight;
        int xMin = 0;
        int xMax = WindowWidth;
        for (int y = yMin; y < yMax; y++) {
            for (int x = xMin; x < xMax; x++) {
                // u, v - ?


            }
        }

#if 0
        double entityToCamAngle = camTransform.angle - entityTransform.angle;
		double cosEntityToCamAngle = cos(entityToCamAngle);
		double sinEntityToCamAngle = sin(entityToCamAngle);
        int entityToCameraRotatedX = camTransform.centerX - entityTransform.centerX;
        int entityToCameraRotatedY =  camTransform.centerY - entityTransform.centerY;
		int entityToCamX = int(double(entityToCameraRotatedX) * cosCamAngle + double(entityToCameraRotatedY) * (-1) * sinCamAngle);
		int entityToCamY = int(-double(entityToCameraRotatedX) * (-1) * sinCamAngle + double(entityToCameraRotatedY) * cosCamAngle);
        LARGE_INTEGER time = qpc();
        for (int windowIndex = 0; windowIndex < WindowWidth * WindowHeight; windowIndex++) {
            int windowX = windowIndex % WindowWidth;
            int windowY = windowIndex / WindowWidth;
			int pixelX = ((windowX - WindowWidth / 2) * camTransform.width) /  WindowWidth;
			int pixelY = ((windowY - WindowHeight / 2) * camTransform.height) / WindowHeight;

			int entityToPixelX = entityToCamX + pixelX;
			int entityToPixelY = entityToCamY + pixelY; 

			int entityPixelX = int(double(entityToPixelX) * cosEntityToCamAngle + double(entityToPixelY) * (-1) * sinEntityToCamAngle);
			int entityPixelY = int(-double(entityToPixelX) * (-1) * sinEntityToCamAngle + double(entityToPixelY) * cosEntityToCamAngle);

			entityPixelX += entityTransform.width / 2;
			entityPixelY += entityTransform.height / 2;
			int imageX = entityPixelX * entityImage.width / entityTransform.width;
			int imageY = entityPixelY * entityImage.height / entityTransform.height;
			if (imageX >= entityImage.width || imageY >= entityImage.height || imageX < 0 || imageY < 0) {
				continue;
			}
			int imageIndex = imageX + imageY * entityImage.width;
			uint32_t pixel = entityImage.pixels[imageIndex];
			uint32_t entityAlpha = uint32_t(pixel & 0xff000000);
			if (entityAlpha > 0) {
				BitmapMemory[windowIndex] = pixel;
			}
        }
        DebugLog("entity %d passed %dms\n", entity, msSinceQpc(time));
#endif 
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
    transforms[guy] = { 10, 10, 0, 20, 20};


    EntityID guyCam = AddEntity();
    transforms[guyCam] = { 15, 15, PI / 4 , 80, 80 };

    EntityID fieldCam = AddEntity();
    transforms[fieldCam] = { 0, 0, PI/ 4, WindowWidth / 5, WindowHeight / 5};

    char fps[10] = {};

    LARGE_INTEGER startMeasure = qpc();
    int passedFrames = 0;
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

        /*
        {
            double mouseDiff = double(Input.mouseX - WindowWidth / 2);
            POINT c = { WindowWidth / 2, WindowHeight / 2 };
            ClientToScreen(hWnd, &c);
            SetCursorPos(c.x, c.y);
            // transforms[guy].angle -= mouseDiff;

        }
        */
        Image fieldImage = GetImage("curve.bmp");
    
        // RenderToMemory(fieldCam);
        RenderRectangleFast(VectorF{ float(WindowWidth) / 2, float(WindowHeight) / 2 }, 0.001 * float(frame), 1000, 1000, &fieldImage);
        //RenderRectangle({ WindowWidth / 2, WindowHeight / 2 }, frame * 0.01, 400, 400, &fieldImage);
        StretchDIBits(
            GetDC(hWnd),
            0, 0, WindowWidth, WindowHeight,
            0, 0, WindowWidth, WindowHeight,
            BitmapMemory,
            &BitmapInfo,
            DIB_RGB_COLORS,
            SRCCOPY
        );

        
        uint64_t passedMs = msSinceQpc(startMeasure);
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
        
    } 
    return 0;
}

