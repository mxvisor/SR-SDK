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

extern uint32_t IDirectInputVtbl_asm2c;
extern uint32_t IDirectInputDeviceVtbl_asm2c;

/* whether the DirectInput keyboard device exists: it then owns the keyboard */
uint32_t CCALL winapi_is_dinput_active(void);

#ifdef __cplusplus
}
#endif
