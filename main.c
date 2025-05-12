#include <LPC17xx.h>
#include "cmsis_os.h"
#include "Board_LED.h"
#include "Board_GLCD.h"
#include <stdio.h>

#define MENU_ITEMS 8
#define White 0xFFFFFF
#define Black 0x000000
#define Blue 0x0000FF
#define Red 0xFF0000
#define Green 0x00FF00

extern GLCD_FONT GLCD_Font_16x24;

osThreadId tid_joystick;
osThreadId tid_screen;
osMutexId glcd_mutex;

typedef struct {
    const char* name;
    void (*action)(void);
} MenuItem;

MenuItem menuItems[MENU_ITEMS];
volatile int currentSelection = 0;

// ? Joystick pin definitions
#define JOYSTICK_UP_PIN     (1 << 23)  // P1.23
#define JOYSTICK_DOWN_PIN   (1 << 25)  // P1.25
#define JOYSTICK_CENTER_PIN (1 << 20)  // P1.20

#define DEBOUNCE_TIME 100
int lastJoystickState = 0;
uint32_t lastActionTime = 0;



// === Initialize Joystick GPIOs ===
void GPIO_Joystick_Init(void) {
    LPC_PINCON->PINSEL3 &= ~((3 << 14) | (3 << 18) | (3 << 8));   // Set P1.23, P1.25, P1.20 to GPIO
    LPC_PINCON->PINMODE3 &= ~((3 << 14) | (3 << 18) | (3 << 8));  // Enable pull-up
    LPC_GPIO1->FIODIR &= ~(JOYSTICK_UP_PIN | JOYSTICK_DOWN_PIN | JOYSTICK_CENTER_PIN); // Input
}

uint32_t readJoystick(void) {
    uint32_t state = 0;
    if (!(LPC_GPIO1->FIOPIN & JOYSTICK_UP_PIN))     state |= 0x01;
    if (!(LPC_GPIO1->FIOPIN & JOYSTICK_DOWN_PIN))   state |= 0x02;
    if (!(LPC_GPIO1->FIOPIN & JOYSTICK_CENTER_PIN)) state |= 0x04;
    return state;
}

void showActionMessage(const char* message, uint32_t color) {
    osMutexWait(glcd_mutex, osWaitForever);
    GLCD_SetBackgroundColor(White);
    GLCD_SetForegroundColor(White);
    GLCD_DrawString(0, 1 * 24, "                        ");
    GLCD_SetForegroundColor(color);
    GLCD_DrawString(0, 1 * 24, message);
    osMutexRelease(glcd_mutex);
}

void startPump(void)           { showActionMessage("Pump Started", Green); LED_On(0); }
void stopPump(void)            { showActionMessage("Pump Stopped", Red);   LED_Off(0); }
void setPumpSpeed(void)        { showActionMessage("Set Pump Speed", Blue); }
void setPumpDuration(void)     { showActionMessage("Set Pump Duration", Blue); }
void viewStatus(void)          { showActionMessage("Viewing Status", Blue); }
void setAlarmThreshold(void)   { showActionMessage("Set Alarm Threshold", Blue); }
void maintenanceMode(void)     { showActionMessage("Maintenance Mode", Blue); }
void exitMenu(void)            { showActionMessage("Exiting Menu", Red); }

void initializeMenu(void) {
    menuItems[0] = (MenuItem){"Start Pump", startPump};
    menuItems[1] = (MenuItem){"Stop Pump", stopPump};
    menuItems[2] = (MenuItem){"Pump Speed", setPumpSpeed};
    menuItems[3] = (MenuItem){"PumpDuration", setPumpDuration};
    menuItems[4] = (MenuItem){"View Status", viewStatus};
    menuItems[5] = (MenuItem){"A_Threshold", setAlarmThreshold};
    menuItems[6] = (MenuItem){"Monitor Mode", maintenanceMode};
    menuItems[7] = (MenuItem){"Exit Menu", exitMenu};
}

// === Initial Full Menu Draw ===
void displayMenu(void) {
    osMutexWait(glcd_mutex, osWaitForever);
    GLCD_SetBackgroundColor(White);
    for (int i = 0; i < MENU_ITEMS; i++) {
        if (i == currentSelection) {
            GLCD_SetBackgroundColor(Blue);
            GLCD_SetForegroundColor(White);
        } else {
            GLCD_SetBackgroundColor(White);
            GLCD_SetForegroundColor(Black);
        }
        char displayText[35];
        sprintf(displayText, "%s                    ", menuItems[i].name);
        GLCD_DrawString(0, (i + 2) * 24, displayText);
    }
    osMutexRelease(glcd_mutex);
}

// === Fast Partial Redraw ===
void displayMenuPartial(int oldSelection, int newSelection) {
    osMutexWait(glcd_mutex, osWaitForever);

    // Redraw old item (remove highlight)
    GLCD_SetBackgroundColor(White);
    GLCD_SetForegroundColor(Black);
    char displayText[30];
    sprintf(displayText, "%s                    ", menuItems[oldSelection].name);
    GLCD_DrawString(0, (oldSelection + 2) * 24, displayText);

    // Draw new item (highlighted)
    GLCD_SetBackgroundColor(Blue);
    GLCD_SetForegroundColor(White);
    sprintf(displayText, "%s                    ", menuItems[newSelection].name);
    GLCD_DrawString(0, (newSelection + 2) * 24, displayText);

    osMutexRelease(glcd_mutex);
}

void joystickControlThread(void const *argument) {
    while (1) {
        uint32_t joystickState = readJoystick();
        uint32_t currentTime = osKernelSysTick();

        // LED Debug (optional)
        if (joystickState & 0x01) LED_On(1); else LED_Off(1); // UP
        if (joystickState & 0x02) LED_On(2); else LED_Off(2); // DOWN
        if (joystickState & 0x04) LED_On(3); else LED_Off(3); // CENTER

        if (currentTime - lastActionTime >= DEBOUNCE_TIME) {
            if ((joystickState & 0x01) && !(lastJoystickState & 0x01)) {
                int oldSel = currentSelection;
                currentSelection = (currentSelection - 1 + MENU_ITEMS) % MENU_ITEMS;
                displayMenuPartial(oldSel, currentSelection);
                lastActionTime = currentTime;
            }
            if ((joystickState & 0x02) && !(lastJoystickState & 0x02)) {
                int oldSel = currentSelection;
                currentSelection = (currentSelection + 1) % MENU_ITEMS;
                displayMenuPartial(oldSel, currentSelection);
                lastActionTime = currentTime;
            }
            if ((joystickState & 0x04) && !(lastJoystickState & 0x04)) {
                menuItems[currentSelection].action();
                lastActionTime = currentTime;
            }
        }

        lastJoystickState = joystickState;
        osDelay(10);
    }
}

void menuDisplayThread(void const *argument) {
    displayMenu();
    while (1) {
        osDelay(1000);  // Could update time, status, etc.
    }
}

osThreadDef(joystickControlThread, osPriorityNormal, 1, 0);
osThreadDef(menuDisplayThread, osPriorityNormal, 1, 0);
osMutexDef(glcd_mutex);

int main(void) {
    SystemInit();
    LED_Initialize();
    GLCD_Initialize();
    GPIO_Joystick_Init();

    GLCD_SetFont(&GLCD_Font_16x24);
    GLCD_SetBackgroundColor(White);
    GLCD_ClearScreen();

    GLCD_SetForegroundColor(Blue);
    GLCD_DrawString(0, 0, "Pump Control System");
    GLCD_SetForegroundColor(Black);
    GLCD_DrawString(0, 1 * 24, "Use joystick to navigate");

    initializeMenu();
    displayMenu();

    osKernelInitialize();
    glcd_mutex = osMutexCreate(osMutex(glcd_mutex));
    tid_joystick = osThreadCreate(osThread(joystickControlThread), NULL);
    tid_screen = osThreadCreate(osThread(menuDisplayThread), NULL);
    osKernelStart();

    while (1);
}
