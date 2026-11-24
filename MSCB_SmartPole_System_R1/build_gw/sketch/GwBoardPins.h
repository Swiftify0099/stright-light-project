#line 1 "C:\\Users\\panka\\OneDrive\\Desktop\\PS\\MSCB_SmartPole_System_R1\\Gateway_ESP32S3\\GwBoardPins.h"
/**
 * @file GwBoardPins.h
 * @brief MSCB Smart Pole Gateway - ESP32-S3 Hardware Pinout & Peripheral Assignment
 * @version 1.0.0
 * 
 * Target Board: ESP32-S3-WROOM-1 / WROOM-2 N16R8 (DOIT Dual Type-C Development Board)
 * 
 * CRITICAL HARDWARE VERIFICATION & SILICON CONSTRAINTS:
 * 1. OCTAL FLASH & PSRAM FORBIDDEN RANGE:
 *    - GPIO 26 to 32: Connected internally to 16MB SPI Flash.
 *    - GPIO 33 to 37: Connected internally to 8MB Octal SPI (OPI) PSRAM.
 *    -> ACCESSING GPIO 26-37 WILL CORRUPT MEMORY OR CAUSE INSTANT SYSTEM CRASH!
 * 2. USB CONSTRAINTS:
 *    - GPIO 19: Native USB D-
 *    - GPIO 20: Native USB D+
 *    -> Reserved for Native USB-CDC / JTAG interface.
 * 3. STRAPPING PINS:
 *    - GPIO 0: Boot mode select (Must not be driven LOW during boot unless flashing).
 *    - GPIO 45: VDD_SPI LDO voltage select (Must remain LOW for 3.3V flash).
 *    - GPIO 46: ROM boot log select.
 * 4. UART0 INTERFACE:
 *    - GPIO 43: TXD0 (Connected to onboard Type-C UART bridge)
 *    - GPIO 44: RXD0 (Connected to onboard Type-C UART bridge)
 */

#ifndef GW_BOARD_PINS_H
#define GW_BOARD_PINS_H

#include <Arduino.h>

// =====================================================================
//  LORA SX1278 RA-02 SPI INTERFACE (FSPI / SPI2 NATIVE IOMUX)
// =====================================================================
// Hardware native IOMUX pins provide maximum signal integrity and high frequency
#define GW_LORA_SCK_PIN       12  // FSPI Clock (Native IOMUX)
#define GW_LORA_MISO_PIN      13  // FSPI MISO / Master In (Native IOMUX)
#define GW_LORA_MOSI_PIN      11  // FSPI MOSI / Master Out (Native IOMUX)
#define GW_LORA_NSS_PIN       10  // FSPI Chip Select (Active LOW)
#define GW_LORA_RST_PIN       14  // Hardware Reset (Active LOW output)
#define GW_LORA_DIO0_PIN       2  // Packet Ready Interrupt (RTC_GPIO2, interrupt capable)

// Optional / Auxiliary LoRa pins
#define GW_LORA_DIO1_PIN       1  // Auxiliary CAD / Timeout Interrupt
#define GW_LORA_BUSY_PIN      -1  // Not present on SX1278 (used on SX1262)

// =====================================================================
//  ONBOARD STATUS INDICATORS
// =====================================================================
// DOIT ESP32-S3 DevKit typically features an onboard RGB LED or standard status LED
#define GW_STATUS_LED_PIN     21  // Diagnostic Heartbeat LED (GPIO 21 or WS2812 on GPIO 48)
#define GW_RGB_WS2812_PIN     48  // Onboard WS2812 RGB LED Data pin

// =====================================================================
//  COMPILE-TIME HARDWARE CONFLICT AUDIT
// =====================================================================
#if defined(CONFIG_IDF_TARGET_ESP32S3)
  // 1. Ensure no forbidden Octal PSRAM/Flash pins (26 to 37) are accidentally configured
  #define IS_OCTAL_MEM_PIN(p) ((p) >= 26 && (p) <= 37)
  static_assert(!IS_OCTAL_MEM_PIN(GW_LORA_SCK_PIN),  "GW_LORA_SCK_PIN collides with ESP32-S3 Flash/PSRAM (26-37)!");
  static_assert(!IS_OCTAL_MEM_PIN(GW_LORA_MISO_PIN), "GW_LORA_MISO_PIN collides with ESP32-S3 Flash/PSRAM (26-37)!");
  static_assert(!IS_OCTAL_MEM_PIN(GW_LORA_MOSI_PIN), "GW_LORA_MOSI_PIN collides with ESP32-S3 Flash/PSRAM (26-37)!");
  static_assert(!IS_OCTAL_MEM_PIN(GW_LORA_NSS_PIN),  "GW_LORA_NSS_PIN collides with ESP32-S3 Flash/PSRAM (26-37)!");
  static_assert(!IS_OCTAL_MEM_PIN(GW_LORA_RST_PIN),  "GW_LORA_RST_PIN collides with ESP32-S3 Flash/PSRAM (26-37)!");
  static_assert(!IS_OCTAL_MEM_PIN(GW_LORA_DIO0_PIN), "GW_LORA_DIO0_PIN collides with ESP32-S3 Flash/PSRAM (26-37)!");
  static_assert(!IS_OCTAL_MEM_PIN(GW_STATUS_LED_PIN),"GW_STATUS_LED_PIN collides with ESP32-S3 Flash/PSRAM (26-37)!");

  // 2. Ensure native USB pins (19, 20) are never used
  #define IS_USB_PIN(p) ((p) == 19 || (p) == 20)
  static_assert(!IS_USB_PIN(GW_LORA_SCK_PIN),  "LoRa SCK collides with Native USB PHY (GPIO 19/20)!");
  static_assert(!IS_USB_PIN(GW_LORA_MISO_PIN), "LoRa MISO collides with Native USB PHY (GPIO 19/20)!");
  static_assert(!IS_USB_PIN(GW_LORA_MOSI_PIN), "LoRa MOSI collides with Native USB PHY (GPIO 19/20)!");
  static_assert(!IS_USB_PIN(GW_LORA_NSS_PIN),  "LoRa NSS collides with Native USB PHY (GPIO 19/20)!");
  static_assert(!IS_USB_PIN(GW_LORA_RST_PIN),  "LoRa RST collides with Native USB PHY (GPIO 19/20)!");
  static_assert(!IS_USB_PIN(GW_LORA_DIO0_PIN), "LoRa DIO0 collides with Native USB PHY (GPIO 19/20)!");

  // 3. Ensure UART0 programming port pins (43, 44) are preserved
  #define IS_UART0_PIN(p) ((p) == 43 || (p) == 44)
  static_assert(!IS_UART0_PIN(GW_LORA_SCK_PIN),  "LoRa SCK collides with UART0 TX/RX!");
  static_assert(!IS_UART0_PIN(GW_LORA_MISO_PIN), "LoRa MISO collides with UART0 TX/RX!");
  static_assert(!IS_UART0_PIN(GW_LORA_MOSI_PIN), "LoRa MOSI collides with UART0 TX/RX!");
  static_assert(!IS_UART0_PIN(GW_LORA_NSS_PIN),  "LoRa NSS collides with UART0 TX/RX!");
  static_assert(!IS_UART0_PIN(GW_LORA_RST_PIN),  "LoRa RST collides with UART0 TX/RX!");
  static_assert(!IS_UART0_PIN(GW_LORA_DIO0_PIN), "LoRa DIO0 collides with UART0 TX/RX!");

  // 4. Ensure strapping pins (0, 45, 46) are not driven by outputs
  #define IS_STRAP_PIN(p) ((p) == 0 || (p) == 45 || (p) == 46)
  static_assert(!IS_STRAP_PIN(GW_LORA_SCK_PIN), "LoRa SCK collides with strapping pin!");
  static_assert(!IS_STRAP_PIN(GW_LORA_MOSI_PIN),"LoRa MOSI collides with strapping pin!");
  static_assert(!IS_STRAP_PIN(GW_LORA_NSS_PIN), "LoRa NSS collides with strapping pin!");
  static_assert(!IS_STRAP_PIN(GW_LORA_RST_PIN), "LoRa RST collides with strapping pin!");

  // 5. Pairwise Distinct Peripheral Pin Checks
  static_assert(GW_LORA_SCK_PIN != GW_LORA_MISO_PIN, "LoRa SCK and MISO share pin!");
  static_assert(GW_LORA_SCK_PIN != GW_LORA_MOSI_PIN, "LoRa SCK and MOSI share pin!");
  static_assert(GW_LORA_SCK_PIN != GW_LORA_NSS_PIN,  "LoRa SCK and NSS share pin!");
  static_assert(GW_LORA_SCK_PIN != GW_LORA_RST_PIN,  "LoRa SCK and RST share pin!");
  static_assert(GW_LORA_SCK_PIN != GW_LORA_DIO0_PIN, "LoRa SCK and DIO0 share pin!");
  static_assert(GW_LORA_MISO_PIN != GW_LORA_MOSI_PIN,"LoRa MISO and MOSI share pin!");
  static_assert(GW_LORA_MISO_PIN != GW_LORA_NSS_PIN, "LoRa MISO and NSS share pin!");
  static_assert(GW_LORA_MISO_PIN != GW_LORA_RST_PIN, "LoRa MISO and RST share pin!");
  static_assert(GW_LORA_MISO_PIN != GW_LORA_DIO0_PIN,"LoRa MISO and DIO0 share pin!");
  static_assert(GW_LORA_MOSI_PIN != GW_LORA_NSS_PIN, "LoRa MOSI and NSS share pin!");
  static_assert(GW_LORA_MOSI_PIN != GW_LORA_RST_PIN, "LoRa MOSI and RST share pin!");
  static_assert(GW_LORA_MOSI_PIN != GW_LORA_DIO0_PIN,"LoRa MOSI and DIO0 share pin!");
  static_assert(GW_LORA_NSS_PIN != GW_LORA_RST_PIN,  "LoRa NSS and RST share pin!");
  static_assert(GW_LORA_NSS_PIN != GW_LORA_DIO0_PIN, "LoRa NSS and DIO0 share pin!");
  static_assert(GW_LORA_RST_PIN != GW_LORA_DIO0_PIN, "LoRa RST and DIO0 share pin!");
  static_assert(GW_STATUS_LED_PIN != GW_LORA_NSS_PIN,"Status LED collides with LoRa NSS!");
  static_assert(GW_STATUS_LED_PIN != GW_LORA_RST_PIN,"Status LED collides with LoRa RST!");
  static_assert(GW_RGB_WS2812_PIN != GW_STATUS_LED_PIN, "RGB WS2812 collides with Status LED!");
#endif

#endif // GW_BOARD_PINS_H
