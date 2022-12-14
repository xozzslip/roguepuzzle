#include <Windows.h>
#include <intrin.h>
#include <stdint.h>
#include <tchar.h>
#include <stdio.h>
#include <strsafe.h>
#include <math.h>
#include <windowsx.h>


typedef struct {
    uint8_t* bytes;
    int size;
} Buffer;


typedef struct {
    Buffer buffer;
    char name[MAX_PATH];
    int nameSize;
} Asset;

typedef struct {
    char name[MAX_PATH];
    int width;
    int height;
    int32_t* values;
} Csv;

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

inline Vector
operator+=(Vector &a, Vector b)
{
  a = a + b;
  return a;
}

inline Vector
operator-=(Vector &a, Vector b)
{
  a = a - b;
  return a;
}

inline Vector
operator-(Vector a)
{
  Vector result;
  result.x = -a.x;
  result.y = -a.y;
  return result;
}


inline Vector
operator*(float a, Vector b)
{
  Vector result = { a * b.x, a * b.y };
  return result;
}

inline Vector
operator*(Vector b, float a)
{
  Vector result = a * b;
  return result;
}

inline Vector
operator*=(Vector &a, float b)
{
  a = b * a;
  return a;
}

typedef struct {
    char name[MAX_PATH];
    int width;
    int height;
    uint32_t* pixels;
} Image;

typedef struct {
    char name[MAX_PATH];
    int width;
    int height;
    uint32_t* pixels;
    int index;
} Tile;

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
    bool shift;
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
static int imagesCount;
static int assetsCount;
static int tilesCount;
static int csvCount;
static const float PI = double(3.141592653589793);
static UserInput Input;
static Vector DefaultOrientation = { 0, 1 };
static int QpcFrequency;
static Image allImages[MAX_ENTITIES];
static Asset allAssets[MAX_ENTITIES];
static Csv allCsvs[MAX_ENTITIES];
static Tile allTiles[MAX_ENTITIES];


void Assert(bool expression,const char* error) {
    if (!expression) {
        FatalError(error);
    }
}


Image GetTile(const char* bmpName, int tileIndex) {
    bool found = false;
    Tile* tile = NULL;
    for (int i = 0; i < tilesCount; i++) {
        tile = &allTiles[i];
        if (StringEqualTo(tile->name, bmpName) && tileIndex == tile->index) {
            found = true;
            break;
        }
    }
    if (!found) {
        FatalError("failed get tile by filename and index %s %d\n", bmpName, tileIndex);
    }
    Image img = {0};
    img.height = tile->height;
    img.width = tile->width;
    img.pixels = tile->pixels;
    StringCchCopy(img.name, MAX_PATH, tile->name);
    return img;
}

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
        FatalError("failed get image by filename %s\n", bmpName);
    }
    return *image;
}


Csv GetCsv(const char* filename) {
    bool found = false;
    Csv* csv = NULL;
    for (int i = 0; i < csvCount; i++) {
        csv = &allCsvs[i];
        if (StringEqualTo(csv->name, filename)) {
            found = true;
            break;
        }
    }
    if (!found) {
        FatalError("failed get csv by filename %s\n", filename);
    }
    return *csv;
}

Asset GetAsset(const char* filename) {
    bool found = false;
    Asset* asset = NULL;
    for (int i = 0; i < assetsCount; i++) {
        asset = &allAssets[i];
        if (StringEqualTo(asset->name, filename)) {
            found = true;
            break;
        }
    }
    if (!found) {
        FatalError("failed get asset by filename %s\n", filename);
    }
    return *asset;
}

Vector RotateVector(Vector vector, float alpha) {
    double cosAlpha = cos(alpha);
    double sinAlpha = sin(alpha);
    // signs are specific for our coordinate system
    float newX = vector.x * cosAlpha + vector.y * sinAlpha;
    float newY = -vector.x * sinAlpha + vector.y * cosAlpha;
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

bool isCharNumeric(char x) {
    if (x == '0' || x == '1' || x == '2' || x == '3' || x == '4' || x == '5'
        || x == '6' || x == '7' || x == '8' || x == '9') {
        return true;
    }
    return false;
}

void UnpackCsvBytes(uint8_t* bytes, int bytesCount, Csv* csv) {
    if (bytesCount == 0) {
        FatalError("csv must be not empty\n");
    }
    int valuesCount = 1;
    for (int i = 0; i < bytesCount; i++) {
        if (bytes[i] == ',' || bytes[i] == '\n') {
            valuesCount++;
        }
    }
    int32_t *values = (int32_t*)VirtualAlloc(0, valuesCount * 4, MEM_COMMIT, PAGE_READWRITE);
    int32_t value = 0;
    int width = 0;
    int height = 0;
    valuesCount = 0;

    uint8_t* next = bytes;
    // parses CSV with /r/n as new line
    // TODO: write general purpose CSV parser
    while (next - bytes < bytesCount) {
        int valuesCountInRow = 0;
        while (*next != '\n') {
            int32_t value = 0;
            bool negative = false;
            if (*next == '-') {
                negative = true;
                next++;
            }
            while (isCharNumeric(*next)) {
                value *= 10;
                value += *next - '0';
                next++;
            }
            if (negative) {
                value = -value;
            }
            values[valuesCount] = value;
            valuesCountInRow ++;
            valuesCount++;
            next++;
        }
        if (width == 0 && valuesCountInRow != 0) {
            width = valuesCountInRow;
        }
        if (valuesCountInRow != 0 && valuesCountInRow != width) {
            FatalError("only csv with constant width are supported\n");
        }
        if (valuesCountInRow != 0) {
            height++;
        }
        next++;
    }
    Assert(valuesCount % width == 0, "amount of csv values must be devidible by width\n");
    Assert(width > 0, "csv width must be non zero\n");
    Assert(height > 0, "csv height must be non zero\n");
    Assert(valuesCount > 0, "csv values count must be non zero\n");
    csv->values = values;
    csv->width = width;
    csv->height = height;
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
        else if (wParam == VK_SHIFT) {
            Input.shift = true;
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
        else if (wParam == VK_SHIFT) {
            Input.shift = false;
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
    return float(elapsed) * 1000.0f / float(QpcFrequency);
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
    float minX = MAXINT;
    float maxX = -MAXINT;
    float minY = MAXINT;
    float maxY = -MAXINT;
    for (int i = 0; i < 4; i++) {
        Vector corner = RotateVector(corners[i], angle) + center;
        corners[i] = corner;
        if (corner.x < minX) {
            minX = corner.x;
        }
        if (corner.x > maxX) {
            maxX = corner.x;
        }
        if (corner.y < minY) {
            minY = corner.y;
        }
        if (corner.y > maxY) {
            maxY = corner.y;
        }
    }
    minX = max(0, minX);
    minY = max(0, minY);
    maxX = min(WindowWidth, maxX);
    maxY = min(WindowHeight, maxY);
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
    for (int y = minY; y < maxY; y++) {
		__m128 distanceY = _mm_set_ps1(y - origin.y);
        for (int x = minX; x < maxX; x+=4){
            __m128 distanceX = _mm_sub_ps(_mm_set_ps(x, x + 1, x + 2, x + 3), originX);
            __m128 dotXAxis = _mm_add_ps(_mm_mul_ps(distanceX, xAxisX), _mm_mul_ps(distanceY, xAxisY));
            __m128 dotYAxis = _mm_add_ps(_mm_mul_ps(distanceX, yAxisX), _mm_mul_ps(distanceY, yAxisY));
            __m128 u = _mm_mul_ps(dotXAxis, xAxisSquareInv);
            __m128 v = _mm_mul_ps(dotYAxis, yAxisSquareInv);
            __m128 tx = _mm_mul_ps(u, textureWidth);
            __m128 ty = _mm_mul_ps(v, textureHeight);
            __m128i txi = _mm_cvttps_epi32(tx);
            __m128i tyi = _mm_cvttps_epi32(ty);
            __m128i textureIndex = _mm_add_epi32(txi, muly(tyi, textureWidthI));
            __m128i txInside = _mm_and_si128(
                _mm_cmpgt_epi32(txi, _mm_set1_epi32(-1)), 
                _mm_cmplt_epi32(txi, textureWidthI)
            );
            __m128i tyInside = _mm_and_si128(
                _mm_cmpgt_epi32(tyi, _mm_set1_epi32(-1)),
                _mm_cmplt_epi32(tyi, textureHeightI)
            );
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

void RenderGrid() {
    for (int x = 0; x < WindowWidth; x++) {
        for (int y = 0; y < WindowHeight; y++) {
            int pixelIndex = x + y * WindowWidth;
            if (x == WindowWidth / 2) {
                BitmapMemory[pixelIndex] = 2390942;
            }
            if (y == WindowHeight / 2) {
                BitmapMemory[pixelIndex] = 2390942;
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

Buffer ReadWholeFile(char *filePath) {
    LARGE_INTEGER fileSize;
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
    if (GetFileSizeEx(fileHandle, &fileSize) == 0) {
        FatalError("failed to get file size\n");
    }
    if (fileSize.QuadPart == 0) {
        FatalError("file size must be non 0\n");
    }
	DWORD readBytes;
	uint8_t* assetContent = (uint8_t *) VirtualAlloc(0, fileSize.QuadPart, MEM_COMMIT, PAGE_READWRITE);
	if (ReadFile(fileHandle, assetContent, fileSize.QuadPart, &readBytes, NULL) <= 0) {
		FatalError("failed to read asset content to memory\n");
	}
	if (uint64_t(readBytes) != uint64_t(fileSize.QuadPart)) {
		FatalError("failed to fully read asset content\n");
	}
    return { assetContent, int(fileSize.QuadPart) };
}

int GetStringLength(char* s) {
    int l = 0;
    for (int i = 0; i < MAX_PATH; i++) {
        if (s[i] != '\0') {
            l++;
        }
        else {
            break;
        }
    }
    return l;
}

bool IsStringEndsWith(char* s, char* ending) {
    int sLen = GetStringLength(s);
    int endingLen = GetStringLength(ending);
    if (endingLen > sLen) {
        return false;
    }
    for (int i = 0; i < endingLen; i++) {
        if (ending[endingLen - i - 1] != s[sLen - 1 - i]) {
            return false;
        }
    }
    return true;
}


int ParseTilesetResolution(char* fileName) {
    char* s = fileName;
    while (*s != '.') {
        s++;
    }
    s++;
    int value = 0;
    while (isCharNumeric(*s)) {
        value *= 10;
        value += *s - '0';
        s++;
    }
    Assert(value > 0, "tileset must contain resolution in pixels\n");
    return value;
}


void InitTileMap(
    const char *tilemapName, 
    const char *csvName,
    float width,
    float height 
) {
    Csv csv = GetCsv(csvName);
    int tileSize = ParseTilesetResolution((char *)tilemapName);
    float scaleX = width / float(csv.width * tileSize);
    float scaleY = height / float(csv.height * tileSize);
    for (int csvX = 0; csvX < csv.width; csvX++) {
        for (int csvY = 0; csvY < csv.height; csvY++) {
            int tileIndex = csv.values[csvX + csvY * csv.width];
            if (tileIndex == -1) {
                continue;
            }
            float x = csvX * tileSize * scaleX;
            float y = csvY * tileSize * scaleY;

			EntityID entity = AddEntity();
			images[entity] = GetTile(tilemapName, tileIndex);
			transforms[entity] = { {x, y}, 0, tileSize * scaleX, tileSize * scaleY };
        }
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
    int assetCount = 0;
    do {
        char filePath[MAX_PATH]; 
        if (StringCchPrintfA(filePath, MAX_PATH, "assets\\%s", fileMetadata.cFileName) < 0) {
            FatalError("large assets are not supported\n");
        }   
        DebugLog("reading file with name=\"%s\" path=\"%s\"\n", fileMetadata.cFileName, (char *)filePath);
        if (imagesCount == MAX_ENTITIES) {
            FatalError("maximum amount of assets exceeded\n");
        }
        if (fileMetadata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            continue;
        }
        Buffer buf = ReadWholeFile(filePath);
        char* fileName = fileMetadata.cFileName;
        if (IsStringEndsWith(fileName, (char*) ".bmp")) {
            Image *image = &allImages[imagesCount];
			UnpackBitmapBytes(buf.bytes, buf.size, image);
			StringCchCopy(image->name, MAX_PATH, fileName);
			imagesCount += 1;
			if (IsStringEndsWith(fileName, (char*)"tileset.bmp")) {
				int resolution = ParseTilesetResolution(fileName);	
                int width = image->width / resolution;
                int height = image->height / resolution;
                for (int tileIndexX = 0; tileIndexX < width; tileIndexX++) {
                    for (int tileIndexY = 0; tileIndexY < height; tileIndexY++) {
                        int tileIndex = tileIndexX + tileIndexY * width;
						Tile *tile = &allTiles[tilesCount];
                        tile->width = resolution;
                        tile->height = resolution;
                        tile->index = tileIndex;
                        tile->pixels = (uint32_t*)VirtualAlloc(0, resolution * resolution * 4, MEM_COMMIT, PAGE_READWRITE);
			            StringCchCopy(tile->name, MAX_PATH, fileName);
                        for (int tileX = 0; tileX < resolution; tileX++) {
                            for (int tileY = 0; tileY < resolution; tileY++) {
                                int pixelIndex = tileX + tileY * resolution;
                                int imageX = tileX + resolution * tileIndexX;
                                int imageY = tileY + resolution * tileIndexY;
                                int imagePixelIndex = imageX + imageY * image->width;
                                tile->pixels[pixelIndex] = image->pixels[imagePixelIndex];
                            }
                        }
						tilesCount += 1;
                    }
                }
			}
        }
        else if (IsStringEndsWith(fileName, (char*)".csv")) {
            Csv *csv= &allCsvs[csvCount];
			UnpackCsvBytes(buf.bytes, buf.size, csv);
			StringCchCopy(csv->name, MAX_PATH, fileName);
			csvCount += 1;
        }
        Asset* asset = &allAssets[assetsCount];
        asset->buffer = buf;
	    StringCchCopy(asset->name, MAX_PATH, fileName);
        assetsCount++;
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
    images[field] = GetTile("gameboy.16tileset.bmp", 5);
    transforms[field] = { {0, 0}, 0, float(WindowWidth), float(WindowHeight)};
    
    EntityID guy = AddEntity();
    images[guy] = GetImage("character.bmp");
    transforms[guy] = { {0, 0}, PI / 4, 80, 80 };

    EntityID fieldCam = AddEntity();
    transforms[fieldCam] = { {0, 0}, 0, float(WindowWidth), float(WindowHeight)};

    EntityID guyCam = AddEntity();
    transforms[guyCam] = { {0, 0}, PI / 4, float(WindowWidth), float(WindowHeight) };
    InitTileMap("gameboy.16tileset.bmp", "lvl1.csv", float(WindowWidth), float(WindowHeight));

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
        EntityID cam = guyCam;

        {
            double mouseDiff = 0;
			if (hWnd == GetActiveWindow()) {
				mouseDiff = double(Input.mouseX - WindowWidth / 2);
				POINT c = { WindowWidth / 2, WindowHeight / 2 };
				ClientToScreen(hWnd, &c);
				SetCursorPos(c.x, c.y);
			}
            transforms[guy].angle -= mouseDiff * 0.001;
            Vector wasd[4] = {
                0, -1,
                -1, 0,
                0, 1,
                1, 0,
            };
            for (int i = 0; i < 4; i++) {
                wasd[i] = RotateVector(wasd[i], transforms[guy].angle);
            }
            float speed = 8;
            if (Input.up) {
                transforms[guy].center += wasd[0] * speed;
            }
            if (Input.left) {
                transforms[guy].center += wasd[1] * speed;
            }
            if (Input.down) {
                transforms[guy].center += wasd[2] * speed;
            }
            if (Input.right) {
                transforms[guy].center += wasd[3] * speed;
            }
            transforms[guyCam].center = transforms[guy].center + wasd[0] * 250;
            if (Input.shift) {
                cam = fieldCam;
            }
        }
        transforms[guyCam].angle = transforms[guy].angle;

        RenderFromCamera(cam);
		StringCchPrintf(maxFps, 15, "compute %.2fms", float(elapsedMs(frameCounter, qpc())));
        /*
        Image fieldImage = GetImage("test2.bmp");
        RenderRectangle({ float(WindowWidth) / 2, float(WindowHeight) / 2}, 0, float(WindowWidth), float(WindowHeight), &fieldImage);
        */
        while (true) {
            float elapsed = elapsedMs(previousFrameRenderedAtCounter, qpc());
            if (elapsed >= frameDurationMs) {
                break;
            }
        }

        // RenderGrid();

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

