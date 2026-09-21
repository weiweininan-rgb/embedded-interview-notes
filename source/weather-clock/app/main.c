#include "page.h"
#include "app.h"

extern void board_lowlevel_init(void);
extern void board_init(void);

int main(void)
{
    board_lowlevel_init();
    board_init();
    
    welcome_page_display();
    
    wifi_init();
    wifi_page_display();
    wifi_wait_connect();
    
    main_loop_init();
    main_page_display();
    
    while (1)
    {
        main_loop();
    }
}
