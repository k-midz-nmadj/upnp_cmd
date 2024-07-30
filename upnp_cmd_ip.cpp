#include <windows.h>
#include <tchar.h>
#include <comutil.h>
#include <comip.h>
#include <upnp.h>

#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "comsuppw.lib")

#define UPNP_DEVICE_TYPE  _T("urn:schemas-upnp-org:device:")
#define UPNP_SERVICE_TYPE _T("urn:schemas-upnp-org:service:")

#define UPNP_ACTION_ADDPORT _T("AddPortMapping")
#define UPNP_ACTION_DELPORT _T("DeletePortMapping")
#define UPNP_ACTION_GETEXIP _T("GetExternalIPAddress")

#define _COM_PTR_T(TYPE) _com_ptr_t<_com_IIID<TYPE, &__uuidof(TYPE)>>

class CConsoleOutput
{
	HANDLE hConsoleOutput;
	
public:
	CConsoleOutput()
	{
		hConsoleOutput = ::GetStdHandle(STD_OUTPUT_HANDLE);
	}
	
	int Write(LPCTSTR pszOutput)
	{
		DWORD dwNumberOfCharsWrite = 0;
		
		return (!pszOutput || !pszOutput[0] ||
			::WriteConsole(
				hConsoleOutput,
				pszOutput,
				lstrlen(pszOutput),
				&dwNumberOfCharsWrite,
				NULL) ? dwNumberOfCharsWrite : -1);
	}
} g_ConOut;
#define CON_PUTS_1(s1)     (g_ConOut.Write(s1), g_ConOut.Write(_T("\n")))
#define CON_PUTS_2(s1, s2) (g_ConOut.Write(s1), g_ConOut.Write(s2), g_ConOut.Write(_T("\n")))
#define SYS_PAUSE(c) {if (lstrcmpi(c, _T("upnp_cmd"))) _tsystem(_T("pause"));}


HRESULT UPnP_getService(LPCTSTR pszServiceName, IUPnPService** pService)
{
	HRESULT hr;
	_bstr_t bstrServiceType = UPNP_SERVICE_TYPE;
	_COM_PTR_T(IUPnPDeviceFinder) pDeviceFinder;
	_COM_PTR_T(IUPnPDevices)      pFoundDevices;
	_COM_PTR_T(IEnumVARIANT)      pEnumDev;
	IUnknown* pUnk;
	
	if (!pszServiceName || !pService)
		return E_INVALIDARG;
	
	hr = pDeviceFinder.CreateInstance(CLSID_UPnPDeviceFinder, NULL, CLSCTX_INPROC_SERVER);
	if (FAILED(hr))
		return hr;
	
	bstrServiceType += pszServiceName;
	hr = pDeviceFinder->FindByType(bstrServiceType, 0, &pFoundDevices);
	if (FAILED(hr))
		return hr;
	
	hr = pFoundDevices->get__NewEnum(&pUnk);
	if (FAILED(hr))
		return hr;
	
	hr = pUnk->QueryInterface(IID_IEnumVARIANT, (void**)&pEnumDev);
	pUnk->Release();
	if (FAILED(hr))
		return hr;
	
	// Loop through each device in the collection
	hr = S_FALSE;
	for (_variant_t varCurDev; S_OK == pEnumDev->Next(1, &varCurDev, NULL); varCurDev.Clear())
	{
		IDispatch* pdispCurItem = V_DISPATCH(&varCurDev);
		_COM_PTR_T(IUPnPDevice)   pDevice;
		_COM_PTR_T(IUPnPServices) pServices;
		_COM_PTR_T(IEnumVARIANT)  pEnumSrv;
		
		hr = pdispCurItem->QueryInterface(IID_IUPnPDevice, (void**)&pDevice);
		if (FAILED(hr))
			continue;
		
		// Get the list of services available on the device
		hr = pDevice->get_Services(&pServices);
		if (FAILED(hr))
			continue;
		
		hr = pServices->get__NewEnum(&pUnk);
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
			BSTR bstrType = NULL;
			hr = (*pService)->get_ServiceTypeIdentifier(&bstrType);
			if (SUCCEEDED(hr))
			{
				if (VARCMP_EQ == VarBstrCmp(bstrType, bstrServiceType, LOCALE_SYSTEM_DEFAULT, NORM_IGNORECASE))
					return S_OK;
				
				hr = S_FALSE;
			}
			(*pService)->Release();
			*pService = NULL;
		}
	}
	return hr;
}

HRESULT UPnP_InvokeAction(IUPnPService* pService, LPCTSTR pszActName, LPCTSTR* params = NULL, ULONG num = 0)
{
	SAFEARRAY* psa = ::SafeArrayCreateVector(VT_VARIANT, 0, num);
	VARIANT* rgElems;
	HRESULT hr;
	
	if (!psa)
		return E_FAIL;
	
	hr = ::SafeArrayAccessData(psa, (void HUGEP**)&rgElems);
	if (SUCCEEDED(hr))
	{
		_bstr_t bstrActName = pszActName;
		_variant_t varInArgs, varOutArgs, varRet;
		ULONG i;
		
		CON_PUTS_2(_T("Action : "), pszActName);
		for (i = 0; i < num; i++)
		{
			V_VT(&rgElems[i]) = VT_BSTR;
			V_BSTR(&rgElems[i]) = ::SysAllocString(params[i]);
		}
		::SafeArrayUnaccessData(psa);
		
		V_VT(&varInArgs) = VT_VARIANT | VT_ARRAY;
		V_ARRAY(&varInArgs) = psa;
		
		hr = pService->InvokeAction(bstrActName, varInArgs, &varOutArgs, &varRet);
		if (FAILED(hr))
		{
			if (V_VT(&varRet) == VT_BSTR)
				CON_PUTS_2(_T("Failed : "), V_BSTR(&varRet));
		}
		else if (V_ISARRAY(&varOutArgs) && (psa = varOutArgs.parray)->cDims == 1)
		{
			hr = ::SafeArrayAccessData(psa, (void HUGEP**)&rgElems);
			if (SUCCEEDED(hr))
			{
				for (i = 0; i < psa->rgsabound[0].cElements; i++)
				{
					if (V_VT(&rgElems[i]) == VT_BSTR)
						CON_PUTS_1(V_BSTR(&rgElems[i]));
				}
				::SafeArrayUnaccessData(psa);
			}
		}
	}
	else
		::SafeArrayDestroy(psa);
	
	return hr;
}

int _tmain(int argc, TCHAR *argv[])
{
	if (argc <= 1 || argc > 7)
	{
		CON_PUTS_1(_T("================== \"upnp_cmd\" Parameter Definition for each Action ==================\n"));
		CON_PUTS_1(_T("GetExternalIPAddress : upnp_cmd type(WANIPConnection:1, WANPPPConnection:1, etc...)"));
		CON_PUTS_1(_T("AddPortMapping       : upnp_cmd type protocol port IPaddress [description] [duration]"));
		CON_PUTS_1(_T("DeletePortMapping    : upnp_cmd type protocol port\n"));
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
		LPCTSTR params[] = {
			_T(""),		// NewRemoteHost
			argv[3],    // NewExternalPort
			argv[2],    // NewProtocol
			argv[3],    // NewInternalPort
			argv[4],    // NewInternalClient
			_T("1"),    // NewEnabled
			_T(""),     // NewPortMappingDescription
			_T("0")     // NewLeaseDuration
		};
		
		switch (argc)	// ParameterCount=1/2/3~5
		{
		case 2:	// GetExternalIPAddress
		case 3:
			hr = UPnP_InvokeAction(pService, UPNP_ACTION_GETEXIP);
			break;
		case 4:	// DeletePortMapping
			hr = UPnP_InvokeAction(pService, UPNP_ACTION_DELPORT, params, 3);
			break;
		case 7:	// AddPortMapping
			params[7] = argv[6];
		case 6:
			params[6] = argv[5];
		case 5:
			hr = UPnP_InvokeAction(pService, UPNP_ACTION_ADDPORT, params, 8);
			break;
		default:
			CON_PUTS_1(_T("Invalid Parameter."));
			hr = E_FAIL;
		}
		pService->Release();
	}
	else
		CON_PUTS_1(_T("Not Found UPnP Service."));
	
	CON_PUTS_1(_T(""));
	if (FAILED(hr))
	{
		TCHAR   wszMsgBuff[512] = _T("");  // Buffer for text.
		
		// Try to get the message from the system errors.
		if (0 < ::FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		                        NULL,
		                        hr,
		                        0,
		                        wszMsgBuff,
		                        sizeof(wszMsgBuff) / sizeof(TCHAR),
		                        NULL))
			CON_PUTS_1(wszMsgBuff);
	}
	
	::CoUninitialize();
	SYS_PAUSE(argv[0]);
	
	return 0;
}
