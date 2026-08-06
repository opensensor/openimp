/* Small process wrapper around the reusable OpenIMP tuning controller. */

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "openimp/openimp_tuning.h"

static volatile sig_atomic_t stop_requested;

static void stop_handler(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
}

static int parse_u8(const char *text, uint8_t *value)
{
    char *end;
    unsigned long parsed = strtoul(text, &end, 0);

    if (!*text || *end || parsed > 255U)
        return -1;
    *value = (uint8_t)parsed;
    return 0;
}

static void usage(const char *program)
{
    fprintf(stderr,
        "usage: %s [-d device] [-p security|psychedelic] "
        "[-b brightness] [-c contrast] "
        "[-s saturation] [-S sharpness] [-H hue] [-n]\n",
        program);
}

int main(int argc, char **argv)
{
    OpenIMPTuningController *controller = NULL;
    OpenIMPTuningConfig config;
    const char *device = NULL;
    int option;
    int ret;

    memset(&config, 0, sizeof(config));
    OpenIMP_Tuning_DefaultProfile(&config.profile);
    while ((option = getopt(argc, argv, "d:p:b:c:s:S:H:nh")) != -1) {
        switch (option) {
        case 'd':
            device = optarg;
            break;
        case 'p':
            if (!strcmp(optarg, "security"))
                ret = OpenIMP_Tuning_ProfilePreset(
                    OPENIMP_TUNING_PROFILE_SECURITY, &config.profile);
            else if (!strcmp(optarg, "psychedelic"))
                ret = OpenIMP_Tuning_ProfilePreset(
                    OPENIMP_TUNING_PROFILE_PSYCHEDELIC, &config.profile);
            else
                goto invalid;
            if (ret)
                goto invalid;
            break;
        case 'b':
            config.profile.kind = OPENIMP_TUNING_PROFILE_CUSTOM;
            if (parse_u8(optarg, &config.profile.brightness))
                goto invalid;
            config.profile.control_mask |=
                OPENIMP_TUNING_CONTROL_BRIGHTNESS;
            break;
        case 'c':
            config.profile.kind = OPENIMP_TUNING_PROFILE_CUSTOM;
            if (parse_u8(optarg, &config.profile.contrast))
                goto invalid;
            config.profile.control_mask |= OPENIMP_TUNING_CONTROL_CONTRAST;
            break;
        case 's':
            config.profile.kind = OPENIMP_TUNING_PROFILE_CUSTOM;
            if (parse_u8(optarg, &config.profile.saturation))
                goto invalid;
            config.profile.control_mask |=
                OPENIMP_TUNING_CONTROL_SATURATION;
            break;
        case 'S':
            config.profile.kind = OPENIMP_TUNING_PROFILE_CUSTOM;
            if (parse_u8(optarg, &config.profile.sharpness))
                goto invalid;
            config.profile.control_mask |= OPENIMP_TUNING_CONTROL_SHARPNESS;
            break;
        case 'H':
            config.profile.kind = OPENIMP_TUNING_PROFILE_CUSTOM;
            if (parse_u8(optarg, &config.profile.hue))
                goto invalid;
            config.profile.control_mask |= OPENIMP_TUNING_CONTROL_HUE;
            break;
        case 'n':
            config.profile.gain_feedback = 0;
            break;
        default:
            goto invalid;
        }
    }
    if (optind != argc)
        goto invalid;
    config.device = device;
    signal(SIGINT, stop_handler);
    signal(SIGTERM, stop_handler);

    ret = OpenIMP_Tuning_Create(&controller, &config);
    if (ret) {
        fprintf(stderr, "openimp-tuningd: create failed: %s\n",
                strerror(-ret));
        return 1;
    }
    ret = OpenIMP_Tuning_Start(controller);
    if (ret) {
        fprintf(stderr, "openimp-tuningd: start failed: %s\n",
                strerror(-ret));
        OpenIMP_Tuning_Destroy(controller);
        return 1;
    }
    while (!stop_requested)
        sleep(1);
    OpenIMP_Tuning_Destroy(controller);
    return 0;

invalid:
    usage(argv[0]);
    return 2;
}
