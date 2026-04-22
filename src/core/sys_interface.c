#include <stdint.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdarg.h>
#include <unistd.h>

#include "imp/imp_common.h"

extern char _gp;

int32_t IMP_Log_Get_Option(void); /* forward decl, ported by T<N> later */
int32_t imp_log_fun(int32_t level, int32_t option, int32_t type, ...); /* forward decl, ported by T<N> later */
int32_t modify_phyclk_strength(void); /* forward decl, ported by T<N> later */
int32_t system_init(void); /* forward decl, ported by T<N> later */
int32_t system_exit(void); /* forward decl, ported by T<N> later */
uint64_t system_gettime(int32_t arg1); /* forward decl, ported by T<N> later */
int32_t system_rebasetime(uint64_t arg1); /* forward decl, ported by T<N> later */
int32_t read_reg_32(int32_t arg1, int32_t *arg2); /* forward decl, ported by T<N> later */
int32_t write_reg_32(int32_t arg1, int32_t arg2); /* forward decl, ported by T<N> later */
int32_t system_bind(IMPCell *arg1, IMPCell *arg2); /* forward decl, ported by T<N> later */
int32_t system_unbind(IMPCell *arg1, IMPCell *arg2); /* forward decl, ported by T<N> later */
int32_t system_get_bind_src(IMPCell *arg1, IMPCell *arg2); /* forward decl, ported by T<N> later */
int32_t get_cpu_id(void); /* forward decl, ported by T<N> later */
const char *pixfmt_to_string(int32_t arg1); /* forward decl, ported by T<N> later */
int32_t IMP_MemPool_InitPool(int32_t arg1, int32_t arg2, int32_t arg3); /* forward decl, ported by T<N> later */
int32_t IMP_MemPool_Release(int32_t arg1); /* forward decl, ported by T<N> later */
int32_t IMP_FrameSource_ClearPoolId(void); /* forward decl, ported by T<N> later */
int32_t IMP_Encoder_ClearPoolId(void); /* forward decl, ported by T<N> later */

static void sysif_trace(const char *fmt, ...)
{
    int fd = open("/dev/kmsg", O_WRONLY);
    if (fd < 0) return;

    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (n > 0) write(fd, buf, (size_t)n);
    close(fd);
}

int32_t IMP_System_Init(void)
{
    imp_log_fun(3, IMP_Log_Get_Option(), 2, "System",
        "/home/user/git/proj/sdk-lv3/src/imp/core/sys_interface.c", 0x1c,
        "IMP_System_Init", "%s SDK Version:%s-%s built: %s %s\n",
        "IMP_System_Init", "1.1.6", "a6394f42-Mon Dec 5 14:39:51 2022 +0800,",
        "Dec 29 2022", "15:38:51");
    modify_phyclk_strength();
    return system_init();
}

int32_t IMP_System_Exit(void)
{
    imp_log_fun(3, IMP_Log_Get_Option(), 2, "System",
        "/home/user/git/proj/sdk-lv3/src/imp/core/sys_interface.c", 0x24,
        "IMP_System_Exit", "%s\n", "IMP_System_Exit");
    return system_exit();
}

uint64_t IMP_System_GetTimeStamp(void)
{
    return system_gettime(0);
}

int32_t IMP_System_RebaseTimeStamp(uint64_t arg1)
{
    return system_rebasetime(arg1);
}

int32_t IMP_System_ReadReg32(int32_t arg1)
{
    int32_t result;

    if (read_reg_32(arg1, &result) >= 0) {
        return result;
    }

    result = 0;
    imp_log_fun(6, IMP_Log_Get_Option(), 2, "System",
        "/home/user/git/proj/sdk-lv3/src/imp/core/sys_interface.c", 0x39,
        "IMP_System_ReadReg32", "%s(): failed\n", &_gp);
    return result;
}

int32_t IMP_System_WriteReg32(int32_t arg1, int32_t arg2)
{
    int32_t result = write_reg_32(arg1, arg2);

    if (result < 0) {
        return imp_log_fun(6, IMP_Log_Get_Option(), 2, "System",
            "/home/user/git/proj/sdk-lv3/src/imp/core/sys_interface.c", 0x44,
            "IMP_System_WriteReg32", "%s(): failed\n", &_gp);
    }

    return result;
}

int32_t IMP_System_GetVersion(IMPVersion *arg1)
{
    if (arg1 == NULL) {
        return -1;
    }

    sprintf(arg1->aVersion, "IMP-%s", "1.1.6");
    return 0;
}

int32_t IMP_System_Bind(IMPCell *arg1, IMPCell *arg2)
{
    const char *var_1c;
    int32_t v0_1;
    int32_t v1_1;

    sysif_trace("libimp/BIND: IMP_System_Bind enter src_ptr=%p dst_ptr=%p src=%d.%d.%d dst=%d.%d.%d\n",
                arg1, arg2,
                arg1 ? arg1->deviceID : -1,
                arg1 ? arg1->groupID : -1,
                arg1 ? arg1->outputID : -1,
                arg2 ? arg2->deviceID : -1,
                arg2 ? arg2->groupID : -1,
                arg2 ? arg2->outputID : -1);

    if (arg1 == NULL) {
        v0_1 = IMP_Log_Get_Option();
        var_1c = "%s(): src channel is NULL\n";
        v1_1 = 0x53;
    } else {
        if (arg2 != NULL) {
            return system_bind(arg1, arg2);
        }

        v0_1 = IMP_Log_Get_Option();
        var_1c = "%s(): dst channel is NULL\n";
        v1_1 = 0x57;
    }

    imp_log_fun(6, v0_1, 2, "System",
        "/home/user/git/proj/sdk-lv3/src/imp/core/sys_interface.c", v1_1,
        "IMP_System_Bind", var_1c, "IMP_System_Bind");
    return -1;
}

int32_t IMP_System_UnBind(IMPCell *arg1, IMPCell *arg2)
{
    const char *var_1c;
    int32_t v0_1;
    int32_t v1_1;

    sysif_trace("libimp/BIND: IMP_System_UnBind enter src_ptr=%p dst_ptr=%p src=%d.%d.%d dst=%d.%d.%d\n",
                arg1, arg2,
                arg1 ? arg1->deviceID : -1,
                arg1 ? arg1->groupID : -1,
                arg1 ? arg1->outputID : -1,
                arg2 ? arg2->deviceID : -1,
                arg2 ? arg2->groupID : -1,
                arg2 ? arg2->outputID : -1);

    if (arg1 == NULL) {
        v0_1 = IMP_Log_Get_Option();
        var_1c = "%s(): src channel is NULL\n";
        v1_1 = 0x61;
    } else {
        if (arg2 != NULL) {
            return system_unbind(arg1, arg2);
        }

        v0_1 = IMP_Log_Get_Option();
        var_1c = "%s(): dst channel is NULL\n";
        v1_1 = 0x65;
    }

    imp_log_fun(6, v0_1, 2, "System",
        "/home/user/git/proj/sdk-lv3/src/imp/core/sys_interface.c", v1_1,
        "IMP_System_UnBind", var_1c, "IMP_System_UnBind");
    return -1;
}

int32_t IMP_System_BindIfNeeded(IMPCell *arg1, IMPCell *arg2)
{
    IMPCell src;

    sysif_trace("libimp/BIND: IMP_System_BindIfNeeded enter src_ptr=%p dst_ptr=%p src=%d.%d.%d dst=%d.%d.%d\n",
                arg1, arg2,
                arg1 ? arg1->deviceID : -1,
                arg1 ? arg1->groupID : -1,
                arg1 ? arg1->outputID : -1,
                arg2 ? arg2->deviceID : -1,
                arg2 ? arg2->groupID : -1,
                arg2 ? arg2->outputID : -1);

    if (arg1 == NULL || arg2 == NULL) {
        sysif_trace("libimp/BIND: IMP_System_BindIfNeeded null-arg\n");
        return -1;
    }

    if (system_get_bind_src(arg2, &src) == 0) {
        sysif_trace("libimp/BIND: IMP_System_BindIfNeeded existing src=%d.%d.%d dst=%d.%d.%d\n",
                    src.deviceID, src.groupID, src.outputID,
                    arg2->deviceID, arg2->groupID, arg2->outputID);
        if (src.deviceID == arg1->deviceID &&
            src.groupID == arg1->groupID &&
            src.outputID == arg1->outputID) {
            sysif_trace("libimp/BIND: IMP_System_BindIfNeeded already-bound\n");
            return 0;
        }
    }

    sysif_trace("libimp/BIND: IMP_System_BindIfNeeded call-bind\n");
    return IMP_System_Bind(arg1, arg2);
}

int32_t IMP_System_GetBindbyDest(IMPCell *arg1, IMPCell *arg2)
{
    const char *var_1c;
    int32_t v0_1;
    int32_t v1_1;

    if (arg1 == NULL) {
        v0_1 = IMP_Log_Get_Option();
        var_1c = "%s(): dst channel is NULL\n";
        v1_1 = 0x6f;
    } else {
        if (arg2 != NULL) {
            return system_get_bind_src(arg1, arg2);
        }

        v0_1 = IMP_Log_Get_Option();
        var_1c = "%s(): src channel is NULL\n";
        v1_1 = 0x74;
    }

    imp_log_fun(6, v0_1, 2, "System",
        "/home/user/git/proj/sdk-lv3/src/imp/core/sys_interface.c", v1_1,
        "IMP_System_GetBindbyDest", var_1c, "IMP_System_GetBindbyDest");
    return -1;
}

const char *IMP_System_GetCPUInfo(void)
{
    int32_t v0 = get_cpu_id();

    if (v0 == 0) {
        return "T10";
    }

    if ((uint32_t)(v0 - 1) < 2U) {
        return "T10-Lite";
    }

    switch (v0) {
    case 3:
        return "T20";
    case 4:
        return "T20-Lite";
    case 5:
        return "T20-X";
    case 6:
        return "T30-Lite";
    case 7:
        return "T30-N";
    case 8:
        return "T30-X";
    case 9:
        return "T30-A";
    case 10:
        return "T30-Z";
    case 11:
        return "T21-L";
    case 12:
        return "T21-N";
    case 13:
        return "T21-X";
    case 14:
        return "T21-Z";
    case 15:
        return "T31-L";
    case 16:
        return "T31-N";
    case 17:
        return "T31-X";
    case 18:
        return "T31-A";
    case 19:
        return "T31-AL";
    case 20:
        return "T31-ZL";
    case 21:
        return "T31-ZC";
    case 22:
        return "T31-LC";
    case 23:
        return "T31-ZX";
    default:
        return "Unknown";
    }
}

const char *IMPPixfmtToString(int32_t arg1)
{
    return pixfmt_to_string(arg1);
}

int32_t IMP_System_MemPoolRequest(int32_t arg1, size_t arg2, const char *arg3)
{
    return IMP_MemPool_InitPool(arg1, (int32_t)arg2, (int32_t)(intptr_t)arg3);
}

int32_t IMP_System_MemPoolFree(int32_t arg1)
{
    if (IMP_MemPool_Release(arg1) < 0) {
        return -1;
    }

    IMP_FrameSource_ClearPoolId();
    IMP_Encoder_ClearPoolId();
    return 0;
}
