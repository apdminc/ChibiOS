[#ftl]
[#--
    ChibiOS/RT - Copyright (C) 2006,2007,2008,2009,2010,
                 2011,2012 Giovanni Di Sirio.

    This file is part of ChibiOS/RT.

    ChibiOS/RT is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 3 of the License, or
    (at your option) any later version.

    ChibiOS/RT is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
  --]
[@pp.dropOutputFile /]
[#import "/@lib/libutils.ftl" as utils /]
[#import "/@lib/liblicense.ftl" as license /]
[@pp.changeOutputFile name="board.h" /]
/*
[@license.EmitLicenseAsText /]
*/

#ifndef _BOARD_H_
#define _BOARD_H_

/*
 * Setup for ${doc1.board.@name[0]} board.
 */

/*
 * Board identifier.
 */
#define BOARD_${doc1.board.@BoardID[0]}
#define BOARD_NAME              "${doc1.board.@name[0]}"

[#if doc1.board.@BoardPHYID[0]??]
/*
 * Ethernet PHY type.
 */
#define BOARD_PHY_ID            ${doc1.board.@BoardPHYID[0]}
[#if doc1.board.@BoardPHYType[0]?string == "RMII"]
#define BOARD_PHY_RMII
[/#if]
[/#if]

/*
 * Board oscillators-related settings.
[#if doc1.board.@LSEFrequency[0]?number == 0]
 * NOTE: LSE not fitted.
[/#if]
[#if doc1.board.@HSEFrequency[0]?number == 0]
 * NOTE: HSE not fitted.
[/#if]
 */
#if !defined(STM32_LSECLK)
#define STM32_LSECLK            ${doc1.board.@LSEFrequency[0]}
#endif

#if !defined(STM32_HSECLK)
#define STM32_HSECLK            ${doc1.board.@HSEFrequency[0]}
#endif

[#if doc1.board.@HSEBypass[0]?string == "true"]
#define STM32_HSE_BYPASS
[/#if]

/*
 * Board voltages.
 * Required for performance limits calculation.
 */
#define STM32_VDD               ${doc1.board.@VDD[0]}

/*
 * MCU type as defined in the ST header file stm32f4xx.h.
 */
#define STM32F4XX

/*
 * IO pins assignments.
 */

#if !defined(_FROM_ASM_)
#ifdef __cplusplus
extern "C" {
#endif
  void boardInit(void);
#ifdef __cplusplus
}
#endif
#endif /* _FROM_ASM_ */

#endif /* _BOARD_H_ */