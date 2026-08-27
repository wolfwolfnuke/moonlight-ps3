#include <ppu-lv2.h>
#include <sys/process.h>
#include <stdlib.h>

#include "common/log.h"
#include "ui/ui.h"

int main(void)
{
    LOGI("moonlight-ps3 starting\n");
    ui_run();
    LOGI("moonlight-ps3 done\n");
    fflush(NULL);
    return 0;
}
