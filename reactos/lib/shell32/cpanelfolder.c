/*
 * Control panel folder
 *
 * Copyright 2003 Martin Fuchs
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or(at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "config.h"
#include "wine/port.h"

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

#define COBJMACROS
#define NONAMELESSUNION
#define NONAMELESSSTRUCT

#include "winerror.h"
#include "windef.h"
#include "winbase.h"
#include "winreg.h"
#include "wingdi.h"
#include "winuser.h"

#include "ole2.h"
#include "shlguid.h"

#include "cpanel.h"
#include "enumidlist.h"
#include "pidl.h"
#include "undocshell.h"
#include "shell32_main.h"
#include "shresdef.h"
#include "shlwapi.h"
#include "wine/debug.h"
#include "debughlp.h"
#include "shfldr.h"

WINE_DEFAULT_DEBUG_CHANNEL(shell);

/***********************************************************************
*   control panel implementation in shell namespace
*/

typedef struct {
    IShellFolder2Vtbl      *lpVtbl;
    DWORD                   ref;
    IPersistFolder2Vtbl    *lpVtblPersistFolder2;
    IShellExecuteHookWVtbl *lpVtblShellExecuteHookW;
    IShellExecuteHookAVtbl *lpVtblShellExecuteHookA;

    IUnknown *pUnkOuter;	/* used for aggregation */

    /* both paths are parsible from the desktop */
    LPITEMIDLIST pidlRoot;	/* absolute pidl */
    int dwAttributes;		/* attributes returned by GetAttributesOf FIXME: use it */
} ICPanelImpl;

static IShellFolder2Vtbl vt_ShellFolder2;
static IPersistFolder2Vtbl vt_PersistFolder2;
static IShellExecuteHookWVtbl vt_ShellExecuteHookW;
static IShellExecuteHookAVtbl vt_ShellExecuteHookA;

#define _IPersistFolder2_Offset	    ((int)(&(((ICPanelImpl*)0)->lpVtblPersistFolder2)))
#define _ICOM_THIS_From_IPersistFolder2(class, name) class* This = (class*)(((char*)name)-_IPersistFolder2_Offset);

#define IShellExecuteHookW_Offset   ((int)(&(((ICPanelImpl*)0)->lpVtblShellExecuteHookW)))
#define _ICOM_THIS_From_IShellExecuteHookW(class, name) class* This = (class*)(((char*)name)-IShellExecuteHookW_Offset);

#define IShellExecuteHookA_Offset   ((int)(&(((ICPanelImpl*)0)->lpVtblShellExecuteHookA)))
#define _ICOM_THIS_From_IShellExecuteHookA(class, name) class* This = (class*)(((char*)name)-IShellExecuteHookA_Offset);


/*
  converts This to a interface pointer
*/
#define _IUnknown_(This)	   (IUnknown*)&(This->lpVtbl)
#define _IShellFolder_(This)	   (IShellFolder*)&(This->lpVtbl)
#define _IShellFolder2_(This)	   (IShellFolder2*)&(This->lpVtbl)

#define _IPersist_(This)	   (IPersist*)&(This->lpVtblPersistFolder2)
#define _IPersistFolder_(This)	   (IPersistFolder*)&(This->lpVtblPersistFolder2)
#define _IPersistFolder2_(This)	   (IPersistFolder2*)&(This->lpVtblPersistFolder2)
#define _IShellExecuteHookW_(This) (IShellExecuteHookW*)&(This->lpVtblShellExecuteHookW)
#define _IShellExecuteHookA_(This) (IShellExecuteHookA*)&(This->lpVtblShellExecuteHookA)

/***********************************************************************
*   IShellFolder [ControlPanel] implementation
*/

static shvheader ControlPanelSFHeader[] = {
    {IDS_SHV_COLUMN8, SHCOLSTATE_TYPE_STR | SHCOLSTATE_ONBYDEFAULT, LVCFMT_RIGHT, 15},/*FIXME*/
    {IDS_SHV_COLUMN9, SHCOLSTATE_TYPE_STR | SHCOLSTATE_ONBYDEFAULT, LVCFMT_RIGHT, 200},/*FIXME*/
};

#define CONROLPANELSHELLVIEWCOLUMNS 2

/**************************************************************************
*	IControlPanel_Constructor
*/
HRESULT WINAPI IControlPanel_Constructor(IUnknown* pUnkOuter, REFIID riid, LPVOID * ppv)
{
    ICPanelImpl *sf;

    TRACE("unkOut=%p %s\n", pUnkOuter, shdebugstr_guid(riid));

    if (!ppv)
	return E_POINTER;
    if (pUnkOuter && !IsEqualIID (riid, &IID_IUnknown))
	return CLASS_E_NOAGGREGATION;

    sf = (ICPanelImpl *) LocalAlloc(GMEM_ZEROINIT, sizeof(ICPanelImpl));
    if (!sf)
	return E_OUTOFMEMORY;

    sf->ref = 0;
    sf->lpVtbl = &vt_ShellFolder2;
    sf->lpVtblPersistFolder2 = &vt_PersistFolder2;
    sf->lpVtblShellExecuteHookW = &vt_ShellExecuteHookW;
    sf->lpVtblShellExecuteHookA = &vt_ShellExecuteHookA;
    sf->pidlRoot = _ILCreateControlPanel();	/* my qualified pidl */
    sf->pUnkOuter = pUnkOuter ? pUnkOuter : _IUnknown_ (sf);

    if (!SUCCEEDED(IUnknown_QueryInterface(_IUnknown_(sf), riid, ppv))) {
	IUnknown_Release(_IUnknown_(sf));
	return E_NOINTERFACE;
    }

    TRACE("--(%p)\n", sf);
    return S_OK;
}

/**************************************************************************
 *	ISF_ControlPanel_fnQueryInterface
 *
 * NOTES supports not IPersist/IPersistFolder
 */
static HRESULT WINAPI ISF_ControlPanel_fnQueryInterface(IShellFolder2 * iface, REFIID riid, LPVOID * ppvObject)
{
    ICPanelImpl *This = (ICPanelImpl *)iface;

    TRACE("(%p)->(%s,%p)\n", This, shdebugstr_guid(riid), ppvObject);

    *ppvObject = NULL;

    if (IsEqualIID(riid, &IID_IUnknown) ||
	IsEqualIID(riid, &IID_IShellFolder) || IsEqualIID(riid, &IID_IShellFolder2))
	*ppvObject = This;
    else if (IsEqualIID(riid, &IID_IPersist) ||
	       IsEqualIID(riid, &IID_IPersistFolder) || IsEqualIID(riid, &IID_IPersistFolder2))
	*ppvObject = _IPersistFolder2_(This);
    else if (IsEqualIID(riid, &IID_IShellExecuteHookW))
	*ppvObject = _IShellExecuteHookW_(This);
    else if (IsEqualIID(riid, &IID_IShellExecuteHookA))
	*ppvObject = _IShellExecuteHookA_(This);

    if (*ppvObject) {
	IUnknown_AddRef((IUnknown *)(*ppvObject));
	TRACE("-- Interface:(%p)->(%p)\n", ppvObject, *ppvObject);
	return S_OK;
    }
    TRACE("-- Interface: E_NOINTERFACE\n");
    return E_NOINTERFACE;
}

static ULONG WINAPI ISF_ControlPanel_fnAddRef(IShellFolder2 * iface)
{
    ICPanelImpl *This = (ICPanelImpl *)iface;

    TRACE("(%p)->(count=%lu)\n", This, This->ref);

    return ++(This->ref);
}

static ULONG WINAPI ISF_ControlPanel_fnRelease(IShellFolder2 * iface)
{
    ICPanelImpl *This = (ICPanelImpl *)iface;

    TRACE("(%p)->(count=%lu)\n", This, This->ref);

    if (!--(This->ref)) {
        TRACE("-- destroying IShellFolder(%p)\n", This);
        if (This->pidlRoot)
            SHFree(This->pidlRoot);
        LocalFree((HLOCAL) This);
        return 0;
    }
    return This->ref;
}

/**************************************************************************
*	ISF_ControlPanel_fnParseDisplayName
*/
static HRESULT WINAPI
ISF_ControlPanel_fnParseDisplayName(IShellFolder2 * iface,
				   HWND hwndOwner,
				   LPBC pbc,
				   LPOLESTR lpszDisplayName,
				   DWORD * pchEaten, LPITEMIDLIST * ppidl, DWORD * pdwAttributes)
{
    ICPanelImpl *This = (ICPanelImpl *)iface;

    HRESULT hr = E_INVALIDARG;

    FIXME("(%p)->(HWND=%p,%p,%p=%s,%p,pidl=%p,%p)\n",
	   This, hwndOwner, pbc, lpszDisplayName, debugstr_w(lpszDisplayName), pchEaten, ppidl, pdwAttributes);

    *ppidl = 0;
    if (pchEaten)
	*pchEaten = 0;

    TRACE("(%p)->(-- ret=0x%08lx)\n", This, hr);

    return hr;
}

static LPITEMIDLIST _ILCreateCPanelApplet(LPCSTR name, LPCSTR displayName,
 LPCSTR comment, int iconIdx)
{
    PIDLCPanelStruct *p;
    LPITEMIDLIST pidl;
    PIDLDATA tmp;
    int size0 = (char*)&tmp.u.cpanel.szName-(char*)&tmp.u.cpanel;
    int size = size0;
    int l;

    tmp.type = PT_CPLAPPLET;
    tmp.u.cpanel.dummy = 0;
    tmp.u.cpanel.iconIdx = iconIdx;

    l = strlen(name);
    size += l+1;

    tmp.u.cpanel.offsDispName = l+1;
    l = strlen(displayName);
    size += l+1;

    tmp.u.cpanel.offsComment = tmp.u.cpanel.offsDispName+1+l;
    l = strlen(comment);
    size += l+1;

    pidl = SHAlloc(size+4);
    if (!pidl)
        return NULL;

    pidl->mkid.cb = size+2;
    memcpy(pidl->mkid.abID, &tmp, 2+size0);

    p = &((PIDLDATA*)pidl->mkid.abID)->u.cpanel;
    strcpy(p->szName, name);
    strcpy(p->szName+tmp.u.cpanel.offsDispName, displayName);
    strcpy(p->szName+tmp.u.cpanel.offsComment, comment);

    *(WORD*)((char*)pidl+(size+2)) = 0;

    pcheck(pidl);

    return pidl;
}

/**************************************************************************
 *  _ILGetCPanelPointer()
 * gets a pointer to the control panel struct stored in the pidl
 */
static PIDLCPanelStruct* _ILGetCPanelPointer(LPCITEMIDLIST pidl)
{
    LPPIDLDATA pdata = _ILGetDataPointer(pidl);

    if (pdata && pdata->type==PT_CPLAPPLET)
        return (PIDLCPanelStruct*)&(pdata->u.cpanel);

    return NULL;
}

 /**************************************************************************
 *		ISF_ControlPanel_fnEnumObjects
 */
static BOOL SHELL_RegisterCPanelApp(IEnumIDList* list, LPCSTR path)
{
    LPITEMIDLIST pidl;
    CPlApplet* applet;
    CPanel panel;
    CPLINFO info;
    unsigned i;
    int iconIdx;

    char displayName[MAX_PATH];
    char comment[MAX_PATH];

    WCHAR wpath[MAX_PATH];

    MultiByteToWideChar(CP_ACP, 0, path, -1, wpath, MAX_PATH);

    panel.first = NULL;
    applet = Control_LoadApplet(0, wpath, &panel);

    if (applet)
    {
        for(i=0; i<applet->count; ++i)
        {
            WideCharToMultiByte(CP_ACP, 0, applet->info[i].szName, -1, displayName, MAX_PATH, 0, 0);
            WideCharToMultiByte(CP_ACP, 0, applet->info[i].szInfo, -1, comment, MAX_PATH, 0, 0);

            applet->proc(0, CPL_INQUIRE, i, (LPARAM)&info);

            if (info.idIcon > 0)
                iconIdx = -info.idIcon; /* negative icon index instead of icon number */
            else
                iconIdx = 0;

            pidl = _ILCreateCPanelApplet(path, displayName, comment, iconIdx);

            if (pidl)
                AddToEnumList(list, pidl);
        }
        Control_UnloadApplet(applet);
    }
    return TRUE;
}

static int SHELL_RegisterRegistryCPanelApps(IEnumIDList* list, HKEY hkey_root, LPCSTR szRepPath)
{
    char name[MAX_PATH];
    char value[MAX_PATH];
    HKEY hkey;

    int cnt = 0;

    if (RegOpenKeyA(hkey_root, szRepPath, &hkey) == ERROR_SUCCESS)
    {
        int idx = 0;

        for(;; ++idx)
        {
            DWORD nameLen = MAX_PATH;
            DWORD valueLen = MAX_PATH;

            if (RegEnumValueA(hkey, idx, name, &nameLen, NULL, NULL, (LPBYTE)&value, &valueLen) != ERROR_SUCCESS)
                break;

            if (SHELL_RegisterCPanelApp(list, value))
                ++cnt;
        }
        RegCloseKey(hkey);
    }

    return cnt;
}

static int SHELL_RegisterCPanelFolders(IEnumIDList* list, HKEY hkey_root, LPCSTR szRepPath)
{
    char name[MAX_PATH];
    HKEY hkey;

    int cnt = 0;

    if (RegOpenKeyA(hkey_root, szRepPath, &hkey) == ERROR_SUCCESS)
    {
        int idx = 0;
        for(;; ++idx)
        {
            if (RegEnumKeyA(hkey, idx, name, MAX_PATH) != ERROR_SUCCESS)
                break;

            if (*name == '{')
            {
                LPITEMIDLIST pidl = _ILCreateGuidFromStrA(name);

                if (pidl && AddToEnumList(list, pidl))
                    ++cnt;
            }
        }

        RegCloseKey(hkey);
    }

  return cnt;
}

/**************************************************************************
 *  CreateCPanelEnumList()
 */
static BOOL CreateCPanelEnumList(
    IEnumIDList * iface,
    DWORD dwFlags)
{
    CHAR szPath[MAX_PATH];
    WIN32_FIND_DATAA wfd;
    HANDLE hFile;

    TRACE("(%p)->(flags=0x%08lx) \n",iface,dwFlags);

    /* enumerate control panel folders folders */
    if (dwFlags & SHCONTF_FOLDERS)
        SHELL_RegisterCPanelFolders(iface, HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\ControlPanel\\NameSpace");

    /* enumerate the control panel applets */
    if (dwFlags & SHCONTF_NONFOLDERS)
    {
        LPSTR p;

        GetSystemDirectoryA(szPath, MAX_PATH);
        p = PathAddBackslashA(szPath);
        strcpy(p, "*.cpl");

        TRACE("-- (%p)-> enumerate SHCONTF_NONFOLDERS of %s\n",iface,debugstr_a(szPath));
        hFile = FindFirstFileA(szPath, &wfd);

        if (hFile != INVALID_HANDLE_VALUE)
        {
            do
            {
                if (!(dwFlags & SHCONTF_INCLUDEHIDDEN) && (wfd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN))
                    continue;

                if (!(wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                    strcpy(p, wfd.cFileName);
                    SHELL_RegisterCPanelApp((IEnumIDList*)iface, szPath);
                }
            } while(FindNextFileA(hFile, &wfd));
            FindClose(hFile);
        }

        SHELL_RegisterRegistryCPanelApps((IEnumIDList*)iface, HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Control Panel\\Cpls");
        SHELL_RegisterRegistryCPanelApps((IEnumIDList*)iface, HKEY_CURRENT_USER, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Control Panel\\Cpls");
    }
    return TRUE;
}

/**************************************************************************
*		ISF_ControlPanel_fnEnumObjects
*/
static HRESULT WINAPI
ISF_ControlPanel_fnEnumObjects(IShellFolder2 * iface, HWND hwndOwner, DWORD dwFlags, LPENUMIDLIST * ppEnumIDList)
{
    ICPanelImpl *This = (ICPanelImpl *)iface;

    TRACE("(%p)->(HWND=%p flags=0x%08lx pplist=%p)\n", This, hwndOwner, dwFlags, ppEnumIDList);

    *ppEnumIDList = IEnumIDList_Constructor();
    if (*ppEnumIDList)
        CreateCPanelEnumList(*ppEnumIDList, dwFlags);

    TRACE("--(%p)->(new ID List: %p)\n", This, *ppEnumIDList);

    return(*ppEnumIDList) ? S_OK : E_OUTOFMEMORY;
}

/**************************************************************************
*		ISF_ControlPanel_fnBindToObject
*/
static HRESULT WINAPI
ISF_ControlPanel_fnBindToObject(IShellFolder2 * iface, LPCITEMIDLIST pidl,
			       LPBC pbcReserved, REFIID riid, LPVOID * ppvOut)
{
    ICPanelImpl *This = (ICPanelImpl *)iface;

    TRACE("(%p)->(pidl=%p,%p,%s,%p)\n", This, pidl, pbcReserved, shdebugstr_guid(riid), ppvOut);

    return SHELL32_BindToChild(This->pidlRoot, NULL, pidl, riid, ppvOut);
}

/**************************************************************************
*	ISF_ControlPanel_fnBindToStorage
*/
static HRESULT WINAPI
ISF_ControlPanel_fnBindToStorage(IShellFolder2 * iface,
				LPCITEMIDLIST pidl, LPBC pbcReserved, REFIID riid, LPVOID * ppvOut)
{
    ICPanelImpl *This = (ICPanelImpl *)iface;

    FIXME("(%p)->(pidl=%p,%p,%s,%p) stub\n", This, pidl, pbcReserved, shdebugstr_guid(riid), ppvOut);

    *ppvOut = NULL;
    return E_NOTIMPL;
}

/**************************************************************************
* 	ISF_ControlPanel_fnCompareIDs
*/

static HRESULT WINAPI
ISF_ControlPanel_fnCompareIDs(IShellFolder2 * iface, LPARAM lParam, LPCITEMIDLIST pidl1, LPCITEMIDLIST pidl2)
{
    ICPanelImpl *This = (ICPanelImpl *)iface;

    int nReturn;

    TRACE("(%p)->(0x%08lx,pidl1=%p,pidl2=%p)\n", This, lParam, pidl1, pidl2);
    nReturn = SHELL32_CompareIDs(_IShellFolder_(This), lParam, pidl1, pidl2);
    TRACE("-- %i\n", nReturn);
    return nReturn;
}

/**************************************************************************
*	ISF_ControlPanel_fnCreateViewObject
*/
static HRESULT WINAPI
ISF_ControlPanel_fnCreateViewObject(IShellFolder2 * iface, HWND hwndOwner, REFIID riid, LPVOID * ppvOut)
{
    ICPanelImpl *This = (ICPanelImpl *)iface;

    LPSHELLVIEW pShellView;
    HRESULT hr = E_INVALIDARG;

    TRACE("(%p)->(hwnd=%p,%s,%p)\n", This, hwndOwner, shdebugstr_guid(riid), ppvOut);

    if (ppvOut) {
	*ppvOut = NULL;

	if (IsEqualIID(riid, &IID_IDropTarget)) {
	    WARN("IDropTarget not implemented\n");
	    hr = E_NOTIMPL;
	} else if (IsEqualIID(riid, &IID_IContextMenu)) {
	    WARN("IContextMenu not implemented\n");
	    hr = E_NOTIMPL;
	} else if (IsEqualIID(riid, &IID_IShellView)) {
	    pShellView = IShellView_Constructor((IShellFolder *) iface);
	    if (pShellView) {
		hr = IShellView_QueryInterface(pShellView, riid, ppvOut);
		IShellView_Release(pShellView);
	    }
	}
    }
    TRACE("--(%p)->(interface=%p)\n", This, ppvOut);
    return hr;
}

/**************************************************************************
*  ISF_ControlPanel_fnGetAttributesOf
*/
static HRESULT WINAPI
ISF_ControlPanel_fnGetAttributesOf(IShellFolder2 * iface, UINT cidl, LPCITEMIDLIST * apidl, DWORD * rgfInOut)
{
    ICPanelImpl *This = (ICPanelImpl *)iface;

    HRESULT hr = S_OK;

    TRACE("(%p)->(cidl=%d apidl=%p mask=0x%08lx)\n", This, cidl, apidl, *rgfInOut);

    if ((!cidl) ||(!apidl) ||(!rgfInOut))
	return E_INVALIDARG;

    if (*rgfInOut == 0)
	*rgfInOut = ~0;

    while(cidl > 0 && *apidl) {
	pdump(*apidl);
	SHELL32_GetItemAttributes(_IShellFolder_(This), *apidl, rgfInOut);
	apidl++;
	cidl--;
    }

    TRACE("-- result=0x%08lx\n", *rgfInOut);
    return hr;
}

/**************************************************************************
*	ISF_ControlPanel_fnGetUIObjectOf
*
* PARAMETERS
*  HWND           hwndOwner, //[in ] Parent window for any output
*  UINT           cidl,      //[in ] array size
*  LPCITEMIDLIST* apidl,     //[in ] simple pidl array
*  REFIID         riid,      //[in ] Requested Interface
*  UINT*          prgfInOut, //[   ] reserved
*  LPVOID*        ppvObject) //[out] Resulting Interface
*
*/
static HRESULT WINAPI
ISF_ControlPanel_fnGetUIObjectOf(IShellFolder2 * iface,
				HWND hwndOwner,
				UINT cidl, LPCITEMIDLIST * apidl, REFIID riid, UINT * prgfInOut, LPVOID * ppvOut)
{
    ICPanelImpl *This = (ICPanelImpl *)iface;

    LPITEMIDLIST pidl;
    IUnknown *pObj = NULL;
    HRESULT hr = E_INVALIDARG;

    TRACE("(%p)->(%p,%u,apidl=%p,%s,%p,%p)\n",
	   This, hwndOwner, cidl, apidl, shdebugstr_guid(riid), prgfInOut, ppvOut);

    if (ppvOut) {
	*ppvOut = NULL;

	if (IsEqualIID(riid, &IID_IContextMenu) &&(cidl >= 1)) {
	    pObj = (LPUNKNOWN) ISvItemCm_Constructor((IShellFolder *) iface, This->pidlRoot, apidl, cidl);
	    hr = S_OK;
	} else if (IsEqualIID(riid, &IID_IDataObject) &&(cidl >= 1)) {
	    pObj = (LPUNKNOWN) IDataObject_Constructor(hwndOwner, This->pidlRoot, apidl, cidl);
	    hr = S_OK;
	} else if (IsEqualIID(riid, &IID_IExtractIconA) &&(cidl == 1)) {
	    pidl = ILCombine(This->pidlRoot, apidl[0]);
	    pObj = (LPUNKNOWN) IExtractIconA_Constructor(pidl);
	    SHFree(pidl);
	    hr = S_OK;
	} else if (IsEqualIID(riid, &IID_IExtractIconW) &&(cidl == 1)) {
	    pidl = ILCombine(This->pidlRoot, apidl[0]);
	    pObj = (LPUNKNOWN) IExtractIconW_Constructor(pidl);
	    SHFree(pidl);
	    hr = S_OK;
	} else if ((IsEqualIID(riid,&IID_IShellLinkW) || IsEqualIID(riid,&IID_IShellLinkA))
				&& (cidl == 1)) {
	    pidl = ILCombine(This->pidlRoot, apidl[0]);
	    hr = IShellLink_ConstructFromFile(NULL, riid, pidl,(LPVOID*)&pObj);
	    SHFree(pidl);
	} else {
	    hr = E_NOINTERFACE;
	}

	if (SUCCEEDED(hr) && !pObj)
	    hr = E_OUTOFMEMORY;

	*ppvOut = pObj;
    }
    TRACE("(%p)->hr=0x%08lx\n", This, hr);
    return hr;
}

/**************************************************************************
*	ISF_ControlPanel_fnGetDisplayNameOf
*/
static HRESULT WINAPI ISF_ControlPanel_fnGetDisplayNameOf(IShellFolder2 * iface, LPCITEMIDLIST pidl, DWORD dwFlags, LPSTRRET strRet)
{
    ICPanelImpl *This = (ICPanelImpl *)iface;

    CHAR szPath[MAX_PATH*2];
    PIDLCPanelStruct* pcpanel;

    *szPath = '\0';

    TRACE("(%p)->(pidl=%p,0x%08lx,%p)\n", This, pidl, dwFlags, strRet);
    pdump(pidl);

    if (!pidl || !strRet)
	return E_INVALIDARG;

    pcpanel = _ILGetCPanelPointer(pidl);

    if (pcpanel) {
	lstrcpyA(szPath, pcpanel->szName+pcpanel->offsDispName);

	if (!(dwFlags & SHGDN_FORPARSING))
	    FIXME("retrieve display name from control panel app\n");
    }
    /* take names of special folders only if its only this folder */
    else if (_ILIsSpecialFolder(pidl)) {
	BOOL bSimplePidl = _ILIsPidlSimple(pidl);

	if (bSimplePidl) {
	    _ILSimpleGetText(pidl, szPath, MAX_PATH);	/* append my own path */
	} else {
	    FIXME("special pidl\n");
	}

	if ((dwFlags & SHGDN_FORPARSING) && !bSimplePidl) { /* go deeper if needed */
	    int len = 0;

	    PathAddBackslashA(szPath); /*FIXME*/
	    len = lstrlenA(szPath);

	    if (!SUCCEEDED
	      (SHELL32_GetDisplayNameOfChild(iface, pidl, dwFlags | SHGDN_INFOLDER, szPath + len, MAX_PATH - len)))
		return E_OUTOFMEMORY;
	}
    }

    strRet->uType = STRRET_CSTR;
    lstrcpynA(strRet->u.cStr, szPath, MAX_PATH);

    TRACE("--(%p)->(%s)\n", This, szPath);
    return S_OK;
}

/**************************************************************************
*  ISF_ControlPanel_fnSetNameOf
*  Changes the name of a file object or subfolder, possibly changing its item
*  identifier in the process.
*
* PARAMETERS
*  HWND          hwndOwner,  //[in ] Owner window for output
*  LPCITEMIDLIST pidl,       //[in ] simple pidl of item to change
*  LPCOLESTR     lpszName,   //[in ] the items new display name
*  DWORD         dwFlags,    //[in ] SHGNO formatting flags
*  LPITEMIDLIST* ppidlOut)   //[out] simple pidl returned
*/
static HRESULT WINAPI ISF_ControlPanel_fnSetNameOf(IShellFolder2 * iface, HWND hwndOwner, LPCITEMIDLIST pidl,	/*simple pidl */
						  LPCOLESTR lpName, DWORD dwFlags, LPITEMIDLIST * pPidlOut)
{
    ICPanelImpl *This = (ICPanelImpl *)iface;
    FIXME("(%p)->(%p,pidl=%p,%s,%lu,%p)\n", This, hwndOwner, pidl, debugstr_w(lpName), dwFlags, pPidlOut);
    return E_FAIL;
}

static HRESULT WINAPI ISF_ControlPanel_fnGetDefaultSearchGUID(IShellFolder2 * iface, GUID * pguid)
{
    ICPanelImpl *This = (ICPanelImpl *)iface;
    FIXME("(%p)\n", This);
    return E_NOTIMPL;
}
static HRESULT WINAPI ISF_ControlPanel_fnEnumSearches(IShellFolder2 * iface, IEnumExtraSearch ** ppenum)
{
    ICPanelImpl *This = (ICPanelImpl *)iface;
    FIXME("(%p)\n", This);
    return E_NOTIMPL;
}
static HRESULT WINAPI ISF_ControlPanel_fnGetDefaultColumn(IShellFolder2 * iface, DWORD dwRes, ULONG * pSort, ULONG * pDisplay)
{
    ICPanelImpl *This = (ICPanelImpl *)iface;

    TRACE("(%p)\n", This);

    if (pSort) *pSort = 0;
    if (pDisplay) *pDisplay = 0;
    return S_OK;
}
static HRESULT WINAPI ISF_ControlPanel_fnGetDefaultColumnState(IShellFolder2 * iface, UINT iColumn, DWORD * pcsFlags)
{
    ICPanelImpl *This = (ICPanelImpl *)iface;

    TRACE("(%p)\n", This);

    if (!pcsFlags || iColumn >= CONROLPANELSHELLVIEWCOLUMNS) return E_INVALIDARG;
    *pcsFlags = ControlPanelSFHeader[iColumn].pcsFlags;
    return S_OK;
}
static HRESULT WINAPI ISF_ControlPanel_fnGetDetailsEx(IShellFolder2 * iface, LPCITEMIDLIST pidl, const SHCOLUMNID * pscid, VARIANT * pv)
{
    ICPanelImpl *This = (ICPanelImpl *)iface;
    FIXME("(%p)\n", This);
    return E_NOTIMPL;
}

static HRESULT WINAPI ISF_ControlPanel_fnGetDetailsOf(IShellFolder2 * iface, LPCITEMIDLIST pidl, UINT iColumn, SHELLDETAILS * psd)
{
    ICPanelImpl *This = (ICPanelImpl *)iface;
    HRESULT hr;

    TRACE("(%p)->(%p %i %p)\n", This, pidl, iColumn, psd);

    if (!psd || iColumn >= CONROLPANELSHELLVIEWCOLUMNS)
	return E_INVALIDARG;

    if (!pidl) {
	psd->fmt = ControlPanelSFHeader[iColumn].fmt;
	psd->cxChar = ControlPanelSFHeader[iColumn].cxChar;
	psd->str.uType = STRRET_CSTR;
	LoadStringA(shell32_hInstance, ControlPanelSFHeader[iColumn].colnameid, psd->str.u.cStr, MAX_PATH);
	return S_OK;
    } else {
	psd->str.u.cStr[0] = 0x00;
	psd->str.uType = STRRET_CSTR;
	switch(iColumn) {
	case 0:		/* name */
	    hr = IShellFolder_GetDisplayNameOf(iface, pidl, SHGDN_NORMAL | SHGDN_INFOLDER, &psd->str);
	    break;
	case 1:		/* comment */
	    _ILGetFileType(pidl, psd->str.u.cStr, MAX_PATH);
	    break;
	}
	hr = S_OK;
    }

    return hr;
}
static HRESULT WINAPI ISF_ControlPanel_fnMapColumnToSCID(IShellFolder2 * iface, UINT column, SHCOLUMNID * pscid)
{
    ICPanelImpl *This = (ICPanelImpl *)iface;
    FIXME("(%p)\n", This);
    return E_NOTIMPL;
}

static IShellFolder2Vtbl vt_ShellFolder2 =
{

    ISF_ControlPanel_fnQueryInterface,
    ISF_ControlPanel_fnAddRef,
    ISF_ControlPanel_fnRelease,
    ISF_ControlPanel_fnParseDisplayName,
    ISF_ControlPanel_fnEnumObjects,
    ISF_ControlPanel_fnBindToObject,
    ISF_ControlPanel_fnBindToStorage,
    ISF_ControlPanel_fnCompareIDs,
    ISF_ControlPanel_fnCreateViewObject,
    ISF_ControlPanel_fnGetAttributesOf,
    ISF_ControlPanel_fnGetUIObjectOf,
    ISF_ControlPanel_fnGetDisplayNameOf,
    ISF_ControlPanel_fnSetNameOf,

    /* ShellFolder2 */
    ISF_ControlPanel_fnGetDefaultSearchGUID,
    ISF_ControlPanel_fnEnumSearches,
    ISF_ControlPanel_fnGetDefaultColumn,
    ISF_ControlPanel_fnGetDefaultColumnState,
    ISF_ControlPanel_fnGetDetailsEx,
    ISF_ControlPanel_fnGetDetailsOf,
    ISF_ControlPanel_fnMapColumnToSCID
};

/************************************************************************
 *	ICPanel_PersistFolder2_QueryInterface
 */
static HRESULT WINAPI ICPanel_PersistFolder2_QueryInterface(IPersistFolder2 * iface, REFIID iid, LPVOID * ppvObject)
{
    _ICOM_THIS_From_IPersistFolder2(ICPanelImpl, iface);

    TRACE("(%p)\n", This);

    return IUnknown_QueryInterface(_IUnknown_(This), iid, ppvObject);
}

/************************************************************************
 *	ICPanel_PersistFolder2_AddRef
 */
static ULONG WINAPI ICPanel_PersistFolder2_AddRef(IPersistFolder2 * iface)
{
    _ICOM_THIS_From_IPersistFolder2(ICPanelImpl, iface);

    TRACE("(%p)->(count=%lu)\n", This, This->ref);

    return IUnknown_AddRef(_IUnknown_(This));
}

/************************************************************************
 *	ISFPersistFolder_Release
 */
static ULONG WINAPI ICPanel_PersistFolder2_Release(IPersistFolder2 * iface)
{
    _ICOM_THIS_From_IPersistFolder2(ICPanelImpl, iface);

    TRACE("(%p)->(count=%lu)\n", This, This->ref);

    return IUnknown_Release(_IUnknown_(This));
}

/************************************************************************
 *	ICPanel_PersistFolder2_GetClassID
 */
static HRESULT WINAPI ICPanel_PersistFolder2_GetClassID(IPersistFolder2 * iface, CLSID * lpClassId)
{
    _ICOM_THIS_From_IPersistFolder2(ICPanelImpl, iface);

    TRACE("(%p)\n", This);

    if (!lpClassId)
	return E_POINTER;
    *lpClassId = CLSID_ControlPanel;

    return S_OK;
}

/************************************************************************
 *	ICPanel_PersistFolder2_Initialize
 *
 * NOTES: it makes no sense to change the pidl
 */
static HRESULT WINAPI ICPanel_PersistFolder2_Initialize(IPersistFolder2 * iface, LPCITEMIDLIST pidl)
{
    _ICOM_THIS_From_IPersistFolder2(ICPanelImpl, iface);
    TRACE("(%p)->(%p)\n", This, pidl);
    return E_NOTIMPL;
}

/**************************************************************************
 *	IPersistFolder2_fnGetCurFolder
 */
static HRESULT WINAPI ICPanel_PersistFolder2_GetCurFolder(IPersistFolder2 * iface, LPITEMIDLIST * pidl)
{
    _ICOM_THIS_From_IPersistFolder2(ICPanelImpl, iface);

    TRACE("(%p)->(%p)\n", This, pidl);

    if (!pidl)
	return E_POINTER;
    *pidl = ILClone(This->pidlRoot);
    return S_OK;
}

static IPersistFolder2Vtbl vt_PersistFolder2 =
{

    ICPanel_PersistFolder2_QueryInterface,
    ICPanel_PersistFolder2_AddRef,
    ICPanel_PersistFolder2_Release,
    ICPanel_PersistFolder2_GetClassID,
    ICPanel_PersistFolder2_Initialize,
    ICPanel_PersistFolder2_GetCurFolder
};

HRESULT WINAPI CPanel_GetIconLocationA(LPITEMIDLIST pidl, LPSTR szIconFile, UINT cchMax, int* piIndex)
{
    PIDLCPanelStruct* pcpanel = _ILGetCPanelPointer(pidl);

    if (!pcpanel)
	return E_INVALIDARG;

    lstrcpyA(szIconFile, pcpanel->szName);
    *piIndex = pcpanel->iconIdx!=-1? pcpanel->iconIdx: 0;

    return S_OK;
}

HRESULT WINAPI CPanel_GetIconLocationW(LPITEMIDLIST pidl, LPWSTR szIconFile, UINT cchMax, int* piIndex)
{
    PIDLCPanelStruct* pcpanel = _ILGetCPanelPointer(pidl);

    if (!pcpanel)
	return E_INVALIDARG;

    MultiByteToWideChar(CP_ACP, 0, pcpanel->szName, -1, szIconFile, cchMax);
    *piIndex = pcpanel->iconIdx!=-1? pcpanel->iconIdx: 0;

    return S_OK;
}


/**************************************************************************
* IShellExecuteHookW Implementation
*/

static HRESULT WINAPI IShellExecuteHookW_fnQueryInterface(IShellExecuteHookW* iface, REFIID riid, void** ppvObject)
{
    _ICOM_THIS_From_IShellExecuteHookW(ICPanelImpl, iface);

    TRACE("(%p)->(count=%lu)\n", This, This->ref);

    return IUnknown_QueryInterface(This->pUnkOuter, riid, ppvObject);
}

static ULONG STDMETHODCALLTYPE IShellExecuteHookW_fnAddRef(IShellExecuteHookW* iface)
{
    _ICOM_THIS_From_IShellExecuteHookW(ICPanelImpl, iface);

    TRACE("(%p)->(count=%lu)\n", This, This->ref);

    return IUnknown_AddRef(This->pUnkOuter);
}

static ULONG STDMETHODCALLTYPE IShellExecuteHookW_fnRelease(IShellExecuteHookW* iface)
{
    _ICOM_THIS_From_IShellExecuteHookW(ICPanelImpl, iface);

    TRACE("(%p)\n", This);

    return IUnknown_Release(This->pUnkOuter);
}

static HRESULT WINAPI IShellExecuteHookW_fnExecute(IShellExecuteHookW* iface, LPSHELLEXECUTEINFOW psei)
{
    ICPanelImpl *This = (ICPanelImpl *)iface;

    SHELLEXECUTEINFOW sei_tmp;
    PIDLCPanelStruct* pcpanel;
    WCHAR path[MAX_PATH];
    BOOL ret;
    int l;

    TRACE("(%p)->execute(%p)\n", This, psei);

    if (!psei)
	return E_INVALIDARG;

    pcpanel = _ILGetCPanelPointer(ILFindLastID(psei->lpIDList));

    if (!pcpanel)
	return E_INVALIDARG;

    path[0] = '\"';
    l = 1 + MultiByteToWideChar(CP_ACP, 0, pcpanel->szName, -1, path+1, MAX_PATH);

    /* pass applet name to Control_RunDLL to distinguish between applets in one .cpl file */
    path[++l] = '"';
    path[++l] = ' ';

    MultiByteToWideChar(CP_ACP, 0, pcpanel->szName+pcpanel->offsDispName, -1, path+l, MAX_PATH);

    memcpy(&sei_tmp, psei, sizeof(sei_tmp));
    sei_tmp.lpFile = path;
    sei_tmp.fMask &= ~SEE_MASK_INVOKEIDLIST;

    ret = ShellExecuteExW(&sei_tmp);
    if (ret)
	return S_OK;
    else
	return S_FALSE;
}

static IShellExecuteHookWVtbl vt_ShellExecuteHookW =
{

    IShellExecuteHookW_fnQueryInterface,
    IShellExecuteHookW_fnAddRef,
    IShellExecuteHookW_fnRelease,

    IShellExecuteHookW_fnExecute
};


/**************************************************************************
* IShellExecuteHookA Implementation
*/

static HRESULT WINAPI IShellExecuteHookA_fnQueryInterface(IShellExecuteHookA* iface, REFIID riid, void** ppvObject)
{
    _ICOM_THIS_From_IShellExecuteHookA(ICPanelImpl, iface);

    TRACE("(%p)->(count=%lu)\n", This, This->ref);

    return IUnknown_QueryInterface(This->pUnkOuter, riid, ppvObject);
}

static ULONG STDMETHODCALLTYPE IShellExecuteHookA_fnAddRef(IShellExecuteHookA* iface)
{
    _ICOM_THIS_From_IShellExecuteHookA(ICPanelImpl, iface);

    TRACE("(%p)->(count=%lu)\n", This, This->ref);

    return IUnknown_AddRef(This->pUnkOuter);
}

static ULONG STDMETHODCALLTYPE IShellExecuteHookA_fnRelease(IShellExecuteHookA* iface)
{
    _ICOM_THIS_From_IShellExecuteHookA(ICPanelImpl, iface);

    TRACE("(%p)\n", This);

    return IUnknown_Release(This->pUnkOuter);
}

static HRESULT WINAPI IShellExecuteHookA_fnExecute(IShellExecuteHookA* iface, LPSHELLEXECUTEINFOA psei)
{
    ICPanelImpl *This = (ICPanelImpl *)iface;

    SHELLEXECUTEINFOA sei_tmp;
    PIDLCPanelStruct* pcpanel;
    char path[MAX_PATH];
    BOOL ret;

    TRACE("(%p)->execute(%p)\n", This, psei);

    if (!psei)
	return E_INVALIDARG;

    pcpanel = _ILGetCPanelPointer(ILFindLastID(psei->lpIDList));

    if (!pcpanel)
	return E_INVALIDARG;

    path[0] = '\"';
    lstrcpyA(path+1, pcpanel->szName);

    /* pass applet name to Control_RunDLL to distinguish between applets in one .cpl file */
    lstrcatA(path, "\" ");
    lstrcatA(path, pcpanel->szName+pcpanel->offsDispName);

    memcpy(&sei_tmp, psei, sizeof(sei_tmp));
    sei_tmp.lpFile = path;
    sei_tmp.fMask &= ~SEE_MASK_INVOKEIDLIST;

    ret = ShellExecuteExA(&sei_tmp);
    if (ret)
	return S_OK;
    else
	return S_FALSE;
}

static IShellExecuteHookAVtbl vt_ShellExecuteHookA =
{

    IShellExecuteHookA_fnQueryInterface,
    IShellExecuteHookA_fnAddRef,
    IShellExecuteHookA_fnRelease,

    IShellExecuteHookA_fnExecute
};
