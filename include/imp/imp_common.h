/**
 * IMP Common Definitions
 * 
 * Common types, enums, and structures used across IMP modules
 */

#ifndef __IMP_COMMON_H__
#define __IMP_COMMON_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * Return codes
 */
#define IMP_SUCCESS 0
#define IMP_FAILURE -1

/**
 * Blocking mode
 */
typedef enum {
    BLOCK = 0,      /**< Blocking mode */
    NOBLOCK = 1     /**< Non-blocking mode */
} IMPBlock;

/**
 * Device IDs for binding
 */
#define DEV_ID_FS       0   /**< Frame Source */
#define DEV_ID_ENC      1   /**< Encoder */
#define DEV_ID_IVS      3   /**< IVS */
#define DEV_ID_OSD      4   /**< OSD */

/**
 * Video input interface (T40/T41)
 */
typedef enum {
    IMPVI_MAIN = 0,     /**< Main video input */
    IMPVI_SEC = 1       /**< Secondary video input */
} IMPVI;

/**
 * Pixel format
 */
typedef enum {
    PIX_FMT_YUV420P,    /**< Planar YUV 4:2:0 */
    PIX_FMT_YUYV422,    /**< Packed YUV 4:2:2 */
    PIX_FMT_UYVY422,    /**< Packed YUV 4:2:2 */
    PIX_FMT_YUV422P,    /**< Planar YUV 4:2:2 */
    PIX_FMT_YUV444P,    /**< Planar YUV 4:4:4 */
    PIX_FMT_YUV410P,    /**< Planar YUV 4:1:0 */
    PIX_FMT_YUV411P,    /**< Planar YUV 4:1:1 */
    PIX_FMT_YUVJ420P,   /**< Planar YUV 4:2:0 full range */
    PIX_FMT_YUVJ422P,   /**< Planar YUV 4:2:2 full range */
    PIX_FMT_YUVJ444P,   /**< Planar YUV 4:4:4 full range */
    PIX_FMT_NV12,       /**< Semi-planar YUV 4:2:0 */
    PIX_FMT_NV21,       /**< Semi-planar YUV 4:2:0 */
    PIX_FMT_BGRA,       /**< Packed BGRA 8:8:8:8 */
    PIX_FMT_RGBA,       /**< Packed RGBA 8:8:8:8 */
    PIX_FMT_BGGR8,      /**< Bayer BGGR 8-bit */
    PIX_FMT_RGGB8,      /**< Bayer RGGB 8-bit */
    PIX_FMT_GBRG8,      /**< Bayer GBRG 8-bit */
    PIX_FMT_GRBG8,      /**< Bayer GRBG 8-bit */
    PIX_FMT_RAW,        /**< Raw data */
} IMPPixelFormat;

/**
 * Cell structure for binding modules
 */
typedef struct {
    int deviceID;       /**< Device ID */
    int groupID;        /**< Group ID */
    int outputID;       /**< Output ID */
} IMPCell;

/**
 * Version information
 */
typedef struct {
    char aVersion[64];  /**< Version string */
} IMPVersion;

/** Frame info structure (width/height) */
typedef struct {
    int width;
    int height;
} IMPFrameInfo;

/**
 * Rectangle structure
 */
typedef struct {
    int x;              /**< X coordinate */
    int y;              /**< Y coordinate */
    int width;          /**< Width */
    int height;         /**< Height */
} IMPRect;

/**
 * Point structure
 */
typedef struct {
    int x;              /**< X coordinate */
    int y;              /**< Y coordinate */
} IMPPoint;

/**
 * Sensor control interface type
 */
typedef enum {
    TX_SENSOR_CONTROL_INTERFACE_I2C = 1,    /**< I2C interface */
    TX_SENSOR_CONTROL_INTERFACE_SPI = 2     /**< SPI interface */
} TXSensorControlBusType;

/**
 * I2C configuration
 */
typedef struct {
    char type[20];      /**< Sensor type string */
    int addr;           /**< I2C address */
    int i2c_adapter;    /**< I2C adapter number */
} TXSNSI2CConfig;

/**
 * Sensor information
 */
typedef struct {
    char name[32];                          /**< Sensor name */
    TXSensorControlBusType cbus_type;       /**< Control bus type */
    TXSNSI2CConfig i2c;                     /**< I2C configuration */
    int rst_gpio;                           /**< Reset GPIO */
    int pwdn_gpio;                          /**< Power down GPIO */
    int power_gpio;                         /**< Power GPIO */
    int sensor_id;                          /**< Sensor ID */
    void *private_data;                     /**< Private data */
} IMPSensorInfo;

/**
 * Region handle type
 */
typedef int IMPRgnHandle;

#ifdef __cplusplus
}
#endif

#endif /* __IMP_COMMON_H__ */

