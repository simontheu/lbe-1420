/*
 * lbe-142x.c - Leo Bodnar LBE-1420 GPS locked clock source config (Linux, hidraw)
 *
 * Original:       Copyright (c) simontheu - https://github.com/simontheu/lbe-1420
 * Modificaciones: Copyright (c) 2026 Manuel - EA7JRS
 *
 * Changes vs. upstream (EA7JRS):
 *  - Fixed stack buffer overflow: HIDIOCGFEATURE(256)/HIDIOCGRAWNAME(256) into a 60-byte buffer
 *  - Set-feature reports are zeroed before use (upstream sent stale bytes from previous reads)
 *  - --f1 now always saves to flash, even if the running frequency already matches
 *    (upstream skipped it, so "--f1_nosave X" followed by "--f1 X" never persisted X)
 *  - Fixed wrong status message for "output 1 disabled" (printed "antenna short circuit")
 *  - current frequency no longer used uninitialised when the status read fails
 *  - Frequency range per datasheet v1.1 (1 Hz .. 1.4 GHz), accepts suffixes: 10M, 25k, 1.4G
 *    (the HID report carries an integer in Hz, so resolution here is 1 Hz)
 *  - --out1 validated (0/1); invalid/unknown options are errors, not silently ignored
 *  - Automatic device detection via sysfs (no need to hunt for /dev/hidrawN), --list, --serial
 *  - Read-back verification after setting the frequency
 *  - Meaningful exit codes, --check for monitoring scripts (exit 2 if not locked / alarm)
 */

#define _GNU_SOURCE

#include <linux/types.h>
#include <linux/input.h>
#include <linux/hidraw.h>

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdint.h>
#include <getopt.h>
#include <time.h>

#define PROG_VERSION "2.0-ea7jrs"

#define VID_LBE         0x1dd2
#define PID_LBE_1420    0x2443

#define FREQ_MIN        1UL
#define FREQ_MAX        1400000000UL  /* datasheet v1.1: 1 Hz .. 1.4 GHz (upstream: 1.1 GHz) */

/* Status bits (feature report 9, byte 1) */
#define GPS_LOCK_BIT    0x01
#define ANT_OK_BIT      0x04
#define OUT1_EN_BIT     0x10
#define STATUS_OK_MASK  (GPS_LOCK_BIT | ANT_OK_BIT | OUT1_EN_BIT)

/* Feature report IDs */
#define RPT_EN_OUT1         0x01
#define RPT_BLINK_OUT1      0x02
#define RPT_SET_F1_NOSAVE   0x03
#define RPT_SET_F1          0x04
#define RPT_STATUS          0x09

#define FEATURE_LEN     60      /* report length used by upstream (tested on hardware) */
#define NAME_LEN        256

#ifndef SYSFS_HIDRAW
#define SYSFS_HIDRAW    "/sys/class/hidraw"
#endif
#ifndef DEV_DIR
#define DEV_DIR         "/dev"
#endif

#ifndef HIDIOCSFEATURE
#warning Please have your distro update the userspace kernel headers
#define HIDIOCSFEATURE(len)    _IOC(_IOC_WRITE|_IOC_READ, 'H', 0x06, len)
#define HIDIOCGFEATURE(len)    _IOC(_IOC_WRITE|_IOC_READ, 'H', 0x07, len)
#endif
#ifndef HIDIOCGRAWNAME
#define HIDIOCGRAWNAME(len)    _IOC(_IOC_READ, 'H', 0x04, len)
#endif

enum {
    RC_OK        = 0,
    RC_ERROR     = 1,   /* usage / I/O error */
    RC_ALARM     = 2,   /* --check: no GPS lock, antenna fault or output disabled */
    RC_NODEV     = 3,   /* no LBE-1420 found / not accessible */
    RC_VERIFY    = 4,   /* frequency read-back did not match */
};

#define MAX_DEVS 16

struct lbe_dev {
    char path[256];     /* /dev/hidrawN */
    char name[128];     /* HID_NAME */
    char serial[64];    /* HID_UNIQ */
};

struct lbe_status {
    int      valid;
    uint8_t  flags;
    uint32_t freq;
};

struct options {
    const char *device;
    const char *serial;
    int      list;
    int      check;
    int      set_freq;
    uint32_t freq;
    int      save;
    int      out1;      /* -1 = unchanged */
    int      blink;
};

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static void sleep_ms(long ms)
{
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR)
        ;
}

/*
 * Parse a frequency in Hz. Accepts plain integers ("10000000") or a
 * decimal number with k/M/G suffix and optional "Hz" ("10M", "2.048MHz").
 * Resolution is 1 Hz; non-integer results are rejected.
 */
static int parse_freq(const char *s, uint32_t *out)
{
    char *end;
    double mult = 1.0, v;

    if (!s || !*s || *s == '-' || *s == '+')
        return -1;
    errno = 0;
    v = strtod(s, &end);
    if (errno || end == s)
        return -1;

    switch (*end) {
    case 'k': case 'K': mult = 1e3; end++; break;
    case 'm': case 'M': mult = 1e6; end++; break;
    case 'g': case 'G': mult = 1e9; end++; break;
    default: break;
    }
    if (strcasecmp(end, "hz") == 0)
        end += 2;
    if (*end != '\0')
        return -1;

    v *= mult;
    if (!(v >= (double)FREQ_MIN - 1e-3 && v <= (double)FREQ_MAX + 1e-3))
        return -2;

    double r = (double)(uint64_t)(v + 0.5);
    if (v - r > 1e-3 || r - v > 1e-3)
        return -3;      /* sub-Hz value */

    *out = (uint32_t)r;
    return 0;
}

static void fprint_freq(FILE *fp, uint32_t f)
{
    fprintf(fp, "%u Hz", f);
    if (f >= 1000000)
        fprintf(fp, " (%.6f MHz)", f / 1e6);
    else if (f >= 1000)
        fprintf(fp, " (%.3f kHz)", f / 1e3);
}
#define print_freq(f) fprint_freq(stdout, (f))

/* ------------------------------------------------------------------ */
/* Device discovery via sysfs                                          */
/* ------------------------------------------------------------------ */

static void strip_nl(char *s)
{
    s[strcspn(s, "\r\n")] = '\0';
}

static int read_uevent(const char *hidraw, struct lbe_dev *d)
{
    char p[512], line[256];
    unsigned bus = 0, vid = 0, pid = 0;
    int is_lbe = 0;
    FILE *f;

    snprintf(p, sizeof p, "%s/%s/device/uevent", SYSFS_HIDRAW, hidraw);
    f = fopen(p, "r");
    if (!f)
        return 0;

    memset(d, 0, sizeof *d);
    while (fgets(line, sizeof line, f)) {
        strip_nl(line);
        if (sscanf(line, "HID_ID=%x:%x:%x", &bus, &vid, &pid) == 3)
            is_lbe = (vid == VID_LBE && pid == PID_LBE_1420);
        else if (strncmp(line, "HID_NAME=", 9) == 0)
            snprintf(d->name, sizeof d->name, "%.*s", (int)sizeof d->name - 1, line + 9);
        else if (strncmp(line, "HID_UNIQ=", 9) == 0)
            snprintf(d->serial, sizeof d->serial, "%.*s", (int)sizeof d->serial - 1, line + 9);
    }
    fclose(f);

    if (is_lbe)
        snprintf(d->path, sizeof d->path, "%s/%.40s", DEV_DIR, hidraw);
    return is_lbe;
}

static int cmp_dev(const void *a, const void *b)
{
    return strverscmp(((const struct lbe_dev *)a)->path, ((const struct lbe_dev *)b)->path);
}

static int find_devices(struct lbe_dev *devs, int max)
{
    DIR *dir = opendir(SYSFS_HIDRAW);
    struct dirent *e;
    int n = 0;

    if (!dir)
        return 0;
    while ((e = readdir(dir)) != NULL && n < max) {
        if (strncmp(e->d_name, "hidraw", 6) != 0)
            continue;
        if (read_uevent(e->d_name, &devs[n]))
            n++;
    }
    closedir(dir);
    qsort(devs, (size_t)n, sizeof *devs, cmp_dev);
    return n;
}

/* ------------------------------------------------------------------ */
/* HID access                                                          */
/* ------------------------------------------------------------------ */

static int open_device(const char *path)
{
    struct hidraw_devinfo info;
    char name[NAME_LEN];
    int fd, res;

    fd = open(path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "Unable to open %s: %s\n", path, strerror(errno));
        if (errno == EACCES)
            fprintf(stderr, "  Hint: run as root or install the udev rule (make install-udev)\n");
        return -1;
    }

    memset(&info, 0, sizeof info);
    if (ioctl(fd, HIDIOCGRAWINFO, &info) < 0) {
        fprintf(stderr, "HIDIOCGRAWINFO on %s: %s\n", path, strerror(errno));
        close(fd);
        return -1;
    }
    if ((uint16_t)info.vendor != VID_LBE || (uint16_t)info.product != PID_LBE_1420) {
        fprintf(stderr, "%s is not an LBE-1420 (vendor 0x%04hx, product 0x%04hx)\n",
                path, (unsigned short)info.vendor, (unsigned short)info.product);
        close(fd);
        return -1;
    }

    memset(name, 0, sizeof name);
    res = ioctl(fd, HIDIOCGRAWNAME(sizeof name - 1), name);
    printf("Device:   %s\n", path);
    if (res > 0)
        printf("Name:     %s\n", name);
    return fd;
}

static int read_status(int fd, struct lbe_status *st)
{
    uint8_t buf[FEATURE_LEN];
    int res;

    memset(st, 0, sizeof *st);
    memset(buf, 0, sizeof buf);
    buf[0] = RPT_STATUS;

    res = ioctl(fd, HIDIOCGFEATURE(sizeof buf), buf);
    if (res < 0) {
        fprintf(stderr, "HIDIOCGFEATURE: %s\n", strerror(errno));
        return -1;
    }
    if (res < 6) {
        fprintf(stderr, "Status report too short (%d bytes)\n", res);
        return -1;
    }
    /* Layout as observed upstream: buf[0]==0, buf[1]=flags, buf[2..5]=freq LE */
    if (buf[0] != 0) {
        fprintf(stderr, "Unexpected status report header 0x%02x\n", buf[0]);
        return -1;
    }

    st->flags = buf[1];
    st->freq  = (uint32_t)buf[2]
              | (uint32_t)buf[3] << 8
              | (uint32_t)buf[4] << 16
              | (uint32_t)buf[5] << 24;
    st->valid = 1;
    return 0;
}

static int set_feature(int fd, uint8_t id, const uint8_t *data, size_t len)
{
    uint8_t buf[FEATURE_LEN];

    if (len > sizeof buf - 1)
        return -1;
    memset(buf, 0, sizeof buf);
    buf[0] = id;
    if (len)
        memcpy(buf + 1, data, len);

    if (ioctl(fd, HIDIOCSFEATURE(sizeof buf), buf) < 0) {
        fprintf(stderr, "HIDIOCSFEATURE (report %u): %s\n", id, strerror(errno));
        return -1;
    }
    return 0;
}

static void print_status(const struct lbe_status *st)
{
    printf("\nStatus:\n");
    if (!st->valid) {
        printf("  (unavailable)\n");
        return;
    }
    printf("  GPS lock:   %s\n", (st->flags & GPS_LOCK_BIT) ? "yes" : "NO");
    printf("  Antenna:    %s\n", (st->flags & ANT_OK_BIT)   ? "OK"  : "FAULT (short circuit)");
    printf("  Output 1:   %s\n", (st->flags & OUT1_EN_BIT)  ? "enabled" : "DISABLED");
    printf("  Frequency:  ");
    print_freq(st->freq);
    printf("\n  Raw flags:  0x%02x\n", st->flags);
    printf("  Overall:    %s\n",
           (st->flags & STATUS_OK_MASK) == STATUS_OK_MASK ? "Device OK" : "ATTENTION");
}

/* ------------------------------------------------------------------ */
/* Command line                                                        */
/* ------------------------------------------------------------------ */

static void usage(const char *prog)
{
    printf("Leo Bodnar LBE-1420 GPS locked clock source config v" PROG_VERSION "\n\n"
           "Usage: %s [/dev/hidrawN] [options]\n\n"
           "  -d, --device PATH     hidraw device (default: auto-detect)\n"
           "  -S, --serial SN       pick the unit with this serial (HID_UNIQ)\n"
           "  -l, --list            list connected LBE-1420 units and exit\n"
           "      --f1 FREQ         set output 1 frequency and save to flash\n"
           "      --f1_nosave FREQ  set output 1 frequency without saving\n"
           "                        FREQ: 1 .. 1400000000 Hz, e.g. 10000000, 10M, 25k, 1.4G\n"
           "      --out1 0|1        disable/enable output 1\n"
           "      --blink1          blink output 1 LED for 3 seconds\n"
           "  -c, --check           exit 2 if not locked, antenna fault or output disabled\n"
           "  -h, --help            this help\n"
           "  -V, --version         show version\n\n"
           "Exit codes: 0 OK, 1 error, 2 alarm (--check), 3 no device, 4 verify failed\n",
           prog);
}

static int parse_args(int argc, char **argv, struct options *o)
{
    enum { OPT_F1 = 1000, OPT_F1_NOSAVE, OPT_OUT1, OPT_BLINK1 };
    static const struct option longopts[] = {
        { "device",    required_argument, 0, 'd' },
        { "serial",    required_argument, 0, 'S' },
        { "list",      no_argument,       0, 'l' },
        { "check",     no_argument,       0, 'c' },
        { "help",      no_argument,       0, 'h' },
        { "version",   no_argument,       0, 'V' },
        { "f1",        required_argument, 0, OPT_F1 },
        { "f1_nosave", required_argument, 0, OPT_F1_NOSAVE },
        { "out1",      required_argument, 0, OPT_OUT1 },
        { "blink1",    no_argument,       0, OPT_BLINK1 },
        { 0, 0, 0, 0 }
    };
    int c, r;

    memset(o, 0, sizeof *o);
    o->out1 = -1;

    while ((c = getopt_long(argc, argv, "d:S:lchV", longopts, NULL)) != -1) {
        switch (c) {
        case 'd': o->device = optarg; break;
        case 'S': o->serial = optarg; break;
        case 'l': o->list = 1; break;
        case 'c': o->check = 1; break;
        case 'h': usage(argv[0]); exit(RC_OK);
        case 'V': printf(PROG_VERSION "\n"); exit(RC_OK);
        case OPT_F1:
        case OPT_F1_NOSAVE:
            r = parse_freq(optarg, &o->freq);
            if (r) {
                fprintf(stderr, "Invalid frequency '%s': %s\n", optarg,
                        r == -2 ? "out of range (1 Hz .. 1.4 GHz)" :
                        r == -3 ? "resolution is 1 Hz" : "not a number");
                return -1;
            }
            o->set_freq = 1;
            o->save = (c == OPT_F1);
            break;
        case OPT_OUT1:
            if (strcmp(optarg, "0") && strcmp(optarg, "1")) {
                fprintf(stderr, "--out1 must be 0 or 1\n");
                return -1;
            }
            o->out1 = optarg[0] - '0';
            break;
        case OPT_BLINK1: o->blink = 1; break;
        default:
            return -1;  /* getopt already printed the error */
        }
    }

    /* Backwards compatible: first positional argument is the device */
    if (optind < argc) {
        if (o->device) {
            fprintf(stderr, "Device given twice\n");
            return -1;
        }
        o->device = argv[optind++];
    }
    if (optind < argc) {
        fprintf(stderr, "Unexpected argument '%s'\n", argv[optind]);
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    struct options o;
    struct lbe_dev devs[MAX_DEVS];
    struct lbe_status st;
    const char *path;
    int ndev, i, fd, rc = RC_OK, changed = 0;

    setvbuf(stdout, NULL, _IOLBF, 0);

    if (parse_args(argc, argv, &o) < 0) {
        fprintf(stderr, "Try '%s --help'\n", argv[0]);
        return RC_ERROR;
    }

    ndev = find_devices(devs, MAX_DEVS);

    if (o.list) {
        if (ndev == 0) {
            printf("No LBE-1420 found\n");
            return RC_NODEV;
        }
        for (i = 0; i < ndev; i++)
            printf("%-16s  SN %-14s  %s\n", devs[i].path,
                   devs[i].serial[0] ? devs[i].serial : "?", devs[i].name);
        return RC_OK;
    }

    path = o.device;
    if (!path) {
        if (o.serial) {
            for (i = 0; i < ndev; i++)
                if (strcasecmp(devs[i].serial, o.serial) == 0)
                    path = devs[i].path;
            if (!path) {
                fprintf(stderr, "No LBE-1420 with serial %s\n", o.serial);
                return RC_NODEV;
            }
        } else if (ndev == 1) {
            path = devs[0].path;
        } else if (ndev == 0) {
            fprintf(stderr, "No LBE-1420 found (is it plugged in?)\n");
            return RC_NODEV;
        } else {
            fprintf(stderr, "Several LBE-1420 found, choose one with --device or --serial:\n");
            for (i = 0; i < ndev; i++)
                fprintf(stderr, "  %s  SN %s\n", devs[i].path, devs[i].serial);
            return RC_ERROR;
        }
    }

    fd = open_device(path);
    if (fd < 0)
        return RC_NODEV;

    read_status(fd, &st);
    print_status(&st);

    if (o.set_freq || o.out1 != -1 || o.blink)
        printf("\nChanges:\n");

    if (o.set_freq) {
        /* Skip only a no-save change to the frequency already running.
         * A save (--f1) is always sent so the value is written to flash. */
        if (!o.save && st.valid && st.freq == o.freq) {
            printf("  Frequency already ");
            print_freq(o.freq);
            printf(", nothing to do\n");
        } else {
            uint8_t d[4] = {
                (uint8_t)(o.freq),       (uint8_t)(o.freq >> 8),
                (uint8_t)(o.freq >> 16), (uint8_t)(o.freq >> 24)
            };
            printf("  Setting frequency: ");
            print_freq(o.freq);
            printf(o.save ? " (saved to flash)\n" : " (not saved)\n");

            if (set_feature(fd, o.save ? RPT_SET_F1 : RPT_SET_F1_NOSAVE, d, sizeof d) < 0) {
                rc = RC_ERROR;
            } else {
                changed = 1;
                /* Read back to confirm (allow the synthesizer some time) */
                int ok = 0;
                for (i = 0; i < 10 && !ok; i++) {
                    sleep_ms(150);
                    ok = read_status(fd, &st) == 0 && st.freq == o.freq;
                }
                if (ok) {
                    printf("  Verified\n");
                } else {
                    fprintf(stderr, "  WARNING: device reports ");
                    if (st.valid) fprint_freq(stderr, st.freq); else fprintf(stderr, "no status");
                    fprintf(stderr, " after setting\n");
                    rc = RC_VERIFY;
                }
            }
        }
    }

    if (o.out1 != -1) {
        uint8_t d = (uint8_t)o.out1;
        printf("  Output 1: %s\n", o.out1 ? "enable" : "disable");
        if (set_feature(fd, RPT_EN_OUT1, &d, 1) < 0)
            rc = RC_ERROR;
        else
            changed = 1;
    }

    if (o.blink) {
        printf("  Blink output 1 LED\n");
        if (set_feature(fd, RPT_BLINK_OUT1, NULL, 0) < 0)
            rc = RC_ERROR;
        else
            changed = 1;
    }

    if (changed && o.out1 != -1) {
        sleep_ms(150);
        read_status(fd, &st);
    }

    if (o.check && rc == RC_OK) {
        if (!st.valid || (st.flags & STATUS_OK_MASK) != STATUS_OK_MASK)
            rc = RC_ALARM;
    }

    close(fd);
    return rc;
}
