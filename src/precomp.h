/*++
    Print to image - XPSDrv render filter (v4 printer driver)

    precomp.h - common includes
--*/
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <sal.h>
#include <windows.h>

#include <cstring>
#include <intsafe.h>
#include <new>
#include <memory>
#include <vector>
#include <string>

#include <objbase.h>
#include <oleauto.h>

//
// Print pipeline types
//
#include <winspool.h>
#include <filterpipeline.h>
#include <prntvpt.h>

//
// ATL (CComPtr / CComBSTR / CComVariant)
//
#include <atlbase.h>

//
// WIC
//
#include <wincodec.h>

//
// MSXML
//
#include <msxml6.h>

//
// OPC + XPS Object Model
//
#include <msopc.h>
#include <xpsobjectmodel.h>

//
// XPS Rasterization Service
//
#include <xpsrassvc.h>
