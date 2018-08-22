
/**************************************************************
* gs_sdk.h : GameServices ����ģ�� API �ӿ�����              *
* Version: 1.0, created by LOU Yihua, 2011-08-29             *
**************************************************************/

#ifndef	GAME_SERVICES_SDK_H
#define GAME_SERVICES_SDK_H

/* �ڱ�Ҫʱ���� Windows.h */
#ifndef _INC_WINDOWS
#include <Windows.h>
#endif

/* �� GameServices.dll �ṩ�ĸ��������� */
typedef struct _HOOK_FUNCTION_TABLE
{
  /****************************************************************************
  * AllocateInlineHookHandle                                                 *
  *                                                                          *
  * ˵����                                                                   *
  *   �������ڹҹ������Ĺ��Ӿ��������������ʹ�á�                           *
  *                                                                          *
  * ������                                                                   *
  *   pfnOriginal�����ҹ��ĺ�������ʼ��ַ                                    *
  *   pfnHook���� DLL ��д���ṩ�Ĺҹ������ĵ�ַ                             *
  *                                                                          *
  * ����ֵ��                                                                 *
  *   ���Ӿ������Ϊ NULL ��ʾ�������                                     *
  ****************************************************************************/
  void* (__stdcall *AllocateInlineHookHandle)(void* pfnOriginal, void* pfnHook);

  /****************************************************************************
  * DestroyInlineHookHandle                                                  *
  *                                                                          *
  * ˵����                                                                   *
  *   ���ٹ��Ӿ����                                                         *
  *                                                                          *
  * ������                                                                   *
  *   hookHandle�������ٵĹ��Ӿ��                                           *
  *                                                                          *
  * ����ֵ��                                                                 *
  *   TRUE ��ʾ���ٳɹ���FALSE ��ʾ����ʧ�ܡ�                                *
  ****************************************************************************/
  BOOL(__stdcall *DestroyInlineHookHandle) (void* hookHandle);

  /****************************************************************************
  * DoInlineHook                                                             *
  *                                                                          *
  * ˵����                                                                   *
  *   ʹ�ù��Ӿ���ҹ�������                                                 *
  *                                                                          *
  * ������                                                                   *
  *   hookHandle�����Ӿ��                                                   *
  *   fHook��Ϊ TRUE ʱ��ʾ�ҹ���Ϊ FALSE ʱ��ʾ����ҹ�                     *
  *                                                                          *
  * ����ֵ��                                                                 *
  *   �ޡ�                                                                   *
  ****************************************************************************/
  void(__stdcall *DoInlineHook)            (void* hookHandle, BOOL fHook);

  /****************************************************************************
  * DoImportHook                                                             *
  *                                                                          *
  * ˵����                                                                   *
  *   �ҹ�ģ��ĵ��뺯����                                                   *
  *                                                                          *
  * ������                                                                   *
  *   hModule�����ҹ���ģ���ַ�������Ĺҹ����Ը�ģ����Ч                    *
  *   szDll�����ҹ��ĺ������ڵ� DLL ���ļ�������·����                       *
  *   szProc�����ҹ��ĺ�����                                                 *
  *   pfnHook���� DLL ��д���ṩ�Ĺҹ������ĵ�ַ                             *
  *                                                                          *
  * ����ֵ��                                                                 *
  *   ���ҹ�������ԭʼ��ַ��                                                 *
  ****************************************************************************/
  void* (__stdcall *DoImportHook)            (HMODULE hModule, LPCSTR szDll, LPCSTR szProc, void* pfnHook);

} HOOK_FUNCTION_TABLE, *PHOOK_FUNCTION_TABLE;

/*******************************************************************************
* Initialize                                                                  *
*                                                                             *
* ˵����                                                                      *
*   DLL ģ���ʼ���������� DLL ʵ���ߵ�����GameServices.dll �ڼ��� DLL ����� *
*                                                                             *
* ������                                                                      *
*   hModuleSelf���� DLL �� HMODULE                                            *
*   pTable��ָ���� GameServices.dll �ṩ�ĺ������ָ��                        *
*                                                                             *
* ����ֵ��                                                                    *
*   TRUE ��ʾ��ʼ���ɹ���FALSE ��ʾ��ʼ��ʧ�ܡ�                               *
*******************************************************************************/
extern "C" __declspec(dllexport) BOOL CALLBACK Initialize(HMODULE hModuleSelf, PHOOK_FUNCTION_TABLE pTable);

#endif
