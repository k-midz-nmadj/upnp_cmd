﻿#include <windows.h>
#include <tchar.h>
#include <comutil.h>
#include <upnp.h>

#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "comsuppw.lib")

#define UPNP_DEVICE_TYPE  _T("urn:schemas-upnp-org:device:")
#define UPNP_SERVICE_TYPE _T("urn:schemas-upnp-org:service:")

#define UPNP_ACTION_ADDPORT _T("AddPortMapping")
#define UPNP_ACTION_DELPORT _T("DeletePortMapping")
#define UPNP_ACTION_GETEXIP _T("GetExternalIPAddress")
#define UPNP_ACTION_GETPMAP _T("GetGenericPortMappingEntry")

class CConsoleOutput
{
	HANDLE hConsoleOutput;
	
public:
	CConsoleOutput()
	{
		hConsoleOutput = ::GetStdHandle(STD_OUTPUT_HANDLE);
	}
	
	int Write(LPCTSTR pszOutput, DWORD dwLength = 0)
	{
		DWORD dwNumberOfCharsWrite = 0;
		
		return (!pszOutput || !pszOutput[0] ||
				::WriteConsole(
					hConsoleOutput,
					pszOutput,
					dwLength > 0 ? dwLength : lstrlen(pszOutput),
					&dwNumberOfCharsWrite,
					NULL) ? 
				dwNumberOfCharsWrite : -1);
	}
} g_ConOut;

#define CON_PUTS(s)       g_ConOut.Write(s)
#define CON_PUTS_n(s, n)  g_ConOut.Write(s, n)
#define CON_PUTS_c(c)     CON_PUTS_n(_T(c), sizeof(_T(c)) / sizeof(TCHAR) - 1)
#define CON_PUTS_b(b)     CON_PUTS_n(b, ::SysStringLen(b))
#define CON_PUTS_cb(c, b) (CON_PUTS_c(c), CON_PUTS_b(b))
#define CON_PUTS_bc(b, c) (CON_PUTS_b(b), CON_PUTS_c(c))
#define CON_LF(...)       (__VA_ARGS__, CON_PUTS_c("\n"))
#define SYS_PAUSE(c) {if (lstrcmpi(c, _T("upnp_cmd"))) _tsystem(_T("pause"));}


HRESULT UPnP_getService(LPCTSTR pszServiceName, IUPnPService** pService)
{
	HRESULT hr;
	IUPnPDeviceFinder* pDeviceFinder;
	IUPnPDevices*      pFoundDevices;
	IEnumVARIANT*      pEnumDev;
	IUnknown* pUnk;
	
	if (!pszServiceName || !pService)
		return E_INVALIDARG;
	
	hr = ::CoCreateInstance(CLSID_UPnPDeviceFinder, NULL, CLSCTX_INPROC_SERVER,
	                        IID_IUPnPDeviceFinder, (void**)&pDeviceFinder);
	if (FAILED(hr))
		return hr;
	
	_bstr_t bstrServiceType = UPNP_SERVICE_TYPE;
	bstrServiceType += pszServiceName;
	
	hr = pDeviceFinder->FindByType(bstrServiceType, 0, &pFoundDevices);
	pDeviceFinder->Release();
	if (FAILED(hr))
		return hr;
	
	hr = pFoundDevices->get__NewEnum(&pUnk);
	pFoundDevices->Release();
	if (FAILED(hr))
		return hr;
	
	hr = pUnk->QueryInterface(IID_IEnumVARIANT, (void**)&pEnumDev);
	pUnk->Release();
	if (FAILED(hr))
		return hr;
	
	// Loop through each device in the collection
	hr = S_FALSE;
	for (_variant_t varCurDev; S_OK != hr && S_OK == pEnumDev->Next(1, &varCurDev, NULL); varCurDev.Clear())
	{
		IDispatch* pdispCurItem = V_DISPATCH(&varCurDev);
		IUPnPDevice*   pDevice;
		IUPnPServices* pServices;
		IEnumVARIANT*  pEnumSrv;
		
		hr = pdispCurItem->QueryInterface(IID_IUPnPDevice, (void**)&pDevice);
		if (FAILED(hr))
			continue;
		
		// Get the list of services available on the device
		hr = pDevice->get_Services(&pServices);
		pDevice->Release();
		if (FAILED(hr))
			continue;
		
		hr = pServices->get__NewEnum(&pUnk);
		pServices->Release();
		if (FAILED(hr))
			continue;
		
		hr = pUnk->QueryInterface(IID_IEnumVARIANT, (void**)&pEnumSrv);
		pUnk->Release();
		if (FAILED(hr))
			continue;
		
		// Loop through each service in the collection
		for (_variant_t varCurSrv; S_OK == pEnumSrv->Next(1, &varCurSrv, NULL); varCurSrv.Clear())
		{
			pdispCurItem = V_DISPATCH(&varCurSrv);
			hr = pdispCurItem->QueryInterface(IID_IUPnPService, (void**)pService);
			if (FAILED(hr))
				continue;
			
			// Do something interesting with pService
			_bstr_t bstrType;
			hr = (*pService)->get_ServiceTypeIdentifier(bstrType.GetAddress());
			if (SUCCEEDED(hr))
			{
				if (VARCMP_EQ == VarBstrCmp(bstrType, bstrServiceType, LOCALE_SYSTEM_DEFAULT, NORM_IGNORECASE))
					break;
				
				hr = S_FALSE;
			}
			(*pService)->Release();
			*pService = NULL;
		}
		pEnumSrv->Release();
	}
	pEnumDev->Release();
	
	return hr;
}

HRESULT UPnP_InvokeAction(IUPnPService* pService, LPCTSTR pszActName, LPCTSTR* params = NULL, ULONG num = 0)
{
	HRESULT hr;
	ULONG i;
	VARIANT* rgElems;
	SAFEARRAY* psa = ::SafeArrayCreateVector(VT_VARIANT, 0, num);
	if (!psa)
		return E_OUTOFMEMORY;
	
	_variant_t varInArgs, varOutArgs, varRet;
	V_VT(&varInArgs) = VT_VARIANT | VT_ARRAY;
	V_ARRAY(&varInArgs) = psa;
	
	if (num > 0)
	{
		hr = ::SafeArrayAccessData(psa, (void HUGEP**)&rgElems);
		if (FAILED(hr))
			return hr;
		
		for (i = 0; i < num; i++)
		{
			V_VT(&rgElems[i]) = VT_BSTR;
			V_BSTR(&rgElems[i]) = ::SysAllocString(params[i]);
		}
		::SafeArrayUnaccessData(psa);
	}
	
	_bstr_t bstrActName = pszActName;
	CON_LF(CON_PUTS_cb("Action : ", bstrActName));
	
	hr = pService->InvokeAction(bstrActName, varInArgs, &varOutArgs, &varRet);
	if (FAILED(hr))
	{
		if (V_VT(&varRet) == VT_BSTR)
			CON_LF(CON_PUTS_cb("Failed : ", V_BSTR(&varRet)));
	}
	else if (V_ISARRAY(&varOutArgs) && (psa = V_ARRAY(&varOutArgs))->rgsabound[0].cElements > 0)
	{
		hr = ::SafeArrayAccessData(psa, (void HUGEP**)&rgElems);
		if (FAILED(hr))
			return hr;
		
		for (i = 0; i < psa->rgsabound[0].cElements; i++)
		{
			if (V_VT(&rgElems[i]) == VT_BSTR)
				CON_LF(CON_PUTS_b(V_BSTR(&rgElems[i])));
		}
		::SafeArrayUnaccessData(psa);
	}
	return hr;
}

int _tmain(int argc, const TCHAR *argv[])
{
	if (argc < 2 || argc > 7)
	{
		CON_PUTS_c("================== \"upnp_cmd\" Parameter Definition for each Action ==================\n\n");
		CON_PUTS_c("GetExternalIPAddress : upnp_cmd type(WANIPConnection:1, WANPPPConnection:1, etc...)\n");
		CON_PUTS_c("GetPortMappingEntry  : upnp_cmd type index\n");
		CON_PUTS_c("AddPortMapping       : upnp_cmd type protocol port IPaddress [description] [duration]\n");
		CON_PUTS_c("DeletePortMapping    : upnp_cmd type protocol port\n\n");
		SYS_PAUSE(argv[0]);
		return 0;
	}
	
	HRESULT hr = ::CoInitialize(NULL);
	if (FAILED(hr))
		return -1;
	
	IUPnPService* pService = NULL;
	hr = UPnP_getService(argv[1], &pService);
	if (S_OK == hr && pService)
	{
		LPCTSTR params[] = {_T(""), NULL, argv[2], NULL, NULL, _T("1"), _T(""), _T("0")};
		
		switch (argc)	// ParameterCount=1/2/3~5
		{
		case 2:	// GetExternalIPAddress
			hr = UPnP_InvokeAction(pService, UPNP_ACTION_GETEXIP);
			break;
		case 3:	// GetGenericPortMappingEntry
			hr = UPnP_InvokeAction(pService, UPNP_ACTION_GETPMAP, &argv[2], 1);
			break;
		case 4:	// DeletePortMapping
			params[1] = argv[3];	// NewExternalPort
			hr = UPnP_InvokeAction(pService, UPNP_ACTION_DELPORT, params, 3);
			break;
		case 7:	// AddPortMapping
			params[7] = argv[6];	// NewLeaseDuration
		case 6:
			params[6] = argv[5];	// NewPortMappingDescription
		case 5:
			params[1] = params[3] = argv[3];	// NewExternalPort,NewInternalPort
			params[4] = argv[4];	// NewInternalClient
			hr = UPnP_InvokeAction(pService, UPNP_ACTION_ADDPORT, params, 8);
			break;
		}
		pService->Release();
	}
	else
		CON_PUTS_c("Not Found UPnP Service.\n");
	
	CON_LF(0);
	if (FAILED(hr))
	{
		TCHAR szMsgBuff[512] = _T("");  // Buffer for text.
		DWORD dwLength = ::FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		                                 NULL,
		                                 hr,
		                                 0,
		                                 szMsgBuff,
		                                 sizeof(szMsgBuff) / sizeof(TCHAR),
		                                 NULL);	// Try to get the message from the system errors.
		if (dwLength > 0)
			CON_LF(CON_PUTS_n(szMsgBuff, dwLength));
	}
	
	::CoUninitialize();
	SYS_PAUSE(argv[0]);
	
	return 0;
}
