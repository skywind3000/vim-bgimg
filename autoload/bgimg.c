/* compile: cl /MD /LD bgimg.c user32.lib gdi32.lib */

#include <windows.h>
#include <string.h>

#define EXPORT __declspec(dllexport)

typedef int (WINAPI *fFillRect)(HDC hDC, CONST RECT *lprc, HBRUSH hbr);
typedef int (WINAPI *fScrollWindowEx)(HWND, int, int, CONST RECT *, CONST RECT *, HRGN, LPRECT, UINT);

int WINAPI MyFillRect(HDC hDC, CONST RECT *lprc, HBRUSH hbr);
int WINAPI MyScrollWindowEx(HWND hWnd, int dx, int dy, CONST RECT *prcScroll, CONST RECT *prcClip, HRGN hrgnUpdate, LPRECT prcUpdate, UINT flags);
BOOL LoadBitmapFromBMPFile( LPCTSTR szFileName, HBITMAP *phBitmap, HPALETTE *phPalette);
HBRUSH create_image_brush(const char *szFileName);
HDC create_image_dc(const char *szFileName, int *pwidth, int *pheight);
BOOL install_hook(HINSTANCE hInst, const char *funcname, FARPROC* orig, FARPROC newfn);
void show_lasterr();

int init(HINSTANCE hinstDLL);

EXPORT int bgimg_set_bg(int color);
EXPORT int bgimg_set_image(const char *path);

fFillRect OrigFillRect = NULL;
fScrollWindowEx OrigScrollWindowEx = NULL;

HBRUSH bgimg_image = NULL;
int bgimg_image_width = 0;
int bgimg_image_height = 0;
int bgimg_color = 0;
HDC testdc = NULL;

int WINAPI
MyFillRect(HDC hDC, CONST RECT *lprc, HBRUSH hbr)
{
	BOOL replace = 0;
	COLORREF color = RGB(0, 0, 0);
    if (bgimg_image == NULL)
        return OrigFillRect(hDC, lprc, hbr);
	color = RGB((bgimg_color >> 16) & 0xFF, (bgimg_color >> 8) & 0xFF, bgimg_color & 0xFF);
#if 0
	{
		RECT rc;
		rc.top = 0;
		rc.left = 0;
		rc.right = 1;
		rc.bottom = 1;
		FillRect(testdc, &rc, hbr);
		if (GetPixel(testdc, 0, 0) == color) replace = 1;
	}
#else
	{
		LOGBRUSH lb;
		if (GetObject(hbr, sizeof(lb), &lb) > 0) {
			if (lb.lbStyle == BS_SOLID && lb.lbColor == color) {
				replace = 1;
			}
		}
	}
#endif
    if (replace != 0)
        return OrigFillRect(hDC, lprc, bgimg_image);
    else
        return OrigFillRect(hDC, lprc, hbr);
}

int WINAPI
MyScrollWindowEx(HWND hWnd, int dx, int dy, CONST RECT *prcScroll, CONST RECT *prcClip, HRGN hrgnUpdate, LPRECT prcUpdate, UINT flags)
{
    if (bgimg_image == NULL)
        return OrigScrollWindowEx(hWnd, dx, dy, prcScroll, prcClip, hrgnUpdate, prcUpdate, flags);
    RedrawWindow(hWnd, NULL, NULL, RDW_INVALIDATE);
    return TRUE;
}

/* http://support.microsoft.com/kb/158898/ja */
BOOL
LoadBitmapFromBMPFile( LPCTSTR szFileName, HBITMAP *phBitmap, HPALETTE *phPalette)
{
    BITMAP  bm;

    *phBitmap = NULL;
    *phPalette = NULL;

    // Use LoadImage() to get the image loaded into a DIBSection
    *phBitmap = (HBITMAP)LoadImage( NULL, szFileName, IMAGE_BITMAP, 0, 0,
            LR_CREATEDIBSECTION | LR_LOADFROMFILE);
    if( *phBitmap == NULL )
        return FALSE;

    // Get the color depth of the DIBSection
    GetObject(*phBitmap, sizeof(BITMAP), &bm );
    // If the DIBSection is 256 color or less, it has a color table
    if( ( bm.bmBitsPixel * bm.bmPlanes ) <= 8 )
    {
        HDC           hMemDC;
        HBITMAP       hOldBitmap;
        RGBQUAD       rgb[256];
        LPLOGPALETTE  pLogPal;
        WORD          i;

        // Create a memory DC and select the DIBSection into it
        hMemDC = CreateCompatibleDC( NULL );
        hOldBitmap = (HBITMAP)SelectObject( hMemDC, *phBitmap );
        // Get the DIBSection's color table
        GetDIBColorTable( hMemDC, 0, 256, rgb );
        // Create a palette from the color tabl
        pLogPal = (LOGPALETTE *)malloc( sizeof(LOGPALETTE) + (256*sizeof(PALETTEENTRY)) );
        pLogPal->palVersion = 0x300;
        pLogPal->palNumEntries = 256;
        for(i=0;i<256;i++)
        {
            pLogPal->palPalEntry[i].peRed = rgb[i].rgbRed;
            pLogPal->palPalEntry[i].peGreen = rgb[i].rgbGreen;
            pLogPal->palPalEntry[i].peBlue = rgb[i].rgbBlue;
            pLogPal->palPalEntry[i].peFlags = 0;
        }
        *phPalette = CreatePalette( pLogPal );
        // Clean up
        free( pLogPal );
        SelectObject( hMemDC, hOldBitmap );
        DeleteDC( hMemDC );
    }
    else   // It has no color table, so use a halftone palette
    {
        HDC    hRefDC;

        hRefDC = GetDC( NULL );
        *phPalette = CreateHalftonePalette( hRefDC );
        ReleaseDC( NULL, hRefDC );
    }
    return TRUE;
}

HBRUSH
create_image_brush(const char *szFileName)
{
    HBITMAP       hBitmap;
    HPALETTE      hPalette;

    if (!LoadBitmapFromBMPFile(szFileName, &hBitmap, &hPalette))
        return NULL;

    return CreatePatternBrush(hBitmap);
}

HDC
create_image_dc(const char *szFileName, int *pwidth, int *pheight)
{
    PAINTSTRUCT   ps;
    HBITMAP       hBitmap;
    HPALETTE      hPalette;
    HDC           hDC, hMemDC;
    BITMAP        bm;

    if (!LoadBitmapFromBMPFile(szFileName, &hBitmap, &hPalette))
        return NULL;

    GetObject(hBitmap, sizeof(BITMAP), &bm);

    hDC = GetDC(NULL);
    hMemDC = CreateCompatibleDC(hDC);
    ReleaseDC(NULL, hDC);

    SelectObject(hMemDC, hBitmap);

    *pwidth = bm.bmWidth;
    *pheight = bm.bmHeight;

    return hMemDC;
}

BOOL
install_hook(HINSTANCE hInst, const char *funcname, FARPROC* orig, FARPROC newfn)
{
    PBYTE                       pImage = (PBYTE)hInst;
    PIMAGE_DOS_HEADER           pDOS = (PIMAGE_DOS_HEADER)hInst;
    PIMAGE_NT_HEADERS           pPE;
    PIMAGE_IMPORT_DESCRIPTOR    pImpDesc;
    PIMAGE_THUNK_DATA           pIAT;       /* Import Address Table */
    PIMAGE_THUNK_DATA           pINT;       /* Import Name Table */
    PIMAGE_IMPORT_BY_NAME       pImpName;

    if (pDOS->e_magic != IMAGE_DOS_SIGNATURE)
        return FALSE;
    pPE = (PIMAGE_NT_HEADERS)(pImage + pDOS->e_lfanew);
    if (pPE->Signature != IMAGE_NT_SIGNATURE)
        return FALSE;
    pImpDesc = (PIMAGE_IMPORT_DESCRIPTOR)(pImage
            + pPE->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT]
            .VirtualAddress);
    for (; pImpDesc->FirstThunk; ++pImpDesc)
    {
        if (!pImpDesc->OriginalFirstThunk)
            continue;
        pIAT = (PIMAGE_THUNK_DATA)(pImage + pImpDesc->FirstThunk);
        pINT = (PIMAGE_THUNK_DATA)(pImage + pImpDesc->OriginalFirstThunk);
        for (; pIAT->u1.Function; ++pIAT, ++pINT)
        {
            if (IMAGE_SNAP_BY_ORDINAL(pINT->u1.Ordinal))
                continue;
            pImpName = (PIMAGE_IMPORT_BY_NAME)(pImage
                    + (UINT_PTR)(pINT->u1.AddressOfData));
            if (strcmp(pImpName->Name, funcname) == 0)
            {
                FARPROC *ppfn = (FARPROC*)&pIAT->u1.Function;
                DWORD dwDummy;
                *orig = (FARPROC)(*ppfn);
                if (!VirtualProtect(ppfn, sizeof(ppfn), PAGE_EXECUTE_READWRITE, &dwDummy))
                    return FALSE;
                if (!WriteProcessMemory(GetCurrentProcess(), ppfn, &newfn, sizeof(newfn), NULL))
                    return FALSE;
                return TRUE;
            }
        }
    }
    return TRUE;
}

void
show_lasterr()
{
    LPVOID lpMessageBuffer;

    FormatMessage(
            FORMAT_MESSAGE_ALLOCATE_BUFFER |
            FORMAT_MESSAGE_FROM_SYSTEM,
            NULL,
            GetLastError(),
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            (LPTSTR) &lpMessageBuffer,
            0,
            NULL );

    MessageBox(NULL, lpMessageBuffer, "Error", MB_OK);

    LocalFree( lpMessageBuffer );
}

int
init(HINSTANCE hinstDLL)
{
    char buf[MAX_PATH];
    HDC hDC;
    HBITMAP hBM;

    /* load myself to keep instance */
    GetModuleFileName(hinstDLL, buf, sizeof(buf));
    LoadLibrary(buf);

    if (!install_hook((HINSTANCE)GetModuleHandle(NULL), "FillRect", (FARPROC*)&OrigFillRect, (FARPROC)MyFillRect))
    {
        show_lasterr();
        return 1;
    }

    if (!install_hook((HINSTANCE)GetModuleHandle(NULL), "ScrollWindowEx", (FARPROC*)&OrigScrollWindowEx, (FARPROC)MyScrollWindowEx))
    {
        show_lasterr();
        return 1;
    }

    hDC = GetDC(NULL);
    testdc = CreateCompatibleDC(hDC);
    hBM = CreateCompatibleBitmap(hDC, 1, 1);
    SelectObject(testdc, hBM);
    ReleaseDC(NULL, hDC);

    return 0;
}

int
bgimg_set_bg(int color)
{
    bgimg_color = color;
    return 0;
}

int
bgimg_set_image(const char *path)
{
    bgimg_image = create_image_brush(path);
    if (bgimg_image == NULL)
        return 1;
    return 0;
}

//! flag: -O2
//! int: objs/$(target)
//! link: winmm, user32, gdi32
//! mode: dll
BOOL WINAPI
DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    switch(fdwReason)
    {
        case DLL_PROCESS_ATTACH:
            init(hinstDLL);
            break;
        case DLL_PROCESS_DETACH:
            break;
        case DLL_THREAD_ATTACH:
            break;
        case DLL_THREAD_DETACH:
            break;
    }
    return  TRUE;
}
