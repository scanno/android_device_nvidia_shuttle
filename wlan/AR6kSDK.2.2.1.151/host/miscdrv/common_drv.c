/*------------------------------------------------------------------------------ */
/* <copyright file="common_drv.c" company="Atheros"> */
/*    Copyright (c) 2004-2008 Atheros Corporation.  All rights reserved. */
/*  */
/* This program is free software; you can redistribute it and/or modify */
/* it under the terms of the GNU General Public License version 2 as */
/* published by the Free Software Foundation; */
/* */
/* Software distributed under the License is distributed on an "AS */
/* IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or */
/* implied. See the License for the specific language governing */
/* rights and limitations under the License. */
/* */
/* */
/*------------------------------------------------------------------------------ */
/*============================================================================== */
/* Author(s): ="Atheros" */
/*============================================================================== */
#include "a_config.h"
#include "athdefs.h"
#include "a_types.h"

#include "AR6002/hw/mbox_host_reg.h"
#include "AR6002/hw/apb_map.h"
#include "AR6002/hw/si_reg.h"
#include "AR6002/hw/gpio_reg.h"
#include "AR6002/hw/rtc_reg.h"
#include "AR6002/hw/vmc_reg.h"
#include "AR6002/hw/mbox_reg.h"

#include "targaddrs.h"
#include "a_osapi.h"
#include "hif.h"
#include "htc_api.h"
#include "wmi.h"
#include "bmi.h"
#include "bmi_msg.h"
#include "common_drv.h"
#include "a_debug.h"

#define HOST_INTEREST_ITEM_ADDRESS(target, item) \
        (((target) == TARGET_TYPE_AR6001) ? AR6001_HOST_INTEREST_ITEM_ADDRESS(item) : \
        (((target) == TARGET_TYPE_AR6002) ? AR6002_HOST_INTEREST_ITEM_ADDRESS(item) : \
        (((target) == TARGET_TYPE_AR6003) ? AR6003_HOST_INTEREST_ITEM_ADDRESS(item) : 0)))


#define AR6001_LOCAL_COUNT_ADDRESS 0x0c014080
#define AR6002_LOCAL_COUNT_ADDRESS 0x00018080
#define AR6003_LOCAL_COUNT_ADDRESS 0x00018080

/* Compile the 4BYTE version of the window register setup routine,
 * This mitigates host interconnect issues with non-4byte aligned bus requests, some
 * interconnects use bus adapters that impose strict limitations.
 * Since diag window access is not intended for performance critical operations, the 4byte mode should
 * be satisfactory even though it generates 4X the bus activity. */

#ifdef USE_4BYTE_REGISTER_ACCESS

    /* set the window address register (using 4-byte register access ). */
A_STATUS ar6000_SetAddressWindowRegister(HIF_DEVICE *hifDevice, A_UINT32 RegisterAddr, A_UINT32 Address)
{
    A_STATUS status;
    A_UINT8 addrValue[4];
    A_INT32 i;

        /* write bytes 1,2,3 of the register to set the upper address bytes, the LSB is written
         * last to initiate the access cycle */

    for (i = 1; i <= 3; i++) {
            /* fill the buffer with the address byte value we want to hit 4 times*/
        addrValue[0] = ((A_UINT8 *)&Address)[i];
        addrValue[1] = addrValue[0];
        addrValue[2] = addrValue[0];
        addrValue[3] = addrValue[0];

            /* hit each byte of the register address with a 4-byte write operation to the same address,
             * this is a harmless operation */
        status = HIFReadWrite(hifDevice,
                              RegisterAddr+i,
                              addrValue,
                              4,
                              HIF_WR_SYNC_BYTE_FIX,
                              NULL);
        if (status != A_OK) {
            break;
        }
    }

    if (status != A_OK) {
        AR_DEBUG_PRINTF(ATH_LOG_ERR, ("Cannot write initial bytes of 0x%x to window reg: 0x%X \n",
             RegisterAddr, Address));
        return status;
    }

        /* write the address register again, this time write the whole 4-byte value.
         * The effect here is that the LSB write causes the cycle to start, the extra
         * 3 byte write to bytes 1,2,3 has no effect since we are writing the same values again */
    status = HIFReadWrite(hifDevice,
                          RegisterAddr,
                          (A_UCHAR *)(&Address),
                          4,
                          HIF_WR_SYNC_BYTE_INC,
                          NULL);

    if (status != A_OK) {
        AR_DEBUG_PRINTF(ATH_LOG_ERR, ("Cannot write 0x%x to window reg: 0x%X \n",
            RegisterAddr, Address));
        return status;
    }

    return A_OK;



}


#else

    /* set the window address register */
A_STATUS ar6000_SetAddressWindowRegister(HIF_DEVICE *hifDevice, A_UINT32 RegisterAddr, A_UINT32 Address)
{
    A_STATUS status;

        /* write bytes 1,2,3 of the register to set the upper address bytes, the LSB is written
         * last to initiate the access cycle */
    status = HIFReadWrite(hifDevice,
                          RegisterAddr+1,  /* write upper 3 bytes */
                          ((A_UCHAR *)(&Address))+1,
                          sizeof(A_UINT32)-1,
                          HIF_WR_SYNC_BYTE_INC,
                          NULL);

    if (status != A_OK) {
        AR_DEBUG_PRINTF(ATH_LOG_ERR, ("Cannot write initial bytes of 0x%x to window reg: 0x%X \n",
             RegisterAddr, Address));
        return status;
    }

        /* write the LSB of the register, this initiates the operation */
    status = HIFReadWrite(hifDevice,
                          RegisterAddr,
                          (A_UCHAR *)(&Address),
                          sizeof(A_UINT8),
                          HIF_WR_SYNC_BYTE_INC,
                          NULL);

    if (status != A_OK) {
        AR_DEBUG_PRINTF(ATH_LOG_ERR, ("Cannot write 0x%x to window reg: 0x%X \n",
            RegisterAddr, Address));
        return status;
    }

    return A_OK;
}

#endif

/*
 * Read from the AR6000 through its diagnostic window.
 * No cooperation from the Target is required for this.
 */
A_STATUS
ar6000_ReadRegDiag(HIF_DEVICE *hifDevice, A_UINT32 *address, A_UINT32 *data)
{
    A_STATUS status;

        /* set window register to start read cycle */
    status = ar6000_SetAddressWindowRegister(hifDevice,
                                             WINDOW_READ_ADDR_ADDRESS,
                                             *address);

    if (status != A_OK) {
        return status;
    }

        /* read the data */
    status = HIFReadWrite(hifDevice,
                          WINDOW_DATA_ADDRESS,
                          (A_UCHAR *)data,
                          sizeof(A_UINT32),
                          HIF_RD_SYNC_BYTE_INC,
                          NULL);
    if (status != A_OK) {
        AR_DEBUG_PRINTF(ATH_LOG_ERR, ("Cannot read from WINDOW_DATA_ADDRESS\n"));
        return status;
    }

    return status;
}


/*
 * Write to the AR6000 through its diagnostic window.
 * No cooperation from the Target is required for this.
 */
A_STATUS
ar6000_WriteRegDiag(HIF_DEVICE *hifDevice, A_UINT32 *address, A_UINT32 *data)
{
    A_STATUS status;

        /* set write data */
    status = HIFReadWrite(hifDevice,
                          WINDOW_DATA_ADDRESS,
                          (A_UCHAR *)data,
                          sizeof(A_UINT32),
                          HIF_WR_SYNC_BYTE_INC,
                          NULL);
    if (status != A_OK) {
        AR_DEBUG_PRINTF(ATH_LOG_ERR, ("Cannot write 0x%x to WINDOW_DATA_ADDRESS\n", *data));
        return status;
    }

        /* set window register, which starts the write cycle */
    return ar6000_SetAddressWindowRegister(hifDevice,
                                           WINDOW_WRITE_ADDR_ADDRESS,
                                           *address);
    }

A_STATUS
ar6000_ReadDataDiag(HIF_DEVICE *hifDevice, A_UINT32 address,
                    A_UCHAR *data, A_UINT32 length)
{
    A_UINT32 count;
    A_STATUS status = A_OK;

    for (count = 0; count < length; count += 4, address += 4) {
        if ((status = ar6000_ReadRegDiag(hifDevice, &address,
                                         (A_UINT32 *)&data[count])) != A_OK)
        {
            break;
        }
    }

    return status;
}

A_STATUS
ar6000_WriteDataDiag(HIF_DEVICE *hifDevice, A_UINT32 address,
                    A_UCHAR *data, A_UINT32 length)
{
    A_UINT32 count;
    A_STATUS status = A_OK;

    for (count = 0; count < length; count += 4, address += 4) {
        if ((status = ar6000_WriteRegDiag(hifDevice, &address,
                                         (A_UINT32 *)&data[count])) != A_OK)
        {
            break;
        }
    }

    return status;
}

#if 0
static A_STATUS
_do_write_diag(HIF_DEVICE *hifDevice, A_UINT32 addr, A_UINT32 value)
{
    A_STATUS status;

    status = ar6000_WriteRegDiag(hifDevice, &addr, &value);
    if (status != A_OK)
    {
        AR_DEBUG_PRINTF(ATH_LOG_ERR, ("Cannot force Target to execute ROM!\n"));
    }

    return status;
}
#endif


/*
 * Delay up to wait_msecs millisecs to allow Target to enter BMI phase,
 * which is a good sign that it's alive and well.  This is used after
 * explicitly forcing the Target to reset.
 *
 * The wait_msecs time should be sufficiently long to cover any reasonable
 * boot-time delay.  For instance, AR6001 firmware allow one second for a
 * low frequency crystal to settle before it calibrates the refclk frequency.
 *
 * TBD: Might want to add special handling for AR6K_OPTION_BMI_DISABLE.
 */
static A_STATUS
_delay_until_target_alive(HIF_DEVICE *hifDevice, A_INT32 wait_msecs, A_UINT32 TargetType)
{
    A_INT32 actual_wait;
    A_INT32 i;
    A_UINT32 address;

    actual_wait = 0;

    /* Hardcode the address of LOCAL_COUNT_ADDRESS based on the target type */
    if (TargetType == TARGET_TYPE_AR6001) {
        address = AR6001_LOCAL_COUNT_ADDRESS;
    } else if (TargetType == TARGET_TYPE_AR6002) {
       address = AR6002_LOCAL_COUNT_ADDRESS;
    } else if (TargetType == TARGET_TYPE_AR6003) {
       address = AR6003_LOCAL_COUNT_ADDRESS;
    } else {
       A_ASSERT(0);
    }
    address += 0x10;
    for (i=0; actual_wait < wait_msecs; i++) {
        A_UINT32 data;

        A_MDELAY(100);
        actual_wait += 100;

        data = 0;
        if (ar6000_ReadRegDiag(hifDevice, &address, &data) != A_OK) {
            return A_ERROR;
        }

        if (data != 0) {
            /* No need to wait longer -- we have a BMI credit */
            return A_OK;
        }
    }
    return A_ERROR; /* timed out */
}

#define AR6001_RESET_CONTROL_ADDRESS 0x0C000000
#define AR6002_RESET_CONTROL_ADDRESS 0x00004000
#define AR6003_RESET_CONTROL_ADDRESS 0x00004000
/* reset device */
A_STATUS ar6000_reset_device(HIF_DEVICE *hifDevice, A_UINT32 TargetType, A_BOOL waitForCompletion)
{
    A_STATUS status = A_OK;
    A_UINT32 address;
    A_UINT32 data;

    do {

        /* address = RESET_CONTROL_ADDRESS; */
        data = RESET_CONTROL_COLD_RST_MASK;

          /* Hardcode the address of RESET_CONTROL_ADDRESS based on the target type */
        if (TargetType == TARGET_TYPE_AR6001) {
            address = AR6001_RESET_CONTROL_ADDRESS;
        } else if (TargetType == TARGET_TYPE_AR6002) {
            address = AR6002_RESET_CONTROL_ADDRESS;
        } else if (TargetType == TARGET_TYPE_AR6003) {
            address = AR6003_RESET_CONTROL_ADDRESS;
        } else {
            A_ASSERT(0);
        }


        status = ar6000_WriteRegDiag(hifDevice, &address, &data);

        if (A_FAILED(status)) {
            break;
        }

        if (!waitForCompletion) {
            break;
        }


        /* Up to 2 second delay to allow things to settle down */
        (void)_delay_until_target_alive(hifDevice, 2000, TargetType);

        /*
         * Read back the RESET CAUSE register to ensure that the cold reset
         * went through.
         */

        /* address = RESET_CAUSE_ADDRESS; */
        /* Hardcode the address of RESET_CAUSE_ADDRESS based on the target type */
        if (TargetType == TARGET_TYPE_AR6001) {
            address = 0x0C0000CC;
        } else if (TargetType == TARGET_TYPE_AR6002) {
            address = 0x000040C0;
        } else if (TargetType == TARGET_TYPE_AR6003) {
            address = 0x000040C0;
        } else {
            A_ASSERT(0);
        }

        data = 0;
        status = ar6000_ReadRegDiag(hifDevice, &address, &data);

        if (A_FAILED(status)) {
            break;
        }

        AR_DEBUG_PRINTF(ATH_LOG_ERR, ("Reset Cause readback: 0x%X \n",data));
        data &= RESET_CAUSE_LAST_MASK;
        if (data != 2) {
            AR_DEBUG_PRINTF(ATH_LOG_ERR, ("Unable to cold reset the target \n"));
        }

    } while (FALSE);

    if (A_FAILED(status)) {
        AR_DEBUG_PRINTF(ATH_LOG_ERR, ("Failed to reset target \n"));
    }

    return A_OK;
}

#define REG_DUMP_COUNT_AR6001   38  /* WORDs, derived from AR600x_regdump.h */
#define REG_DUMP_COUNT_AR6002   32
#define REG_DUMP_COUNT_AR6003   32
#define REGISTER_DUMP_LEN_MAX   38
#if REG_DUMP_COUNT_AR6001 > REGISTER_DUMP_LEN_MAX
#error "REG_DUMP_COUNT_AR6001 too large"
#endif
#if REG_DUMP_COUNT_AR6002 > REGISTER_DUMP_LEN_MAX
#error "REG_DUMP_COUNT_AR6002 too large"
#endif
#if REG_DUMP_COUNT_AR6003 > REGISTER_DUMP_LEN_MAX
#error "REG_DUMP_COUNT_AR6003 too large"
#endif


void ar6000_dump_target_assert_info(HIF_DEVICE *hifDevice, A_UINT32 TargetType)
{
    A_UINT32 address;
    A_UINT32 regDumpArea = 0;
    A_STATUS status;
    A_UINT32 regDumpValues[REGISTER_DUMP_LEN_MAX];
    A_UINT32 regDumpCount = 0;
    A_UINT32 i;

    do {

            /* the reg dump pointer is copied to the host interest area */
        address = HOST_INTEREST_ITEM_ADDRESS(TargetType, hi_failure_state);
        address = TARG_VTOP(TargetType, address);

        if (TargetType == TARGET_TYPE_AR6001) {
                /* for AR6001, this is a fixed location because the ptr is actually stuck in cache,
                 * this may be fixed in later firmware versions */
            address = 0x18a0;
            regDumpCount = REG_DUMP_COUNT_AR6001;
        } else  if (TargetType == TARGET_TYPE_AR6002) {
            regDumpCount = REG_DUMP_COUNT_AR6002;
        } else  if (TargetType == TARGET_TYPE_AR6003) {
            regDumpCount = REG_DUMP_COUNT_AR6003;
        } else {
            A_ASSERT(0);
        }

            /* read RAM location through diagnostic window */
        status = ar6000_ReadRegDiag(hifDevice, &address, &regDumpArea);

        if (A_FAILED(status)) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("AR6K: Failed to get ptr to register dump area \n"));
            break;
        }

        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("AR6K: Location of register dump data: 0x%X \n",regDumpArea));

        if (regDumpArea == 0) {
                /* no reg dump */
            break;
        }

        regDumpArea = TARG_VTOP(TargetType, regDumpArea);

            /* fetch register dump data */
        status = ar6000_ReadDataDiag(hifDevice,
                                     regDumpArea,
                                     (A_UCHAR *)&regDumpValues[0],
                                     regDumpCount * (sizeof(A_UINT32)));

        if (A_FAILED(status)) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("AR6K: Failed to get register dump \n"));
            break;
        }
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("AR6K: Register Dump: \n"));

        for (i = 0; i < regDumpCount; i++) {
            ATHR_DISPLAY_MSG (_T(" %d :  0x%8.8X \n"), i, regDumpValues[i]);
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,(" %d :  0x%8.8X \n",i, regDumpValues[i]));
#ifdef UNDER_CE
            logPrintf(ATH_DEBUG_ERR," %d:  0x%8.8X \n",i, regDumpValues[i]);
#endif
        }

    } while (FALSE);

}

/* set HTC/Mbox operational parameters, this can only be called when the target is in the
 * BMI phase */
A_STATUS ar6000_set_htc_params(HIF_DEVICE *hifDevice,
                               A_UINT32    TargetType,
                               A_UINT32    MboxIsrYieldValue,
                               A_UINT8     HtcControlBuffers)
{
    A_STATUS status;
    A_UINT32 blocksizes[HTC_MAILBOX_NUM_MAX];

    do {
            /* get the block sizes */
        status = HIFConfigureDevice(hifDevice, HIF_DEVICE_GET_MBOX_BLOCK_SIZE,
                                    blocksizes, sizeof(blocksizes));

        if (A_FAILED(status)) {
            AR_DEBUG_PRINTF(ATH_LOG_ERR,("Failed to get block size info from HIF layer...\n"));
            break;
        }
            /* note: we actually get the block size for mailbox 1, for SDIO the block
             * size on mailbox 0 is artificially set to 1 */
            /* must be a power of 2 */
        A_ASSERT((blocksizes[1] & (blocksizes[1] - 1)) == 0);

        if (HtcControlBuffers != 0) {
                /* set override for number of control buffers to use */
            blocksizes[1] |=  ((A_UINT32)HtcControlBuffers) << 16;
        }

            /* set the host interest area for the block size */
        status = BMIWriteMemory(hifDevice,
                                HOST_INTEREST_ITEM_ADDRESS(TargetType, hi_mbox_io_block_sz),
                                (A_UCHAR *)&blocksizes[1],
                                4);

        if (A_FAILED(status)) {
            AR_DEBUG_PRINTF(ATH_LOG_ERR,("BMIWriteMemory for IO block size failed \n"));
            break;
        }

        AR_DEBUG_PRINTF(ATH_LOG_INF,("Block Size Set: %d (target address:0x%X)\n",
                blocksizes[1], HOST_INTEREST_ITEM_ADDRESS(TargetType, hi_mbox_io_block_sz)));

        if (MboxIsrYieldValue != 0) {
                /* set the host interest area for the mbox ISR yield limit */
            status = BMIWriteMemory(hifDevice,
                                    HOST_INTEREST_ITEM_ADDRESS(TargetType, hi_mbox_isr_yield_limit),
                                    (A_UCHAR *)&MboxIsrYieldValue,
                                    4);

            if (A_FAILED(status)) {
                AR_DEBUG_PRINTF(ATH_LOG_ERR,("BMIWriteMemory for yield limit failed \n"));
                break;
            }
        }

    } while (FALSE);

    return status;
}


static A_STATUS prepare_ar6002(HIF_DEVICE *hifDevice, A_UINT32 TargetVersion)
{
    A_STATUS status = A_OK;

    /* placeholder */

    return status;
}

static A_STATUS prepare_ar6003(HIF_DEVICE *hifDevice, A_UINT32 TargetVersion)
{
    A_STATUS status = A_OK;

    /* placeholder */

    return status;
}

/* this function assumes the caller has already initialized the BMI APIs */
A_STATUS ar6000_prepare_target(HIF_DEVICE *hifDevice,
                               A_UINT32    TargetType,
                               A_UINT32    TargetVersion)
{
    if (TargetType == TARGET_TYPE_AR6002) {
            /* do any preparations for AR6002 devices */
        return prepare_ar6002(hifDevice,TargetVersion);
    } else if (TargetType == TARGET_TYPE_AR6003) {
        return prepare_ar6003(hifDevice,TargetVersion);
    }

    return A_OK;
}

#if defined(CONFIG_AR6002_REV1_FORCE_HOST)
/*
 * Call this function just before the call to BMIInit
 * in order to force* AR6002 rev 1.x firmware to detect a Host.
 * THIS IS FOR USE ONLY WITH AR6002 REV 1.x.
 * TBDXXX: Remove this function when REV 1.x is desupported.
 */
A_STATUS
ar6002_REV1_reset_force_host (HIF_DEVICE *hifDevice)
{
    A_INT32 i;
    struct forceROM_s {
        A_UINT32 addr;
        A_UINT32 data;
    };
    struct forceROM_s *ForceROM;
    A_INT32 szForceROM;
    A_STATUS status = A_OK;
    A_UINT32 address;
    A_UINT32 data;

    /* Force AR6002 REV1.x to recognize Host presence.
     *
     * Note: Use RAM at 0x52df80..0x52dfa0 with ROM Remap entry 0
     * so that this workaround functions with AR6002.war1.sh.  We
     * could fold that entire workaround into this one, but it's not
     * worth the effort at this point.  This workaround cannot be
     * merged into the other workaround because this must be done
     * before BMI.
     */

    static struct forceROM_s ForceROM_NEW[] = {
        {0x52df80, 0x20f31c07},
        {0x52df84, 0x92374420},
        {0x52df88, 0x1d120c03},
        {0x52df8c, 0xff8216f0},
        {0x52df90, 0xf01d120c},
        {0x52df94, 0x81004136},
        {0x52df98, 0xbc9100bd},
        {0x52df9c, 0x00bba100},

        {0x00008000|MC_TCAM_TARGET_ADDRESS, 0x0012dfe0}, /* Use remap entry 0 */
        {0x00008000|MC_TCAM_COMPARE_ADDRESS, 0x000e2380},
        {0x00008000|MC_TCAM_MASK_ADDRESS, 0x00000000},
        {0x00008000|MC_TCAM_VALID_ADDRESS, 0x00000001},

        {0x00018000|(LOCAL_COUNT_ADDRESS+0x10), 0}, /* clear BMI credit counter */

        {0x00004000|AR6002_RESET_CONTROL_ADDRESS, RESET_CONTROL_WARM_RST_MASK},
    };

    address = 0x004ed4b0; /* REV1 target software ID is stored here */
    status = ar6000_ReadRegDiag(hifDevice, &address, &data);
    if (A_FAILED(status) || (data != AR6002_VERSION_REV1)) {
        return A_ERROR; /* Not AR6002 REV1 */
    }

    ForceROM = ForceROM_NEW;
    szForceROM = sizeof(ForceROM_NEW)/sizeof(*ForceROM);

    ATH_DEBUG_PRINTF (DBG_MISC_DRV, ATH_DEBUG_TRC, ("Force Target to recognize Host....\n"));
    for (i = 0; i < szForceROM; i++)
    {
        if (ar6000_WriteRegDiag(hifDevice,
                                &ForceROM[i].addr,
                                &ForceROM[i].data) != A_OK)
        {
            ATH_DEBUG_PRINTF (DBG_MISC_DRV, ATH_DEBUG_TRC, ("Cannot force Target to recognize Host!\n"));
            return A_ERROR;
        }
    }

    A_MDELAY(1000);

    return A_OK;
}
#endif /* CONFIG_AR6002_REV1_FORCE_HOST */


