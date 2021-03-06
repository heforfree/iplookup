// Ip Lookup
// Copyright (c) 2011-2016 Henry++

#include <ws2tcpip.h>
#include <windows.h>
#include <winsock2.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>

#include "main.h"
#include "rapp.h"
#include "routine.h"

#include "resource.h"

rapp app (APP_NAME, APP_NAME_SHORT, APP_VERSION, APP_COPYRIGHT);

volatile LONG lock = 0;

INT inet_ntop (ADDRESS_FAMILY af, const LPVOID src, LPWSTR dst, socklen_t size)
{
	struct sockaddr_storage ss;
	ULONG s = size;

	SecureZeroMemory (&ss, sizeof (ss));

	ss.ss_family = af;

	switch (af)
	{
		case AF_INET:
		{
			((struct sockaddr_in *)&ss)->sin_addr = *(struct in_addr *)src;
			break;
		}

		case AF_INET6:
		{
			((struct sockaddr_in6 *)&ss)->sin6_addr = *(struct in6_addr *)src;
			break;
		}

		default:
		{
			return 0;
		}
	}

	/* cannot direclty use &size because of strict aliasing rules */
	return WSAAddressToString ((struct sockaddr *)&ss, sizeof (ss), nullptr, dst, &s);
}

UINT WINAPI _app_print (LPVOID)
{
	_R_SPINLOCK (lock);

	SendDlgItemMessage (app.GetHWND (), IDC_LISTVIEW, LVM_DELETEALLITEMS, 0, 0);

	_r_ctrl_enable (app.GetHWND (), IDC_REFRESH, FALSE);

	PIP_ADAPTER_ADDRESSES adapter_addresses = nullptr;
	PIP_ADAPTER_ADDRESSES adapter = nullptr;

	ULONG size = 0;

	rstring buffer;

	while (1)
	{
		size += _R_BUFFER_LENGTH;

		adapter_addresses = (PIP_ADAPTER_ADDRESSES)malloc (size);

		DWORD error = GetAdaptersAddresses (AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_SKIP_FRIENDLY_NAME, nullptr, adapter_addresses, &size);

		if (error == ERROR_SUCCESS)
		{
			break;
		}
		else
		{
			free (adapter_addresses);
			adapter_addresses = nullptr;

			if (error == ERROR_BUFFER_OVERFLOW)
				continue;

			break;
		}
	}

	if (adapter_addresses)
	{
		for (adapter = adapter_addresses; adapter != nullptr; adapter = adapter->Next)
		{
			if (IF_TYPE_SOFTWARE_LOOPBACK == adapter->IfType)
			{
				continue;
			}

			for (IP_ADAPTER_UNICAST_ADDRESS* address = adapter->FirstUnicastAddress; address != nullptr; address = address->Next)
			{
				auto family = address->Address.lpSockaddr->sa_family;

				if (family == AF_INET)
				{
					// ipv4
					SOCKADDR_IN* ipv4 = reinterpret_cast<SOCKADDR_IN*>(address->Address.lpSockaddr);

					inet_ntop (AF_INET, &(ipv4->sin_addr), buffer.GetBuffer (INET_ADDRSTRLEN), INET_ADDRSTRLEN);
					buffer.ReleaseBuffer ();

					_r_listview_additem (app.GetHWND (), IDC_LISTVIEW, buffer, LAST_VALUE, 0, LAST_VALUE, 0);
				}
				else if (family == AF_INET6)
				{
					// ipv6
					SOCKADDR_IN6* ipv6 = reinterpret_cast<SOCKADDR_IN6*>(address->Address.lpSockaddr);

					inet_ntop (AF_INET6, &(ipv6->sin6_addr), buffer.GetBuffer (INET6_ADDRSTRLEN), INET6_ADDRSTRLEN);
					buffer.ReleaseBuffer ();

					// detect and skip non-external addresses
					if (tolower (buffer.At(0)) == L'f' && tolower (buffer.At (1)) == L'e')
					{
						if (buffer.At (2) == L'8' || buffer.At (2) == L'9' || buffer.At (2) == L'a' || buffer.At (2) == L'b')
							continue;
					}
					else if (buffer.Find (L"2001:0:") == 0)
					{
						continue;
					}

					_r_listview_additem (app.GetHWND (), IDC_LISTVIEW, buffer, LAST_VALUE, 0, LAST_VALUE, 0);
				}
				else
				{
					continue;
				}
			}
		}

		free (adapter_addresses);
		adapter_addresses = nullptr;
	}

	if (app.ConfigGet (L"GetExternalIp", 0).AsBool ())
	{
		HINTERNET internet = nullptr;
		HINTERNET connect = nullptr;

		internet = InternetOpen (app.GetUserAgent (), INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);

		if (internet)
		{
			connect = InternetOpenUrl (internet, app.ConfigGet (L"ExternalUrl", EXTERNAL_URL), nullptr, 0, INTERNET_FLAG_RESYNCHRONIZE | INTERNET_FLAG_NO_COOKIES, 0);

			if (connect)
			{
				DWORD dwStatus = 0, dwStatusSize = sizeof (dwStatus);
				HttpQueryInfo (connect, HTTP_QUERY_FLAG_NUMBER | HTTP_QUERY_STATUS_CODE, &dwStatus, &dwStatusSize, nullptr);

				if (dwStatus == HTTP_STATUS_OK)
				{
					DWORD out = 0;
					CHAR buffera[MAX_PATH] = {0};

					InternetReadFile (connect, buffera, MAX_PATH - 1, &out);

					if (out)
					{
						buffera[out] = 0;

						_r_listview_additem (app.GetHWND (), IDC_LISTVIEW, rstring (buffera), LAST_VALUE, 0, LAST_VALUE, 1);
					}
				}
			}
		}

		InternetCloseHandle (connect);
		InternetCloseHandle (internet);
	}

	_r_ctrl_enable (app.GetHWND (), IDC_REFRESH, TRUE);

	_R_SPINUNLOCK (lock);

	return ERROR_SUCCESS;
}

BOOL initializer_callback (HWND hwnd, DWORD msg, LPVOID, LPVOID)
{
	switch (msg)
	{
		case _RM_LOCALIZE:
		{
			// configure listview
			_r_listview_deleteallgroups (hwnd, IDC_LISTVIEW);

			_r_listview_addgroup (hwnd, IDC_LISTVIEW, 0, I18N (&app, IDS_GROUP1, 0));
			_r_listview_addgroup (hwnd, IDC_LISTVIEW, 1, I18N (&app, IDS_GROUP2, 0));

			// localize
			HMENU menu = GetMenu (hwnd);

			app.LocaleMenu (menu, I18N (&app, IDS_FILE, 0), 0, TRUE);
			app.LocaleMenu (menu, I18N (&app, IDS_EXIT, 0), IDM_EXIT, FALSE);
			app.LocaleMenu (menu, I18N (&app, IDS_SETTINGS, 0), 1, TRUE);
			app.LocaleMenu (menu, I18N (&app, IDS_ALWAYSONTOP_CHK, 0), IDM_ALWAYSONTOP_CHK, FALSE);
			app.LocaleMenu (menu, I18N (&app, IDS_CHECKUPDATES_CHK, 0), IDM_CHECKUPDATES_CHK, FALSE);
			app.LocaleMenu (GetSubMenu (menu, 1), I18N (&app, IDS_LANGUAGE, 0), 5, TRUE);
			app.LocaleMenu (menu, I18N (&app, IDS_GETEXTERNALIP_CHK, 0), IDM_GETEXTERNALIP_CHK, FALSE);
			app.LocaleMenu (menu, I18N (&app, IDS_HELP, 0), 2, TRUE);
			app.LocaleMenu (menu, I18N (&app, IDS_WEBSITE, 0), IDM_WEBSITE, FALSE);
			app.LocaleMenu (menu, I18N (&app, IDS_DONATE, 0), IDM_DONATE, FALSE);
			app.LocaleMenu (menu, I18N (&app, IDS_CHECKUPDATES, 0), IDM_CHECKUPDATES, FALSE);
			app.LocaleMenu (menu, I18N (&app, IDS_ABOUT, 0), IDM_ABOUT, FALSE);

			DrawMenuBar (hwnd); // redraw menu

			SetDlgItemText (hwnd, IDC_REFRESH, I18N (&app, IDS_REFRESH, 0));

			// refresh list
			SendMessage (hwnd, WM_COMMAND, MAKEWPARAM (IDC_REFRESH, 0), 0);

			app.LocaleEnum ((HWND)GetSubMenu (menu, 1), 5, TRUE, IDM_DEFAULT); // enum localizations

			break;
		}

		case _RM_UNINITIALIZE:
		{
			break;
		}
	}

	return FALSE;
}

INT_PTR CALLBACK DlgProc (HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	switch (msg)
	{
		case WM_INITDIALOG:
		{
			app.SetHWND (hwnd);

			WSADATA wsa = {0};
			WSAStartup (MAKEWORD (2, 2), &wsa);

			// configure listview
			_r_listview_setstyle (hwnd, IDC_LISTVIEW, LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP | LVS_EX_LABELTIP);

			_r_listview_addcolumn (hwnd, IDC_LISTVIEW, nullptr, 95, 0, LVCFMT_LEFT);

			// configure menu
			CheckMenuItem (GetMenu (hwnd), IDM_ALWAYSONTOP_CHK, MF_BYCOMMAND | (app.ConfigGet (L"AlwaysOnTop", 0).AsBool () ? MF_CHECKED : MF_UNCHECKED));
			CheckMenuItem (GetMenu (hwnd), IDM_CHECKUPDATES_CHK, MF_BYCOMMAND | (app.ConfigGet (L"CheckUpdates", 1).AsBool () ? MF_CHECKED : MF_UNCHECKED));
			CheckMenuItem (GetMenu (hwnd), IDM_GETEXTERNALIP_CHK, MF_BYCOMMAND | (app.ConfigGet (L"GetExternalIp", 0).AsBool () ? MF_CHECKED : MF_UNCHECKED));

			_r_wnd_addstyle (hwnd, IDC_REFRESH, app.IsClassicUI () ? WS_EX_STATICEDGE : 0, WS_EX_STATICEDGE, GWL_EXSTYLE);

			break;
		}

		case WM_DESTROY:
		{
			WSACleanup ();
			PostQuitMessage (0);

			break;
		}

		case WM_QUERYENDSESSION:
		{
			SetWindowLongPtr (hwnd, DWLP_MSGRESULT, TRUE);
			return TRUE;
		}

		case WM_PAINT:
		{
			PAINTSTRUCT ps = {0};
			HDC dc = BeginPaint (hwnd, &ps);

			RECT rc = {0};
			GetClientRect (hwnd, &rc);

			static INT height = app.GetDPI (46);

			rc.top = rc.bottom - height;
			rc.bottom = rc.top + height;

			COLORREF clr_prev = SetBkColor (dc, GetSysColor (COLOR_3DFACE));
			ExtTextOut (dc, 0, 0, ETO_OPAQUE, &rc, nullptr, 0, nullptr);
			SetBkColor (dc, clr_prev);

			for (INT i = 0; i < rc.right; i++)
			{
				SetPixel (dc, i, rc.top, RGB (223, 223, 223));
			}

			EndPaint (hwnd, &ps);

			break;
		}

		case WM_CTLCOLORSTATIC:
		case WM_CTLCOLORDLG:
		{
			return (INT_PTR)GetSysColorBrush (COLOR_WINDOW);
		}

		case WM_CONTEXTMENU:
		{
			if (GetDlgCtrlID ((HWND)wparam) == IDC_LISTVIEW)
			{
				HMENU menu = LoadMenu (nullptr, MAKEINTRESOURCE (IDM_LISTVIEW)), submenu = GetSubMenu (menu, 0);

				// localize
				app.LocaleMenu (submenu, I18N (&app, IDS_COPY, 0), IDM_COPY, FALSE);

				if (!SendDlgItemMessage (hwnd, IDC_LISTVIEW, LVM_GETSELECTEDCOUNT, 0, 0))
				{
					EnableMenuItem (submenu, IDM_COPY, MF_BYCOMMAND | MF_DISABLED);
				}

				TrackPopupMenuEx (submenu, TPM_RIGHTBUTTON | TPM_LEFTBUTTON, LOWORD (lparam), HIWORD (lparam), hwnd, nullptr);

				DestroyMenu (menu);
				DestroyMenu (submenu);
			}

			break;
		}

		case WM_NOTIFY:
		{
			LPNMHDR nmlp = (LPNMHDR)lparam;

			switch (nmlp->code)
			{
				case LVN_DELETEALLITEMS:
				case LVN_INSERTITEM:
				case LVN_DELETEITEM:
				{
					_r_listview_resizeonecolumn (hwnd, IDC_LISTVIEW);
					break;
				}
			}

			break;
		}

		case WM_COMMAND:
		{
			if (HIWORD (wparam) == 0 && LOWORD (wparam) >= IDM_DEFAULT && LOWORD (wparam) <= IDM_DEFAULT + app.LocaleGetCount ())
			{
				app.LocaleApplyFromMenu (GetSubMenu (GetSubMenu (GetMenu (hwnd), 1), 5), LOWORD (wparam), IDM_DEFAULT);

				return FALSE;
			}

			switch (LOWORD (wparam))
			{
				case IDCANCEL: // process Esc key
				case IDM_EXIT:
				case IDC_EXIT:
				{
					DestroyWindow (hwnd);
					break;
				}

				case IDM_ALWAYSONTOP_CHK:
				{
					BOOL val = app.ConfigGet (L"AlwaysOnTop", 0).AsBool ();

					CheckMenuItem (GetMenu (hwnd), IDM_ALWAYSONTOP_CHK, MF_BYCOMMAND | (!val ? MF_CHECKED : MF_UNCHECKED));
					app.ConfigSet (L"AlwaysOnTop", !val);

					_r_wnd_top (hwnd, !val);

					break;
				}

				case IDM_CHECKUPDATES_CHK:
				{
					BOOL val = app.ConfigGet (L"CheckUpdates", 1).AsBool ();

					CheckMenuItem (GetMenu (hwnd), IDM_CHECKUPDATES_CHK, MF_BYCOMMAND | (!val ? MF_CHECKED : MF_UNCHECKED));
					app.ConfigSet (L"CheckUpdates", !val);

					break;
				}

				case IDM_GETEXTERNALIP_CHK:
				{
					BOOL val = app.ConfigGet (L"GetExternalIp", 0).AsBool ();

					CheckMenuItem (GetMenu (hwnd), IDM_GETEXTERNALIP_CHK, MF_BYCOMMAND | (!val ? MF_CHECKED : MF_UNCHECKED));
					app.ConfigSet (L"GetExternalIp", !val);

					SendMessage (hwnd, WM_COMMAND, MAKEWPARAM (IDC_REFRESH, 0), 0);

					break;
				}

				case IDC_REFRESH:
				{
					_beginthreadex (nullptr, 0, &_app_print, nullptr, 0, nullptr);
					break;
				}

				case IDM_WEBSITE:
				{
					ShellExecute (hwnd, nullptr, _APP_WEBSITE_URL, nullptr, nullptr, SW_SHOWDEFAULT);
					break;
				}

				case IDM_DONATE:
				{
					ShellExecute (hwnd, nullptr, _APP_DONATION_URL, nullptr, nullptr, SW_SHOWDEFAULT);
					break;
				}

				case IDM_CHECKUPDATES:
				{
					app.CheckForUpdates (FALSE);
					break;
				}

				case IDM_ABOUT:
				{
					app.CreateAboutWindow ();
					break;
				}

				case IDM_COPY:
				{
					rstring buffer;

					INT item = -1;

					while ((item = (INT)SendDlgItemMessage (hwnd, IDC_LISTVIEW, LVM_GETNEXTITEM, item, LVNI_SELECTED)) != -1)
					{
						buffer.Append (_r_listview_gettext (hwnd, IDC_LISTVIEW, item, 0));
						buffer.Append (L"\r\n");
					}

					if (!buffer.IsEmpty ())
					{
						buffer.Trim (L"\r\n");

						_r_clipboard_set (hwnd, buffer, buffer.GetLength ());
					}

					break;
				}
			}

			break;
		}
	}

	return FALSE;
}

INT APIENTRY wWinMain (HINSTANCE, HINSTANCE, LPWSTR, INT)
{
	if (app.CreateMainWindow (&DlgProc, &initializer_callback))
	{
		MSG msg = {0};

		while (GetMessage (&msg, nullptr, 0, 0) > 0)
		{
			if (!IsDialogMessage (app.GetHWND (), &msg))
			{
				TranslateMessage (&msg);
				DispatchMessage (&msg);
			}
		}
	}

	return ERROR_SUCCESS;
}
