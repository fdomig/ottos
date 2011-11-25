/* mmchs.c
 * 
 * Copyright (c) 2011 The ottos project.
 *
 * This work is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 2.1 of the License, or (at your option)
 * any later version.
 * 
 * This work is distributed in the hope that it will be useful, but without
 * any warranty; without even the implied warranty of merchantability or
 * fitness for a particular purpose. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 *
 *  Created on: 24.11.2011
 *      Author: Franziskus Domig <fdomig@gmail.com>
 */

#include "mmchs.h"

#include <ottos/memory.h>
#include <ottos/kernel.h>

#include "../../hal/mmchs.h"

static MMCHS_CARD_INFO mmchs_card_info;

void mmchs_init() {
  // TODO
}

static int mmchs_error(MMCHS_STATUS status) {
  return (status != 0);
}

void mmchs_test() {
  mmchs_init();
}

/**
 * Internal functions
 */

static void mmchs_parse_card_cid_data(uint32_t r0, uint32_t r1, uint32_t r2,
                                      uint32_t r3) {

  mmchs_card_info.CIDData.MDT = ((r0 >> 8) & 0xFFF);
  mmchs_card_info.CIDData.PSN = (((r0 >> 24) & 0xFF) | ((r1 & 0xFFFFFF) << 8));
  mmchs_card_info.CIDData.PRV = ((r1 >> 24) & 0xFF);
  mmchs_card_info.CIDData.PNM[4] = ((r2) & 0xFF);
  mmchs_card_info.CIDData.PNM[3] = ((r2 >> 8) & 0xFF);
  mmchs_card_info.CIDData.PNM[2] = ((r2 >> 16) & 0xFF);
  mmchs_card_info.CIDData.PNM[1] = ((r2 >> 24) & 0xFF);
  mmchs_card_info.CIDData.PNM[0] = ((r3) & 0xFF);
  mmchs_card_info.CIDData.OID = ((r3 >> 8) & 0xFFFF);
  mmchs_card_info.CIDData.MID = ((r3 >> 24) & 0xFF);
}

static MMCHS_STATUS mmchs_send_cmd(uint32_t cmd, uint32_t cmd_int_en,
                                   uint32_t arg) {
  uint32_t mmc_status;
  uint32_t retry_count = 0;

  // check if command line is in use; poll until command line is available
  while ((MMIO_READ32(MMCHS_PSTATE) & DATI_MASK) == DATI_NOT_ALLOWED)
    ;

  // provide the block size
  MMIO_WRITE32(MMCHS_BLK, BLEN_512BYTES);

  // setting data timeout counter to max
  MMIO_AND_THEN_OR32(MMCHS_SYSCTL, ~DTO_MASK, DTO_VAL);

  // clear status register
  MMIO_WRITE32(MMCHS_STAT, 0xFFFFFFFF);

  // set command argument register
  MMIO_WRITE32(MMCHS_ARG, arg);

  // enable interrupt enable events to occur
  MMIO_WRITE32(MMCHS_IE, cmd_int_en);

  // send command
  MMIO_WRITE32(MMCHS_CMD, cmd);

  // check for the command status
  while (retry_count < MMCHS_MAX_RETRY_COUNT) {
    do {
      mmc_status = MMIO_READ32(MMCHS_STAT);
    } while (mmc_status == 0);

    // read status of command response
    if ((mmc_status & ERRI) != 0) {
      // perform soft reset for mmci_cmd line
      MMIO_OR32(MMCHS_SYSCTL, SRC);
      while ((MMIO_READ32(MMCHS_SYSCTL) & SRC))
        ;

      kernel_error(MMCHS_ERROR_DEVICE, "MMCHS could not send command\n");
      return MMCHS_ERROR_DEVICE;
    }

    // check if command is completed
    if ((mmc_status & CC) == CC) {
      MMIO_WRITE32(MMCHS_STAT, CC);
      break;
    }

    retry_count++;
  }

  if (retry_count == MMCHS_MAX_RETRY_COUNT) {
    kernel_error(MMCHS_STATUS_TIMEOUT, "MMCHS send command timeout\n");
    return MMCHS_STATUS_TIMEOUT;
  }

  return MMCHS_STATUS_SUCCESS;
}

static void mmchs_update_clk_freq(uint32_t new_clkd) {
  // set clock enable to 0x0 to not provide the clock to the card
  MMIO_AND32(MMCHS_SYSCTL, ~CEN);

  // set new clock frequency
  MMIO_AND_THEN_OR32(MMCHS_SYSCTL, ~CLKD_MASK, new_clkd << 6);

  // poll until internal clock is stable
  while ((MMIO_READ32(MMCHS_SYSCTL) & ICS_MASK) != ICS)
    ;

  // set clock enable to 0x1 to provide the clock to the card
  MMIO_OR32(MMCHS_SYSCTL, CEN);
}

static MMCHS_STATUS mmchs_perform_card_identification() {
  MMCHS_STATUS Status;
  uint32_t CmdArgument = 0;
  uint32_t Response = 0;
  uint32_t RetryCount = 0;
  uint8_t SDCmd8Supported = FALSE;

  //Enable interrupts.
  MMIO_WRITE32(MMCHS_IE, (BADA_EN | CERR_EN | DEB_EN | DCRC_EN | DTO_EN | CIE_EN
          | CEB_EN | CCRC_EN | CTO_EN | BRR_EN | BWR_EN | TC_EN | CC_EN));

  //Controller INIT procedure start.
  MMIO_OR32(MMCHS_CON, INIT);
  MMIO_WRITE32(MMCHS_CMD, 0x00000000);
  while (!(MMIO_READ32(MMCHS_STAT) & CC))
    ;

  //Wait for 1ms
  kernel_sleep(1);

  //Set CC bit to 0x1 to clear the flag
  MMIO_OR32(MMCHS_STAT, CC);
  //Retry INIT procedure.
  MMIO_WRITE32(MMCHS_CMD, 0x00000000);
  while (!(MMIO_READ32(MMCHS_STAT) & CC))
    ;

  //End initialization sequence
  MMIO_AND32(MMCHS_CON, ~INIT);
  MMIO_OR32(MMCHS_HCTL, (SDVS_3_0_V | DTW_1_BIT | SDBP_ON));

  //Change clock frequency to 400KHz to fit protocol
  mmchs_update_clk_freq(CLKD_400KHZ);
  MMIO_OR32(MMCHS_CON, OD);

  //Send CMD0 command.
  Status = mmchs_send_cmd(CMD0, CMD0_INT_EN, CmdArgument);

  if (mmchs_error(Status)) {
    kernel_error(MMCHS_ERROR_DEVICE, "Cmd0 fails.\n");
    return Status;
  }

  //kernel_debug(MMCHS_DEBUG_INFO, "CMD0 response: %x\n", MMIO_READ32(MMCHS_RSP10));

  //Send CMD5 command.
  Status = mmchs_send_cmd(CMD5, CMD5_INT_EN, CmdArgument);

  if (Status == MMCHS_STATUS_SUCCESS) {
    /*DEBUG(
     (EFI_D_ERROR, "CMD5 Success. SDIO card. Follow SDIO card specification.\n"));
     DEBUG((EFI_D_INFO, "CMD5 response: %x\n", MMIO_READ32(MMCHS_RSP10)));*/

    //NOTE: Returning unsupported error for now. Need to implement SDIO specification.
    return MMCHS_ERROR_UNSUPPORTED;

  } else {
    //DEBUG((EFI_D_INFO, "CMD5 fails. Not an SDIO card.\n"));
  }

  MMIO_OR32(MMCHS_SYSCTL, SRC);

  kernel_sleep(1);

  while ((MMIO_READ32(MMCHS_SYSCTL) & SRC))
    ;

  //Send CMD8 command. (New v2.00 command for Voltage check)
  //Only 2.7V - 3.6V is supported for SD2.0, only SD 2.0 card can pass.
  //MMC & SD1.1 card will fail this command.
  CmdArgument = CMD8_ARG;
  Status = mmchs_send_cmd(CMD8, CMD8_INT_EN, CmdArgument);

  if (Status == MMCHS_STATUS_SUCCESS) {
    Response = MMIO_READ32(MMCHS_RSP10);
    //DEBUG((EFI_D_INFO, "CMD8 success. CMD8 response: %x\n", Response));

    if (Response != CmdArgument) {
      return MMCHS_ERROR_DEVICE;
    }
    //DEBUG((EFI_D_INFO, "Card is SD2.0\n"));
    SDCmd8Supported = TRUE; //Supports high capacity.
  } else {
    //DEBUG((EFI_D_INFO, "CMD8 fails. Not an SD2.0 card.\n"));
  }

  MMIO_OR32(MMCHS_SYSCTL, SRC);
  kernel_sleep(1);

  while ((MMIO_READ32(MMCHS_SYSCTL) & SRC))
    ;

  //Poll till card is busy
  while (RetryCount < MMCHS_MAX_RETRY_COUNT) {
    //Send CMD55 command.
    CmdArgument = 0;
    Status = mmchs_send_cmd(CMD55, CMD55_INT_EN, CmdArgument);
    if (Status == MMCHS_STATUS_SUCCESS) {
      /*DEBUG(
       (EFI_D_INFO, "CMD55 success. CMD55 response: %x\n", MMIO_READ32(
       MMCHS_RSP10)));*/
      mmchs_card_info.CardType = SD_CARD;
    } else {
      //DEBUG((EFI_D_INFO, "CMD55 fails.\n"));
      mmchs_card_info.CardType = MMC_CARD;
    }
    //Send appropriate command for the card type which got detected.
    if (mmchs_card_info.CardType == SD_CARD) {
      CmdArgument = ((uint32_t *) &(mmchs_card_info.OCRData))[0];
      //Set HCS bit.
      if (SDCmd8Supported) {
        CmdArgument |= (uint32_t) MMCHS_HCS;
      }
      Status = mmchs_send_cmd(ACMD41, ACMD41_INT_EN, CmdArgument);
      if (mmchs_error(Status)) {
        //DEBUG((MMCHS_DEBUG_INFO, "ACMD41 fails.\n"));
        return Status;
      }
      ((uint32_t *) &(mmchs_card_info.OCRData))[0] = MMIO_READ32(MMCHS_RSP10);
      /*DEBUG(
       (EFI_D_INFO, "SD card detected. ACMD41 OCR: %x\n", ((uint32_t *) &(mmchs_card_info.OCRData))[0]));*/
    } else if (mmchs_card_info.CardType == MMC_CARD) {
      CmdArgument = 0;
      Status = mmchs_send_cmd(CMD1, CMD1_INT_EN, CmdArgument);
      if (mmchs_error(Status)) {
        //DEBUG((EFI_D_INFO, "CMD1 fails.\n"));
        return Status;
      }
      Response = MMIO_READ32(MMCHS_RSP10);
      //DEBUG((EFI_D_INFO, "MMC card detected.. CMD1 response: %x\n", Response));
      //NOTE: For now, I am skipping this since I only have an SD card.
      //Compare card OCR and host OCR (Section 22.6.1.3.2.4)
      return MMCHS_ERROR_UNSUPPORTED; //For now, MMC is not supported.
    }
    //Poll the card until it is out of its power-up sequence.
    if (mmchs_card_info.OCRData.Busy == 1) {
      if (SDCmd8Supported) {
        mmchs_card_info.CardType = SD_CARD_2;
      }
      //Card is ready. Check CCS (Card capacity status) bit (bit#30).
      //SD 2.0 standard card will response with CCS 0, SD high capacity card will respond with CCS 1.
      if (mmchs_card_info.OCRData.AccessMode & BIT1) {
        mmchs_card_info.CardType = SD_CARD_2_HIGH;
        //DEBUG((EFI_D_INFO, "High capacity card.\n"));
      } else {
        //DEBUG((EFI_D_INFO, "Standard capacity card.\n"));
      }
      break;
    }
    kernel_sleep(1);

    RetryCount++;
  }
  if (RetryCount == MMCHS_MAX_RETRY_COUNT) {
    //DEBUG((EFI_D_ERROR, "Timeout error. RetryCount: %d\n", RetryCount));
    return MMCHS_STATUS_TIMEOUT;
  }
  //Read CID data.
  CmdArgument = 0;
  Status = mmchs_send_cmd(CMD2, CMD2_INT_EN, CmdArgument);
  if (mmchs_error(Status)) {
    //DEBUG((EFI_D_ERROR, "CMD2 fails. Status: %x\n", Status));
    return Status;
  }
  /*DEBUG(
   (EFI_D_INFO, "CMD2 response: %x %x %x %x\n", MMIO_READ32(MMCHS_RSP10), MMIO_READ32(
   MMCHS_RSP32), MMIO_READ32(
   MMCHS_RSP54), MMIO_READ32(
   MMCHS_RSP76)));*/
  //Parse CID register data.
  mmchs_parse_card_cid_data(MMIO_READ32(MMCHS_RSP10), MMIO_READ32(MMCHS_RSP32),
                            MMIO_READ32(MMCHS_RSP54), MMIO_READ32(MMCHS_RSP76));
  //Read RCA
  CmdArgument = 0;
  Status = mmchs_send_cmd(CMD3, CMD3_INT_EN, CmdArgument);
  if (mmchs_error(Status)) {
    //DEBUG((EFI_D_ERROR, "CMD3 fails. Status: %x\n", Status));
    return Status;
  }
  //Set RCA for the detected card. RCA is CMD3 response.
  mmchs_card_info.RCA = (MMIO_READ32(MMCHS_RSP10) >> 16);
  //DEBUG((EFI_D_INFO, "CMD3 response: RCA %x\n", mmchs_card_info.RCA));

  //MMC Bus setting change after card identification.
  MMIO_AND32(MMCHS_CON, ~OD);
  MMIO_OR32(MMCHS_HCTL, SDVS_3_0_V);
  mmchs_update_clk_freq(CLKD_400KHZ); //Set the clock frequency to 400KHz.

  return MMCHS_STATUS_SUCCESS;
}

static void mmchs_block_information(uint32_t *BlockSize, uint32_t *NumBlocks) {
  MMCHS_CSD_SDV2 *CsdSDV2Data;
  uint32_t CardSize;

  if (mmchs_card_info.CardType == SD_CARD_2_HIGH) {
    CsdSDV2Data = (MMCHS_CSD_SDV2 *) &mmchs_card_info.CSDData;

    //Populate BlockSize.
    *BlockSize = (0x1UL << CsdSDV2Data->READ_BL_LEN);

    //Calculate Total number of blocks.
    CardSize = CsdSDV2Data->C_SIZELow16 | (CsdSDV2Data->C_SIZEHigh6 << 2);
    *NumBlocks = ((CardSize + 1) * 1024);
  } else {
    //Populate BlockSize.
    *BlockSize = (0x1UL << mmchs_card_info.CSDData.READ_BL_LEN);

    //Calculate Total number of blocks.
    CardSize = mmchs_card_info.CSDData.C_SIZELow2
        | (mmchs_card_info.CSDData.C_SIZEHigh10 << 2);
    *NumBlocks = (CardSize + 1) * (1 << (mmchs_card_info.CSDData.C_SIZE_MULT
        + 2));
  }

  //For >=2G card, BlockSize may be 1K, but the transfer size is 512 bytes.
  if (*BlockSize > 512) {
    // TODO: we do not support cards > 2gb yet
    //*NumBlocks = MultU64x32(*NumBlocks, *BlockSize/2);
    *BlockSize = 512;
  }

  //DEBUG ((EFI_D_INFO, "Card type: %x, BlockSize: %x, NumBlocks: %x\n", gCardInfo.CardType, *BlockSize, *NumBlocks));
}

static void mmchs_calculate_card_CLKD(uint32_t *ClockFrequencySelect) {
  uint8_t MaxDataTransferRate;
  uint32_t TransferRateValue = 0;
  uint32_t TimeValue = 0;
  uint32_t Frequency = 0;

  MaxDataTransferRate = mmchs_card_info.CSDData.TRAN_SPEED;

  // For SD Cards  we would need to send CMD6 to set
  // speeds abouve 25MHz. High Speed mode 50 MHz and up

  //Calculate Transfer rate unit (Bits 2:0 of TRAN_SPEED)
  switch (MaxDataTransferRate & 0x7) {
    case 0:
      TransferRateValue = 100 * 1000;
      break;

    case 1:
      TransferRateValue = 1 * 1000 * 1000;
      break;

    case 2:
      TransferRateValue = 10 * 1000 * 1000;
      break;

    case 3:
      TransferRateValue = 100 * 1000 * 1000;
      break;

    default:
      //DEBUG((EFI_D_ERROR, "Invalid parameter.\n"));
      //ASSERT(FALSE);
      kernel_error(MMCHS_ERROR_INVALID, "Invalid parameter.\n");
  }

  //Calculate Time value (Bits 6:3 of TRAN_SPEED)
  switch ((MaxDataTransferRate >> 3) & 0xF) {
    case 1:
      TimeValue = 10;
      break;

    case 2:
      TimeValue = 12;
      break;

    case 3:
      TimeValue = 13;
      break;

    case 4:
      TimeValue = 15;
      break;

    case 5:
      TimeValue = 20;
      break;

    case 6:
      TimeValue = 25;
      break;

    case 7:
      TimeValue = 30;
      break;

    case 8:
      TimeValue = 35;
      break;

    case 9:
      TimeValue = 40;
      break;

    case 10:
      TimeValue = 45;
      break;

    case 11:
      TimeValue = 50;
      break;

    case 12:
      TimeValue = 55;
      break;

    case 13:
      TimeValue = 60;
      break;

    case 14:
      TimeValue = 70;
      break;

    case 15:
      TimeValue = 80;
      break;

    default:
      //DEBUG((EFI_D_ERROR, "Invalid parameter.\n"));
      //ASSERT(FALSE);
      kernel_error(MMCHS_ERROR_INVALID, "Invalid parameter.\n");
  }

  Frequency = TransferRateValue * TimeValue / 10;

  //Calculate Clock divider value to program in MMCHS_SYSCTL[CLKD] field.
  *ClockFrequencySelect = ((MMC_REFERENCE_CLK / Frequency) + 1);

  //DEBUG ((EFI_D_INFO, "MaxDataTransferRate: 0x%x, Frequency: %d KHz, ClockFrequencySelect: %x\n", MaxDataTransferRate, Frequency/1000, *ClockFrequencySelect));
}

static void mmchs_card_configuration_data() {
  uint32_t BlockSize;
  uint32_t NumBlocks;
  uint32_t ClockFrequencySelect;

  //Calculate BlockSize and Total number of blocks in the detected card.
  mmchs_block_information(&BlockSize, &NumBlocks);

  mmchs_card_info.BlockSize = BlockSize;
  mmchs_card_info.NumBlocks = NumBlocks;

  //Calculate Card clock divider value.
  mmchs_calculate_card_CLKD(&ClockFrequencySelect);
  mmchs_card_info.ClockFrequencySelect = ClockFrequencySelect;
}

static MMCHS_STATUS mmchs_card_specific_data() {
  MMCHS_STATUS Status;
  uint32_t CmdArgument;

  //Send CMD9 to retrieve CSD.
  CmdArgument = mmchs_card_info.RCA << 16;
  Status = mmchs_send_cmd(CMD9, CMD9_INT_EN, CmdArgument);

  if (mmchs_error(Status)) {
    //DEBUG((EFI_D_ERROR, "CMD9 fails. Status: %x\n", Status));
    return Status;
  }
  //Populate 128-bit CSD register data.
  ((uint32_t *) &(mmchs_card_info.CSDData))[0] = MMIO_READ32(MMCHS_RSP10);
  ((uint32_t *) &(mmchs_card_info.CSDData))[1] = MMIO_READ32(MMCHS_RSP32);
  ((uint32_t *) &(mmchs_card_info.CSDData))[2] = MMIO_READ32(MMCHS_RSP54);
  ((uint32_t *) &(mmchs_card_info.CSDData))[3] = MMIO_READ32(MMCHS_RSP76);
  /*DEBUG(
   (EFI_D_INFO, "CMD9 response: %x %x %x %x\n", MMIO_READ32(MMCHS_RSP10), MMIO_READ32(
   MMCHS_RSP32), MMIO_READ32(MMCHS_RSP54), MMIO_READ32(MMCHS_RSP76)));*/

  //Calculate total number of blocks and max. data transfer rate supported by the detected card.
  mmchs_card_configuration_data();

  return Status;
}

MMCHS_STATUS mmchs_perform_card_configuration() {
  uint32_t CmdArgument = 0;
  MMCHS_STATUS Status;

  //Send CMD7
  CmdArgument = mmchs_card_info.RCA << 16;
  Status = mmchs_send_cmd(CMD7, CMD7_INT_EN, CmdArgument);
  if (EFI_ERROR(Status)) {
    //DEBUG((EFI_D_ERROR, "CMD7 fails. Status: %x\n", Status));
    return Status;
  }

  if ((mmchs_card_info.CardType != UNKNOWN_CARD) && (mmchs_card_info.CardType != MMC_CARD)) {
    // We could read SCR register, but SD Card Phys spec stats any SD Card shall
    // set SCR.SD_BUS_WIDTHS to support 4-bit mode, so why bother?

    // Send ACMD6 (application specific commands must be prefixed with CMD55)
    Status = mmchs_send_cmd(CMD55, CMD55_INT_EN, CmdArgument);
    if (!mmchs_error(Status)) {
      // set device into 4-bit data bus mode
      Status = mmchs_send_cmd(ACMD6, ACMD6_INT_EN, 0x2);
      if (!mmchs_error(Status)) {
        // Set host controler into 4-bit mode
        MMIO_OR32(MMCHS_HCTL, DTW_4_BIT);
        //DEBUG((EFI_D_INFO, "SD Memory Card set to 4-bit mode\n"));
      }
    }
  }

  //Send CMD16 to set the block length
  CmdArgument = mmchs_card_info.BlockSize;
  Status = mmchs_send_cmd(CMD16, CMD16_INT_EN, CmdArgument);
  if (mmchs_error(Status)) {
    //DEBUG((EFI_D_ERROR, "CMD16 fails. Status: %x\n", Status));
    return Status;
  }

  //Change MMCHS clock frequency to what detected card can support.
  mmchs_update_clk_freq(mmchs_card_info.ClockFrequencySelect);

  return MMCHS_STATUS_SUCCESS;
}