#include "hoststore.h"
#include "log.h"

#include <stdio.h>
#include <string.h>

/* Persisted under the app's USRDIR. Path matches a hypothetical
 * MLIGHT000 title ID; adjust when the package is finalized. */
#define HOSTSTORE_PATH "/dev_hdd0/game/MLIGHT000/USRDIR/hosts.dat"

static paired_host_t g_hosts[MAX_HOSTS];
static int g_count = 0;

int hoststore_load(void)
{
    FILE *f = fopen(HOSTSTORE_PATH, "rb");
    if (!f) {
        g_count = 0;
        return -1; /* first run / no store yet */
    }
    if (fread(&g_count, sizeof(g_count), 1, f) == 1 && g_count > 0)
        fread(g_hosts, sizeof(paired_host_t), g_count, f);
    fclose(f);
    return 0;
}

int hoststore_save(void)
{
    FILE *f = fopen(HOSTSTORE_PATH, "wb");
    if (!f)
        return -1;
    fwrite(&g_count, sizeof(g_count), 1, f);
    fwrite(g_hosts, sizeof(paired_host_t), g_count, f);
    fclose(f);
    return 0;
}

int hoststore_count(void)
{
    return g_count;
}

paired_host_t *hoststore_get(int index)
{
    if (index < 0 || index >= g_count)
        return NULL;
    return &g_hosts[index];
}

paired_host_t *hoststore_add(const char *ip, const char *name)
{
    paired_host_t *h = NULL;
    for (int i = 0; i < g_count; i++) {
        if (strcmp(g_hosts[i].ip, ip) == 0) {
            h = &g_hosts[i];
            break;
        }
    }
    if (!h) {
        if (g_count >= MAX_HOSTS)
            return NULL;
        h = &g_hosts[g_count++];
        memset(h, 0, sizeof(*h));
    }
    snprintf(h->ip, sizeof(h->ip), "%s", ip);
    snprintf(h->name, sizeof(h->name), "%s",
             name ? name : ip);
    return h;
}
