// TextChat_P2P_GUI.cpp : Defines the entry point for the application.
//
#include "framework.h"
#include "TextChat_P2P_GUI.h"
#include "ctime"
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <iostream>
#include <WinSock2.h>
#pragma comment(lib,"ws2_32.lib")
#include <WS2tcpip.h>
//sfml
#include <SFML/Audio.hpp>
#include <atomic>
#include <cassert>
#include <mutex>
#include <queue>
//sfml
//===============================================================================================================
//<<<const
#define MAX_LOADSTRING 100
#define SERVER_IP "192.168.0.101"
#define PORT_CLIENT_A 8000
#define PORT_CLIENT_B 8500
#define PORT_SERVER 9000

//const>>>
//<<<TAG
#define SIZE_OF_BUFF 1024
#define SIZE_OF_BUFF_FROM_SERVER 128
#define LOGIN "LOGIN"
#define MSG_LOGIN_SUCCESSFULLY "LOGIN_SUCCESSFULLY"
#define MSG_LOGIN_WRONG_USER_OR_PASSWORD "LOGIN_WRONG_USER_OR_PASSWORD"
#define NOTICE_LOGIN_WRONG_USER_OR_PASSWORD "Wrong user or password, try again!"
#define MSG_WRONG_FORM "WRONG_FORM"
#define NOTICE_WRONG_FORM "Wrong submit form, pls try again!"
#define CALL "CALL"
#define CALL_FROM "CALL_FROM"
#define ACCEPT "ACCEPT"
#define ACCEPT_FROM "ACCEPT_FROM"
#define DECLINE "DECLINE"
#define DECLINE_FROM "DECLINE_FROM"
#define UPDATE_LIST_ONLINE_USER_START "UPDATE_LIST_ONLINE_USER_START"
#define UPDATE_LIST_ONLINE_USER_DOING "UPDATE_LIST_ONLINE_USER_DOING"
//TAG>>>
//==========================================================================================================


//<<<Prototype
DWORD WINAPI ReceiverFromServerThread(LPVOID);
DWORD WINAPI ReceiverFromFriendThread(LPVOID);
DWORD WINAPI SendDataThread(LPVOID lpParam);
DWORD WINAPI ReadKeyBoardThread(LPVOID);
void callUDP_P2P();
void changeWindowBaseOnParentWindowPosition(HWND, HWND);
//Prototype>>>

//<<<Struct
struct Samples {
	Samples(sf::Int16 const* ss, std::size_t count) {
		samples.reserve(count);
		std::copy_n(ss, count, std::back_inserter(samples));
	}

	Samples() {}

	std::vector<sf::Int16> samples;
};
//Struct>>>

//<<< Global Variables:
	//sfml: audio vars
		std::atomic<bool> isRecording{ false };
		std::mutex mutex_recv, mutex_send; // protects `data`
		std::condition_variable cv_recv, cv_send; // notify consumer thread of new samples
		std::queue<Samples> data_recv;
		std::queue<Samples> data_send; // samples come in from the recorder, and popped by the output stream
		Samples playingSamples, sendingSamples; // used by the output stream.
	//sfml: audio vars

	//win32app 
HINSTANCE hInst;                                // current instance
WCHAR szTitle[MAX_LOADSTRING];                  // The title bar text
WCHAR szWindowClass[MAX_LOADSTRING];            // the main window class name
HWND loginWnd, listOnlWnd, callFromWnd, inTheCallWnd;
	//win32app>>>

	//winsock
WSADATA wsa;
SOCKET p2p, client_to_server;
SOCKADDR_IN friendaddr, myaddr, serveraddr;
	//winsock>>>
char friendUserName[32], myIP[32], myPort[32];
int myRandomPort;
char myUser[128];
bool isCallingToFriend = false;
//Global Vars>>>
//============================================================================================================

//<<OVERWRITE Class
class MyRecorder : public sf::SoundRecorder {
public: /** API **/

	// Initialise capturing input & setup output
	void start() {
		sf::SoundRecorder::start();
	}

	// Stop both recording & playback
	void stop() {
		sf::SoundRecorder::stop();
	}

	bool isRunning() { return isRecording; }


	~MyRecorder() {
		stop();
	}


protected: /** OVERRIDING SoundRecorder **/

	bool onProcessSamples(sf::Int16 const* samples, std::size_t sampleCount) override {
		{
			std::lock_guard<std::mutex> lock(mutex_send);
			data_send.emplace(samples, sampleCount);
		}
		cv_send.notify_one();
		return true; // continue capture
	}

	bool onStart() override {
		isRecording = true;
		return true;
	}

	void onStop() override {
		isRecording = false;
		cv_send.notify_one();
	}
};
class MyStream : private sf::SoundStream {
public:
	void initialize(unsigned int channelCount, unsigned int sampleRate) {
		sf::SoundStream::initialize(channelCount, sampleRate);
	}
	void play() {
		sf::SoundStream::play();
	}
	void stop() {
		sf::SoundStream::stop();
	}
protected: /** OVERRIDING SoundStream **/

	bool onGetData(Chunk& chunk) override {
		// Wait until either:
		//  a) the recording was stopped
		//  b) new data is available
		std::unique_lock<std::mutex> lock2(mutex_recv);
		cv_recv.wait(lock2);

		// Lock was acquired, examine which case we're into:
		if (!isRecording) return false;
		else
		{
			assert(!data_recv.empty());
			playingSamples.samples = std::move(data_recv.front().samples);
			data_recv.pop();
			//std::cout << "*******size" << playingSamples.samples.size() << "\n";
			chunk.sampleCount = playingSamples.samples.size();
			chunk.samples = playingSamples.samples.data();
			return true;
		}
	}
	void onSeek(sf::Time) override { /* Not supported, silently does nothing. */ }

private:
};
//OVERWRITE Class>>>
//==========================================================================================================
MyRecorder myRecorder;
MyStream myStream;

// Forward declarations of functions included in this code module:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // TODO: Place code here.
	//main()
	WSAStartup(MAKEWORD(2, 2), &wsa);
	p2p = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	client_to_server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	
//**init socket addr
	//sockaddr for socket that listen from client friend
	srand(time(NULL));
	myRandomPort = rand() % 250 + 8000;
	myaddr.sin_addr.s_addr = htonl(ADDR_ANY);
	myaddr.sin_family = AF_INET;
	myaddr.sin_port = htons(myRandomPort);

	_itoa(myaddr.sin_addr.s_addr, myIP, 10);
	_itoa(myaddr.sin_port, myPort, 10);

	//sockaddr for socket to send msg to friend
	friendaddr.sin_family = AF_INET;

	//sockaddr for socket that listen from server
	serveraddr.sin_addr.s_addr = inet_addr(SERVER_IP);
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_port = htons(PORT_SERVER);
	connect(client_to_server, (SOCKADDR*)& serveraddr, sizeof(SOCKADDR_IN));


//**init 1 thread one to recv data from Server, one for reading keyboard typing
	CreateThread(0, 0, ReceiverFromServerThread, 0, 0, 0);
    // Initialize global strings
    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_TEXTCHATP2PGUI, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    // Perform application initialization:
    if (!InitInstance (hInstance, nCmdShow))
    {
        return FALSE;
    }

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_TEXTCHATP2PGUI));

    MSG msg;

    // Main message loop:
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

	closesocket(client_to_server);
	WSACleanup();
    return (int) msg.wParam;
}

//===============================================================================================================


//
//  FUNCTION: MyRegisterClass()
//
//  PURPOSE: Registers the window class.
//
ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = WndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_TEXTCHATP2PGUI));
    wcex.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName   = MAKEINTRESOURCEW(IDC_TEXTCHATP2PGUI);
    wcex.lpszClassName  = szWindowClass;
    wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

//
//   FUNCTION: InitInstance(HINSTANCE, int)
//
//   PURPOSE: Saves instance handle and creates main window
//
//   COMMENTS:
//
//        In this function, we save the instance handle in a global variable and
//        create and display the main program window.
//
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
	hInst = hInstance; // Store instance handle in our global variable
//** Create Login Window
	loginWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
		500, 100, 400, 600, nullptr, nullptr, hInstance, nullptr);
	CreateWindowEx(WS_EX_CLIENTEDGE, TEXT("STATIC"), TEXT("We invented a phone"), WS_CHILD | WS_VISIBLE | WS_TABSTOP, 110, 10, 180, 25, loginWnd, (HMENU)IDC_EDIT_TEXT_STATIC_LOGIN, GetModuleHandle(NULL), NULL);
	CreateWindowEx(WS_EX_CLIENTEDGE, TEXT("STATIC"), TEXT("User: "), WS_CHILD | WS_VISIBLE | WS_TABSTOP, 40, 60, 60, 40, loginWnd, (HMENU)IDC_EDIT_TEXT_STATIC_LOGIN, GetModuleHandle(NULL), NULL);
	CreateWindowEx(WS_EX_CLIENTEDGE, TEXT("EDIT"), TEXT(""), WS_CHILD | WS_VISIBLE | WS_TABSTOP, 100, 60, 250, 40, loginWnd, (HMENU)IDC_EDIT_TEXT_USER, GetModuleHandle(NULL), NULL);
	CreateWindowEx(WS_EX_CLIENTEDGE, TEXT("STATIC"), TEXT("Password: "), WS_CHILD | WS_VISIBLE | WS_TABSTOP, 40, 110, 60, 40, loginWnd, (HMENU)IDC_EDIT_TEXT_STATIC_LOGIN, GetModuleHandle(NULL), NULL);
	CreateWindowEx(WS_EX_CLIENTEDGE, TEXT("EDIT"), TEXT(""), WS_CHILD | WS_VISIBLE | WS_TABSTOP, 100, 110, 250, 40, loginWnd, (HMENU)IDC_EDIT_TEXT_PASSWORD, GetModuleHandle(NULL), NULL);
	CreateWindowEx(WS_EX_CLIENTEDGE, TEXT("BUTTON"), TEXT("Login"), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 160, 170, 60, 40, loginWnd, (HMENU)IDC_BUTTON_LOGIN, GetModuleHandle(NULL), NULL);
	CreateWindowEx(WS_EX_CLIENTEDGE, TEXT("STATIC"), TEXT("............"), WS_CHILD | WS_VISIBLE | WS_TABSTOP, 60, 220, 260, 30, loginWnd, (HMENU)IDC_EDIT_TEXT_STATIC_MSG, GetModuleHandle(NULL), NULL);
	if (!loginWnd)
	{
		return FALSE;
	}
	
//** Create Main Calling Window
	listOnlWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
		500, 100, 400, 600, nullptr, nullptr, hInstance, nullptr);

	
	CreateWindowEx(WS_EX_CLIENTEDGE, TEXT("STATIC"), TEXT("Friend NickName:"), WS_CHILD | WS_VISIBLE | WS_TABSTOP, 10, 10, 80, 40, listOnlWnd, (HMENU)IDC_EDIT_TEXT_STATIC, GetModuleHandle(NULL), NULL);
	CreateWindowEx(WS_EX_CLIENTEDGE, TEXT("EDIT"), TEXT(""), WS_CHILD | WS_VISIBLE | WS_TABSTOP, 100, 10, 200, 40, listOnlWnd, (HMENU)IDC_EDIT_TEXT, GetModuleHandle(NULL), NULL);
	CreateWindowEx(WS_EX_CLIENTEDGE, TEXT("BUTTON"), TEXT("Call"), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 310, 10, 60, 40, listOnlWnd, (HMENU)IDC_BUTTON_CALL, GetModuleHandle(NULL), NULL);

	CreateWindowEx(WS_EX_CLIENTEDGE, TEXT("STATIC"), TEXT("Chat"), WS_CHILD | WS_VISIBLE | WS_TABSTOP, 10, 60, 80, 40, listOnlWnd, (HMENU)IDC_EDIT_TEXT_CHAT_STATIC, GetModuleHandle(NULL), NULL);
	CreateWindowEx(WS_EX_CLIENTEDGE, TEXT("EDIT"), TEXT(""), WS_CHILD | WS_VISIBLE | WS_TABSTOP, 100, 60, 200, 40, listOnlWnd, (HMENU)IDC_EDIT_TEXT_CHAT, GetModuleHandle(NULL), NULL);
	CreateWindowEx(WS_EX_CLIENTEDGE, TEXT("BUTTON"), TEXT("Send"), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 310, 60, 60, 40, listOnlWnd, (HMENU)IDC_BUTTON_SEND, GetModuleHandle(NULL), NULL);

	CreateWindowEx(WS_EX_CLIENTEDGE, TEXT("LISTBOX"), TEXT("LIST ONLINE CLIENTS"), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOVSCROLL, 10, 110, 360, 430, listOnlWnd, (HMENU)IDC_LIST_CLIENT, GetModuleHandle(NULL), NULL);
	SendDlgItemMessageA(listOnlWnd, IDC_LIST_CLIENT, LB_ADDSTRING, 0, (LPARAM) "Hello");

	
	if (!listOnlWnd)
	{
		return FALSE;
	}

	callFromWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
		500, 100, 400, 600, nullptr, nullptr, hInstance, nullptr);
	CreateWindowEx(WS_EX_CLIENTEDGE, TEXT("STATIC"), TEXT("CALL FROM"), WS_CHILD | WS_VISIBLE | WS_TABSTOP, 100, 10, 100, 40, callFromWnd, (HMENU)IDC_EDIT_TEXT_STATIC_CALL_FROM, GetModuleHandle(NULL), NULL);
	CreateWindowEx(WS_EX_CLIENTEDGE, TEXT("STATIC"), TEXT(""), WS_CHILD | WS_VISIBLE | WS_TABSTOP, 100, 70, 150, 40, callFromWnd, (HMENU)IDC_EDIT_TEXT_STATIC_CALLER_NAME, GetModuleHandle(NULL), NULL);
	CreateWindowEx(WS_EX_CLIENTEDGE, TEXT("BUTTON"), TEXT("Decline"), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 80, 120, 80, 40, callFromWnd, (HMENU)IDC_BUTTON_DECLINE, GetModuleHandle(NULL), NULL);
	CreateWindowEx(WS_EX_CLIENTEDGE, TEXT("BUTTON"), TEXT("Accept"), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 220, 120, 80, 40, callFromWnd, (HMENU)IDC_BUTTON_ACCEPT, GetModuleHandle(NULL), NULL);


	if (!callFromWnd)
	{
		return FALSE;
	}


	inTheCallWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
		500, 100, 400, 600, nullptr, nullptr, hInstance, nullptr);
	CreateWindowEx(WS_EX_CLIENTEDGE, TEXT("STATIC"), TEXT("IN CONVERSTATION WITH"), WS_CHILD | WS_VISIBLE | WS_TABSTOP, 80, 10, 200, 40, inTheCallWnd, (HMENU)IDC_EDIT_TEXT_STATIC_IN_THE_CALL, GetModuleHandle(NULL), NULL);
	CreateWindowEx(WS_EX_CLIENTEDGE, TEXT("STATIC"), TEXT(""), WS_CHILD | WS_VISIBLE | WS_TABSTOP, 100, 70, 150, 40, inTheCallWnd, (HMENU)IDC_EDIT_TEXT_STATIC__IN_THE_CALL_CALLER_NAME, GetModuleHandle(NULL), NULL);
	CreateWindowEx(WS_EX_CLIENTEDGE, TEXT("BUTTON"), TEXT("STOP"), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 80, 120, 80, 40, inTheCallWnd, (HMENU)IDC_BUTTON_STOP, GetModuleHandle(NULL), NULL);

	CreateWindowEx(WS_EX_CLIENTEDGE, TEXT("STATIC"), TEXT("Chat"), WS_CHILD | WS_VISIBLE | WS_TABSTOP, 10, 170, 80, 40, inTheCallWnd, (HMENU)IDC_EDIT_TEXT_CHAT_STATIC_IN_THE_CALL_CHAT, GetModuleHandle(NULL), NULL);
	CreateWindowEx(WS_EX_CLIENTEDGE, TEXT("EDIT"), TEXT(""), WS_CHILD | WS_VISIBLE | WS_TABSTOP, 100, 170, 200, 40, inTheCallWnd, (HMENU)IDC_EDIT_TEXT_CHAT_IN_THE_CALL, GetModuleHandle(NULL), NULL);
	CreateWindowEx(WS_EX_CLIENTEDGE, TEXT("BUTTON"), TEXT("Send"), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 310, 170, 60, 40, inTheCallWnd, (HMENU)IDC_BUTTON_SEND_IN_THE_CALL, GetModuleHandle(NULL), NULL);

	ShowWindow(loginWnd, nCmdShow);
	UpdateWindow(loginWnd);


	/*ShowWindow(hWnd, nCmdShow);
	UpdateWindow(hWnd);*/

	return TRUE;
}

//
//  FUNCTION: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  PURPOSE: Processes messages for the main window.
//
//  WM_COMMAND  - process the application menu
//  WM_PAINT    - Paint the main window
//  WM_DESTROY  - post a quit message and return
//
//

//===============================================================================================================

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	int i = 0;
    switch (message)
    {
	case WM_COMMAND:
	{
		int wmId = LOWORD(wParam);
		// Parse the menu selections:
		switch (wmId)
		{
		case IDC_BUTTON_CALL:
			char buff_call[128], buff_friend[32];
			GetDlgItemTextA(listOnlWnd, IDC_EDIT_TEXT, buff_friend, sizeof(buff_friend));
			strcpy(friendUserName, buff_friend);
			sprintf(buff_call, "%s %s %s %s %s", CALL, myUser, friendUserName, myIP, myPort);
			send(client_to_server, buff_call, strlen(buff_call), 0);
			break;
		case IDC_BUTTON_ACCEPT:
			char buff_accept[128];
			sprintf(buff_accept, "%s %s %s %s %s", ACCEPT, friendUserName, myUser ,myIP, myPort);
			send(client_to_server, buff_accept, strlen(buff_accept), 0);
			SetDlgItemTextA(inTheCallWnd, IDC_EDIT_TEXT_STATIC__IN_THE_CALL_CALLER_NAME, friendUserName);
			changeWindowBaseOnParentWindowPosition(callFromWnd, inTheCallWnd);
			callUDP_P2P();
			break;
		case IDC_BUTTON_DECLINE:
			changeWindowBaseOnParentWindowPosition(callFromWnd, listOnlWnd);
			break;
		case IDC_BUTTON_STOP: 
			changeWindowBaseOnParentWindowPosition(inTheCallWnd, listOnlWnd);
			break;
		case IDC_BUTTON_SEND_IN_THE_CALL:
			char buff_inTheCall[1024];
			GetDlgItemTextA(inTheCallWnd, IDC_EDIT_TEXT_CHAT_IN_THE_CALL, buff_inTheCall, sizeof(buff_inTheCall));
			sendto(p2p, buff_inTheCall, strlen(buff_inTheCall), 0, (SOCKADDR*)& friendaddr, sizeof(SOCKADDR));
			SetDlgItemTextA(inTheCallWnd, IDC_EDIT_TEXT_CHAT_IN_THE_CALL, "");
			break;
		case IDC_BUTTON_SEND:
			changeWindowBaseOnParentWindowPosition(listOnlWnd, callFromWnd);
			char buff[1024];
			GetDlgItemTextA(listOnlWnd, IDC_EDIT_TEXT_CHAT, buff, sizeof(buff));
			sendto(client_to_server, buff, strlen(buff), 0, (SOCKADDR*)& friendaddr, sizeof(SOCKADDR));
			SetDlgItemTextA(listOnlWnd, IDC_EDIT_TEXT_CHAT, "");
			break;
		case IDC_BUTTON_LOGIN:
			char buff_login[128], buff_user[32], buff_password[32];
			//get user name and password then send to server to LOGIN
			GetDlgItemTextA(loginWnd, IDC_EDIT_TEXT_USER, buff_user, sizeof(buff_user));
			GetDlgItemTextA(loginWnd, IDC_EDIT_TEXT_PASSWORD, buff_password, sizeof(buff_password));
			sprintf(buff_login, "%s %s %s %s %s", LOGIN, buff_user, buff_password, "nah", "nah");
			send(client_to_server, buff_login, strlen(buff_login), 0);
			break;
		case IDC_LIST_CLIENT:
			i = SendDlgItemMessageA(listOnlWnd, IDC_LIST_CLIENT, LB_GETCURSEL, 0, 0);
			if (i >= 0) {
				char buff_listbox_item[128];
				SendDlgItemMessageA(listOnlWnd, IDC_LIST_CLIENT, LB_GETTEXT, i, (LPARAM)buff_listbox_item);
				SetDlgItemTextA(listOnlWnd, IDC_EDIT_TEXT, buff_listbox_item);
			}
			break;
		case IDM_ABOUT:
			DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
			break;
		case IDM_EXIT:
			DestroyWindow(hWnd);
			break;
		default:
			return DefWindowProc(hWnd, message, wParam, lParam);
		}
	}
	break;
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            // TODO: Add any drawing code that uses hdc here...
            EndPaint(hWnd, &ps);
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}


//** My Functions
DWORD WINAPI ReceiverFromServerThread(LPVOID lpParam) {
	//***new thread to recv data from server
	int ret;
	char buff_RESPONSE[32], buff_arg1[32], buff_arg2[32], buff_arg3[32], buff_arg4[32];
	char buff[SIZE_OF_BUFF_FROM_SERVER];

	while (true) {
			ret = recv(client_to_server, buff, sizeof(buff), 0);
		if (ret < 0) continue;
		else if (ret < 1024) {
			buff[ret] = 0;
			//form of buff: ACCEPT_FROM a b "ip" "port"
			sscanf(buff, "%s %s %s %s %s", buff_RESPONSE, buff_arg1, buff_arg2, buff_arg3, buff_arg4);
			if (strcmp(buff_RESPONSE, CALL_FROM) == 0) {
				//update info of friend to prepare for accepting  the call from friend
				strcpy(friendUserName, buff_arg1);
				friendaddr.sin_addr.s_addr = inet_addr(buff_arg3);
 				friendaddr.sin_port = (unsigned short)atoi(buff_arg4);

				SetDlgItemTextA(callFromWnd, IDC_EDIT_TEXT_STATIC_CALLER_NAME, friendUserName);
				changeWindowBaseOnParentWindowPosition(listOnlWnd, callFromWnd);
			}
			if (strcmp(buff_RESPONSE, ACCEPT_FROM) == 0) {
				SetDlgItemTextA(inTheCallWnd, IDC_EDIT_TEXT_STATIC__IN_THE_CALL_CALLER_NAME, friendUserName);
				friendaddr.sin_addr.s_addr = inet_addr(buff_arg3);
				friendaddr.sin_port = (unsigned short)atoi(buff_arg4);
				changeWindowBaseOnParentWindowPosition(listOnlWnd, inTheCallWnd);
				callUDP_P2P();
			}
			else if (strcmp(buff_RESPONSE, DECLINE_FROM) == 0) {

			}
			else if (strcmp(buff_RESPONSE, MSG_LOGIN_SUCCESSFULLY) == 0) {
				GetDlgItemTextA(loginWnd, IDC_EDIT_TEXT_USER, myUser, sizeof(myUser));
				changeWindowBaseOnParentWindowPosition(loginWnd, listOnlWnd);
				char port[32];
				sprintf(port, "%d : %d",myRandomPort, myaddr.sin_port);
				SetDlgItemTextA(listOnlWnd, IDC_EDIT_TEXT_CHAT, port);
			}
			else if (strcmp(buff_RESPONSE, MSG_WRONG_FORM) == 0) {
				SetDlgItemTextA(loginWnd, IDC_EDIT_TEXT_STATIC_MSG, NOTICE_WRONG_FORM);

			}
			else if (strcmp(buff_RESPONSE, MSG_LOGIN_WRONG_USER_OR_PASSWORD) == 0) {	
				SetDlgItemTextA(loginWnd, IDC_EDIT_TEXT_STATIC_MSG, NOTICE_LOGIN_WRONG_USER_OR_PASSWORD);
			}
			else if (strcmp(buff_RESPONSE, UPDATE_LIST_ONLINE_USER_START) == 0) {
				SendMessage(GetDlgItem(listOnlWnd,IDC_LIST_CLIENT), LB_RESETCONTENT, 0, 0);
				SendDlgItemMessageA(listOnlWnd, IDC_LIST_CLIENT, LB_ADDSTRING, 0, (LPARAM)buff_arg1);
			}else if (strcmp(buff_RESPONSE, UPDATE_LIST_ONLINE_USER_DOING) == 0) {
				SendDlgItemMessageA(listOnlWnd, IDC_LIST_CLIENT, LB_ADDSTRING, 0, (LPARAM)buff_arg1);
			}
		}
	}
}

DWORD WINAPI ReceiverFromFriendThread(LPVOID lpParam) {
	int ret;
	sf::Int16 buff[10000];
	while (true) {
		ret = recvfrom(p2p, (char*)buff, sizeof(buff), 0, NULL, NULL);
		std::lock_guard<std::mutex> lock2(mutex_recv);
		data_recv.emplace(buff, ret / 2);
		cv_recv.notify_one();	
	}
	/*int ret;
	char buff[SIZE_OF_BUFF];
	while (true) {
		ret = recvfrom(p2p, buff, sizeof(buff), 0, NULL, NULL);
		if (ret < 0)continue;
		if (ret < SIZE_OF_BUFF) {
			buff[ret] = 0;
			SetDlgItemTextA(inTheCallWnd, IDC_EDIT_TEXT_STATIC_IN_THE_CALL, buff);
		}
	} //pre: this was belong to text chat*/
}
DWORD WINAPI SendDataThread(LPVOID lpParam) {
	int size = sizeof(sf::Int16);
	while (true) {
		std::unique_lock<std::mutex> lock1(mutex_send);
		cv_send.wait(lock1);
		// Lock was acquired, examine which case we're into:
		if (!isRecording) return false;
		else
		{
			assert(!data_send.empty());
			sendingSamples.samples = std::move(data_send.front().samples);
			data_send.pop();
			sendto(p2p, (char*)(sendingSamples.samples.data()), sendingSamples.samples.size() * size, 0, (SOCKADDR*)& friendaddr, sizeof(SOCKADDR));
		}
	}

}
void callUDP_P2P() {
	isCallingToFriend = true;
	//create listener from client friend
	bind(p2p, (SOCKADDR*)& myaddr, sizeof(SOCKADDR));

	//create thread that recv audio from friend and push to data queue
	CreateThread(0, 0, ReceiverFromFriendThread, &p2p, 0, 0);

	//init recorder to record from device, stream to playback audio that recved from friend
	myRecorder.start();
	CreateThread(0, 0, SendDataThread, &p2p, 0, 0);
	
	myStream.initialize(myRecorder.getChannelCount(), myRecorder.getSampleRate());
	myStream.play();
	CreateThread(0, 0, ReceiverFromFriendThread, &p2p, 0, 0);
}
void changeWindowBaseOnParentWindowPosition(HWND parent, HWND current) {
	RECT rect;
	GetWindowRect(parent, &rect);
	//SetWindowPos(current, HWND_TOP, rect.left, rect.top, 400, 600, SWP_SHOWWINDOW); this works, but i like the other way
	SetWindowPos(current, NULL, rect.left, rect.top, 400, 600, NULL);
	ShowWindow(parent, SW_HIDE);
	ShowWindow(current, SW_SHOW);
	UpdateWindow(parent);
	UpdateWindow(current);
}
//===============================================================================================================

// Message handler for about box.
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}
//TODO: Change port and ip of server