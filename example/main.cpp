#include<rasterizer.hpp>
#include<functional>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include"stb_image_write.h"

typedef Eigen::Vector3f TeapotVert;

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define SCRWIDTH	640
#define SCRHEIGHT	480

static WNDCLASS wc;
static HWND wnd;
static char bitmapbuffer[sizeof(BITMAPINFO) + 16];
static BITMAPINFO* bh;
HDC window_hdc;
int* buffer = new int[SCRWIDTH * SCRHEIGHT];

struct TeapotVertVsOut
{
	Eigen::Vector4f p;
	Eigen::Vector3f color;

	TeapotVertVsOut():
		p(0.0f,0.0f,0.0f,0.0f),color(0.0f,0.0f,0.0f)
	{}
	const Eigen::Vector4f& position() const
	{
		return p;
	}
	TeapotVertVsOut& operator+=(const TeapotVertVsOut& tp)
	{
		p+=tp.p;
		color+=tp.color;
		return *this;
	}
	TeapotVertVsOut& operator*=(const float& f)
	{
		p*=f;color*=f;return *this;
	}
};

class TeapotPixel
{
public:
	Eigen::Vector4f color;
	float& depth() { return color[3]; }
	TeapotPixel():color(0.0f,0.0f,0.0f,-1e10f)
	{}
};

TeapotVertVsOut vertexShader(const TeapotVert& vertexIn,const Eigen::Matrix4f& mvp,float t)
{
	TeapotVertVsOut vout;
	vout.p=mvp*Eigen::Vector4f(vertexIn[0],vertexIn[1],vertexIn[2],1.0f);
	vout.color=Eigen::Vector3f(1.0f,0.0f,0.0f);
	return vout;
}

TeapotPixel fragmentShader(const TeapotVertVsOut& fsin)
{
	TeapotPixel p;
	p.color.head<3>()=fsin.color;
	return p;
}

void WriteFramebuffer(const SoftwareRasterizer::Framebuffer<TeapotPixel>& fb,const std::string& filename)
{
	uint8_t* pixels=new uint8_t[fb.width*fb.height*3];
	std::unique_ptr<uint8_t[]> data(pixels);

	const float* fbdata=&(fb(0,0).color[0]);
	for(size_t i=0;i<fb.width*fb.height;i++)
	{
		int color[3];
		for(int c=0;c<3;c++)
		{
			int value = max(0.0f, min(fbdata[4 * i + c] * 255.0f, 255.0f));
			pixels[3 * i + c] = value;
			color[c] = value;
		}
		buffer[i] = (color[0] << 16) + (color[1] << 8) + color[2];;
	}

	if(0==stbi_write_png(filename.c_str(),fb.width,fb.height,3,pixels,0))
	{
		std::cout << "Failure to write " << filename << std::endl;
	}
}

void DrawWindow();

static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	int result = 0, keycode = 0;
	switch (message)
	{
	case WM_PAINT:
		if (!buffer) break;
		StretchDIBits(window_hdc, 0, 0, SCRWIDTH, SCRHEIGHT, 0, 0, SCRWIDTH, SCRHEIGHT, buffer, bh, DIB_RGB_COLORS, SRCCOPY);
		ValidateRect(wnd, NULL);
		break;
	case WM_KEYDOWN:
		if ((wParam & 0xFF) != 27) break;
	case WM_CLOSE:
		delete[] buffer;
		ReleaseDC(wnd, window_hdc);
		DestroyWindow(wnd);
		SystemParametersInfo(SPI_SETSCREENSAVEACTIVE, 1, 0, 0);
		ExitProcess(0);
		break;
	default:
		result = DefWindowProc(hWnd, message, wParam, lParam);
	}
	return result;
}

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	RECT rect;
	int cc;
	wc.style = CS_OWNDC | CS_VREDRAW | CS_HREDRAW;
	wc.lpfnWndProc = WndProc;
	wc.cbClsExtra = wc.cbWndExtra = 0;
	wc.hInstance = 0;
	wc.hIcon = NULL;
	wc.hCursor = LoadCursor(0, IDC_ARROW);
	wc.hbrBackground = NULL;
	wc.lpszMenuName = NULL;
	wc.lpszClassName = "software rasterizer";
	if (!RegisterClass(&wc)) return FALSE;
	rect.left = rect.top = 0;
	rect.right = SCRWIDTH, rect.bottom = SCRHEIGHT;
	AdjustWindowRect(&rect, WS_POPUP | WS_SYSMENU | WS_CAPTION, 0);
	rect.right -= rect.left, rect.bottom -= rect.top;
	wnd = CreateWindowEx(0, "software rasterizer", "software rasterizer", WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME,
		CW_USEDEFAULT, CW_USEDEFAULT, rect.right, rect.bottom, 0, 0, 0, 0);
	ShowWindow(wnd, SW_NORMAL);
	for (cc = 0; cc < sizeof(BITMAPINFOHEADER) + 16; cc++) bitmapbuffer[cc] = 0;
	bh = (BITMAPINFO *)&bitmapbuffer;
	bh->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bh->bmiHeader.biPlanes = 1;
	bh->bmiHeader.biBitCount = 32;
	bh->bmiHeader.biCompression = BI_BITFIELDS;
	bh->bmiHeader.biWidth = SCRWIDTH, bh->bmiHeader.biHeight = -SCRHEIGHT;
	((unsigned long*)bh->bmiColors)[0] = 255 << 16;
	((unsigned long*)bh->bmiColors)[1] = 255 << 8;
	((unsigned long*)bh->bmiColors)[2] = 255;
	window_hdc = GetDC(wnd);
	SystemParametersInfo(SPI_SETSCREENSAVEACTIVE, 0, 0, 0);
	const TeapotVert triangle[6] = {
		{ -0.5f,-0.5f,0.0f },
	{ 0.5f,-0.5f,0.0f },
	{ 0.0f,.5f,0.0f },

	{ -0.0f,-0.0f,0.2f },
	{ 1.0f,-0.0f,0.2f },
	{ 0.5f,1.0f,  0.2f }
	};
	const size_t indexBuffer[6] = { 0,1,2,3,4,5 };

	const TeapotVert* vertexBufferBegin = triangle;
	const TeapotVert* vertexBufferEnd = triangle + 6;
	const size_t* indexBufferBegin = indexBuffer;
	const size_t* indexBufferEnd = indexBuffer + 6;

	Eigen::Matrix4f cameraMatrix = Eigen::Matrix4f::Identity();
	float time = 0.0;
	SoftwareRasterizer::Framebuffer<TeapotPixel> tp(640, 480);
	SoftwareRasterizer::draw(tp,
			vertexBufferBegin, vertexBufferEnd,
			indexBufferBegin, indexBufferEnd,
			(TeapotVertVsOut*)NULL, (TeapotVertVsOut*)NULL,
			std::bind(vertexShader, std::placeholders::_1, cameraMatrix, time),
			fragmentShader
		);
	WriteFramebuffer(tp, "out.png");

	while(1)
		DrawWindow();
	return 1;
}

void DrawWindow()
{
	MSG message;
	HACCEL haccel = 0;
	InvalidateRect(wnd, NULL, TRUE);
	SendMessage(wnd, WM_PAINT, 0, 0);
	while (PeekMessage(&message, wnd, 0, 0, PM_REMOVE))
	{
		if (TranslateAccelerator(wnd, haccel, &message) == 0)
		{
			TranslateMessage(&message);
			DispatchMessage(&message);
		}
	}
	Sleep(0);
}

