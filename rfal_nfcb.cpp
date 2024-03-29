
/******************************************************************************
  * @attention
  *
  * <h2><center>&copy; COPYRIGHT 2016 STMicroelectronics</center></h2>
  *
  * Licensed under ST MYLIBERTY SOFTWARE LICENSE AGREEMENT (the "License");
  * You may not use this file except in compliance with the License.
  * You may obtain a copy of the License at:
  *
  *        http://www.st.com/myliberty
  *
  * Unless required by applicable law or agreed to in writing, software 
  * distributed under the License is distributed on an "AS IS" BASIS, 
  * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied,
  * AND SPECIFICALLY DISCLAIMING THE IMPLIED WARRANTIES OF MERCHANTABILITY,
  * FITNESS FOR A PARTICULAR PURPOSE, AND NON-INFRINGEMENT.
  * See the License for the specific language governing permissions and
  * limitations under the License.
  *
******************************************************************************/

/*
 *      PROJECT:   ST25R391x firmware
 *      $Revision: $
 *      LANGUAGE:  ISO C99
 */

/*! \file rfal_nfcb.c
 *
 *  \author Gustavo Patricio
 *
 *  \brief Implementation of NFC-B (ISO14443B) helpers 
 *
 */

/*
 ******************************************************************************
 * INCLUDES
 ******************************************************************************
 */
#include <platform1.h>
#include "rfal_nfcb.h"
#include "utils.h"


/*
 ******************************************************************************
 * ENABLE SWITCH
 ******************************************************************************
 */

#ifndef RFAL_FEATURE_NFCB
    #error " RFAL: Module configuration missing. Please enable/disable NFC-B module by setting: RFAL_FEATURE_NFCB "
#endif

#if RFAL_FEATURE_NFCB

/*
 ******************************************************************************
 * GLOBAL DEFINES
 ******************************************************************************
 */

#define RFAL_NFCB_SENSB_REQ_EXT_SENSB_RES_SUPPORTED  0x10 /*!< Bit mask for Extended SensB Response support in SENSB_REQ */
#define RFAL_NFCB_SENSB_RES_PROT_TYPE_RFU            0x08 /*!< Bit mask for Protocol Type RFU in SENSB_RES */
#define RFAL_NFCB_SLOT_MARKER_SC_SHIFT               4    /*!< Slot Code position on SLOT_MARKER APn                     */

#define RFAL_NFCB_SLOTMARKER_SLOTCODE_MIN            1    /*!< SLOT_MARKER Slot Code minimum   Digital 1.1  Table 37 */ 
#define RFAL_NFCB_SLOTMARKER_SLOTCODE_MAX            16   /*!< SLOT_MARKER Slot Code maximum   Digital 1.1  Table 37 */

#define RFAL_NFCB_ACTIVATION_FWT                    (RFAL_NFCB_FWTSENSB + RFAL_NFCB_DFWT_10)  /*!< FWT(SENSB) + dFWT  Digital 1.1  7.9.1.5  */

/*! Advanced and Extended bit mask in Parameter of SENSB_REQ */
#define RFAL_NFCB_SENSB_REQ_PARAM                   (RFAL_NFCB_SENSB_REQ_ADV_FEATURE | RFAL_NFCB_SENSB_REQ_EXT_SENSB_RES_SUPPORTED)


/*! NFC-B commands definition */
static enum
{
    RFAL_NFCB_CMD_SENSB_REQ = 0x05,   /*!< SENSB_REQ (REQB) & SLOT_MARKER  Digital 1.1 Table 24 */
    RFAL_NFCB_CMD_SENSB_RES = 0x50,   /*!< SENSB_RES (ATQB) & SLOT_MARKER  Digital 1.1 Table 27 */
    RFAL_NFCB_CMD_SLPB_REQ  = 0x50,   /*!< SLPB_REQ (HLTB command)  Digital 1.1 Table 38        */
    RFAL_NFCB_CMD_SLPB_RES  = 0x00    /*!< SLPB_RES (HLTB Answer)   Digital 1.1 Table 39        */
}rfalCmd;

/*
 ******************************************************************************
 * GLOBAL MACROS
 ******************************************************************************
 */

#define rfalNfcbNI2NumberOfSlots( ni )  (1 << ni)            /*!< Converts the Number of slots Identifier to slot number*/

/*
******************************************************************************
* GLOBAL TYPES
******************************************************************************
*/

/*! ALLB_REQ (WUPB) and SENSB_REQ (REQB) Command Format   Digital 1.1  7.6.1 */
typedef struct
{
    uint8_t  cmd;                            /*!< xxxxB_REQ: 05h       */
    uint8_t  AFI;                            /*!< NFC Identifier       */
    uint8_t  PARAM;                          /*!< Application Data     */
} rfalNfcbSensbReq;

/*! SLOT_MARKER Command format  Digital 1.1  7.7.1 */
typedef struct
{
    uint8_t  APn;    /*!< Slot number 2..16 | 0101b */
} rfalNfcbSlotMarker;

/*! SLPB_REQ (HLTB) Command Format   Digital 1.1  7.8.1 */
typedef struct
{
    uint8_t  cmd;                            /*!< SLPB_REQ: 50h        */
    uint8_t  nfcid0[RFAL_NFCB_NFCID0_LEN];   /*!< NFC Identifier (PUPI)*/
} rfalNfcbSlpbReq;


/*! SLPB_RES (HLTB) Response Format   Digital 1.1  7.8.2 */
typedef struct
{
    uint8_t  cmd;                            /*!< SLPB_RES: 00h        */
} rfalNfcbSlpbRes;


/*! RFAL NFC-B instance */
typedef struct
{
    uint8_t  AFI;                            /*!< AFI to be used       */
    uint8_t  PARAM;                          /*!< PARAM to be used     */
} rfalNfcb;

/*
******************************************************************************
* LOCAL FUNCTION PROTOTYPES
******************************************************************************
*/
static ReturnCode rfalNfcbCheckSensbRes( rfalNfcbSensbRes *sensbRes, uint8_t sensbResLen );


/*
******************************************************************************
* LOCAL VARIABLES
******************************************************************************
*/
/*! TR2 Table according to Digital 1.1 Table 33 */
static const uint16_t rfalNfcbTr2Table[] = { 1792, 3328, 5376, 9472 };

rfalNfcb gRfalNfcb; /*!< RFAL NFC-B Instance */


/*
******************************************************************************
* LOCAL FUNCTIONS
******************************************************************************
*/

/*******************************************************************************/
static ReturnCode rfalNfcbCheckSensbRes( rfalNfcbSensbRes *sensbRes, uint8_t sensbResLen )
{
    /* Check response length */
    if( ( (sensbResLen != RFAL_NFCB_SENSB_RES_LEN) && (sensbResLen != RFAL_NFCB_SENSB_RES_EXT_LEN) ) )
    {
        return ERR_PROTO;
    }
    
    /* Check SENSB_RES and Protocol Type   Digital 1.1 7.6.2.19 */
    if( (sensbRes->protInfo.FsciProType & RFAL_NFCB_SENSB_RES_PROT_TYPE_RFU) || (sensbRes->cmd != RFAL_NFCB_CMD_SENSB_RES) )
    {
        return ERR_PROTO;
    }
    return ERR_NONE;
}

/*
******************************************************************************
* GLOBAL FUNCTIONS
******************************************************************************
*/

/*******************************************************************************/
ReturnCode rfalNfcbPollerInitialize( SPI*  mspiChannel, ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 )
{
    ReturnCode ret;
    
    EXIT_ON_ERR( ret, rfalSetMode( RFAL_MODE_POLL_NFCB, RFAL_BR_106, RFAL_BR_106, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 )  );
    rfalSetErrorHandling( RFAL_ERRORHANDLING_NFC );
    
    rfalSetGT( RFAL_GT_NFCB );
    rfalSetFDTListen( RFAL_FDT_LISTEN_NFCB_POLLER );
    rfalSetFDTPoll( RFAL_FDT_POLL_NFCB_POLLER );
    
    gRfalNfcb.AFI    = RFAL_NFCB_AFI;
    gRfalNfcb.PARAM  = RFAL_NFCB_PARAM;
    
    return ERR_NONE;
}

/*******************************************************************************/
ReturnCode rfalNfcbPollerInitializeWithParams( uint8_t AFI, uint8_t PARAM, SPI* mspiChannel, ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 )
{
    ReturnCode ret;
        
    EXIT_ON_ERR( ret, rfalNfcbPollerInitialize( mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 )  );
    
    gRfalNfcb.AFI   = AFI;
    gRfalNfcb.PARAM = (PARAM & RFAL_NFCB_SENSB_REQ_PARAM);
    
    return ERR_NONE;
}


/*******************************************************************************/
ReturnCode rfalNfcbPollerCheckPresence( rfalNfcbSensCmd cmd, rfalNfcbSlots slots, rfalNfcbSensbRes *sensbRes, uint8_t *sensbResLen, SPI* mspiChannel, ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 )
{
    uint16_t         rxLen;
    ReturnCode       ret;
    rfalNfcbSensbReq sensbReq;
    

    /* Check if the command requested and given the slot number are valid */
    if( ((RFAL_NFCB_SENS_CMD_SENSB_REQ != cmd) && (RFAL_NFCB_SENS_CMD_ALLB_REQ != cmd)) ||
        (slots > RFAL_NFCB_SLOT_NUM_16) || (sensbRes == NULL) || (sensbResLen == NULL)    )
    {
        return ERR_PARAM;
    }
    
    *sensbResLen = 0;
    ST_MEMSET(sensbRes, 0x00, sizeof(rfalNfcbSensbRes) );
    
    /* Compute SENSB_REQ */
    sensbReq.cmd   = RFAL_NFCB_CMD_SENSB_REQ;
    sensbReq.AFI   = gRfalNfcb.AFI;
    sensbReq.PARAM = ((gRfalNfcb.PARAM & RFAL_NFCB_SENSB_REQ_PARAM) | cmd | slots);
    
    /* Send SENSB_REQ and disable AGC to detect collisions */
    ret = rfalTransceiveBlockingTxRx( (uint8_t*)&sensbReq, sizeof(rfalNfcbSensbReq), (uint8_t*)sensbRes, sizeof(rfalNfcbSensbRes), &rxLen, RFAL_TXRX_FLAGS_DEFAULT, RFAL_NFCB_FWTSENSB, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    *sensbResLen = rxLen;
    
    /*  Check if a transmission error was detected */
    if( (ret == ERR_CRC) || (ret == ERR_FRAMING) )
    {
        /* Invalidate received frame as an error was detected (CollisionResolution checks if valid) */
        *sensbResLen = 0;
        return ERR_NONE;
    }
    
    if( ret == ERR_NONE )
    {
        return rfalNfcbCheckSensbRes( sensbRes, *sensbResLen );
    }
    
    return ret;
}


/*******************************************************************************/
ReturnCode rfalNfcbPollerSleep( uint8_t* nfcid0, SPI* mspiChannel, ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 )
{
    uint16_t        rxLen;
    ReturnCode      ret;
    rfalNfcbSlpbReq slpbReq;
    rfalNfcbSlpbRes slpbRes;
    
    if( nfcid0 == NULL )
    {
        return ERR_PARAM;
    }
    
    /* Compute SLPB_REQ */
    slpbReq.cmd = RFAL_NFCB_CMD_SLPB_REQ;
    ST_MEMCPY( slpbReq.nfcid0, nfcid0, RFAL_NFCB_NFCID0_LEN );
    
    EXIT_ON_ERR( ret, rfalTransceiveBlockingTxRx( (uint8_t*)&slpbReq, sizeof(rfalNfcbSlpbReq), (uint8_t*)&slpbRes, sizeof(rfalNfcbSlpbRes), &rxLen, RFAL_TXRX_FLAGS_DEFAULT, RFAL_NFCB_ACTIVATION_FWT, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) );
    
    /* Check SLPB_RES */
    if( (rxLen != sizeof(rfalNfcbSlpbRes)) || (slpbRes.cmd != RFAL_NFCB_CMD_SLPB_RES) )
    {
        return ERR_PROTO;
    }
    return ERR_NONE;
}


/*******************************************************************************/
ReturnCode rfalNfcbPollerSlotMarker( uint8_t slotCode, rfalNfcbSensbRes *sensbRes, uint8_t *sensbResLen, SPI* mspiChannel, ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 )
{
    ReturnCode         ret;
    rfalNfcbSlotMarker slotMarker;
    uint16_t           rxLen;
    
    /* Check parameters */
    if( (sensbRes == NULL) || (sensbResLen == NULL)    || 
        (slotCode < RFAL_NFCB_SLOTMARKER_SLOTCODE_MIN) || 
        (slotCode > RFAL_NFCB_SLOTMARKER_SLOTCODE_MAX)   )
    {
        return ERR_PARAM;
    }
    /* Compose and send SLOT_MARKER with disabled AGC to detect collisions  */
    slotMarker.APn = ((slotCode << RFAL_NFCB_SLOT_MARKER_SC_SHIFT) | RFAL_NFCB_CMD_SENSB_REQ);
    
    ret = rfalTransceiveBlockingTxRx( (uint8_t*)&slotMarker, sizeof(rfalNfcbSlotMarker), (uint8_t*)sensbRes, sizeof(rfalNfcbSensbRes), &rxLen, RFAL_TXRX_FLAGS_DEFAULT, RFAL_NFCB_ACTIVATION_FWT, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    *sensbResLen = rxLen;
    
    /* Check if a transmission error was detected */
    if( (ret == ERR_CRC) || (ret == ERR_FRAMING) )
    {
        return ERR_RF_COLLISION;
    }
    
    if( ret == ERR_NONE )
    {
        return rfalNfcbCheckSensbRes( sensbRes, *sensbResLen );
    }
    
    return ret;
}


ReturnCode rfalNfcbPollerTechnologyDetection( rfalComplianceMode compMode, rfalNfcbSensbRes *sensbRes, uint8_t *sensbResLen, SPI* mspiChannel, ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 )
{
    NO_WARNING(compMode);
    
    return rfalNfcbPollerCheckPresence( RFAL_NFCB_SENS_CMD_SENSB_REQ, RFAL_NFCB_SLOT_NUM_1, sensbRes, sensbResLen, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
}


/*******************************************************************************/
ReturnCode rfalNfcbPollerCollisionResolution( rfalComplianceMode compMode, uint8_t devLimit, rfalNfcbListenDevice *nfcbDevList, uint8_t *devCnt, SPI* mspiChannel, ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 )
{
    bool colPending; /* dummy */
    return rfalNfcbPollerCollisionResolutionSlotted( compMode, devLimit, RFAL_NFCB_SLOT_NUM_1, RFAL_NFCB_SLOT_NUM_16, nfcbDevList, devCnt, &colPending, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
}


/*******************************************************************************/
ReturnCode rfalNfcbPollerCollisionResolutionSlotted( rfalComplianceMode compMode, uint8_t devLimit, rfalNfcbSlots initSlots, rfalNfcbSlots endSlots, rfalNfcbListenDevice *nfcbDevList, uint8_t *devCnt, bool *colPending, SPI* mspiChannel, ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 )
{
		ReturnCode    ret;
        rfalNfcbSlots slotsNum;
        uint8_t       slotCode;
        uint8_t       curDevCnt;
        
        
        /* Check parameters. In ISO | Activity 1.0 mode the initial slots must be 1 as continuation of Technology Detection */
        if( (nfcbDevList == NULL) || (devCnt == NULL)  || (colPending == NULL) || (initSlots > RFAL_NFCB_SLOT_NUM_16) || 
            (endSlots > RFAL_NFCB_SLOT_NUM_16) || ((compMode == RFAL_COMPLIANCE_MODE_ISO) && (initSlots != RFAL_NFCB_SLOT_NUM_1)) )
        {
            return ERR_PARAM;
        }
        
        /* Initialise as no error in case Activity 1.0 where the previous SENSB_RES from technology detection should be used */
        ret         = ERR_NONE;
        *devCnt     = 0;
        curDevCnt   = 0;
        *colPending = false;
           
        
        /* Send ALLB_REQ   Activity 1.1   9.3.5.2 and 9.3.5.3  (Symbol 1 and 2) */
        if( compMode != RFAL_COMPLIANCE_MODE_ISO )
        {
           ret =  rfalNfcbPollerCheckPresence( RFAL_NFCB_SENS_CMD_ALLB_REQ, initSlots, &nfcbDevList->sensbRes, &nfcbDevList->sensbResLen, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
           if( (ret != ERR_NONE) && (initSlots == RFAL_NFCB_SLOT_NUM_1) )
           {
               return ret;
           }
        }

        
        /* Check if there was a transmission error on WUPB  EMVCo 2.6  9.3.3.1 */
        if( (compMode == RFAL_COMPLIANCE_MODE_EMV) && (nfcbDevList->sensbResLen == 0) )
        {
            return ERR_FRAMING;
        }
        //(int)slotsNum = (int)initSlots; (int)slotsNum <= (int)endSlots; ((int)slotsNum)++ )

        for( int i = 0; i < RFAL_NFCB_SLOT_NUM_16; i++)
        {
        	rfalNfcbSlots slotsNum = (rfalNfcbSlots) i;
            /* Activity 1.1  9.3.5.23  -  Symbol 22 */
            if( (compMode == RFAL_COMPLIANCE_MODE_NFC) && (curDevCnt != 0) )
            {
                rfalNfcbPollerSleep( nfcbDevList[(*devCnt-1)].sensbRes.nfcid0, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
                nfcbDevList[(*devCnt-1)].isSleep = true;
            }
            
            /* Send SENSB_REQ with number of slots if not the first Activity 1.1  9.3.5.24  -  Symbol 23 */
            if( (slotsNum != initSlots) || *colPending )
            {
               ret = rfalNfcbPollerCheckPresence( RFAL_NFCB_SENS_CMD_SENSB_REQ, slotsNum, &nfcbDevList[*devCnt].sensbRes, &nfcbDevList[*devCnt].sensbResLen, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            }
            
            /* Activity 1.1  9.3.5.6  -  Symbol 5 */
            slotCode    = 0;
            curDevCnt   = 0;
            *colPending = false;

            do{
                /* Activity 1.1  9.3.5.26  -  Symbol 25 */
                if( slotCode != 0 )
                {
                    ret = rfalNfcbPollerSlotMarker( slotCode, &nfcbDevList[*devCnt].sensbRes, &nfcbDevList[*devCnt].sensbResLen, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
                }
                
                /* Activity 1.1  9.3.5.7 and 9.3.5.8  -  Symbol 6 */
                if( ret != ERR_TIMEOUT )
                {
                    /* Activity 1.1  9.3.5.8  -  Symbol 7 */
                    if( (ret == ERR_NONE) && (rfalNfcbCheckSensbRes( &nfcbDevList[*devCnt].sensbRes, nfcbDevList[*devCnt].sensbResLen) == ERR_NONE) )
                    {
                        nfcbDevList[*devCnt].isSleep = false;
                        
                        if( compMode == RFAL_COMPLIANCE_MODE_EMV )
                        {
                            (*devCnt)++;
                            return ret;
                        }
                        else if( compMode == RFAL_COMPLIANCE_MODE_ISO )
                        {
                            /* Activity 1.0  9.3.5.8  -  Symbol 7 */
                            (*devCnt)++;
                            curDevCnt++;
                            
                            /* Activity 1.0  9.3.5.10  -  Symbol 9 */
                            if( (*devCnt >= devLimit) || (slotsNum == RFAL_NFCB_SLOT_NUM_1) )
                            {
                                return ret;
                            }

                            /* Activity 1.0  9.3.5.11  -  Symbol 10 */
                            rfalNfcbPollerSleep( nfcbDevList[*devCnt-1].sensbRes.nfcid0, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
                            nfcbDevList[*devCnt-1].isSleep =  true;
                        }
                        else if( compMode == RFAL_COMPLIANCE_MODE_NFC )
                        {
                            /* Activity 1.1  9.3.5.10 and 9.3.5.11  -  Symbol 9 and Symbol 11*/
                            if(curDevCnt != 0)
                            {
                                rfalNfcbPollerSleep( nfcbDevList[*devCnt-1].sensbRes.nfcid0, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
                                nfcbDevList[*devCnt-1].isSleep = true;
                            }
                            
                            /* Activity 1.1  9.3.5.12  -  Symbol 11 */
                            (*devCnt)++;
                            curDevCnt++;
                            
                            /* Activity 1.1  9.3.5.6  -  Symbol 13 */
                            if( (*devCnt >= devLimit) || (slotsNum == RFAL_NFCB_SLOT_NUM_1) )
                            {
                                return ret;
                            }
                        }
                    }
                    else
                    {
                        /* If deviceLimit is set to 0 the NFC Forum Device is configured to perform collision detection only  Activity 1.0 and 1.1  9.3.5.5  - Symbol 4 */
                        if( (devLimit == 0) && (slotsNum == RFAL_NFCB_SLOT_NUM_1) )
                        {
                            return ERR_RF_COLLISION;
                        }
                        
                        /* Activity 1.1  9.3.5.9  -  Symbol 8 */
                        *colPending = true;
                    }
                }
                
                /* Activity 1.1  9.3.5.15  -  Symbol 14 */
                (int)slotCode++;
            }
            while( slotCode < rfalNfcbNI2NumberOfSlots(slotsNum) );
            
            /* Activity 1.1  9.3.5.17  -  Symbol 16 */
            if( !(*colPending) )
            {
                return ERR_NONE;
            }
            
            /* Activity 1.1  9.3.5.18  -  Symbol 17 */
            if( curDevCnt != 0 )
            {
                /* If a collision is detected and card(s) were found on this loop keep the same number of available slots */
            	//((int)slotsNum)--;
            	i--;
            }

        }
        
        return ERR_NONE;
}


/*******************************************************************************/
uint32_t rfalNfcbTR2ToFDT( uint8_t tr2Code )
{
    return rfalNfcbTr2Table[ (tr2Code & RFAL_NFCB_SENSB_RES_PROTO_TR2_MASK) ];
}

#endif /* RFAL_FEATURE_NFCB */
