/*
 * osd_attach_test - IMP_OSD_AttachToGroup of T21/T20 (gap work 2026-10-10).
 * The OEM wraps system_attach(): src->to becomes src->from->to, any failure
 * restores the original bind and returns -1.  Builds the real
 * src/t31/openimp_t31_services.c against a fake bind table.
 */
#include "../../src/t31/openimp_t31_services.c"

/* the error log of the pool request is not under test (level 0: silent) */
int openimp_log_level;
void imp_log_emit(int prio, const char *tag, const char *fmt, ...)
{
    (void)prio; (void)tag; (void)fmt;
}

static int failures;
#define CHECK(c) do { if (!(c)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

/* fake IMP_System bind table */
static struct { IMPCell s, d; int used; } binds[8];
static int fail_bind_nth;           /* make the nth IMP_System_Bind fail */
static int bind_calls;

static int same(const IMPCell *a, const IMPCell *b)
{
    return a->deviceID == b->deviceID && a->groupID == b->groupID &&
           a->outputID == b->outputID;
}

int IMP_System_Bind(IMPCell *s, IMPCell *d)
{
    int i;

    if (fail_bind_nth && ++bind_calls == fail_bind_nth)
        return -1;
    for (i = 0; i < 8; i++)
        if (!binds[i].used) {
            binds[i].s = *s; binds[i].d = *d; binds[i].used = 1;
            return 0;
        }
    return -1;
}

int IMP_System_UnBind(IMPCell *s, IMPCell *d)
{
    int i;

    for (i = 0; i < 8; i++)
        if (binds[i].used && same(&binds[i].s, s) && same(&binds[i].d, d)) {
            binds[i].used = 0;
            return 0;
        }
    return -1;
}

int IMP_System_GetBindbyDest(IMPCell *d, IMPCell *s)
{
    int i;

    for (i = 0; i < 8; i++)
        if (binds[i].used && same(&binds[i].d, d)) {
            *s = binds[i].s;
            return 0;
        }
    return -1;
}

static int linked(const IMPCell *s, const IMPCell *d)
{
    int i;

    for (i = 0; i < 8; i++)
        if (binds[i].used && same(&binds[i].s, s) && same(&binds[i].d, d))
            return 1;
    return 0;
}

static int nbinds(void)
{
    int i, n = 0;

    for (i = 0; i < 8; i++)
        n += binds[i].used;
    return n;
}

int main(void)
{
    IMPCell fs = { DEV_ID_FS, 0, 0 }, osd = { DEV_ID_OSD, 0, 0 },
            enc = { DEV_ID_ENC, 0, 0 };
    int n;

    CHECK(IMP_OSD_AttachToGroup(NULL, &enc) == -1);
    CHECK(IMP_OSD_AttachToGroup(&osd, NULL) == -1);
    CHECK(IMP_OSD_AttachToGroup(&osd, &enc) == -1);       /* enc not bound */

    CHECK(IMP_System_Bind(&fs, &enc) == 0);
    CHECK(IMP_OSD_AttachToGroup(&osd, &enc) == 0);
    CHECK(nbinds() == 2 && linked(&fs, &osd) && linked(&osd, &enc));

    /* the first or the second bind of the attach fails: fs->enc is back */
    for (n = 1; n <= 2; n++) {
        memset(binds, 0, sizeof(binds));
        fail_bind_nth = 0;
        CHECK(IMP_System_Bind(&fs, &enc) == 0);
        bind_calls = 0;
        fail_bind_nth = n;
        CHECK(IMP_OSD_AttachToGroup(&osd, &enc) == -1);
        fail_bind_nth = 0;
        CHECK(nbinds() == 1 && linked(&fs, &enc));
    }
    /* IMP_OSD_MemPoolRequest: once per group until the OSD exit (stock
     * T31 DestroyGroup does not clear it), IMP_System_Exit resets it */
    CHECK(IMP_OSD_MemPoolRequest(1, 4096, "pool") == 0);
    CHECK(IMP_OSD_MemPoolRequest(1, 4096, "pool") == -1);
    CHECK(IMP_OSD_MemPoolRequest(5, 4096, "pool") == -1);
    CHECK(IMP_OSD_MemPoolRequest(2, 0, "pool") == -1);
    openimp_t31_osd_pool_reset();
    CHECK(IMP_OSD_MemPoolRequest(1, 4096, "pool") == 0);
    if (failures) {
        fprintf(stderr, "osd_attach_test: %d failure(s)\n", failures);
        return 1;
    }
    puts("osd_attach_test: PASS");
    return 0;
}
