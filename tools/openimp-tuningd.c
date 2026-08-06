/* Small process wrapper and live control socket for OpenIMP tuning policy. */

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#include "openimp/openimp_tuning.h"

#define DEFAULT_CONTROL_SOCKET "/var/run/openimp-tuning.sock"
#define COMMAND_BYTES 256
#define RESPONSE_BYTES 512

static volatile sig_atomic_t stop_requested;

static void stop_handler(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
}

static int parse_u8(const char *text, uint8_t *value)
{
    char *end;
    unsigned long parsed;

    if (!text)
        return -1;
    parsed = strtoul(text, &end, 0);
    if (!*text || *end || parsed > 255U)
        return -1;
    *value = (uint8_t)parsed;
    return 0;
}

static int parse_gain(const char *text, uint16_t *value)
{
    char *end;
    unsigned long parsed;

    if (!text)
        return -1;
    parsed = strtoul(text, &end, 0);
    if (!*text || *end || parsed < 0x200U || parsed > 0x1800U)
        return -1;
    *value = (uint16_t)parsed;
    return 0;
}

static int parse_exposure_target(const char *text, uint16_t *value)
{
    char *end;
    unsigned long parsed;

    if (!text)
        return -1;
    parsed = strtoul(text, &end, 0);
    if (!*text || *end || parsed < 0x400U || parsed > 0xffffU)
        return -1;
    *value = (uint16_t)parsed;
    return 0;
}

static const char *profile_name(OpenIMPTuningProfileKind kind)
{
    switch (kind) {
    case OPENIMP_TUNING_PROFILE_SECURITY:
        return "security";
    case OPENIMP_TUNING_PROFILE_PSYCHEDELIC:
        return "psychedelic";
    case OPENIMP_TUNING_PROFILE_CUSTOM:
        return "custom";
    default:
        return "unknown";
    }
}

static void set_response(char *response, size_t response_bytes,
                         const char *format, ...)
{
    va_list arguments;

    va_start(arguments, format);
    vsnprintf(response, response_bytes, format, arguments);
    va_end(arguments);
}

static int apply_named_profile(OpenIMPTuningController *controller,
                               const char *name, char *response,
                               size_t response_bytes)
{
    OpenIMPTuningProfile profile;
    int ret;

    if (!name)
        return -EINVAL;
    if (!strcmp(name, "security"))
        ret = OpenIMP_Tuning_ProfilePreset(
            OPENIMP_TUNING_PROFILE_SECURITY, &profile);
    else if (!strcmp(name, "psychedelic"))
        ret = OpenIMP_Tuning_ProfilePreset(
            OPENIMP_TUNING_PROFILE_PSYCHEDELIC, &profile);
    else
        return -EINVAL;
    if (!ret)
        ret = OpenIMP_Tuning_SetProfile(controller, &profile);
    if (!ret)
        set_response(response, response_bytes, "OK profile=%s\n", name);
    return ret;
}

static int apply_set_command(OpenIMPTuningController *controller,
                             char *save, char *response,
                             size_t response_bytes)
{
    OpenIMPTuningStatus status;
    OpenIMPTuningProfile profile;
    char *control = strtok_r(save, " \t\r\n", &save);
    char *value = strtok_r(NULL, " \t\r\n", &save);
    char *extra;
    int ret;

    if (!control || !value)
        return -EINVAL;
    ret = OpenIMP_Tuning_GetStatus(controller, &status);
    if (ret)
        return ret;
    profile = status.profile;
    profile.kind = OPENIMP_TUNING_PROFILE_CUSTOM;

    if (!strcmp(control, "brightness")) {
        if (parse_u8(value, &profile.brightness))
            return -EINVAL;
        profile.control_mask |= OPENIMP_TUNING_CONTROL_BRIGHTNESS;
    } else if (!strcmp(control, "contrast")) {
        if (parse_u8(value, &profile.contrast))
            return -EINVAL;
        profile.control_mask |= OPENIMP_TUNING_CONTROL_CONTRAST;
    } else if (!strcmp(control, "saturation")) {
        if (parse_u8(value, &profile.saturation))
            return -EINVAL;
        profile.control_mask |= OPENIMP_TUNING_CONTROL_SATURATION;
    } else if (!strcmp(control, "sharpness")) {
        if (parse_u8(value, &profile.sharpness))
            return -EINVAL;
        profile.control_mask |= OPENIMP_TUNING_CONTROL_SHARPNESS;
    } else if (!strcmp(control, "hue")) {
        if (parse_u8(value, &profile.hue))
            return -EINVAL;
        profile.control_mask |= OPENIMP_TUNING_CONTROL_HUE;
    } else if (!strcmp(control, "exposure")) {
        if (parse_exposure_target(value, &profile.exposure_target_q8))
            return -EINVAL;
        profile.control_mask |= OPENIMP_TUNING_CONTROL_EXPOSURE_TARGET;
    } else if (!strcmp(control, "awb")) {
        profile.control_mask |= OPENIMP_TUNING_CONTROL_WHITE_BALANCE;
        if (!strcmp(value, "auto")) {
            profile.auto_white_balance = 1;
            profile.red_gain = status.active_red_gain;
            profile.blue_gain = status.active_blue_gain;
        } else if (!strcmp(value, "manual")) {
            char *red = strtok_r(NULL, " \t\r\n", &save);
            char *blue = strtok_r(NULL, " \t\r\n", &save);

            if (parse_gain(red, &profile.red_gain) ||
                parse_gain(blue, &profile.blue_gain))
                return -EINVAL;
            profile.auto_white_balance = 0;
        } else {
            return -EINVAL;
        }
    } else {
        return -EINVAL;
    }
    extra = strtok_r(NULL, " \t\r\n", &save);
    if (extra)
        return -EINVAL;

    ret = OpenIMP_Tuning_SetProfile(controller, &profile);
    if (!ret)
        set_response(response, response_bytes, "OK profile=custom\n");
    return ret;
}

static int handle_command(OpenIMPTuningController *controller, char *command,
                          char *response, size_t response_bytes)
{
    OpenIMPTuningStatus status;
    char *save = NULL;
    char *verb = strtok_r(command, " \t\r\n", &save);
    char *argument;
    int ret;

    if (!verb)
        ret = -EINVAL;
    else if (!strcmp(verb, "status")) {
        if (strtok_r(NULL, " \t\r\n", &save))
            ret = -EINVAL;
        else {
            ret = OpenIMP_Tuning_GetStatus(controller, &status);
            if (!ret)
                set_response(response, response_bytes,
                    "OK profile=%s running=%d feedback_updates=%u "
                    "total_gain=%d ae_target=%u awb=%s "
                    "red_gain=%u blue_gain=%u\n",
                    profile_name(status.profile.kind), status.running,
                    status.feedback_updates, status.last_total_gain,
                    status.active_exposure_target_q8,
                    status.active_auto_white_balance ? "auto" : "manual",
                    status.active_red_gain, status.active_blue_gain);
        }
    } else if (!strcmp(verb, "profile")) {
        argument = strtok_r(NULL, " \t\r\n", &save);
        if (strtok_r(NULL, " \t\r\n", &save))
            ret = -EINVAL;
        else
            ret = apply_named_profile(controller, argument, response,
                                      response_bytes);
    } else if (!strcmp(verb, "set")) {
        ret = apply_set_command(controller, save, response, response_bytes);
    } else if (!strcmp(verb, "help")) {
        ret = 0;
        set_response(response, response_bytes,
            "OK commands: status | profile security|psychedelic | "
            "set brightness|contrast|saturation|sharpness|hue 0..255 | "
            "set exposure 1024..65535 | "
            "set awb auto | set awb manual 512..6144 512..6144\n");
    } else {
        ret = -EINVAL;
    }

    if (ret)
        set_response(response, response_bytes, "ERR %s\n", strerror(-ret));
    return ret;
}

static int make_control_socket(const char *path)
{
    struct sockaddr_un address;
    int fd;

    if (!path || strlen(path) >= sizeof(address.sun_path))
        return -ENAMETOOLONG;
    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return -errno;
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    strcpy(address.sun_path, path);
    unlink(path);
    if (bind(fd, (struct sockaddr *)&address, sizeof(address)) < 0 ||
        chmod(path, 0660) < 0 || listen(fd, 4) < 0) {
        int ret = -errno;

        close(fd);
        unlink(path);
        return ret;
    }
    return fd;
}

static int run_client(const char *path, const char *command)
{
    struct sockaddr_un address;
    char request[COMMAND_BYTES];
    char response[RESPONSE_BYTES];
    size_t request_bytes;
    size_t sent = 0;
    ssize_t got;
    int fd;

    if (!path || !command || strlen(path) >= sizeof(address.sun_path) ||
        strlen(command) + 2U > sizeof(request))
        return 2;
    signal(SIGPIPE, SIG_IGN);
    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("openimp-tuningd: socket");
        return 1;
    }
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    strcpy(address.sun_path, path);
    if (connect(fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("openimp-tuningd: connect");
        close(fd);
        return 1;
    }
    request_bytes = (size_t)snprintf(request, sizeof(request), "%s\n",
                                     command);
    while (sent < request_bytes) {
        ssize_t written = write(fd, request + sent, request_bytes - sent);

        if (written <= 0) {
            perror("openimp-tuningd: write");
            close(fd);
            return 1;
        }
        sent += (size_t)written;
    }
    shutdown(fd, SHUT_WR);
    got = read(fd, response, sizeof(response) - 1U);
    if (got < 0) {
        perror("openimp-tuningd: read");
        close(fd);
        return 1;
    }
    close(fd);
    response[got] = '\0';
    fputs(response, stdout);
    return strncmp(response, "OK ", 3) ? 1 : 0;
}

static void usage(const char *program)
{
    fprintf(stderr,
        "usage: %s [-d device] [-p security|psychedelic] "
        "[-b brightness] [-c contrast] "
        "[-s saturation] [-S sharpness] [-H hue] [-n] "
        "[-U socket]\n"
        "       %s [-U socket] -C 'status|profile ...|set ...'\n",
        program, program);
}

int main(int argc, char **argv)
{
    OpenIMPTuningController *controller = NULL;
    OpenIMPTuningConfig config;
    const char *control_socket = DEFAULT_CONTROL_SOCKET;
    const char *client_command = NULL;
    const char *device = NULL;
    int listen_fd = -1;
    int option;
    int ret;

    memset(&config, 0, sizeof(config));
    OpenIMP_Tuning_DefaultProfile(&config.profile);
    while ((option = getopt(argc, argv, "d:p:b:c:s:S:H:nU:C:h")) != -1) {
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
        case 'U':
            control_socket = optarg;
            break;
        case 'C':
            client_command = optarg;
            break;
        default:
            goto invalid;
        }
    }
    if (optind != argc)
        goto invalid;
    if (client_command)
        return run_client(control_socket, client_command);

    config.device = device;
    signal(SIGINT, stop_handler);
    signal(SIGTERM, stop_handler);
    signal(SIGPIPE, SIG_IGN);

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
    listen_fd = make_control_socket(control_socket);
    if (listen_fd < 0) {
        fprintf(stderr, "openimp-tuningd: control socket failed: %s\n",
                strerror(-listen_fd));
        OpenIMP_Tuning_Destroy(controller);
        return 1;
    }

    while (!stop_requested) {
        struct pollfd poll_fd = { listen_fd, POLLIN, 0 };

        ret = poll(&poll_fd, 1, 1000);
        if (ret < 0 && errno != EINTR)
            break;
        if (ret > 0 && (poll_fd.revents & POLLIN)) {
            struct timeval timeout = { 2, 0 };
            char command[COMMAND_BYTES];
            char response[RESPONSE_BYTES];
            size_t used = 0;
            ssize_t got;
            int client_fd = accept(listen_fd, NULL, NULL);

            if (client_fd < 0)
                continue;
            setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO,
                       &timeout, sizeof(timeout));
            do {
                got = read(client_fd, command + used,
                           sizeof(command) - 1U - used);
                if (got > 0)
                    used += (size_t)got;
            } while (got > 0 && used < sizeof(command) - 1U &&
                     !memchr(command, '\n', used));
            if (used > 0) {
                command[used] = '\0';
                handle_command(controller, command, response,
                               sizeof(response));
                write(client_fd, response, strlen(response));
            }
            close(client_fd);
        }
    }

    close(listen_fd);
    unlink(control_socket);
    OpenIMP_Tuning_Destroy(controller);
    return 0;

invalid:
    usage(argv[0]);
    return 2;
}
