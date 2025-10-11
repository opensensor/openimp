/**
 * OpenIMP API Surface Test
 * 
 * This test verifies that all expected IMP functions are present and callable.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <imp/imp_common.h>
#include <imp/imp_system.h>
#include <imp/imp_isp.h>
#include <imp/imp_framesource.h>
#include <imp/imp_encoder.h>
#include <imp/imp_audio.h>
#include <imp/imp_osd.h>
#include <imp/imp_ivs.h>
#include <sysutils/su_base.h>

int main(void) {
    int ret;
    printf("OpenIMP API Surface Test\n");
    printf("=========================\n\n");
    
    /* Test System Module */
    printf("Testing IMP_System...\n");
    
    ret = IMP_System_Init();
    printf("  IMP_System_Init: %s\n", ret == 0 ? "OK" : "FAIL");
    
    IMPVersion version;
    ret = IMP_System_GetVersion(&version);
    printf("  IMP_System_GetVersion: %s (version: %s)\n", 
           ret == 0 ? "OK" : "FAIL", version.aVersion);
    
    const char *cpu = IMP_System_GetCPUInfo();
    printf("  IMP_System_GetCPUInfo: %s\n", cpu);
    
    uint64_t ts = IMP_System_GetTimeStamp();
    printf("  IMP_System_GetTimeStamp: %llu us\n", (unsigned long long)ts);
    
    ret = IMP_System_RebaseTimeStamp(1000000);
    printf("  IMP_System_RebaseTimeStamp: %s\n", ret == 0 ? "OK" : "FAIL");
    
    IMPCell src = {DEV_ID_FS, 0, 0};
    IMPCell dst = {DEV_ID_ENC, 0, 0};
    ret = IMP_System_Bind(&src, &dst);
    printf("  IMP_System_Bind: %s\n", ret == 0 ? "OK" : "FAIL");
    
    ret = IMP_System_UnBind(&src, &dst);
    printf("  IMP_System_UnBind: %s\n", ret == 0 ? "OK" : "FAIL");
    
    /* Test Sysutils */
    printf("\nTesting SU_Base...\n");
    
    SUVersion suver;
    ret = SU_Base_GetVersion(&suver);
    printf("  SU_Base_GetVersion: %s (version: %s)\n", 
           ret == 0 ? "OK" : "FAIL", suver.chr);
    
    /* Test ISP Module */
    printf("\nTesting IMP_ISP...\n");
    
    ret = IMP_ISP_Open();
    printf("  IMP_ISP_Open: %s\n", ret == 0 ? "OK" : "FAIL");
    
    IMPSensorInfo sinfo;
    memset(&sinfo, 0, sizeof(sinfo));
    strcpy(sinfo.name, "test_sensor");
    sinfo.cbus_type = TX_SENSOR_CONTROL_INTERFACE_I2C;
    
    ret = IMP_ISP_AddSensor(&sinfo);
    printf("  IMP_ISP_AddSensor: %s\n", ret == 0 ? "OK" : "FAIL");
    
    ret = IMP_ISP_EnableSensor();
    printf("  IMP_ISP_EnableSensor: %s\n", ret == 0 ? "OK" : "FAIL");
    
    ret = IMP_ISP_EnableTuning();
    printf("  IMP_ISP_EnableTuning: %s\n", ret == 0 ? "OK" : "FAIL");
    
    ret = IMP_ISP_Tuning_SetSensorFPS(25, 1);
    printf("  IMP_ISP_Tuning_SetSensorFPS: %s\n", ret == 0 ? "OK" : "FAIL");
    
    /* Test FrameSource Module */
    printf("\nTesting IMP_FrameSource...\n");
    
    IMPFSChnAttr fs_attr;
    memset(&fs_attr, 0, sizeof(fs_attr));
    fs_attr.picWidth = 1920;
    fs_attr.picHeight = 1080;
    fs_attr.pixFmt = PIX_FMT_NV12;
    
    ret = IMP_FrameSource_CreateChn(0, &fs_attr);
    printf("  IMP_FrameSource_CreateChn: %s\n", ret == 0 ? "OK" : "FAIL");
    
    ret = IMP_FrameSource_EnableChn(0);
    printf("  IMP_FrameSource_EnableChn: %s\n", ret == 0 ? "OK" : "FAIL");
    
    /* Test Encoder Module */
    printf("\nTesting IMP_Encoder...\n");
    
    ret = IMP_Encoder_CreateGroup(0);
    printf("  IMP_Encoder_CreateGroup: %s\n", ret == 0 ? "OK" : "FAIL");
    
    IMPEncoderChnAttr enc_attr;
    ret = IMP_Encoder_SetDefaultParam(&enc_attr, IMP_ENC_PROFILE_AVC_MAIN,
                                       IMP_ENC_RC_MODE_CBR, 1920, 1080,
                                       25, 1, 50, 2, -1, 2000);
    printf("  IMP_Encoder_SetDefaultParam: %s\n", ret == 0 ? "OK" : "FAIL");
    
    ret = IMP_Encoder_CreateChn(0, (IMPEncoderCHNAttr*)&enc_attr);
    printf("  IMP_Encoder_CreateChn: %s\n", ret == 0 ? "OK" : "FAIL");
    
    ret = IMP_Encoder_RegisterChn(0, 0);
    printf("  IMP_Encoder_RegisterChn: %s\n", ret == 0 ? "OK" : "FAIL");
    
    /* Test Audio Module */
    printf("\nTesting IMP_Audio...\n");
    
    ret = IMP_AI_Enable(0);
    printf("  IMP_AI_Enable: %s\n", ret == 0 ? "OK" : "FAIL");
    
    IMPAudioIOAttr ai_attr;
    memset(&ai_attr, 0, sizeof(ai_attr));
    ai_attr.samplerate = AUDIO_SAMPLE_RATE_16000;
    ai_attr.bitwidth = AUDIO_BIT_WIDTH_16;
    ai_attr.soundmode = AUDIO_SOUND_MODE_MONO;
    
    ret = IMP_AI_SetPubAttr(0, &ai_attr);
    printf("  IMP_AI_SetPubAttr: %s\n", ret == 0 ? "OK" : "FAIL");
    
    ret = IMP_AI_EnableChn(0, 0);
    printf("  IMP_AI_EnableChn: %s\n", ret == 0 ? "OK" : "FAIL");
    
    /* Test OSD Module */
    printf("\nTesting IMP_OSD...\n");
    
    ret = IMP_OSD_SetPoolSize(512 * 1024);
    printf("  IMP_OSD_SetPoolSize: %s\n", ret == 0 ? "OK" : "FAIL");
    
    ret = IMP_OSD_CreateGroup(0);
    printf("  IMP_OSD_CreateGroup: %s\n", ret == 0 ? "OK" : "FAIL");
    
    IMPOSDRgnAttr osd_attr;
    memset(&osd_attr, 0, sizeof(osd_attr));
    osd_attr.type = OSD_REG_BITMAP;
    
    ret = IMP_OSD_CreateRgn(0, &osd_attr);
    printf("  IMP_OSD_CreateRgn: %s\n", ret == 0 ? "OK" : "FAIL");
    
    /* Test IVS Module */
    printf("\nTesting IMP_IVS...\n");
    
    ret = IMP_IVS_CreateGroup(0);
    printf("  IMP_IVS_CreateGroup: %s\n", ret == 0 ? "OK" : "FAIL");
    
    /* Cleanup */
    printf("\nCleaning up...\n");
    
    ret = IMP_System_Exit();
    printf("  IMP_System_Exit: %s\n", ret == 0 ? "OK" : "FAIL");
    
    printf("\nAll API tests completed!\n");
    return 0;
}

