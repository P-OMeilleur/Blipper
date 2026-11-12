/* nRF52840 + S340 v7.x : application starts right after the SoftDevice.
 * RAM start is conservative (BLE is never enabled, ANT uses an app buffer). */
SEARCH_DIR(.)
GROUP(-lgcc -lc -lnosys)

MEMORY
{
  FLASH (rx)  : ORIGIN = 0x31000,    LENGTH = 0xCF000
  RAM   (rwx) : ORIGIN = 0x20006000, LENGTH = 0x3A000
}

SECTIONS
{
}

INCLUDE "nrf_common.ld"
