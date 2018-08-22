
/**************************************************************
* gs_sdk.h : GameServices 公共模块 API 接口声明              *
* Version: 1.0, created by LOU Yihua, 2011-08-29             *
**************************************************************/

#ifndef	GAME_SERVICES_SDK_H
#define GAME_SERVICES_SDK_H

/* 在必要时包含 Windows.h */
#ifndef _INC_WINDOWS
#include <Windows.h>
#endif

/* 由 GameServices.dll 提供的辅助函数表 */
typedef struct _HOOK_FUNCTION_TABLE
{
  /****************************************************************************
  * AllocateInlineHookHandle                                                 *
  *                                                                          *
  * 说明：                                                                   *
  *   分配用于挂钩函数的钩子句柄，供其他函数使用。                           *
  *                                                                          *
  * 参数：                                                                   *
  *   pfnOriginal：被挂钩的函数的起始地址                                    *
  *   pfnHook：由 DLL 编写者提供的挂钩函数的地址                             *
  *                                                                          *
  * 返回值：                                                                 *
  *   钩子句柄，若为 NULL 表示分配出错。                                     *
  ****************************************************************************/
  void* (__stdcall *AllocateInlineHookHandle)(void* pfnOriginal, void* pfnHook);

  /****************************************************************************
  * DestroyInlineHookHandle                                                  *
  *                                                                          *
  * 说明：                                                                   *
  *   销毁钩子句柄。                                                         *
  *                                                                          *
  * 参数：                                                                   *
  *   hookHandle：待销毁的钩子句柄                                           *
  *                                                                          *
  * 返回值：                                                                 *
  *   TRUE 表示销毁成功，FALSE 表示销毁失败。                                *
  ****************************************************************************/
  BOOL(__stdcall *DestroyInlineHookHandle) (void* hookHandle);

  /****************************************************************************
  * DoInlineHook                                                             *
  *                                                                          *
  * 说明：                                                                   *
  *   使用钩子句柄挂钩函数。                                                 *
  *                                                                          *
  * 参数：                                                                   *
  *   hookHandle：钩子句柄                                                   *
  *   fHook：为 TRUE 时表示挂钩，为 FALSE 时表示解除挂钩                     *
  *                                                                          *
  * 返回值：                                                                 *
  *   无。                                                                   *
  ****************************************************************************/
  void(__stdcall *DoInlineHook)            (void* hookHandle, BOOL fHook);

  /****************************************************************************
  * DoImportHook                                                             *
  *                                                                          *
  * 说明：                                                                   *
  *   挂钩模块的导入函数。                                                   *
  *                                                                          *
  * 参数：                                                                   *
  *   hModule：待挂钩的模块地址，函数的挂钩仅对该模块生效                    *
  *   szDll：待挂钩的函数所在的 DLL 的文件名（无路径）                       *
  *   szProc：待挂钩的函数名                                                 *
  *   pfnHook：由 DLL 编写者提供的挂钩函数的地址                             *
  *                                                                          *
  * 返回值：                                                                 *
  *   待挂钩函数的原始地址。                                                 *
  ****************************************************************************/
  void* (__stdcall *DoImportHook)            (HMODULE hModule, LPCSTR szDll, LPCSTR szProc, void* pfnHook);

} HOOK_FUNCTION_TABLE, *PHOOK_FUNCTION_TABLE;

/*******************************************************************************
* Initialize                                                                  *
*                                                                             *
* 说明：                                                                      *
*   DLL 模块初始化函数，由 DLL 实现者导出，GameServices.dll 在加载 DLL 后调用 *
*                                                                             *
* 参数：                                                                      *
*   hModuleSelf：本 DLL 的 HMODULE                                            *
*   pTable：指向由 GameServices.dll 提供的函数表的指针                        *
*                                                                             *
* 返回值：                                                                    *
*   TRUE 表示初始化成功，FALSE 表示初始化失败。                               *
*******************************************************************************/
extern "C" __declspec(dllexport) BOOL CALLBACK Initialize(HMODULE hModuleSelf, PHOOK_FUNCTION_TABLE pTable);

#endif
