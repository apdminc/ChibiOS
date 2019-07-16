/*
    ChibiOS/RT - Copyright (C) 2006,2007,2008,2009,2010,
                 2011,2012,2013 Giovanni Di Sirio.

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
*/

/**
 * @file    sdc.c
 * @brief   SDC Driver code.
 *
 * @addtogroup SDC
 * @{
 */

#include "ch.h"
#include "hal.h"

#if HAL_USE_SDC || defined(__DOXYGEN__)

/*===========================================================================*/
/* Driver local definitions.                                                 */
/*===========================================================================*/

/*===========================================================================*/
/* Driver exported variables.                                                */
/*===========================================================================*/

/*===========================================================================*/
/* Driver local variables and types.                                         */
/*===========================================================================*/

/**
 * @brief   Virtual methods table.
 */
static const struct SDCDriverVMT sdc_vmt = {
  (bool_t (*)(void *))sdc_lld_is_card_inserted,
  (bool_t (*)(void *))sdc_lld_is_write_protected,
  (bool_t (*)(void *))sdcConnect,
  (bool_t (*)(void *))sdcDisconnect,
  (bool_t (*)(void *, uint32_t, uint8_t *, uint32_t))sdcRead,
  (bool_t (*)(void *, uint32_t, const uint8_t *, uint32_t))sdcWrite,
  (bool_t (*)(void *))sdcSync,
  (bool_t (*)(void *, BlockDeviceInfo *))sdcGetInfo
};

/*===========================================================================*/
/* Driver local functions.                                                   */
/*===========================================================================*/

/**
 * @brief   Wait for the card to complete pending operations.
 *
 * @param[in] sdcp      pointer to the @p SDCDriver object
 *
 * @return              The operation status.
 * @retval CH_SUCCESS   operation succeeded.
 * @retval CH_FAILED    operation failed.
 *
 * @notapi
 */
bool_t _sdc_wait_for_transfer_state(SDCDriver *sdcp) {
  uint32_t resp[1];

  while (TRUE) {
    if (sdc_lld_send_cmd_short_crc(sdcp, MMCSD_CMD_SEND_STATUS,
                                   sdcp->rca<<SDC_RCA_SHIFT_COUNT, resp) ||
        MMCSD_R1_ERROR(resp[0]))
      return CH_FAILED;
    switch (MMCSD_R1_STS(resp[0])) {
    case MMCSD_STS_TRAN:
      return CH_SUCCESS;
    case MMCSD_STS_DATA:
    case MMCSD_STS_RCV:
    case MMCSD_STS_PRG:
#if SDC_NICE_WAITING
      chThdSleepMilliseconds(1);
#endif
      continue;
    default:
      /* The card should have been initialized so any other state is not
         valid and is reported as an error.*/
      return CH_FAILED;
    }
  }
  /* If something going too wrong.*/
  return CH_FAILED;
}

/*===========================================================================*/
/* Driver exported functions.                                                */
/*===========================================================================*/

/**
 * @brief   SDC Driver initialization.
 * @note    This function is implicitly invoked by @p halInit(), there is
 *          no need to explicitly initialize the driver.
 *
 * @init
 */
void sdcInit(void) {

  sdc_lld_init();
}

/**
 * @brief   Initializes the standard part of a @p SDCDriver structure.
 *
 * @param[out] sdcp     pointer to the @p SDCDriver object
 *
 * @init
 */
void sdcObjectInit(SDCDriver *sdcp) {

  sdcp->vmt      = &sdc_vmt;
  sdcp->state    = BLK_STOP;
  sdcp->errors   = SDC_NO_ERROR;
  sdcp->config   = NULL;
  sdcp->capacity = 0;
  sdcp->ext_csd_revision = 0;
  sdcp->lifetime_est_a = 0;
  sdcp->lifetime_est_b = 0;
}

/**
 * @brief   Configures and activates the SDC peripheral.
 *
 * @param[in] sdcp      pointer to the @p SDCDriver object
 * @param[in] config    pointer to the @p SDCConfig object, can be @p NULL if
 *                      the driver supports a default configuration or
 *                      requires no configuration
 *
 * @api
 */
void sdcStart(SDCDriver *sdcp, const SDCConfig *config) {

  chDbgCheck(sdcp != NULL, "sdcStart");

  chSysLock();
  chDbgAssert((sdcp->state == BLK_STOP) || (sdcp->state == BLK_ACTIVE),
              "sdcStart(), #1", "invalid state");
  sdcp->config = config;
  sdc_lld_start(sdcp);
  sdcp->state = BLK_ACTIVE;
  chSysUnlock();
}

/**
 * @brief   Deactivates the SDC peripheral.
 *
 * @param[in] sdcp      pointer to the @p SDCDriver object
 *
 * @api
 */
void sdcStop(SDCDriver *sdcp) {

  chDbgCheck(sdcp != NULL, "sdcStop");

  chSysLock();
  chDbgAssert((sdcp->state == BLK_STOP) || (sdcp->state == BLK_ACTIVE),
              "sdcStop(), #1", "invalid state");
  sdc_lld_stop(sdcp);
  sdcp->state = BLK_STOP;
  chSysUnlock();
}

/**
 * @brief   Performs the initialization procedure on the inserted card.
 * @details This function should be invoked when a card is inserted and
 *          brings the driver in the @p BLK_READY state where it is possible
 *          to perform read and write operations.
 *
 * @param[in] sdcp      pointer to the @p SDCDriver object
 *
 * @return              The operation status.
 * @retval CH_SUCCESS   operation succeeded.
 * @retval CH_FAILED    operation failed.
 *
 * @api
 */
bool_t sdcConnect(SDCDriver *sdcp) {
  uint32_t resp[1];

  chDbgCheck(sdcp != NULL, "sdcConnect");
  chDbgAssert((sdcp->state == BLK_ACTIVE) || (sdcp->state == BLK_READY),
              "mmcConnect(), #1", "invalid state");

  /* Connection procedure in progress.*/
  sdcp->state = BLK_CONNECTING;

  /* Card clock initialization.*/
  sdc_lld_start_clk(sdcp);

  /* Enforces the initial card state.*/
  sdc_lld_send_cmd_none(sdcp, MMCSD_CMD_GO_IDLE_STATE, 0);

  /* V2.0 cards detection.*/
  if (!sdc_lld_send_cmd_short_crc(sdcp, MMCSD_CMD_SEND_IF_COND,
                                  MMCSD_CMD8_PATTERN, resp)) {
    sdcp->cardmode = SDC_MODE_CARDTYPE_SDV20;
    /* Voltage verification.*/
    if (((resp[0] >> 8) & 0xF) != 1)
      goto failed;
    if (sdc_lld_send_cmd_short_crc(sdcp, MMCSD_CMD_APP_CMD, 0, resp) ||
        MMCSD_R1_ERROR(resp[0]))
      goto failed;
  }
  else {
#if SDC_MMC_SUPPORT
    /* MMC or SD V1.1 detection.*/
    if (sdc_lld_send_cmd_short_crc(sdcp, MMCSD_CMD_APP_CMD, 0, resp) ||
        MMCSD_R1_ERROR(resp[0]))
      sdcp->cardmode = SDC_MODE_CARDTYPE_MMC;
    else
#endif /* SDC_MMC_SUPPORT */
      sdcp->cardmode = SDC_MODE_CARDTYPE_SDV11;


  }

#if SDC_MMC_SUPPORT
  if ((sdcp->cardmode &  SDC_MODE_CARDTYPE_MASK) == SDC_MODE_CARDTYPE_MMC) {
    uint32_t i;

    /* MMC initialization */
    i = 0;
    while (TRUE) {
      if (sdc_lld_send_cmd_short(sdcp, MMCSD_CMD_SEND_OP_COND, 0x00FF8000, resp))
        goto failed;

      if ((resp[0] & 0x80000000) != 0) {
        if (resp[0] & 0x40000000)
          sdcp->cardmode |= SDC_MODE_HIGH_CAPACITY;
        break;
      }

      if (++i >= SDC_INIT_RETRY)
        goto failed;

      chThdSleepMilliseconds(10);
    }

  }
  else
#endif /* SDC_MMC_SUPPORT */
  {
    unsigned i;
    uint32_t ocr;

    /* SD initialization.*/
    if ((sdcp->cardmode &  SDC_MODE_CARDTYPE_MASK) == SDC_MODE_CARDTYPE_SDV20)
      ocr = 0xC0100000;
    else
      ocr = 0x80100000;

    /* SD-type initialization. */
    i = 0;
    while (TRUE) {
      if (sdc_lld_send_cmd_short_crc(sdcp, MMCSD_CMD_APP_CMD, 0, resp) ||
        MMCSD_R1_ERROR(resp[0]))
        goto failed;
      if (sdc_lld_send_cmd_short(sdcp, MMCSD_ACMD_SD_SEND_OP_COND, ocr, resp))
        goto failed;
      if ((resp[0] & 0x80000000) != 0) {
        if (resp[0] & 0x40000000)
          sdcp->cardmode |= SDC_MODE_HIGH_CAPACITY;
        break;
      }
      if (++i >= SDC_INIT_RETRY)
        goto failed;
      chThdSleepMilliseconds(10);
    }
  }

  /* Reads CID.*/
  if (sdc_lld_send_cmd_long_crc(sdcp, MMCSD_CMD_ALL_SEND_CID, 0, sdcp->cid))
    goto failed;

  /* Assign relative card address */
#if SDC_RCA_SHIFT_COUNT != 0
  sdcp->rca = SDC_RELATIVE_CARD_ADDRESS;
#endif

#if SDC_RCA_SHIFT_COUNT != 0
  if (sdc_lld_send_cmd_short_crc(sdcp, MMCSD_CMD_SEND_RELATIVE_ADDR,
                                 sdcp->rca<<SDC_RCA_SHIFT_COUNT, resp) ||
      MMCSD_R1_ERROR(resp[0]))
    goto failed;
#else
  if (sdc_lld_send_cmd_short_crc(sdcp, MMCSD_CMD_SEND_RELATIVE_ADDR,
                                 0, &sdcp->rca))
    goto failed;
#endif


  /* Reads CSD.*/
  if (sdc_lld_send_cmd_long_crc(sdcp, MMCSD_CMD_SEND_CSD,
                                sdcp->rca<<SDC_RCA_SHIFT_COUNT, sdcp->csd))
    goto failed;

  /* Selects the card for operations.*/
  if (sdc_lld_send_cmd_short_crc(sdcp, MMCSD_CMD_SEL_DESEL_CARD,
                                 sdcp->rca<<SDC_RCA_SHIFT_COUNT, resp))
    goto failed;

  /* Block length fixed at 512 bytes.*/
  if (sdc_lld_send_cmd_short_crc(sdcp, MMCSD_CMD_SET_BLOCKLEN,
                                 MMCSD_BLOCK_SIZE, resp) ||
      MMCSD_R1_ERROR(resp[0]))
    goto failed;

  /* Switches to wide bus mode.*/
  switch (sdcp->cardmode & SDC_MODE_CARDTYPE_MASK) {
  case SDC_MODE_CARDTYPE_SDV11:
  case SDC_MODE_CARDTYPE_SDV20:
#if SDC_BUS_WIDTH == 4
    sdc_lld_set_bus_mode(sdcp, SDC_MODE_4BIT);
    if (sdc_lld_send_cmd_short_crc(sdcp, MMCSD_CMD_APP_CMD, sdcp->rca<<SDC_RCA_SHIFT_COUNT, resp) ||
        MMCSD_R1_ERROR(resp[0]))
      goto failed;

    if (sdc_lld_send_cmd_short_crc(sdcp, MMCSD_ACMD_SET_BUS_WIDTH, 2, resp) ||
        MMCSD_R1_ERROR(resp[0]))
      goto failed;
#endif
    break;
  case SDC_MODE_CARDTYPE_MMC:
    /* EXT_CSD
     *
     * access: write 0x03
     * index: MMCSD_EXT_CSD_BUS_WIDTH (183,0xB7)
     * value: 4bit 0x01, 8bit 0x02
     *
     * 0x03B70100 - 4bit
     * 0x03B70200 - 8bit
     */
#if SDC_BUS_WIDTH == 4
    if (sdc_lld_send_cmd_short_crc(sdcp, MMCSD_CMD_SWITCH, 0x03B70100, resp) ||
        MMCSD_R1_MMC_ERROR(resp[0]))
      goto failed;
    sdc_lld_set_bus_mode(sdcp, SDC_MODE_4BIT);
#endif
#if SDC_BUS_WIDTH == 8
    if (sdc_lld_send_cmd_short_crc(sdcp, MMCSD_CMD_SWITCH, 0x03B70200, resp) ||
        MMCSD_R1_MMC_ERROR(resp[0]))
      goto failed;
    sdc_lld_set_bus_mode(sdcp, SDC_MODE_8BIT);
#endif
    break;
  }

  chThdSleep(MS2ST(1));
  
  /* Determine capacity.*/
  if (sdcp->cardmode == (SDC_MODE_CARDTYPE_MMC | SDC_MODE_HIGH_CAPACITY)) {
    if (sdc_lld_read_ext_csd(sdcp, (uint8_t*)&sdcp->capacity, MMCSD_EXT_CSD_SEC_COUNT, sizeof(sdcp->capacity)) == CH_FAILED)
      goto failed;
  } else {
    sdcp->capacity = mmcsdGetCapacity(sdcp->csd);
  }
  if (sdcp->capacity == 0)
    goto failed;


  if (sdcp->cardmode == (SDC_MODE_CARDTYPE_MMC | SDC_MODE_HIGH_CAPACITY)) {
    chThdSleep(MS2ST(1));
    if (sdc_lld_read_ext_csd(sdcp, (uint8_t*)&sdcp->ext_csd_revision, MMCSD_EXT_CSD_REVISION, sizeof(sdcp->ext_csd_revision)) == CH_FAILED) {

    }

    chThdSleep(MS2ST(1));
    if (sdc_lld_read_ext_csd(sdcp, (uint8_t*)&sdcp->lifetime_est_a, MMCSD_EXT_CSD_DEV_LIFETIME_EST_TYP_A, sizeof(sdcp->lifetime_est_a)) == CH_FAILED) {

    }

    chThdSleep(MS2ST(1));
    if (sdc_lld_read_ext_csd(sdcp, (uint8_t*)&sdcp->lifetime_est_b, MMCSD_EXT_CSD_DEV_LIFETIME_EST_TYP_B, sizeof(sdcp->lifetime_est_b)) == CH_FAILED) {

    }
  }


  /* Switches to high speed.*/
  sdc_lld_set_data_clk(sdcp);

  /* Initialization complete.*/
  sdcp->state = BLK_READY;
  return CH_SUCCESS;

  /* Connection failed, state reset to BLK_ACTIVE.*/
failed:
  sdc_lld_stop_clk(sdcp);
  sdcp->state = BLK_ACTIVE;
  return CH_FAILED;
}

/**
 * @brief   Brings the driver in a state safe for card removal.
 *
 * @param[in] sdcp      pointer to the @p SDCDriver object
 *
 * @return              The operation status.
 * @retval CH_SUCCESS   operation succeeded.
 * @retval CH_FAILED    operation failed.
 *
 * @api
 */
bool_t sdcDisconnect(SDCDriver *sdcp) {

  chDbgCheck(sdcp != NULL, "sdcDisconnect");

  chSysLock();
  chDbgAssert((sdcp->state == BLK_ACTIVE) || (sdcp->state == BLK_READY),
              "sdcDisconnect(), #1", "invalid state");
  if (sdcp->state == BLK_ACTIVE) {
    chSysUnlock();
    return CH_SUCCESS;
  }
  sdcp->state = BLK_DISCONNECTING;
  chSysUnlock();

  /* Waits for eventual pending operations completion.*/
  if (_sdc_wait_for_transfer_state(sdcp)) {
    sdc_lld_stop_clk(sdcp);
    sdcp->state = BLK_ACTIVE;
    return CH_FAILED;
  }

  /* Card clock stopped.*/
  sdc_lld_stop_clk(sdcp);
  sdcp->state = BLK_ACTIVE;
  return CH_SUCCESS;
}

/**
 * @brief   Reads one or more blocks.
 * @pre     The driver must be in the @p BLK_READY state after a successful
 *          sdcConnect() invocation.
 *
 * @param[in] sdcp      pointer to the @p SDCDriver object
 * @param[in] startblk  first block to read
 * @param[out] buf      pointer to the read buffer
 * @param[in] n         number of blocks to read
 *
 * @return              The operation status.
 * @retval CH_SUCCESS   operation succeeded.
 * @retval CH_FAILED    operation failed.
 *
 * @api
 */
bool_t sdcRead(SDCDriver *sdcp, uint32_t startblk,
               uint8_t *buf, uint32_t n) {
  bool_t status;

  chDbgCheck((sdcp != NULL) && (buf != NULL) && (n > 0), "sdcRead");
  chDbgAssert(sdcp->state == BLK_READY, "sdcRead(), #1", "invalid state");

  if ((startblk + n - 1) > sdcp->capacity){
    sdcp->errors |= SDC_OVERFLOW_ERROR;
    return CH_FAILED;
  }

  /* Read operation in progress.*/
  sdcp->state = BLK_READING;

  status = sdc_lld_read(sdcp, startblk, buf, n);

  /* Read operation finished.*/
  sdcp->state = BLK_READY;
  return status;
}

/**
 * @brief   Writes one or more blocks.
 * @pre     The driver must be in the @p BLK_READY state after a successful
 *          sdcConnect() invocation.
 *
 * @param[in] sdcp      pointer to the @p SDCDriver object
 * @param[in] startblk  first block to write
 * @param[out] buf      pointer to the write buffer
 * @param[in] n         number of blocks to write
 *
 * @return              The operation status.
 * @retval CH_SUCCESS   operation succeeded.
 * @retval CH_FAILED    operation failed.
 *
 * @api
 */
bool_t sdcWrite(SDCDriver *sdcp, uint32_t startblk,
                const uint8_t *buf, uint32_t n) {
  bool_t status;

  chDbgCheck((sdcp != NULL) && (buf != NULL) && (n > 0), "sdcWrite");
  chDbgAssert(sdcp->state == BLK_READY, "sdcWrite(), #1", "invalid state");

  if ((startblk + n - 1) > sdcp->capacity){
    sdcp->errors |= SDC_OVERFLOW_ERROR;
    return CH_FAILED;
  }

  /* Write operation in progress.*/
  sdcp->state = BLK_WRITING;

  status = sdc_lld_write(sdcp, startblk, buf, n);

  /* Write operation finished.*/
  sdcp->state = BLK_READY;
  return status;
}

/**
 * @brief   Returns the errors mask associated to the previous operation.
 *
 * @param[in] sdcp      pointer to the @p SDCDriver object
 * @return              The errors mask.
 *
 * @api
 */
sdcflags_t sdcGetAndClearErrors(SDCDriver *sdcp) {
  sdcflags_t flags;

  chDbgCheck(sdcp != NULL, "sdcGetAndClearErrors");
  chDbgAssert(sdcp->state == BLK_READY,
              "sdcGetAndClearErrors(), #1", "invalid state");

  chSysLock();
  flags = sdcp->errors;
  sdcp->errors = SDC_NO_ERROR;
  chSysUnlock();
  return flags;
}

/**
 * @brief   Waits for card idle condition.
 *
 * @param[in] sdcp      pointer to the @p SDCDriver object
 *
 * @return              The operation status.
 * @retval CH_SUCCESS   the operation succeeded.
 * @retval CH_FAILED    the operation failed.
 *
 * @api
 */
bool_t sdcSync(SDCDriver *sdcp) {
  bool_t result;

  chDbgCheck(sdcp != NULL, "sdcSync");

  if (sdcp->state != BLK_READY)
    return CH_FAILED;

  /* Synchronization operation in progress.*/
  sdcp->state = BLK_SYNCING;

  result = sdc_lld_sync(sdcp);

  /* Synchronization operation finished.*/
  sdcp->state = BLK_READY;
  return result;
}

/**
 * @brief   Returns the media info.
 *
 * @param[in] sdcp      pointer to the @p SDCDriver object
 * @param[out] bdip     pointer to a @p BlockDeviceInfo structure
 *
 * @return              The operation status.
 * @retval CH_SUCCESS   the operation succeeded.
 * @retval CH_FAILED    the operation failed.
 *
 * @api
 */
bool_t sdcGetInfo(SDCDriver *sdcp, BlockDeviceInfo *bdip) {

  chDbgCheck((sdcp != NULL) && (bdip != NULL), "sdcGetInfo");

  if (sdcp->state != BLK_READY)
    return CH_FAILED;

  bdip->blk_num = sdcp->capacity;
  bdip->blk_size = MMCSD_BLOCK_SIZE;

  return CH_SUCCESS;
}


/**
 * @brief   Erases the supplied blocks.
 *
 * @param[in] sdcp      pointer to the @p SDCDriver object
 * @param[in] startblk  starting block number
 * @param[in] endblk    ending block number
 *
 * @return              The operation status.
 * @retval CH_SUCCESS   the operation succeeded.
 * @retval CH_FAILED    the operation failed.
 *
 * @api
 */
bool_t sdcErase(SDCDriver *sdcp, uint32_t startblk, uint32_t endblk) {
  uint32_t resp[1];

  chDbgCheck((sdcp != NULL), "sdcErase");
  chDbgAssert(sdcp->state == BLK_READY, "sdcErase(), #1", "invalid state");

  /* Erase operation in progress.*/
  sdcp->state = BLK_WRITING;

  /* Handling command differences between HC and normal cards.*/
  if (!(sdcp->cardmode & SDC_MODE_HIGH_CAPACITY)) {
    startblk *= MMCSD_BLOCK_SIZE;
    endblk *= MMCSD_BLOCK_SIZE;
  }

  _sdc_wait_for_transfer_state(sdcp);

  if ((sdc_lld_send_cmd_short_crc(sdcp, MMCSD_CMD_ERASE_RW_BLK_START,
                                  startblk, resp) != CH_SUCCESS) ||
      MMCSD_R1_ERROR(resp[0]))
    goto failed;

  if ((sdc_lld_send_cmd_short_crc(sdcp, MMCSD_CMD_ERASE_RW_BLK_END,
                                  endblk, resp) != CH_SUCCESS) ||
      MMCSD_R1_ERROR(resp[0]))
    goto failed;

  if ((sdc_lld_send_cmd_short_crc(sdcp, MMCSD_CMD_ERASE,
                                  0, resp) != CH_SUCCESS) ||
      MMCSD_R1_ERROR(resp[0]))
    goto failed;

  /* Quick sleep to allow it to transition to programming or receiving state */
  /* TODO: ??????????????????????????? */

  /* Wait for it to return to transfer state to indicate it has finished erasing */
  _sdc_wait_for_transfer_state(sdcp);

  sdcp->state = BLK_READY;
  return CH_SUCCESS;

failed:
  sdcp->state = BLK_READY;
  return CH_FAILED;
}

#endif /* HAL_USE_SDC */

/** @} */
