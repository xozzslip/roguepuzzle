#include <Windows.h>
#include <stdint.h>
#include <tchar.h>
#include <stdio.h>
#include <strsafe.h>
#include <math.h>
#include <windowsx.h>


typedef struct {
    char name[MAX_PATH];
    int width;
    int height;
    uint32_t* pixels;
} Image;

typedef struct {
    int x;
    int y;
} Vector;

typedef struct {
    bool visible;
    int x;
    int y;
    double rotation;
    int width; // scale factor
    int height;
    Image *image;
    int effectiveWidth; // after rotation
    int effectiveHeight;
    uint32_t* pixels; // cache for scaled and rotated image
    double _rotation;
    int _width;
    int _height;
} Entity;


typedef struct {
    int x;
    int y;
    int width; // scale factor
    int height;
    double rotation;
} Cam;

typedef struct {
    bool up;
    bool down;
    bool left;
    bool right;
    int mouseX; 
    int mouseY;
} UserInput;

bool StringEqualTo(char* s, const char* sample);
void TransformEntity(Entity* entity, int newWidth, int newHeight, double degree);
void DebugLog(const char* format, ...);
void FatalError(const char* format, ...);
Vector EntityCenter(Entity* entity);

static uint32_t* BitmapMemory;
static BITMAPINFO BitmapInfo;
static int WindowHeight;
static int WindowWidth;
static bool Running = true;
static const int MAX_ENTITIES = 1000;
static Entity entities[MAX_ENTITIES];
static int entitiesCount;
static const int MAX_IMAGES = 1000;
static int imagesCount;
static const double PI = double(3.141592653589793);
static Image images[MAX_IMAGES];
static UserInput Input;


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
        FatalError("failed to create entity %s", bmpName);
    }
    Entity* entity = &entities[entitiesCount];
    entity->image = image;
    entity->rotation = 0;
    entity->width = image->width;
    entity->height = image->height;
    entity->x = 0;
    entity->y = 0;
    entity->visible = true;
    entity->pixels = (uint32_t*)VirtualAlloc(0, image->width * image->height * 4, MEM_COMMIT, PAGE_READWRITE);
    for (int i = 0; i < image->width * image->height; i++) {
        entity->pixels[i] = image->pixels[i];
    }
    entity->effectiveWidth = image->width;
    entity->effectiveHeight = image->height;
    entitiesCount++;
    return entity;
}

Vector EntityCenter(Entity* entity) {
    int centerX = entity->x + entity->width / 2;
    int centerY = entity->y + entity->height / 2;

    return { centerX, centerY };
}


void MoveEntity(Entity* entity, int x, int y) {
    entity->x = x;
    entity->y = y;
}

Vector RotateVector(Vector vector, double alpha) {
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


void RecalculateEntity(Entity* entity) {
    if (entity->width == entity->_width && entity->height == entity->_height && entity->rotation == entity->_rotation) {
        return;
    }
    VirtualFree(entity->pixels, 0, MEM_RELEASE);
    Vector center = { entity->width / 2, entity->height/ 2 };
    Vector corners[4] = { 
        {-center.x, -center.y}, 
        {entity->width - center.x, -center.y},
        {entity->width - center.x, entity->height - center.y},
        {-center.x, entity->height - center.y},
    };
    Vector rotatedCorners[4] = {};
    int minX = INT_MAX;
    int maxX = -INT_MAX;
    int minY = INT_MAX;
    int maxY = -INT_MAX;
    for (int i = 0; i < 4; i++) {
        Vector rotated = RotateVector(corners[i], entity->rotation);
        rotatedCorners[i] = rotated;
        if (minX > rotated.x) {
            minX = rotated.x;
        }
        if (maxX < rotated.x) {
            maxX = rotated.x;
        }
        if (minY > rotated.y) {
            minY = rotated.y;
        }
        if (maxY < rotated.y) {
            maxY = rotated.y;
        }
    }
    int finalWidth = maxX - minX;
    int finalHeight = maxY - minY;
    Vector finalCenter = { finalWidth / 2, finalHeight / 2 };
    entity->pixels = (uint32_t*)VirtualAlloc(0, finalWidth * finalHeight * 4, MEM_COMMIT, PAGE_READWRITE);
    int originWidth = entity->image->width;
    int originHeight = entity->image->height;
    double scaleX = double(entity->width) / double(originWidth);
    double scaleY = double(entity->height) / double(originHeight);
    for (int i = 0; i < finalWidth * finalHeight; i++) {
        int x = i % finalWidth;
        int y = i / finalWidth;
        Vector v = { x - finalCenter.x, y - finalCenter.y };
        Vector notRotated = RotateVector(v, -entity->rotation);
        int scaledX = notRotated.x + entity->width / 2;
        int scaledY = notRotated.y + entity->height / 2;
        if (scaledX >= entity->width || scaledY >= entity->height || scaledX < 0 || scaledY < 0) {
            /*
            uint32_t pixel = 0;
            *(((uint8_t*)&pixel) + 1) = 255;
            *(((uint8_t*)&pixel) + 3) = 255;
            entity->pixels[i] = pixel; // rotate with green background for debug
            */
            entity->pixels[i] = 0; // transparent pixel
        }
        else {
			int originX = int(double(scaledX) / scaleX);
			int originY = int(double(scaledY) / scaleY);
			int originI = originX + originY * originWidth;
			entity->pixels[i] = entity->image->pixels[originI];
        }
    }

    entity->effectiveWidth = finalWidth;
    entity->effectiveHeight = finalHeight;
    entity->_width = entity->width;
    entity->_height = entity->height;
    entity->_rotation = entity->rotation;
    return;
}

void ResizeEntity(Entity* entity, int newWidth, int newHeight) {
    VirtualFree(entity->pixels, 0, MEM_RELEASE);
    entity->pixels = (uint32_t*)VirtualAlloc(0, newWidth * newHeight * 4, MEM_COMMIT, PAGE_READWRITE);
    entity->width = newWidth;
    entity->height = newHeight;
    int originWidth = entity->image->width;
    int originHeight = entity->image->height;
    double scaleX = double(newWidth) / double(originWidth);
    double scaleY = double(newHeight) / double(originHeight);
    for (int i = 0; i < newWidth * newHeight; i++) {
        int newX = i % newWidth;
        int newY = i / newWidth;
        int originX = int(double(newX) / scaleX);
        int originY = int(double(newY) / scaleY);
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
    ShowWindow(hWnd, SW_MAXIMIZE);
    ShowCursor(0);
    int frame = 0;

    Cam cam = {};
    
    Entity* field = CreateEntity("curve.bmp");
    field->width *= 10;
    field->height *= 10;
    field->visible = true;

    Entity* character = CreateEntity("character.bmp");
	character->x = 20;
    character->y = 250;
    character->width *= 2;
    character->height *= 2;
    character->visible = true;

    Entity* testEntity = CreateEntity("test3.bmp");
    testEntity->x = 200;
    testEntity->y = 300;
    testEntity->rotation = PI / 4;
    testEntity->width *= 50;
    testEntity->height *= 50;
    testEntity->visible = false;

    Entity* testEntity2 = CreateEntity("test3.bmp");
    testEntity2->x = 600;
    testEntity2->y = 300;
    testEntity2->rotation = 0;
    testEntity2->width *= 50;
    testEntity2->height *= 50;
    testEntity2->visible = false;

    Entity* cursor = CreateEntity("cursor.bmp");
    cursor->width *= 5;
    cursor->height *= 5;
    cursor->visible = false;

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
        

		cam.height = WindowWidth;
		cam.width = WindowHeight;
        cam.x = character->x - WindowWidth / 2;
        cam.y = character->y - WindowHeight /2;
        cursor->x = Input.mouseX - cursor->width / 2 + cam.x;
        cursor->y = Input.mouseY - cursor->height / 2 + cam.y;


        {
            POINT c = {WindowWidth/2, WindowHeight/2};
            ClientToScreen(hWnd, &c);
            SetCursorPos(c.x, c.y);
        }
        {
			int characterSpeed = 5;
            Vector center = EntityCenter(character);
            int centerX = center.x;
            int centerY = center.y;

            character->rotation -= double(Input.mouseX - WindowWidth / 2) / 1000;
            cam.rotation = character->rotation;

			if (Input.up) {
                character->y -= characterSpeed;
			}
			if (Input.down) {
                character->y += characterSpeed;
			}
			if (Input.left) {
                character->x -= characterSpeed;
			}
			if (Input.right) {
                character->x += characterSpeed;
			}
        }


        for (int i = 0; i < entitiesCount; i++) {
		    Entity * entity = &entities[i];
            if (!entity->visible) {
                continue;
            }
            RecalculateEntity(entity);
			int sizeCorrectionX = (entity->effectiveWidth - entity->width) / 2;
			int sizeCorrectionY = (entity->effectiveHeight - entity->height) / 2;
            /*
            int camXMin = max(0, entity->x - cam.x - sizeCorrectionX);
            int camXMax = min(WindowWidth, entity->x - cam.x + entity->effectiveWidth - sizeCorrectionX);
            int camYMin = max(0, entity->y - cam.y - sizeCorrectionY);
            int camYMax = min(WindowHeight, entity->y - cam.y + entity->effectiveHeight- sizeCorrectionY);
            */

			double cosAlpha = cos(cam.rotation);
			double sinAlpha = sin(cam.rotation);
            for (int camY = 0; camY < WindowHeight; camY++) {
                for (int camX = 0; camX < WindowWidth; camX++) {
                    Vector vector = {camX - WindowWidth / 2, camY - WindowHeight / 2};
					int newX = int(double(vector.x) * cosAlpha + double(vector.y) * sinAlpha);
					int newY = int(-double(vector.x) * sinAlpha + double(vector.y) * cosAlpha);
                    int absCamX = camX + cam.x + newX - vector.x;
                    int absCamY = camY + cam.y  + newY - vector.y;
                    int entityX = absCamX - entity->x + sizeCorrectionX; 
                    int entityY = absCamY - entity->y + sizeCorrectionY;
					if (entityX >= entity->effectiveWidth || entityY >= entity->effectiveHeight || entityX < 0 || entityY < 0) {
						continue;
					}
                    int entityIndex = entityX + entityY * entity->effectiveWidth;
                    uint32_t entityPixel = entity->pixels[entityIndex];
                    int screenIndex = camX + camY * WindowWidth;
					if (entityX == 0 || entityY == 0 || entityX == entity->effectiveWidth -1 || entityY == entity->effectiveHeight -1 ) {
						uint32_t pixel = 0;
						*(((uint8_t*)&pixel) + 2) = 255;
						*(((uint8_t*)&pixel) + 3) = 255;
						entityPixel = pixel;
					}
					uint32_t entityAlpha = uint32_t(entityPixel & 0xff000000);
					if (entityAlpha > 0) { // TODO: blend RGB
						BitmapMemory[screenIndex] = entityPixel;
					}
                }
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

        if (frame % 30 == 0) {
			uint64_t time = uint64_t(GetTickCount64());
			uint64_t passedMs = time - frame30Ms;
            frame30Ms = time;
			StringCchPrintf(fps, 10, "fps %d ", int(30 / (double(passedMs) / 1000)));
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

