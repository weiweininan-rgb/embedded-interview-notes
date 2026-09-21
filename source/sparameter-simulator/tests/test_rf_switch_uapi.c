#include "rf_switch_uapi.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    /* Keep the future kernel/user ABI explicit and stable. */
    assert(RF_SWITCH_PATH_SAFE == 0);
    assert(RF_SWITCH_PATH_OPEN == 1);
    assert(RF_SWITCH_PATH_SHORT == 2);
    assert(RF_SWITCH_PATH_LOAD == 3);
    assert(RF_SWITCH_PATH_THRU == 4);
    assert(RF_SWITCH_PATH_DUT == 5);
    assert(_IOC_TYPE(RF_SWITCH_SET_PATH) == RF_SWITCH_IOC_MAGIC);
    assert(_IOC_DIR(RF_SWITCH_SET_PATH) == _IOC_WRITE);
    assert(_IOC_SIZE(RF_SWITCH_SET_PATH) == sizeof(__u32));
    assert(_IOC_TYPE(RF_SWITCH_GET_PATH) == RF_SWITCH_IOC_MAGIC);
    assert(_IOC_DIR(RF_SWITCH_GET_PATH) == _IOC_READ);
    assert(_IOC_SIZE(RF_SWITCH_GET_PATH) == sizeof(__u32));

    puts("rf switch UAPI: PASS");
    return 0;
}
