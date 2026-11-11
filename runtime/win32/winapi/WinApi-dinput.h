/**
 *
 *  Copyright (C) 2026 mxvisor
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy of
 *  this software and associated documentation files (the "Software"), to deal in
 *  the Software without restriction, including without limitation the rights to
 *  use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 *  of the Software, and to permit persons to whom the Software is furnished to do
 *  so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 *
 */

/* generated with llasm/WinApi-dinput-asm.llasm */
#include <stdint.h>
#include "platform.h"
#ifdef __cplusplus
extern "C" {
#endif

uint32_t CCALL IDirectInput_QueryInterface_c(uint32_t a0, uint32_t a1, uint32_t a2);
uint32_t CCALL IDirectInput_AddRef_c(uint32_t a0);
uint32_t CCALL IDirectInput_Release_c(uint32_t a0);
uint32_t CCALL IDirectInput_CreateDevice_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3);
uint32_t CCALL IDirectInput_EnumDevices_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4);
uint32_t CCALL IDirectInput_GetDeviceStatus_c(uint32_t a0, uint32_t a1);
uint32_t CCALL IDirectInput_RunControlPanel_c(uint32_t a0, uint32_t a1, uint32_t a2);
uint32_t CCALL IDirectInput_Initialize_c(uint32_t a0, uint32_t a1, uint32_t a2);
uint32_t CCALL IDirectInputDevice_QueryInterface_c(uint32_t a0, uint32_t a1, uint32_t a2);
uint32_t CCALL IDirectInputDevice_AddRef_c(uint32_t a0);
uint32_t CCALL IDirectInputDevice_Release_c(uint32_t a0);
uint32_t CCALL IDirectInputDevice_GetCapabilities_c(uint32_t a0, uint32_t a1);
uint32_t CCALL IDirectInputDevice_EnumObjects_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3);
uint32_t CCALL IDirectInputDevice_GetProperty_c(uint32_t a0, uint32_t a1, uint32_t a2);
uint32_t CCALL IDirectInputDevice_SetProperty_c(uint32_t a0, uint32_t a1, uint32_t a2);
uint32_t CCALL IDirectInputDevice_Acquire_c(uint32_t a0);
uint32_t CCALL IDirectInputDevice_Unacquire_c(uint32_t a0);
uint32_t CCALL IDirectInputDevice_GetDeviceState_c(uint32_t a0, uint32_t a1, uint32_t a2);
uint32_t CCALL IDirectInputDevice_GetDeviceData_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4);
uint32_t CCALL IDirectInputDevice_SetDataFormat_c(uint32_t a0, uint32_t a1);
uint32_t CCALL IDirectInputDevice_SetEventNotification_c(uint32_t a0, uint32_t a1);
uint32_t CCALL IDirectInputDevice_SetCooperativeLevel_c(uint32_t a0, uint32_t a1, uint32_t a2);
uint32_t CCALL IDirectInputDevice_GetObjectInfo_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3);
uint32_t CCALL IDirectInputDevice_GetDeviceInfo_c(uint32_t a0, uint32_t a1);
uint32_t CCALL IDirectInputDevice_RunControlPanel_c(uint32_t a0, uint32_t a1, uint32_t a2);
uint32_t CCALL IDirectInputDevice_Initialize_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3);
uint32_t CCALL IDirectInputDevice2_CreateEffect_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4);
uint32_t CCALL IDirectInputDevice2_EnumEffects_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3);
uint32_t CCALL IDirectInputDevice2_GetEffectInfo_c(uint32_t a0, uint32_t a1, uint32_t a2);
uint32_t CCALL IDirectInputDevice2_GetForceFeedbackState_c(uint32_t a0, uint32_t a1);
uint32_t CCALL IDirectInputDevice2_SendForceFeedbackCommand_c(uint32_t a0, uint32_t a1);
uint32_t CCALL IDirectInputDevice2_EnumCreatedEffectObjects_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3);
uint32_t CCALL IDirectInputDevice2_Escape_c(uint32_t a0, uint32_t a1);
uint32_t CCALL IDirectInputDevice2_Poll_c(uint32_t a0);
uint32_t CCALL IDirectInputDevice2_SendDeviceData_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4);
uint32_t CCALL IDirectInputEffect_QueryInterface_c(uint32_t a0, uint32_t a1, uint32_t a2);
uint32_t CCALL IDirectInputEffect_AddRef_c(uint32_t a0);
uint32_t CCALL IDirectInputEffect_Release_c(uint32_t a0);
uint32_t CCALL IDirectInputEffect_Initialize_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3);
uint32_t CCALL IDirectInputEffect_GetEffectGuid_c(uint32_t a0, uint32_t a1);
uint32_t CCALL IDirectInputEffect_GetParameters_c(uint32_t a0, uint32_t a1, uint32_t a2);
uint32_t CCALL IDirectInputEffect_SetParameters_c(uint32_t a0, uint32_t a1, uint32_t a2);
uint32_t CCALL IDirectInputEffect_Start_c(uint32_t a0, uint32_t a1, uint32_t a2);
uint32_t CCALL IDirectInputEffect_Stop_c(uint32_t a0);
uint32_t CCALL IDirectInputEffect_GetEffectStatus_c(uint32_t a0, uint32_t a1);
uint32_t CCALL IDirectInputEffect_Download_c(uint32_t a0);
uint32_t CCALL IDirectInputEffect_Unload_c(uint32_t a0);
uint32_t CCALL IDirectInputEffect_Escape_c(uint32_t a0, uint32_t a1);

extern uint32_t IDirectInputVtbl_asm2c;
extern uint32_t IDirectInputDeviceVtbl_asm2c;
extern uint32_t IDirectInputEffectVtbl_asm2c;

/* whether the DirectInput keyboard device exists: it then owns the keyboard */
uint32_t CCALL winapi_is_dinput_active(void);

#ifdef __cplusplus
}
#endif
