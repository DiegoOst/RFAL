
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
 *      PROJECT:   ST25R3911 firmware
 *      $Revision: $
 *      LANGUAGE:  ISO C99
 */

/*! \file
 *
 *  \author Gustavo Patricio 
 *
 *  \brief RF Abstraction Layer (RFAL)
 *  
 *  RFAL implementation for ST25R3911
 */


/*
******************************************************************************
* INCLUDES
******************************************************************************
*/


#include "rfal_rf.h"

#include <rfal_rf.h>
#include "utils.h"
#include "ST25R3911.h"
#include "st25r3911_com.h"
#include "st25r3911_interrupt.h"
#include "rfal_AnalogConfig.h"
#include "rfal_iso15693_2.h"
#include "rfal_chip.h"
#include "platform1.h"
#include <stdint.h>

/*
******************************************************************************
* GLOBAL TYPES
******************************************************************************
*/



/*! Struct that holds all involved on a Transceive including the context passed by the caller     */
typedef struct{
    rfalTransceiveState     state;       /*!< Current transceive state                            */
    rfalTransceiveState     lastState;   /*!< Last transceive state (debug purposes)              */
    ReturnCode              status;      /*!< Current status/error of the transceive              */
    bool                    rxse;        /*!< Flag indicating if RXE was received with RXS        */
    
    rfalTransceiveContext   ctx;         /*!< The transceive context given by the caller          */
} rfalTxRx;


/*! Struct that holds all context for the Listen Mode                                             */
typedef struct{
    rfalLmState             state;       /*!< Current Listen Mode state                           */
    rfalBitRate             brDetected;  /*!< Last bit rate detected                              */
    
    uint8_t*                rxBuf;       /*!< Location to store incoming data in Listen Mode      */
    uint16_t                rxBufLen;    /*!< Length of rxBuf                                     */
    uint16_t*               rxLen;       /*!< Pointer to write the data length placed into rxBuf  */
    bool                    dataFlag;    /*!< Listen Mode current Data Flag                       */
} rfalLm;


/*! Struct that holds all context for the Wake-Up Mode                                             */
typedef struct{
    rfalWumState            state;       /*!< Current Wake-Up Mode state                           */
    st25r3911WakeUpConfig   cfg;         /*!< Current Wake-Up Mode context                         */     
} rfalWum;


/*! Struct that holds the timings GT and FDTs                             */
typedef struct{
    uint32_t                GT;          /*!< GT in 1/fc                  */
    uint32_t                FDTListen;   /*!< FDTListen in 1/fc           */
    uint32_t                FDTPoll;     /*!< FDTPoll in 1/fc             */
} rfalTimings;


/*! Struct that holds the software timers                                 */
typedef struct{
    uint32_t                GT;          /*!< RFAL's GT timer             */
    uint32_t                FWT;         /*!< FWT/RWT timer for Active P2P*/
    uint32_t                RXE;         /*!< Timer between RXS and RXE   */ 
} rfalTimers;


/*! Struct that holds the RFAL's callbacks                                */
typedef struct{
    rfalPreTxRxCallback     preTxRx;     /*!< RFAL's Pre TxRx callback    */
    rfalPostTxRxCallback    postTxRx;    /*!< RFAL's Post TxRx callback   */
} rfalCallbacks;


/*! Struct that holds counters to control the FIFO on Tx and Rx                                                                          */
typedef struct{    
    uint16_t                expWL;       /*!< The amount of bytes expected to be Tx when a WL interrupt occours                          */
    uint16_t                bytesTotal;  /*!< Total bytes to be transmitted OR the total bytes received                                  */
    uint16_t                bytesWritten;/*!< Amount of bytes already written on FIFO (Tx) OR read (RX) from FIFO and written on rxBuffer*/
    uint8_t                 status[ST25R3911_FIFO_STATUS_LEN];   /*!< FIFO Status Registers                                              */
} rfalFIFO;


/*! Struct that holds RFAL's configuration settings                                                      */
typedef struct{    
    uint8_t                 obsvModeTx;  /*!< RFAL's config of the ST25R3911's observation mode while Tx */
    uint8_t                 obsvModeRx;  /*!< RFAL's config of the ST25R3911's observation mode while Rx */
    rfalEHandling           eHandling;   /*!< RFAL's error handling config/mode                          */
} rfalConfigs;


/*! Struct that holds NFC-F data - Used only inside rfalFelicaPoll() (static to avoid adding it into stack) */
typedef struct{    
    rfalFeliCaPollRes pollResponses[RFAL_FELICA_POLL_MAX_SLOTS];   /* FeliCa Poll response container for 16 slots */
} rfalNfcfWorkingData;


/*! Struct that holds NFC-V current context
 *
 * 96 bytes is FIFO size of ST25R3911, codingBuffer has to be big enough for coping with maximum response size (Manchester coded)
 *    - current implementation expects it be written in one bulk into FIFO
 *    - needs to be above FIFO water level of ST25R3911 (64)
 *    - 65 is actually 1 byte too much, but ~75us in 1of256 another byte is already gone
 *    
 *    - inventory requests responses: 14 bytes 
 *    - Max read single block responses: 32 bytes
 *    - Read multiple block responses: variable    
 *    
 *    ISO15693 frame: SOF + Flags + Data + CRC + EOF  
 */
typedef struct{    
    uint8_t               codingBuffer[((2 + 255 + 3)*2)];/*!< Coding buffer,   length MUST be above 64: [65; ...]                   */
    uint16_t              nfcvOffset;                    /*!< Offset needed for ISO15693 coding function                             */
    rfalTransceiveContext origCtx;                       /*!< Context provided by user                                               */
    uint16_t              ignoreBits;                    /*!< Number of bits at the beginning of a frame to be ignored when decoding */
} rfalNfcvWorkingData;


/*! RFAL instance  */
typedef struct{
    rfalState             state;     /*!< RFAL's current state                            */
    rfalMode              mode;      /*!< RFAL's current mode                             */
    rfalBitRate           txBR;      /*!< RFAL's current Tx Bit Rate                      */
    rfalBitRate           rxBR;      /*!< RFAL's current Rx Bit Rate                      */
    bool                  field;     /*!< Current field state (On / Off)                  */
    
    rfalConfigs           conf;      /*!< RFAL's configuration settings                   */
    rfalTimings           timings;   /*!< RFAL's timing setting                           */
    rfalTxRx              TxRx;      /*!< RFAL's transceive management                    */
    rfalLm                Lm;        /*!< RFAL's listen mode management                   */
    rfalWum               wum;       /*!< RFAL's Wake-Up mode management                  */
    
    rfalFIFO              fifo;      /*!< RFAL's FIFO management                          */
    rfalTimers            tmr;       /*!< RFAL's Software timers                          */
    rfalCallbacks         callbacks; /*!< RFAL's callbacks                                */
    
#if RFAL_FEATURE_NFCF
    rfalNfcfWorkingData     nfcfData; /*!< RFAL's working data when supporting NFC-F      */
#endif /* RFAL_FEATURE_NFCF */
    
#if RFAL_FEATURE_NFCV
    rfalNfcvWorkingData     nfcvData; /*!< RFAL's working data when supporting NFC-V      */
#endif /* RFAL_FEATURE_NFCV */
    
} rfal;



/*! Felica's command set */
typedef enum 
{
    FELICA_CMD_POLLING                  = 0x00, /*!< Felica Poll/REQC command (aka SENSF_REQ) to identify a card    */
    FELICA_CMD_POLLING_RES              = 0x01, /*!< Felica Poll/REQC command (aka SENSF_RES) response              */
    FELICA_CMD_REQUEST_SERVICE          = 0x02, /*!< verify the existence of Area and Service                       */
    FELICA_CMD_REQUEST_RESPONSE         = 0x04, /*!< verify the existence of a card                                 */
    FELICA_CMD_READ_WITHOUT_ENCRYPTION  = 0x06, /*!< read Block Data from a Service that requires no authentication */
    FELICA_CMD_WRITE_WITHOUT_ENCRYPTION = 0x08, /*!< write Block Data to a Service that requires no authentication  */
    FELICA_CMD_REQUEST_SYSTEM_CODE      = 0x0c, /*!< acquire the System Code registered to a card                   */
    FELICA_CMD_AUTHENTICATION1          = 0x10, /*!< authenticate a card                                            */
    FELICA_CMD_AUTHENTICATION2          = 0x12, /*!< allow a card to authenticate a Reader/Writer                   */
    FELICA_CMD_READ                     = 0x14, /*!< read Block Data from a Service that requires authentication    */
    FELICA_CMD_WRITE                    = 0x16, /*!< write Block Data to a Service that requires authentication     */
}t_rfalFeliCaCmd;

/*
******************************************************************************
* GLOBAL DEFINES
******************************************************************************
*/

#define RFAL_TIMING_NONE                0x00                                         /*!< Timing disable | Don't apply                                                    */

#define RFAL_FIFO_IN_LT_32              32                                           /*!< Number of bytes in the FIFO when WL interrupt occurs while Tx ( fifo_lt: 0 )    */
#define RFAL_FIFO_IN_LT_16              16                                           /*!< Number of bytes in the FIFO when WL interrupt occurs while Tx ( fifo_lt: 1 )    */

#define RFAL_FIFO_OUT_LT_32             (ST25R3911_FIFO_DEPTH - RFAL_FIFO_IN_LT_32)  /*!< Number of bytes sent/out of the FIFO when WL interrupt occurs while Tx ( fifo_lt: 0 ) */
#define RFAL_FIFO_OUT_LT_16             (ST25R3911_FIFO_DEPTH - RFAL_FIFO_IN_LT_16)  /*!< Number of bytes sent/out of the FIFO when WL interrupt occurs while Tx ( fifo_lt: 1 ) */

#define RFAL_FIFO_STATUS_REG1           0                                            /*!< Location of FIFO status register 1 in local copy                                */
#define RFAL_FIFO_STATUS_REG2           1                                            /*!< Location of FIFO status register 2 in local copy                                */
#define RFAL_FIFO_STATUS_INVALID        0xFF                                         /*!< Value indicating that the local FIFO status in invalid|cleared                  */

#define RFAL_ST25R3911_GPT_MAX_1FC      rfalConv8fcTo1fc(  0xFFFF )                  /*!< Max GPT steps in 1fc (0xFFFF steps of 8/fc    => 0xFFFF * 590ns  = 38,7ms)      */
#define RFAL_ST25R3911_NRT_MAX_1FC      rfalConv4096fcTo1fc( 0xFFFF )                /*!< Max NRT steps in 1fc (0xFFFF steps of 4096/fc => 0xFFFF * 302us  = 19.8s )      */
#define RFAL_ST25R3911_NRT_DISABLED     0                                            /*!< NRT Disabled: All 0 No-response timer is not started, wait forever              */
#define RFAL_ST25R3911_MRT_MAX_1FC      rfalConv64fcTo1fc( 0x00FF )                  /*!< Max MRT steps in 1fc (0x00FF steps of 64/fc   => 0x00FF * 4.72us = 1.2ms )      */
#define RFAL_ST25R3911_MRT_MIN_1FC      rfalConv64fcTo1fc( 0x0004 )                  /*!< Min MRT steps in 1fc ( 0<=mrt<=4 ; 4 (64/fc)  => 0x0004 * 4.72us = 18.88us )    */
#define RFAL_ST25R3911_GT_MAX_1FC       rfalConvMsTo1fc( 5000 )                      /*!< Max GT value allowed in 1/fc                                                    */
#define RFAL_ST25R3911_GT_MIN_1FC       rfalConvMsTo1fc(RFAL_ST25R3911_SW_TMR_MIN_1MS)/*!< Min GT value allowed in 1/fc                                                   */
#define RFAL_ST25R3911_SW_TMR_MIN_1MS   1                                            /*!< Min value of a SW timer in ms                                                   */

#define RFAL_OBSMODE_DISABLE            0x00                                         /*!< Observation Mode disabled                                                       */

#define RFAL_NFC_RX_INCOMPLETE_LEN      1                                            /*!< Threshold value where incoming rx may be considered as incomplete in NFC        */
#define RFAL_EMVCO_RX_MAXLEN            4                                            /*!< Maximum value where EMVCo to apply special error handling                       */
#define RFAL_EMVCO_RX_MINLEN            2                                            /*!< Minimum value where EMVCo to apply special error handling                       */

#define RFAL_NORXE_TOUT                 10                                           /*!< Timeout to be used on a potential missing RXE - Silicon ST25R3911B Errata #1.1  */

#define RFAL_ISO14443A_SDD_RES_LEN      5                                            /*!< SDD_RES | Anticollision (UID CLn) length  -  rfalNfcaSddRes                     */

#define RFAL_FELICA_POLL_DELAY_TIME     512                                          /*!<  FeliCa Poll Processing time is 2.417 ms ~512*64/fc Digital 1.1 A4              */
#define RFAL_FELICA_POLL_SLOT_TIME      256                                          /*!<  FeliCa Poll Time Slot duration is 1.208 ms ~256*64/fc Digital 1.1 A4           */

#define RFAL_ISO15693_IGNORE_BITS       rfalConvBytesToBits(2)                       /*!< Ignore collisions before the UID (RES_FLAG + DSFID)                             */


/*******************************************************************************/

#define RFAL_LM_GT                      rfalConvUsTo1fc(100)                         /*!< Listen Mode Guard Time enforced (GT - Passive; TIRFG - Active)                  */
#define RFAL_FDT_POLL_ADJUSTMENT        rfalConvUsTo1fc(80)                          /*!< FDT Poll adjustment: Time between the expiration of GPT to the actual Tx        */
#define RFAL_FDT_LISTEN_MRT_ADJUSTMENT  64                                           /*!< MRT jitter adjustment: timeout will be between [ tout ; tout + 64 cycles ]      */
#define RFAL_AP2P_FIELDOFF_TRFW         rfalConv8fcTo1fc(64)                         /*!< Time after TXE and Field Off in AP2P Trfw: 37.76us -> 64  (8/fc)                */


/*! FWT adjustment: 
 *    64 : NRT jitter between TXE and NRT start      */
#define RFAL_FWT_ADJUSTMENT             64

/*! FWT ISO14443A adjustment:  
 *   512  : Initial 4bit length                      */
#define RFAL_FWT_A_ADJUSTMENT           512

/*! FWT ISO14443B adjustment:  
 *   2784 : Adjustment for the SOF and initial byte  */
#define RFAL_FWT_B_ADJUSTMENT           2784


/*! FWT FeliCa 212 adjustment:  
 *    1024 : Length of the two Sync bytes at 212kbps */
#define RFAL_FWT_F_212_ADJUSTMENT       1024

/*! FWT FeliCa 424 adjustment:  
 *    512 : Length of the two Sync bytes at 424kbps  */
#define RFAL_FWT_F_424_ADJUSTMENT       512


/*! Time between our field Off and other peer field On : Tadt + (n x Trfw)
 * Ecma 340 11.1.2 - Tadt: [56.64 , 188.72] us ;  n: [0 , 3]  ; Trfw = 37.76 us        
 * Should be: 189 + (3*38) = 303us ; we'll use a more relaxed setting: 605 us    */
#define RFAL_AP2P_FIELDON_TADTTRFW      rfalConvUsTo1fc(605)


/*! FDT Poll adjustment for ISO14443A   EMVCo 2.6  4.8.1.3  ;  Digital 1.1  6.10
 *
 *  276: Time from the rising pulse of the pause of the logic '1' (i.e. the time point to measure the deaftime from), 
 *       to the actual end of the EOF sequence (the point where the MRT starts). Please note that the ST25R391x uses the 
 *       ISO14443-2 definition where the EOF consists of logic '0' followed by sequence Y. 
 */
#define RFAL_FDT_LISTEN_A_ADJUSTMENT    276


/*! FDT Poll adjustment for ISO14443B   EMVCo 2.6  4.8.1.6  ;  Digital 1.1  7.9
 *
 *  340: Time from the rising edge of the EoS to the starting point of the MRT timer (sometime after the final high 
 *       part of the EoS is completed).
 *       
 *  -64: Adjustment for the TR1PUTMIN.
 *       The TR1PUTMIN of the ST25R3911 is 1152/fc (72/fs). The EMVCo test case TB0000 measures the TR1PUTMIN.
 *       It takes the default value of TR1PUTMIN (79/fs) and reduces it by 128/fc in every iteration.
 *       This results in a TR1PUTMIN of 1136/fc (71/fs) for the second iteration. The ST25R3911 sends a NAK because 
 *       the TR1PUTMIN of the ST25R3911 (72/fs) is higher than 71/fs.
 *       Therefore the test suite assumes TR1PUTMIN of 1264/fc (79/fs). 
 *       The test cases TB340.0 and TB435.0 uses the TR1PUTMIN to send frames too early. In order to correctly 
 *       recognise these frames as being sent too early (starting inside reader deaf time), the MRT has to be
 *       increased by at least 64/fc (8/fs).
 */
#define RFAL_FDT_LISTEN_B_ADJUSTMENT    (340 - 64)



/*
******************************************************************************
* GLOBAL MACROS
******************************************************************************
*/

#define rfalCalcNumBytes( nBits )                (uint32_t)( (nBits + 7) / 8 )        /*!< Returns the number of bytes required to fit given the number of bits */

#define rfalTimerStart( timer, time_ms )         timer = platformTimerCreate(time_ms) /*!< Configures and starts the RTOX timer          */
#define rfalTimerisExpired( timer )              platformTimerIsExpired( timer )      /*!< Checks if timer has expired                   */

#define rfalST25R3911ObsModeDisable()            st25r3911WriteTestRegister(0x01, 0x00, mST25, mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 )                   /*!< Disable ST25R3911 Observation mode                                                               */
#define rfalST25R3911ObsModeTx()                 st25r3911WriteTestRegister(0x01, gRFAL.conf.obsvModeTx, mST25, mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 )  /*!< Enable Observation mode 0x0A CSI: Digital TX modulation signal CSO: none                         */
#define rfalST25R3911ObsModeRx()                 st25r3911WriteTestRegister(0x01, gRFAL.conf.obsvModeRx, mST25, mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 )  /*!< Enable Observation mode 0x04 CSI: Digital output of AM channel CSO: Digital output of PM channel */


#define rfalCheckDisableObsMode()                if(gRFAL.conf.obsvModeRx){ rfalST25R3911ObsModeDisable(); }    /*!< Checks if the observation mode is enabled, and applies on ST25R3911 */
#define rfalCheckEnableObsModeTx()               if(gRFAL.conf.obsvModeTx){ rfalST25R3911ObsModeTx(); }         /*!< Checks if the observation mode is enabled, and applies on ST25R3911 */
#define rfalCheckEnableObsModeRx()               if(gRFAL.conf.obsvModeRx){ rfalST25R3911ObsModeRx(); }         /*!< Checks if the observation mode is enabled, and applies on ST25R3911 */


#define rfalGetIncmplBits( FIFOStatus2 )         (( FIFOStatus2 >> 1) & 0x07)                                             /*!< Returns the number of bits from fifo status */
#define rfalIsIncompleteByteError( error )       ((error >= ERR_INCOMPLETE_BYTE) && (error <= ERR_INCOMPLETE_BYTE_07))    /*!< Checks if given error is a Incomplete error */

#define rfalConvBR2ACBR( b )                     (((b+1)<<RFAL_ANALOG_CONFIG_BITRATE_SHIFT) & RFAL_ANALOG_CONFIG_BITRATE_MASK) /*!< Converts ST25R391x Bit rate to Analog Configuration bit rate id */


#define rfalIsModeActiveComm( md )               ( (md == RFAL_MODE_POLL_ACTIVE_P2P) || (md == RFAL_MODE_LISTEN_ACTIVE_P2P) )  /*!< Checks if mode md is Active Communication */
#define rfalIsModePassiveComm( md )              ( !rfalIsModeActiveComm(md) )                                                 /*!< Checks if mode md is Passive Communication*/
#define rfalIsModePassiveListen( md )            ( (md == RFAL_MODE_LISTEN_NFCA) || (md == RFAL_MODE_LISTEN_NFCB) || (md == RFAL_MODE_LISTEN_NFCF) ) /*!< Checks if mode md is Passive Listen */
#define rfalIsModePassivePoll( md )              ( rfalIsModePassiveComm(md) && !rfalIsModePassiveListen(md) )                 /*!< Checks if mode md is Passive Poll         */

/*
 ******************************************************************************
 * LOCAL VARIABLES
 ******************************************************************************
 */

static rfal gRFAL;              /*!< RFAL module instance               */

/*
******************************************************************************
* LOCAL FUNCTION PROTOTYPES
******************************************************************************
*/

static void rfalTransceiveTx( SPI* mspiChannel, ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 );
static void rfalTransceiveRx( SPI* mspiChannel, ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 );
static ReturnCode rfalTransceiveRunBlockingTx( SPI* mspiChannel, ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 );
static void rfalPrepareTransceive( SPI* mspiChannel, ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 );
static void rfalCleanupTransceive( SPI* mspiChannel, ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 );
static void rfalErrorHandling( ST25R3911* mST25, SPI * mspiChannel, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 );
static ReturnCode rfalRunTransceiveWorker( SPI* mspiChannel, ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 );
static ReturnCode rfalRunListenModeWorker( SPI* mspiChannel, ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 );
static void rfalRunWakeUpModeWorker( SPI* mspiChannel, ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 );

static void rfalFIFOStatusUpdate( ST25R3911* mST25, SPI * mspiChannel, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 );
static void rfalFIFOStatusClear( void );
static bool rfalFIFOStatusIsMissingPar( ST25R3911* mST25, SPI * mspiChannel, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 );
static bool rfalFIFOStatusIsIncompleteByte( ST25R3911* mST25, SPI * mspiChannel, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 );
static uint8_t rfalFIFOStatusGetNumBytes( ST25R3911* mST25, SPI * mspiChannel, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 );
static uint8_t rfalFIFOGetNumIncompleteBits( ST25R3911* mST25, SPI * mspiChannel, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 );


/*
******************************************************************************
* GLOBAL FUNCTIONS
******************************************************************************
*/

/*******************************************************************************/
ReturnCode rfalInitialize( SPI* mspiChannel, ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01,
		DigitalOut* fieldLED_02, DigitalOut* fieldLED_03,
		DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 )
{

    st25r3911InitInterrupts( fieldLED_06 );
    
    /* Initialize chip */
    st25r3911Initialize( mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    
    /* Check expected chip: ST25R3911 */
    if( !st25r3911CheckChipID( NULL, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 )  )
    {
        return ERR_HW_MISMATCH;
    }
    
    /*******************************************************************************/    
    /* Apply RF Chip general initialization */
    rfalSetAnalogConfig( RFAL_ANALOG_CONFIG_TECH_CHIP, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;

    /*******************************************************************************/
    /* Set FIFO Water Levels to be used */
    st25r3911ChangeRegisterBits( ST25R3911_REG_IO_CONF1, (ST25R3911_REG_IO_CONF1_fifo_lt | ST25R3911_REG_IO_CONF1_fifo_lr), (ST25R3911_REG_IO_CONF1_fifo_lt_32bytes | ST25R3911_REG_IO_CONF1_fifo_lr_64bytes), mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    
    /* Always have CRC in FIFO upon reception  */
    st25r3911SetRegisterBits( ST25R3911_REG_AUX, ST25R3911_REG_AUX_crc_2_fifo, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    
    /* Enable External Field Detector */
    st25r3911SetRegisterBits( ST25R3911_REG_AUX, ST25R3911_REG_AUX_en_fd, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    
    /* Disable any previous observation mode */
    rfalST25R3911ObsModeDisable();
    
    /* Clear FIFO status local copy */
    rfalFIFOStatusClear();
    
    
    /*******************************************************************************/
    /* Debug purposes */
    /*logSetLevel( LOG_MODULE_DEFAULT, LOG_LEVEL_INFO ); !!!!!!!!!!!!!!! */
    /* rfalSetObsvMode( 0x0A, 0x04 ); */
    
    /*******************************************************************************/
    gRFAL.state              = RFAL_STATE_INIT;
    gRFAL.mode               = RFAL_MODE_NONE;
    gRFAL.field              = false;
    
    /* Set RFAL default configs */
    gRFAL.conf.obsvModeTx    = RFAL_OBSMODE_DISABLE;
    gRFAL.conf.obsvModeRx    = RFAL_OBSMODE_DISABLE;
    gRFAL.conf.eHandling     = RFAL_ERRORHANDLING_NONE;
    
    /* Transceive set to IDLE */
    gRFAL.TxRx.lastState     = RFAL_TXRX_STATE_IDLE;
    gRFAL.TxRx.state         = RFAL_TXRX_STATE_IDLE;
    
    /* Disable all timings */
    gRFAL.timings.FDTListen  = RFAL_TIMING_NONE;
    gRFAL.timings.FDTPoll    = RFAL_TIMING_NONE;
    gRFAL.timings.GT         = RFAL_TIMING_NONE;
    
    gRFAL.tmr.GT             = RFAL_TIMING_NONE;
    
    gRFAL.callbacks.preTxRx  = NULL;
    gRFAL.callbacks.postTxRx = NULL;
    
#if RFAL_FEATURE_NFCV    
    /* Initialize NFC-V Data */
    gRFAL.nfcvData.ignoreBits = 0;
#endif /* RFAL_FEATURE_NFCV */
    
    /* Initialize Listen Mode */
    gRFAL.Lm.state           = RFAL_LM_STATE_NOT_INIT;
    gRFAL.Lm.brDetected      = RFAL_BR_KEEP;
    
    /* Initialize Wake-Up Mode */
    gRFAL.wum.state = RFAL_WUM_STATE_NOT_INIT;
    
    
    /*******************************************************************************/    
    /* Perform Automatic Calibration (if configured to do so).                     *
     * Registers set by rfalSetAnalogConfig will tell rfalCalibrate what to perform*/
    rfalCalibrate( mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    
    return ERR_NONE;
}


/*******************************************************************************/
ReturnCode rfalCalibrate( SPI* mspiChannel, ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 )
{
    uint16_t resValue;
    
    /* Check if RFAL is not initialized */
    if( gRFAL.state == RFAL_STATE_IDLE )
    {
        return ERR_WRONG_STATE;
    }

    /*******************************************************************************/
    /* Perform ST25R3911 regulators and antenna calibration                        */
    /*******************************************************************************/
    
    /* Automatic regulator adjustment only performed if not set manually on Analog Configs */
    if( st25r3911CheckReg( ST25R3911_REG_REGULATOR_CONTROL, ST25R3911_REG_REGULATOR_CONTROL_reg_s, 0x00, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 )  )
    {
        /* Adjust the regulators so that Antenna Calibrate has better Regulator values */
        st25r3911AdjustRegulators( &resValue, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    }
    
    /* Automatic Antenna calibration only performed if not set manually on Analog Configs */
    if( st25r3911CheckReg( ST25R3911_REG_ANT_CAL_CONTROL, ST25R3911_REG_ANT_CAL_CONTROL_trim_s, 0x00, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 )  )
    {
        st25r3911CalibrateAntenna( (uint8_t*) &resValue, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
      
        /*******************************************************************************/
        /* REMARK: Silicon workaround ST25R3911 Errata #1.5                            */
        /* Always run the command Calibrate Antenna twice                              */
        st25r3911CalibrateAntenna( (uint8_t*) &resValue, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
        /*******************************************************************************/
        
    }
    else
    {
        /* If no antenna calibration is performed there is no need to perform second regulator adjustment again */
        return ERR_NONE; 
    }
    
    if( st25r3911CheckReg( ST25R3911_REG_REGULATOR_CONTROL, ST25R3911_REG_REGULATOR_CONTROL_reg_s, 0x00, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 )  )
    {
        /* Adjust the regulators again with the Antenna calibrated */
        st25r3911AdjustRegulators( &resValue, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    }
    
    return ERR_NONE;
}


/*******************************************************************************/
ReturnCode rfalAdjustRegulators( uint16_t* result, SPI *mspiChannel, ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 )
{
    /*******************************************************************************/
    /* Make use of the Automatic Adjust  */
    st25r3911ClrRegisterBits( ST25R3911_REG_REGULATOR_CONTROL, ST25R3911_REG_REGULATOR_CONTROL_reg_s, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    
    return st25r3911AdjustRegulators( result, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
}


/*******************************************************************************/
void rfalSetUpperLayerCallback( rfalUpperLayerCallback pFunc )
{
    st25r3911IRQCallbackSet( pFunc );
}


/*******************************************************************************/
void rfalSetPreTxRxCallback( rfalPreTxRxCallback pFunc )
{
    gRFAL.callbacks.preTxRx = pFunc;
}


/*******************************************************************************/
void rfalSetPostTxRxCallback( rfalPostTxRxCallback pFunc )
{
    gRFAL.callbacks.postTxRx = pFunc;
}


/*******************************************************************************/
ReturnCode rfalDeinitialize( SPI* mspiChannel, ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 )
{
    /* Deinitialize chip */
    st25r3911Deinitialize(mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
 
    gRFAL.state = RFAL_STATE_IDLE;
    return ERR_NONE;
}


/*******************************************************************************/
void rfalSetObsvMode( uint8_t txMode, uint8_t rxMode )
{
    gRFAL.conf.obsvModeTx = txMode;
    gRFAL.conf.obsvModeRx = rxMode;
}


/*******************************************************************************/
void rfalGetObsvMode( uint8_t* txMode, uint8_t* rxMode )
{
    if(txMode != NULL)
    {
        *txMode = gRFAL.conf.obsvModeTx;
    }
    
    if(rxMode != NULL)
    {
        *rxMode = gRFAL.conf.obsvModeRx;
    }
}


/*******************************************************************************/
void rfalDisableObsvMode( void )
{
    gRFAL.conf.obsvModeTx = RFAL_OBSMODE_DISABLE;
    gRFAL.conf.obsvModeRx = RFAL_OBSMODE_DISABLE;
}


/*******************************************************************************/
ReturnCode rfalSetMode( rfalMode mode, rfalBitRate txBR, rfalBitRate rxBR,SPI* mspiChannel, ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 )
{

    /* Check if RFAL is not initialized */
    if( gRFAL.state == RFAL_STATE_IDLE )
    {
        return ERR_WRONG_STATE;
    }
    
    /* Check allowed bit rate value */
    if( (txBR == RFAL_BR_KEEP) || (rxBR == RFAL_BR_KEEP) )
    {
        return ERR_PARAM;
    }
   
    switch( mode )
    {
        /*******************************************************************************/
        case RFAL_MODE_POLL_NFCA:
            
            /* Disable wake up mode, if set */
            st25r3911ClrRegisterBits( ST25R3911_REG_OP_CONTROL, ST25R3911_REG_OP_CONTROL_wu, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            
            /* Enable ISO14443A mode */
            mST25 -> writeRegister(ST25R3911_REG_MODE, ST25R3911_REG_MODE_om_iso14443a, mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            
            /* Set Analog configurations for this mode and bit rate */
            rfalSetAnalogConfig( (RFAL_ANALOG_CONFIG_POLL | RFAL_ANALOG_CONFIG_TECH_NFCA | RFAL_ANALOG_CONFIG_BITRATE_COMMON | RFAL_ANALOG_CONFIG_TX), mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            rfalSetAnalogConfig( (RFAL_ANALOG_CONFIG_POLL | RFAL_ANALOG_CONFIG_TECH_NFCA | RFAL_ANALOG_CONFIG_BITRATE_COMMON | RFAL_ANALOG_CONFIG_RX), mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            break;
            
        /*******************************************************************************/
        case RFAL_MODE_POLL_NFCA_T1T:
            /* Disable wake up mode, if set */
            st25r3911ClrRegisterBits( ST25R3911_REG_OP_CONTROL, ST25R3911_REG_OP_CONTROL_wu, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            
            /* Enable Topaz mode */
            mST25 ->writeRegister( ST25R3911_REG_MODE, ST25R3911_REG_MODE_om_topaz, mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            
            /* Set Analog configurations for this mode and bit rate */
            rfalSetAnalogConfig( (RFAL_ANALOG_CONFIG_POLL | RFAL_ANALOG_CONFIG_TECH_NFCA | RFAL_ANALOG_CONFIG_BITRATE_COMMON | RFAL_ANALOG_CONFIG_TX), mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            rfalSetAnalogConfig( (RFAL_ANALOG_CONFIG_POLL | RFAL_ANALOG_CONFIG_TECH_NFCA | RFAL_ANALOG_CONFIG_BITRATE_COMMON | RFAL_ANALOG_CONFIG_RX), mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            break;
            
        /*******************************************************************************/
        case RFAL_MODE_POLL_NFCB:
            
            /* Disable wake up mode, if set */
            st25r3911ClrRegisterBits( ST25R3911_REG_OP_CONTROL, ST25R3911_REG_OP_CONTROL_wu, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            
            /* Enable ISO14443B mode */
            mST25 -> writeRegister(ST25R3911_REG_MODE, ST25R3911_REG_MODE_om_iso14443b, mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            
            /* Set the EGT, SOF, EOF and EOF */
            st25r3911ChangeRegisterBits(  ST25R3911_REG_ISO14443B_1, 
                                      (ST25R3911_REG_ISO14443B_1_mask_egt | ST25R3911_REG_ISO14443B_1_mask_sof | ST25R3911_REG_ISO14443B_1_mask_eof), 
                                      ( (0<<ST25R3911_REG_ISO14443B_1_shift_egt) | ST25R3911_REG_ISO14443B_1_sof_0_10etu | ST25R3911_REG_ISO14443B_1_sof_1_2etu),
									  mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
                        
            /* Set the minimum TR1, SOF, EOF and EOF12 */
            st25r3911ChangeRegisterBits( ST25R3911_REG_ISO14443B_2, 
                                      (ST25R3911_REG_ISO14443B_2_mask_tr1 | ST25R3911_REG_ISO14443B_2_no_sof | ST25R3911_REG_ISO14443B_2_no_eof |ST25R3911_REG_ISO14443B_2_eof_12),
                                      (ST25R3911_REG_ISO14443B_2_tr1_80fs80fs | ST25R3911_REG_ISO14443B_2_eof_12_10to11etu ),
									  mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;


            /* Set Analog configurations for this mode and bit rate */
            rfalSetAnalogConfig( (RFAL_ANALOG_CONFIG_POLL | RFAL_ANALOG_CONFIG_TECH_NFCB | RFAL_ANALOG_CONFIG_BITRATE_COMMON | RFAL_ANALOG_CONFIG_TX), mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            rfalSetAnalogConfig( (RFAL_ANALOG_CONFIG_POLL | RFAL_ANALOG_CONFIG_TECH_NFCB | RFAL_ANALOG_CONFIG_BITRATE_COMMON | RFAL_ANALOG_CONFIG_RX), mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            break;
            
        /*******************************************************************************/
        case RFAL_MODE_POLL_B_PRIME:
            
            /* Disable wake up mode, if set */
            st25r3911ClrRegisterBits( ST25R3911_REG_OP_CONTROL, ST25R3911_REG_OP_CONTROL_wu, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            
            /* Enable ISO14443B mode */
            mST25->writeRegister(ST25R3911_REG_MODE, ST25R3911_REG_MODE_om_iso14443b, mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 );
            
            /* Set the EGT, SOF, EOF and EOF */
            st25r3911ChangeRegisterBits(  ST25R3911_REG_ISO14443B_1, 
                                      (ST25R3911_REG_ISO14443B_1_mask_egt | ST25R3911_REG_ISO14443B_1_mask_sof | ST25R3911_REG_ISO14443B_1_mask_eof), 
                                      ( (0<<ST25R3911_REG_ISO14443B_1_shift_egt) | ST25R3911_REG_ISO14443B_1_sof_0_10etu | ST25R3911_REG_ISO14443B_1_sof_1_2etu),
									  mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
                        
            /* Set the minimum TR1, EOF and EOF12 */
            st25r3911ChangeRegisterBits( ST25R3911_REG_ISO14443B_2, 
                                      (ST25R3911_REG_ISO14443B_2_mask_tr1 | ST25R3911_REG_ISO14443B_2_no_sof | ST25R3911_REG_ISO14443B_2_no_eof |ST25R3911_REG_ISO14443B_2_eof_12),
                                      (ST25R3911_REG_ISO14443B_2_tr1_80fs80fs | ST25R3911_REG_ISO14443B_2_no_sof | ST25R3911_REG_ISO14443B_2_eof_12_10to12etu ),
									  mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;


            /* Set Analog configurations for this mode and bit rate */
            rfalSetAnalogConfig( (RFAL_ANALOG_CONFIG_POLL | RFAL_ANALOG_CONFIG_TECH_NFCB | RFAL_ANALOG_CONFIG_BITRATE_COMMON | RFAL_ANALOG_CONFIG_TX), mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            rfalSetAnalogConfig( (RFAL_ANALOG_CONFIG_POLL | RFAL_ANALOG_CONFIG_TECH_NFCB | RFAL_ANALOG_CONFIG_BITRATE_COMMON | RFAL_ANALOG_CONFIG_RX), mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            break;
            
        /*******************************************************************************/
        case RFAL_MODE_POLL_B_CTS:
            
            /* Disable wake up mode, if set */
            st25r3911ClrRegisterBits( ST25R3911_REG_OP_CONTROL, ST25R3911_REG_OP_CONTROL_wu, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            
            /* Enable ISO14443B mode */
            mST25->writeRegister(ST25R3911_REG_MODE, ST25R3911_REG_MODE_om_iso14443b, mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            
            /* Set the EGT, SOF, EOF and EOF */
            st25r3911ChangeRegisterBits(  ST25R3911_REG_ISO14443B_1, 
                                      (ST25R3911_REG_ISO14443B_1_mask_egt | ST25R3911_REG_ISO14443B_1_mask_sof | ST25R3911_REG_ISO14443B_1_mask_eof), 
                                      ( (0<<ST25R3911_REG_ISO14443B_1_shift_egt) | ST25R3911_REG_ISO14443B_1_sof_0_10etu | ST25R3911_REG_ISO14443B_1_sof_1_2etu),
									  mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
                        
            /* Set the minimum TR1, clear SOF, EOF and EOF12 */
            st25r3911ChangeRegisterBits( ST25R3911_REG_ISO14443B_2, 
                                      (ST25R3911_REG_ISO14443B_2_mask_tr1 | ST25R3911_REG_ISO14443B_2_no_sof | ST25R3911_REG_ISO14443B_2_no_eof |ST25R3911_REG_ISO14443B_2_eof_12),
                                      (ST25R3911_REG_ISO14443B_2_tr1_80fs80fs | ST25R3911_REG_ISO14443B_2_no_sof | ST25R3911_REG_ISO14443B_2_no_eof ),
									  mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;


            /* Set Analog configurations for this mode and bit rate */
            rfalSetAnalogConfig( (RFAL_ANALOG_CONFIG_POLL | RFAL_ANALOG_CONFIG_TECH_NFCB | RFAL_ANALOG_CONFIG_BITRATE_COMMON | RFAL_ANALOG_CONFIG_TX), mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            rfalSetAnalogConfig( (RFAL_ANALOG_CONFIG_POLL | RFAL_ANALOG_CONFIG_TECH_NFCB | RFAL_ANALOG_CONFIG_BITRATE_COMMON | RFAL_ANALOG_CONFIG_RX), mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            break;
            
        /*******************************************************************************/
        case RFAL_MODE_POLL_NFCF:
            
            /* Disable wake up mode, if set */
            st25r3911ClrRegisterBits( ST25R3911_REG_OP_CONTROL, ST25R3911_REG_OP_CONTROL_wu, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            
            /* Enable FeliCa mode */
            mST25->writeRegister( ST25R3911_REG_MODE, ST25R3911_REG_MODE_om_felica, mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            
            /* Set Analog configurations for this mode and bit rate */
            rfalSetAnalogConfig( (RFAL_ANALOG_CONFIG_POLL | RFAL_ANALOG_CONFIG_TECH_NFCF | RFAL_ANALOG_CONFIG_BITRATE_COMMON | RFAL_ANALOG_CONFIG_TX), mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            rfalSetAnalogConfig( (RFAL_ANALOG_CONFIG_POLL | RFAL_ANALOG_CONFIG_TECH_NFCF | RFAL_ANALOG_CONFIG_BITRATE_COMMON | RFAL_ANALOG_CONFIG_RX), mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            break;
        
        /*******************************************************************************/
        case RFAL_MODE_POLL_NFCV:
        case RFAL_MODE_POLL_PICOPASS:
        
            /* Disable wake up mode, if set */
            st25r3911ClrRegisterBits( ST25R3911_REG_OP_CONTROL, ST25R3911_REG_OP_CONTROL_wu, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            
            /* Set Analog configurations for this mode and bit rate */
            rfalSetAnalogConfig( (RFAL_ANALOG_CONFIG_POLL | RFAL_ANALOG_CONFIG_TECH_NFCV | RFAL_ANALOG_CONFIG_BITRATE_COMMON | RFAL_ANALOG_CONFIG_TX), mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            rfalSetAnalogConfig( (RFAL_ANALOG_CONFIG_POLL | RFAL_ANALOG_CONFIG_TECH_NFCV | RFAL_ANALOG_CONFIG_BITRATE_COMMON | RFAL_ANALOG_CONFIG_RX), mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            break;

        /*******************************************************************************/
        case RFAL_MODE_POLL_ACTIVE_P2P:
            
            /* Set NFCIP1 active communication initiator mode and Enable NFC Automatic Response RF Collision Avoidance */
        	mST25->writeRegister(ST25R3911_REG_MODE, (ST25R3911_REG_MODE_targ_init | ST25R3911_REG_MODE_om_nfc | ST25R3911_REG_MODE_nfc_ar),
        			mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            
            /* Set GPT to start after end of TX, as GPT is used in active communication mode to timeout the field switching off */
            /* The field is turned off 37.76us after the end of the transmission  Trfw                                          */
            st25r3911StartGPTimer_8fcs( rfalConv1fcTo8fc( RFAL_AP2P_FIELDOFF_TRFW ), ST25R3911_REG_GPT_CONTROL_gptc_etx_nfc,
            		mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            
            /* Enable External Field Detector */
            st25r3911SetRegisterBits( ST25R3911_REG_AUX, ST25R3911_REG_AUX_en_fd, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            
            /* Set Analog configurations for this mode and bit rate */
            rfalSetAnalogConfig( (RFAL_ANALOG_CONFIG_POLL | RFAL_ANALOG_CONFIG_TECH_AP2P | RFAL_ANALOG_CONFIG_BITRATE_COMMON | RFAL_ANALOG_CONFIG_TX), mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            rfalSetAnalogConfig( (RFAL_ANALOG_CONFIG_POLL | RFAL_ANALOG_CONFIG_TECH_AP2P | RFAL_ANALOG_CONFIG_BITRATE_COMMON | RFAL_ANALOG_CONFIG_RX), mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            break;
        
        /*******************************************************************************/
        case RFAL_MODE_LISTEN_ACTIVE_P2P:

            /* Set NFCIP1 active communication initiator mode and Enable NFC Automatic Response RF Collision Avoidance */
        	mST25-> writeRegister(ST25R3911_REG_MODE, (ST25R3911_REG_MODE_targ_targ | ST25R3911_REG_MODE_om_nfcip1_normal_mode | ST25R3911_REG_MODE_nfc_ar),
        			mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            
            /* Set GPT to start after end of TX, as GPT is used in active communication mode to timeout the field switching off */
            /* The field is turned off 37.76us after the end of the transmission  Trfw                                          */
            st25r3911StartGPTimer_8fcs( rfalConv1fcTo8fc( RFAL_AP2P_FIELDOFF_TRFW ), ST25R3911_REG_GPT_CONTROL_gptc_etx_nfc, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 );
            
            /* Enable External Field Detector */
            st25r3911SetRegisterBits( ST25R3911_REG_AUX, ST25R3911_REG_AUX_en_fd, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            
            /* Set Analog configurations for this mode and bit rate */
            rfalSetAnalogConfig( (RFAL_ANALOG_CONFIG_LISTEN | RFAL_ANALOG_CONFIG_TECH_AP2P | RFAL_ANALOG_CONFIG_BITRATE_COMMON | RFAL_ANALOG_CONFIG_TX), mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            rfalSetAnalogConfig( (RFAL_ANALOG_CONFIG_LISTEN | RFAL_ANALOG_CONFIG_TECH_AP2P | RFAL_ANALOG_CONFIG_BITRATE_COMMON | RFAL_ANALOG_CONFIG_RX), mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            break;
            
        /*******************************************************************************/
        case RFAL_MODE_LISTEN_NFCA:
        case RFAL_MODE_LISTEN_NFCB:
        case RFAL_MODE_LISTEN_NFCF:
            return ERR_NOTSUPP;
            
        /*******************************************************************************/
        default:
            return ERR_NOT_IMPLEMENTED;
    }
    
    /* Set state as STATE_MODE_SET only if not initialized yet (PSL) */
    gRFAL.state = ((gRFAL.state < RFAL_STATE_MODE_SET) ? RFAL_STATE_MODE_SET : gRFAL.state);
    gRFAL.mode  = mode;
    
    /* Apply the given bit rate */
    return rfalSetBitRate(txBR, rxBR, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
}


/*******************************************************************************/
rfalMode rfalGetMode( void )
{
    return gRFAL.mode;
}


/*******************************************************************************/
ReturnCode rfalSetBitRate( rfalBitRate txBR, rfalBitRate rxBR, SPI* mspiChannel, ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 )
{
    ReturnCode ret;
    
    /* Check if RFAL is not initialized */
    if( gRFAL.state == RFAL_STATE_IDLE )
    {
        return ERR_WRONG_STATE;
    }
   
    /* Store the new Bit Rates */
    gRFAL.txBR = ((txBR == RFAL_BR_KEEP) ? gRFAL.txBR : txBR);
    gRFAL.rxBR = ((rxBR == RFAL_BR_KEEP) ? gRFAL.rxBR : rxBR);
    
    /* Update the bitrate reg if not in NFCV mode (streaming) */
    if( (RFAL_MODE_POLL_NFCV != gRFAL.mode) && (RFAL_MODE_POLL_PICOPASS != gRFAL.mode) )
    {
        EXIT_ON_ERR( ret, st25r3911SetBitrate( gRFAL.txBR, gRFAL.rxBR, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 )  );
    }
    
    
    switch( gRFAL.mode )
    {
        /*******************************************************************************/
        case RFAL_MODE_POLL_NFCA:
        case RFAL_MODE_POLL_NFCA_T1T:
            
            /* Set Analog configurations for this bit rate */
            rfalSetAnalogConfig( (RFAL_ANALOG_CONFIG_POLL | RFAL_ANALOG_CONFIG_TECH_NFCA | rfalConvBR2ACBR(gRFAL.txBR) | RFAL_ANALOG_CONFIG_TX ), mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            rfalSetAnalogConfig( (RFAL_ANALOG_CONFIG_POLL | RFAL_ANALOG_CONFIG_TECH_NFCA | rfalConvBR2ACBR(gRFAL.rxBR) | RFAL_ANALOG_CONFIG_RX ), mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            break;
            
        /*******************************************************************************/
        case RFAL_MODE_POLL_NFCB:
        case RFAL_MODE_POLL_B_PRIME:
        case RFAL_MODE_POLL_B_CTS:
            
            /* Set Analog configurations for this bit rate */
            rfalSetAnalogConfig( (RFAL_ANALOG_CONFIG_POLL | RFAL_ANALOG_CONFIG_TECH_NFCB | rfalConvBR2ACBR(gRFAL.txBR) | RFAL_ANALOG_CONFIG_TX ), mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            rfalSetAnalogConfig( (RFAL_ANALOG_CONFIG_POLL | RFAL_ANALOG_CONFIG_TECH_NFCB | rfalConvBR2ACBR(gRFAL.rxBR) | RFAL_ANALOG_CONFIG_RX ), mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            break;
            
        /*******************************************************************************/
        case RFAL_MODE_POLL_NFCF:
            
            /* Set Analog configurations for this bit rate */
            rfalSetAnalogConfig( (RFAL_ANALOG_CONFIG_POLL | RFAL_ANALOG_CONFIG_TECH_NFCF | rfalConvBR2ACBR(gRFAL.txBR) | RFAL_ANALOG_CONFIG_TX ), mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            rfalSetAnalogConfig( (RFAL_ANALOG_CONFIG_POLL | RFAL_ANALOG_CONFIG_TECH_NFCF | rfalConvBR2ACBR(gRFAL.rxBR) | RFAL_ANALOG_CONFIG_RX ), mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            break;
        
        /*******************************************************************************/
        case RFAL_MODE_POLL_NFCV:
        case RFAL_MODE_POLL_PICOPASS:
        
           #if !RFAL_FEATURE_NFCV
                return ERR_DISABLED;
           #else
               
                if( ((gRFAL.rxBR != RFAL_BR_26p48) && (gRFAL.rxBR != RFAL_BR_52p97)) || ((gRFAL.txBR != RFAL_BR_1p66) && (gRFAL.txBR != RFAL_BR_26p48)) )
                {
                    return ERR_PARAM;
                }
        
                {
                    const struct iso15693StreamConfig *stream_config;
                    iso15693PhyConfig_t                config;
                        
                    config.coding     = (( gRFAL.txBR == RFAL_BR_1p66  ) ? ISO15693_VCD_CODING_1_256 : ISO15693_VCD_CODING_1_4);
                    config.fastMode   = (( gRFAL.rxBR == RFAL_BR_52p97 ) ? true : false);
                    
                    iso15693PhyConfigure(&config, &stream_config);
                    st25r3911StreamConfigure((struct st25r3911StreamConfig*)stream_config, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
                }
    
                /* Set Analog configurations for this bit rate */
                rfalSetAnalogConfig( (RFAL_ANALOG_CONFIG_POLL | RFAL_ANALOG_CONFIG_TECH_NFCV | rfalConvBR2ACBR(gRFAL.txBR) | RFAL_ANALOG_CONFIG_TX ), mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
                rfalSetAnalogConfig( (RFAL_ANALOG_CONFIG_POLL | RFAL_ANALOG_CONFIG_TECH_NFCV | rfalConvBR2ACBR(gRFAL.rxBR) | RFAL_ANALOG_CONFIG_RX ), mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
                break;
            
            #endif /* RFAL_FEATURE_NFCV */
                
        
        /*******************************************************************************/
        case RFAL_MODE_POLL_ACTIVE_P2P:
            
            /* Set Analog configurations for this bit rate */
            rfalSetAnalogConfig( (RFAL_ANALOG_CONFIG_POLL | RFAL_ANALOG_CONFIG_TECH_AP2P | rfalConvBR2ACBR(gRFAL.txBR) | RFAL_ANALOG_CONFIG_TX ), mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            rfalSetAnalogConfig( (RFAL_ANALOG_CONFIG_POLL | RFAL_ANALOG_CONFIG_TECH_AP2P | rfalConvBR2ACBR(gRFAL.rxBR) | RFAL_ANALOG_CONFIG_RX ), mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            break;
        
        /*******************************************************************************/
        case RFAL_MODE_LISTEN_ACTIVE_P2P:
            
            /* Set Analog configurations for this bit rate */
            rfalSetAnalogConfig( (RFAL_ANALOG_CONFIG_LISTEN | RFAL_ANALOG_CONFIG_TECH_AP2P | rfalConvBR2ACBR(gRFAL.txBR) | RFAL_ANALOG_CONFIG_TX ), mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            rfalSetAnalogConfig( (RFAL_ANALOG_CONFIG_LISTEN | RFAL_ANALOG_CONFIG_TECH_AP2P | rfalConvBR2ACBR(gRFAL.rxBR) | RFAL_ANALOG_CONFIG_RX ), mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            break;
            
        /*******************************************************************************/
        case RFAL_MODE_LISTEN_NFCA:
        case RFAL_MODE_LISTEN_NFCB:
        case RFAL_MODE_LISTEN_NFCF:
        case RFAL_MODE_NONE:
            return ERR_WRONG_STATE;
            
        /*******************************************************************************/
        default:
            return ERR_NOT_IMPLEMENTED;
    }
    
    return ERR_NONE;
}


/*******************************************************************************/
ReturnCode rfalGetBitRate( rfalBitRate *txBR, rfalBitRate *rxBR )
{
    if( (gRFAL.state == RFAL_STATE_IDLE) || (gRFAL.mode == RFAL_MODE_NONE) )
    {
        return ERR_WRONG_STATE;
    }
    
    if( txBR != NULL )
    {
        *txBR = gRFAL.txBR;
    }
    
    if( rxBR != NULL )
    {
        *rxBR = gRFAL.rxBR;
    }
    
    return ERR_NONE;
}


/*******************************************************************************/
ReturnCode rfalSetModulatedRFO( uint8_t rfo, SPI* mspiChannel, ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 )
{
	  mST25->writeRegister( ST25R3911_REG_RFO_AM_ON_LEVEL, rfo, mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;

    return ERR_NONE;
}


/*******************************************************************************/
uint8_t rfalGetModulatedRFO( SPI* mspiChannel, ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 )
{
    uint8_t ret;
    
    mST25->readRegister(ST25R3911_REG_RFO_AM_ON_LEVEL, &ret, mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;

    return ret;
}


/*******************************************************************************/
ReturnCode rfalMeasureRF( uint8_t* result, SPI* mspiChannel, ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 )
{
    st25r3911MeasureRF( result, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;

    return ERR_NONE;
}


/*******************************************************************************/
void rfalSetErrorHandling( rfalEHandling eHandling )
{
    gRFAL.conf.eHandling = eHandling;
}


/*******************************************************************************/
rfalEHandling rfalGetErrorHandling( void )
{
    return gRFAL.conf.eHandling;
}


/*******************************************************************************/
void rfalSetFDTPoll( uint32_t FDTPoll )
{
    gRFAL.timings.FDTPoll = MIN( FDTPoll, RFAL_ST25R3911_GPT_MAX_1FC );
}


/*******************************************************************************/
uint32_t rfalGetFDTPoll( void )
{
    return gRFAL.timings.FDTPoll;
}


/*******************************************************************************/
void rfalSetFDTListen( uint32_t FDTListen )
{
    gRFAL.timings.FDTListen = MIN( FDTListen, RFAL_ST25R3911_MRT_MAX_1FC);
}

/*******************************************************************************/
uint32_t rfalGetFDTListen( void )
{
    return gRFAL.timings.FDTListen;
}

void rfalSetGT( uint32_t GT )
{
    gRFAL.timings.GT = MIN( GT, RFAL_ST25R3911_GT_MAX_1FC );
}

/*******************************************************************************/
uint32_t rfalGetGT( void )
{
    return gRFAL.timings.GT;
}

/*******************************************************************************/
bool rfalIsGTExpired( void )//check here the platform time
{
    if( gRFAL.tmr.GT != RFAL_TIMING_NONE )
    {
        if( !rfalTimerisExpired( gRFAL.tmr.GT ) )
        {
            return false;
        }
    }    
    return true;
}

/*******************************************************************************/
ReturnCode rfalFieldOnAndStartGT( SPI *mspiChannel, ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 )
{
    ReturnCode ret;
    
    /* Check if RFAL has been initialized (Oscillator should be running) and also
     * if a direct register access has been performed and left the Oscillator Off */
    if( (gRFAL.state < RFAL_STATE_INIT) || !st25r3911CheckReg( ST25R3911_REG_OP_CONTROL, ST25R3911_REG_OP_CONTROL_en, ST25R3911_REG_OP_CONTROL_en, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 )  )
    {
        return ERR_WRONG_STATE;
    }
    
    ret = ERR_NONE;
    
    /*******************************************************************************/
    /* Perform collision avoidance and turn field On if not already On */
    if( !gRFAL.field || !( st25r3911CheckReg(ST25R3911_REG_OP_CONTROL, ST25R3911_REG_OP_CONTROL_tx_en, ST25R3911_REG_OP_CONTROL_tx_en, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 )  ) )
    {
        /* Use Thresholds set by AnalogConfig */
        ret = st25r3911PerformCollisionAvoidance( ST25R3911_CMD_RESPONSE_RF_COLLISION_0, ST25R3911_THRESHOLD_DO_NOT_SET, ST25R3911_THRESHOLD_DO_NOT_SET, 0, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
        
        gRFAL.field = ( st25r3911CheckReg(ST25R3911_REG_OP_CONTROL, ST25R3911_REG_OP_CONTROL_tx_en, ST25R3911_REG_OP_CONTROL_tx_en, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 )  );
        
        /* Only turn on Receiver and Transmitter if field was successfully turned On */
        if(gRFAL.field)
        {            
            st25r3911TxRxOn( mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ; /* Enable Tx and Rx (Tx is already On) */
        }
    }
    
    /*******************************************************************************/
    /* Start GT timer in case the GT value is set */
    if( (gRFAL.timings.GT != RFAL_TIMING_NONE) )
    {
        /* Ensure that a SW timer doesn't have a lower value then the minimum  */
        rfalTimerStart( gRFAL.tmr.GT, rfalConv1fcToMs( MAX( (gRFAL.timings.GT), RFAL_ST25R3911_GT_MIN_1FC) ) );
    }
    
    return ret;
}


/*******************************************************************************/
ReturnCode rfalFieldOff( SPI* mspiChannel, ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 )
{
    /* Check whether a TxRx is not yet finished */
    if( gRFAL.TxRx.state != RFAL_TXRX_STATE_IDLE )
    {
        rfalCleanupTransceive(mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    }
    
    /* Disable Tx and Rx */
    st25r3911TxRxOff( mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    gRFAL.field = false;
    
    return ERR_NONE;
}


/*******************************************************************************/
ReturnCode rfalStartTransceive( rfalTransceiveContext *ctx,SPI* mspiChannel, ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 )
{
    uint32_t FxTAdj;  /* FWT or FDT adjustment calculation */
    
    /* Ensure that RFAL is already Initialized and the mode has been set */
    if( (gRFAL.state >= RFAL_STATE_MODE_SET) /*&& (gRFAL.TxRx.state == RFAL_TXRX_STATE_INIT )*/ )
    {
        /*******************************************************************************/
        /* Check whether the field is already On, otherwise no TXE will be received  */
        if( !( st25r3911CheckReg(ST25R3911_REG_OP_CONTROL, ST25R3911_REG_OP_CONTROL_tx_en, ST25R3911_REG_OP_CONTROL_tx_en, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 )  )
        		&& (!rfalIsModePassiveListen( gRFAL.mode ) && (ctx->txBuf != NULL)) )
        {
            return ERR_WRONG_STATE;
        }
        
        gRFAL.TxRx.ctx = *ctx;
        
        /*******************************************************************************/
        if( gRFAL.timings.FDTListen != RFAL_TIMING_NONE )
        {
            /* Calculate MRT adjustment accordingly to the current mode */
            FxTAdj = RFAL_FDT_LISTEN_MRT_ADJUSTMENT;
            if(gRFAL.mode == RFAL_MODE_POLL_NFCA)           FxTAdj += RFAL_FDT_LISTEN_A_ADJUSTMENT;
            else if(gRFAL.mode == RFAL_MODE_POLL_NFCA_T1T)  FxTAdj += RFAL_FDT_LISTEN_A_ADJUSTMENT;
            else if(gRFAL.mode == RFAL_MODE_POLL_NFCB)      FxTAdj += RFAL_FDT_LISTEN_B_ADJUSTMENT;
            
            
            /* Set Minimum FDT(Listen) in which PICC is not allowed to send a response */
                   mST25 -> writeRegister( ST25R3911_REG_MASK_RX_TIMER, rfalConv1fcTo64fc( (FxTAdj > gRFAL.timings.FDTListen) ? RFAL_ST25R3911_MRT_MIN_1FC : (gRFAL.timings.FDTListen - FxTAdj) ), mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 );
        }
        
        /*******************************************************************************/
        /* FDT Poll will be loaded in rfalPrepareTransceive() once the previous was expired */
        
        /*******************************************************************************/
        if( rfalIsModePassiveComm( gRFAL.mode ) )  /* Passive Comms */
        {
            if( (gRFAL.TxRx.ctx.fwt != RFAL_FWT_NONE) && (gRFAL.TxRx.ctx.fwt != 0) )
            {
                FxTAdj = RFAL_FWT_ADJUSTMENT;
                if(gRFAL.mode == RFAL_MODE_POLL_NFCA)           FxTAdj += RFAL_FWT_A_ADJUSTMENT;
                else if(gRFAL.mode == RFAL_MODE_POLL_NFCA_T1T)  FxTAdj += RFAL_FWT_A_ADJUSTMENT;
                else if(gRFAL.mode == RFAL_MODE_POLL_NFCB)      FxTAdj += RFAL_FWT_B_ADJUSTMENT;
                else if(gRFAL.mode == RFAL_MODE_POLL_NFCF)      
                {
                    FxTAdj += ((gRFAL.txBR == RFAL_BR_212) ? RFAL_FWT_F_212_ADJUSTMENT : RFAL_FWT_F_424_ADJUSTMENT );
                }
                
                /* Ensure that the given FWT doesn't exceed NRT maximum */
                gRFAL.TxRx.ctx.fwt = MIN( (gRFAL.TxRx.ctx.fwt + FxTAdj), RFAL_ST25R3911_NRT_MAX_1FC );
                
                /* Set FWT in the NRT */
                st25r3911SetNoResponseTime_64fcs( rfalConv1fcTo64fc( gRFAL.TxRx.ctx.fwt ), mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            }
            else
            {
                /* Disable NRT, no NRE will be triggered, therefore wait endlessly for Rx */
                st25r3911SetNoResponseTime_64fcs( RFAL_ST25R3911_NRT_DISABLED, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            }
        }
        else /* Active Comms */
        {
            /* Setup NRT timer for rf response RF collision timeout. */
            st25r3911SetNoResponseTime_64fcs( rfalConv1fcTo64fc(RFAL_AP2P_FIELDON_TADTTRFW), mspiChannel, mST25, gpio_cs, IRQ , fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 );
            
            /* In Active Mode No Response Timer cannot be used to measure FWT a SW timer is used instead */
        }
        
        gRFAL.state       = RFAL_STATE_TXRX;
        gRFAL.TxRx.state  = RFAL_TXRX_STATE_TX_IDLE;
        gRFAL.TxRx.status = ERR_BUSY;
        gRFAL.TxRx.rxse   = false;
        
    #if RFAL_FEATURE_NFCV        
        /*******************************************************************************/
        if( (RFAL_MODE_POLL_NFCV == gRFAL.mode) || (RFAL_MODE_POLL_PICOPASS == gRFAL.mode) )
        { /* Exchange receive buffer with internal buffer */
            gRFAL.nfcvData.origCtx = gRFAL.TxRx.ctx;

            gRFAL.TxRx.ctx.rxBuf    = ((gRFAL.nfcvData.origCtx.rxBuf != NULL) ? gRFAL.nfcvData.codingBuffer : NULL);
            gRFAL.TxRx.ctx.rxBufLen = rfalConvBytesToBits(sizeof(gRFAL.nfcvData.codingBuffer));
            gRFAL.TxRx.ctx.flags = RFAL_TXRX_FLAGS_CRC_TX_MANUAL
                                 | RFAL_TXRX_FLAGS_CRC_RX_KEEP
                                 | RFAL_TXRX_FLAGS_NFCIP1_OFF
                                 | (gRFAL.nfcvData.origCtx.flags & RFAL_TXRX_FLAGS_AGC_OFF)
                                 | RFAL_TXRX_FLAGS_PAR_RX_KEEP
                                 | RFAL_TXRX_FLAGS_PAR_TX_NONE;
            /* In NFCV a transceive with valid txBuf and txBufSize==0 should send first an EOF. 
               In this case avoid below code for going directly into receive.                 */
            if (gRFAL.TxRx.ctx.txBuf) return  ERR_NONE;
        }
    #endif /* RFAL_FEATURE_NFCV */

        
        /*******************************************************************************/
        /* Check if the Transceive start performing Tx or goes directly to Rx          */
        if( (gRFAL.TxRx.ctx.txBuf == NULL) || (gRFAL.TxRx.ctx.txBufLen == 0) )
        {
            /* Disable our field upon a Rx reEnable on AP2P */
            if( rfalIsModeActiveComm(gRFAL.mode) )
            {
            	 st25r3911ClrRegisterBits((unsigned char)(ST25R3911_REG_OP_CONTROL),(unsigned char)(ST25R3911_REG_OP_CONTROL_tx_en), mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            }
            
            /* Clear FIFO, Clear and Enable the Interrupts */
            rfalPrepareTransceive( mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;

            /* No Tx done, enable the Receiver */
            mST25 -> executeCommand( ST25R3911_CMD_UNMASK_RECEIVE_DATA, mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;

            /* Start NRT manually, if FWT = 0 (wait endlessly for Rx) chip will ignore anyhow */
            mST25 -> executeCommand( ST25R3911_CMD_START_NO_RESPONSE_TIMER, mspiChannel,  gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            
            gRFAL.TxRx.state  = RFAL_TXRX_STATE_RX_IDLE;
        }
        
        return ERR_NONE;
    }
    
    return ERR_WRONG_STATE;
}


/*******************************************************************************/
ReturnCode rfalTransceiveBlockingTx( uint8_t* txBuf, uint16_t txBufLen, uint8_t* rxBuf, uint16_t rxBufLen, uint16_t* actLen, uint32_t flags, uint32_t fwt, SPI* mspiChannel, ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 )
{
    ReturnCode               ret;
    rfalTransceiveContext    ctx;
    
    rfalCreateByteFlagsTxRxContext( ctx, txBuf, txBufLen, rxBuf, rxBufLen, actLen, flags, fwt );
    EXIT_ON_ERR( ret, rfalStartTransceive( &ctx, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 )  );
    
    return rfalTransceiveRunBlockingTx( mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
}


/*******************************************************************************/
static ReturnCode rfalTransceiveRunBlockingTx( SPI* mspiChannel, ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 )
{
    ReturnCode ret;
        
    do{
        rfalWorker(mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    }
    while( ((ret = rfalGetTransceiveStatus() ) == ERR_BUSY) && rfalIsTransceiveInTx() );
    
    if( rfalIsTransceiveInRx() )
    {
        return ERR_NONE;
    }
    
    return ret;
}


/*******************************************************************************/
ReturnCode rfalTransceiveBlockingRx( SPI* mspiChannel, ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 )
{
    ReturnCode ret;
    
    do{
        rfalWorker( mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    }
    while( ((ret = rfalGetTransceiveStatus() ) == ERR_BUSY) && rfalIsTransceiveInRx() );    
        
    return ret;
}


/*******************************************************************************/
ReturnCode rfalTransceiveBlockingTxRx( uint8_t* txBuf, uint16_t txBufLen,
		uint8_t* rxBuf, uint16_t rxBufLen, uint16_t* actLen, uint32_t flags,
		uint32_t fwt, SPI* mspiChannel, ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 )
{
    ReturnCode ret;
    
    EXIT_ON_ERR( ret, rfalTransceiveBlockingTx( txBuf, txBufLen, rxBuf, rxBufLen, actLen, flags, fwt, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 )  );
    ret = rfalTransceiveBlockingRx( mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 );
    
    /* Convert received bits to bytes */
    if( actLen != NULL )
    {
        *actLen = rfalConvBitsToBytes(*actLen);
    }
    
    return ret;
}


/*******************************************************************************/
static ReturnCode rfalRunTransceiveWorker( SPI* mspiChannel, ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 )
{
    if( gRFAL.state == RFAL_STATE_TXRX )
    {     
        /* Run Tx or Rx state machines */
        if( rfalIsTransceiveInTx() )
        {
            rfalTransceiveTx( mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            return rfalGetTransceiveStatus();
        }
        else if( rfalIsTransceiveInRx() )
        {
            rfalTransceiveRx( mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            return rfalGetTransceiveStatus();
        }
    }    
    return ERR_WRONG_STATE;
}

/*******************************************************************************/
rfalTransceiveState rfalGetTransceiveState( void )
{
    return gRFAL.TxRx.state;
}

ReturnCode rfalGetTransceiveStatus( void )
{
	uint16_t ERR = uint16_t(ERR_BUSY);
    return ((gRFAL.TxRx.state == RFAL_TXRX_STATE_IDLE) ? gRFAL.TxRx.status : ERR);
}


/*******************************************************************************/
void rfalWorker( SPI* mspiChannel, ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 )
{
    switch( gRFAL.state )
    {
        case RFAL_STATE_TXRX:
            rfalRunTransceiveWorker( mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            break;
            
        case RFAL_STATE_LM:
            rfalRunListenModeWorker( mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            break;
            
        case RFAL_STATE_WUM:
            rfalRunWakeUpModeWorker(mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            break;
            
        /* Nothing to be done */
        default:            
            break;
    }
}


/*******************************************************************************/
static void rfalErrorHandling(  ST25R3911* mST25, SPI * mspiChannel, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 )
{
    uint8_t fifoBytesToRead;
    uint8_t reEnRx[] = { ST25R3911_CMD_CLEAR_FIFO, ST25R3911_CMD_UNMASK_RECEIVE_DATA };
    

    fifoBytesToRead = rfalFIFOStatusGetNumBytes(mST25, mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    
    
    /*******************************************************************************/
    /* EMVCo                                                                       */
    /*******************************************************************************/
    if( gRFAL.conf.eHandling == RFAL_ERRORHANDLING_EMVCO )
    {
        /*******************************************************************************/
        /* EMD Handling - NFC Forum Digital 1.1  4.1.1.1 ; EMVCo 2.6  4.9.2            */
        /* ReEnable the receiver on frames with a length < 4 bytes, upon:              */
        /*   - Collision or Framing error detected                                     */
        /*   - Residual bits are detected (hard framing error)                         */
        /*   - Parity error                                                            */
        /*   - CRC error                                                               */
        /*******************************************************************************/
        
        /* In case there are residual bits decrement FIFO bytes */
        if( rfalFIFOStatusIsIncompleteByte(mST25, mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 )  || rfalFIFOStatusIsMissingPar(mST25, mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 )  )
        {
            fifoBytesToRead--;
        }
            
        if( ( (gRFAL.fifo.bytesTotal + fifoBytesToRead) < RFAL_EMVCO_RX_MAXLEN )            &&
            ( (gRFAL.TxRx.status == ERR_RF_COLLISION) || (gRFAL.TxRx.status == ERR_FRAMING) || 
              (gRFAL.TxRx.status == ERR_PAR)          || (gRFAL.TxRx.status == ERR_CRC)     || 
              rfalFIFOStatusIsIncompleteByte(mST25, mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 )         || rfalFIFOStatusIsMissingPar(mST25, mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 )         ) )
        {
            /* Ignore this reception, ReEnable receiver */
        	    mST25 -> executeCommands( reEnRx, sizeof(reEnRx), mST25, mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            
            rfalFIFOStatusClear();
            gRFAL.fifo.bytesTotal = 0;
            gRFAL.TxRx.status = ERR_BUSY;
            gRFAL.TxRx.state  = RFAL_TXRX_STATE_RX_WAIT_RXS;
        }
        return;
    }

    /*******************************************************************************/
    /* ISO14443A Mode                                                              */
    /*******************************************************************************/
    if( gRFAL.mode == RFAL_MODE_POLL_NFCA )
    {
        
        /*******************************************************************************/
        /* If we received one incomplete byte (not a block and a incomplete byte at    *
         * the end) we`ll raise a specific error ( support for T2T 4 bit ACK / NAK )   *
         * Otherwise just leave it as an CRC/FRAMING/PAR error                         */    
        /*******************************************************************************/
        if( (gRFAL.TxRx.status == ERR_PAR) || (gRFAL.TxRx.status == ERR_CRC) )
        {
            if( rfalFIFOStatusIsIncompleteByte(mST25, mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 )  && (fifoBytesToRead == RFAL_NFC_RX_INCOMPLETE_LEN) )
            {
                mST25 -> readFifo( (uint8_t*)(gRFAL.TxRx.ctx.rxBuf), fifoBytesToRead, mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
                if( gRFAL.TxRx.ctx.rxRcvdLen )  *gRFAL.TxRx.ctx.rxRcvdLen = rfalFIFOGetNumIncompleteBits(mST25, mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
                
                gRFAL.TxRx.status = ERR_INCOMPLETE_BYTE;
                gRFAL.TxRx.state  = RFAL_TXRX_STATE_RX_FAIL;
            }
        }
    }
    
}


/*******************************************************************************/
static void rfalCleanupTransceive( SPI* mspiChannel, ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 )
{
    /*******************************************************************************/
    /* Transceive flags                                                            */
    /*******************************************************************************/
    
    /* Restore default settings on NFCIP1 mode, Receiving parity + CRC bits and manual Tx Parity*/
    st25r3911ClrRegisterBits( ST25R3911_REG_ISO14443A_NFC, (ST25R3911_REG_ISO14443A_NFC_no_tx_par | ST25R3911_REG_ISO14443A_NFC_no_rx_par | ST25R3911_REG_ISO14443A_NFC_nfc_f0),mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    
    /* Restore AGC enabled */
    st25r3911SetRegisterBits( ST25R3911_REG_RX_CONF2, ST25R3911_REG_RX_CONF2_agc_en, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    
    /*******************************************************************************/
    
    
    
    /*******************************************************************************/
    /* Execute Post Transceive Callback                                            */
    /*******************************************************************************/
    if( gRFAL.callbacks.postTxRx != NULL )
    {
        gRFAL.callbacks.postTxRx();
    }
    /*******************************************************************************/

}


/*******************************************************************************/
static void rfalPrepareTransceive( SPI* mspiChannel, ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 )
{
    uint32_t maskInterrupts;
    uint8_t  reg;
    
    /*******************************************************************************/
    /* In the EMVCo mode the NRT will continue to run.                             *
     * For the clear to stop it, the EMV mode has to be disabled before            */
    st25r3911ClrRegisterBits( ST25R3911_REG_GPT_CONTROL, ST25R3911_REG_GPT_CONTROL_nrt_emv, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    
    /* Reset receive logic */
      mST25 -> executeCommand( ST25R3911_CMD_CLEAR_FIFO, mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    
    /* Reset Rx Gain */
      mST25 -> executeCommand( ST25R3911_CMD_CLEAR_SQUELCH, mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    
    
    /*******************************************************************************/
    /* FDT Poll                                                                    */
    /*******************************************************************************/
    if( rfalIsModePassiveComm( gRFAL.mode ) )  /* Passive Comms */
    {
       /* In Passive communications General Purpose Timer is used to measure FDT Poll */
       if( gRFAL.timings.FDTPoll != RFAL_TIMING_NONE )
       {
           /* Configure GPT to start at RX end */
           st25r3911StartGPTimer_8fcs( rfalConv1fcTo8fc( MIN( gRFAL.timings.FDTPoll, (gRFAL.timings.FDTPoll - RFAL_FDT_POLL_ADJUSTMENT) ) ), ST25R3911_REG_GPT_CONTROL_gptc_erx,
        		   mspiChannel,  mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
       }
    }
    
    
    /*******************************************************************************/
    /* Execute Pre Transceive Callback                                             */
    /*******************************************************************************/
    if( gRFAL.callbacks.preTxRx != NULL )
    {
        gRFAL.callbacks.preTxRx();
    }
    /*******************************************************************************/
    
    maskInterrupts = ( ST25R3911_IRQ_MASK_FWL  | ST25R3911_IRQ_MASK_TXE  |
                       ST25R3911_IRQ_MASK_RXS  | ST25R3911_IRQ_MASK_RXE  |
                       ST25R3911_IRQ_MASK_FWL  | ST25R3911_IRQ_MASK_NRE  |
                       ST25R3911_IRQ_MASK_PAR  | ST25R3911_IRQ_MASK_CRC  |
                       ST25R3911_IRQ_MASK_ERR1 | ST25R3911_IRQ_MASK_ERR2  );
    
    
    /*******************************************************************************/
    /* Transceive flags                                                            */
    /*******************************************************************************/
    
    reg = (ST25R3911_REG_ISO14443A_NFC_no_tx_par_off | ST25R3911_REG_ISO14443A_NFC_no_rx_par_off | ST25R3911_REG_ISO14443A_NFC_nfc_f0_off);
    
    /* Check if NFCIP1 mode is to be enabled */
    if( (gRFAL.TxRx.ctx.flags & RFAL_TXRX_FLAGS_NFCIP1_ON) )
    {
        reg |= ST25R3911_REG_ISO14443A_NFC_nfc_f0;
    }
    
    /* Check if Parity check is to be skipped and to keep the parity + CRC bits in FIFO */
    if( (gRFAL.TxRx.ctx.flags & RFAL_TXRX_FLAGS_PAR_RX_KEEP) )
    {
        reg |= ST25R3911_REG_ISO14443A_NFC_no_rx_par;
    }

    /* Check if automatic Parity bits is to be disabled */
    if( (gRFAL.TxRx.ctx.flags & RFAL_TXRX_FLAGS_PAR_TX_NONE) )
    {
        reg |= ST25R3911_REG_ISO14443A_NFC_no_tx_par;
    }
    
    /* Apply current TxRx flags on ISO14443A and NFC 106kb/s Settings Register */
    st25r3911ChangeRegisterBits( ST25R3911_REG_ISO14443A_NFC, (ST25R3911_REG_ISO14443A_NFC_no_tx_par | ST25R3911_REG_ISO14443A_NFC_no_rx_par | ST25R3911_REG_ISO14443A_NFC_nfc_f0),
    		reg, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    
    
    /* Check if AGC is to be disabled */
    if( (gRFAL.TxRx.ctx.flags & RFAL_TXRX_FLAGS_AGC_OFF) )
    {
        st25r3911ClrRegisterBits( ST25R3911_REG_RX_CONF2, ST25R3911_REG_RX_CONF2_agc_en, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    }
    else
    {
        st25r3911SetRegisterBits( ST25R3911_REG_RX_CONF2, ST25R3911_REG_RX_CONF2_agc_en, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    }
    /*******************************************************************************/
    
    

    /*******************************************************************************/
    /* EMVCo NRT mode                                                              */
    /*******************************************************************************/
    if( gRFAL.conf.eHandling == RFAL_ERRORHANDLING_EMVCO )
    {
        st25r3911SetRegisterBits( ST25R3911_REG_GPT_CONTROL, ST25R3911_REG_GPT_CONTROL_nrt_emv, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    }
    else
    {
        st25r3911ClrRegisterBits( ST25R3911_REG_GPT_CONTROL, ST25R3911_REG_GPT_CONTROL_nrt_emv, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    }
    /*******************************************************************************/
    
    
    
    /* In Active comms enable also External Field interrupts  */
    if( rfalIsModeActiveComm( gRFAL.mode ) )
    {
        maskInterrupts |= ( ST25R3911_IRQ_MASK_EOF  | ST25R3911_IRQ_MASK_EON | ST25R3911_IRQ_MASK_CAT | ST25R3911_IRQ_MASK_CAC );
    }
    
    
    /*******************************************************************************/
    /* clear and enable these interrupts */
    st25r3911GetInterrupt( maskInterrupts, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    st25r3911EnableInterrupts( maskInterrupts, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    
    /* Clear FIFO status local copy */
    rfalFIFOStatusClear();
}

/*******************************************************************************/
static void rfalTransceiveTx( SPI* mspiChannel, ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 )
{
    volatile uint32_t irqs;
    uint16_t          tmp;
    ReturnCode        ret;
    
   /* NO_WARNING(ret); */
    
    irqs = ST25R3911_IRQ_MASK_NONE;
    
    if( gRFAL.TxRx.state != gRFAL.TxRx.lastState )
    {        
        /* rfalLogD( "RFAL: lastSt: %d curSt: %d \r\n", gRFAL.TxRx.lastState, gRFAL.TxRx.state ); */
        gRFAL.TxRx.lastState = gRFAL.TxRx.state;
    }
    
    switch( gRFAL.TxRx.state )
    {
        /*******************************************************************************/
        case RFAL_TXRX_STATE_TX_IDLE:
            
            /* Nothing to do */
            
            gRFAL.TxRx.state = RFAL_TXRX_STATE_TX_WAIT_GT ;
            /* fall through */
            
            
        /*******************************************************************************/
        case RFAL_TXRX_STATE_TX_WAIT_GT:
            
            if( !rfalIsGTExpired() )
            {
                break;
            }
            
            gRFAL.tmr.GT = RFAL_TIMING_NONE;
            
            gRFAL.TxRx.state = RFAL_TXRX_STATE_TX_WAIT_FDT;
            /* fall through */
            
            
        /*******************************************************************************/
        case RFAL_TXRX_STATE_TX_WAIT_FDT:
            
            /* Only in Passive communications GPT is used to measure FDT Poll */
            if( rfalIsModePassiveComm( gRFAL.mode ) )
            {
                if( ( st25r3911CheckReg(ST25R3911_REG_REGULATOR_RESULT, ST25R3911_REG_REGULATOR_RESULT_gpt_on, ST25R3911_REG_REGULATOR_RESULT_gpt_on, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 )  ) )
                {                
                   break;
                }
            }
            
            gRFAL.TxRx.state = RFAL_TXRX_STATE_TX_TRANSMIT;
            /* fall through */
            
        
        /*******************************************************************************/
        case RFAL_TXRX_STATE_TX_TRANSMIT:
            
            /* Clear FIFO, Clear and Enable the Interrupts */
            rfalPrepareTransceive( mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;

            /* Calculate when Water Level Interrupt will be triggered */
            gRFAL.fifo.expWL = ( st25r3911CheckReg( ST25R3911_REG_IO_CONF1, ST25R3911_REG_IO_CONF1_fifo_lt, ST25R3911_REG_IO_CONF1_fifo_lt_16bytes, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 )  ? RFAL_FIFO_OUT_LT_16 : RFAL_FIFO_OUT_LT_32);
            
        #if RFAL_FEATURE_NFCV
            /*******************************************************************************/
            /* In NFC-V streaming mode, the FIFO needs to be loaded with the coded bits    */
            if( (RFAL_MODE_POLL_NFCV == gRFAL.mode) || (RFAL_MODE_POLL_PICOPASS == gRFAL.mode) )
            {
#if 0
                /* Debugging code: output the payload bits by writing into the FIFO and subsequent clearing */
                mST25 -> writeFifo(gRFAL.TxRx.ctx.txBuf, rfalConvBitsToBytes(gRFAL.TxRx.ctx.txBufLen));
                mST25 -> executeCommand( ST25R3911_CMD_CLEAR_FIFO, mspiChannel );
#endif
                /* Calculate the bytes needed to be Written into FIFO (a incomplete byte will be added as 1byte) */
                gRFAL.nfcvData.nfcvOffset = 0;
                ret = iso15693VCDCode(gRFAL.TxRx.ctx.txBuf, rfalConvBitsToBytes(gRFAL.TxRx.ctx.txBufLen), ((gRFAL.nfcvData.origCtx.flags & RFAL_TXRX_FLAGS_CRC_TX_MANUAL)?false:true),((gRFAL.nfcvData.origCtx.flags & RFAL_TXRX_FLAGS_NFCV_FLAG_MANUAL)?false:true), (RFAL_MODE_POLL_PICOPASS == gRFAL.mode),
                          &gRFAL.fifo.bytesTotal, &gRFAL.nfcvData.nfcvOffset, gRFAL.nfcvData.codingBuffer, MIN( ST25R3911_FIFO_DEPTH, sizeof(gRFAL.nfcvData.codingBuffer) ), &gRFAL.fifo.bytesWritten);

                if( (ret != ERR_NONE) && (ret != ERR_AGAIN) )
                {
                    gRFAL.TxRx.status = ret;
                    gRFAL.TxRx.state  = RFAL_TXRX_STATE_TX_FAIL;
                    break;
                }
                /* Set the number of full bytes and bits to be transmitted */
                st25r3911SetNumTxBits( rfalConvBytesToBits(gRFAL.fifo.bytesTotal), mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;

                /* Load FIFO with coded bytes */
                mST25 -> writeFifo( gRFAL.nfcvData.codingBuffer, gRFAL.fifo.bytesWritten, mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;

            }
            /*******************************************************************************/
            else
        #endif /* RFAL_FEATURE_NFCV */
            {
                /* Calculate the bytes needed to be Written into FIFO (a incomplete byte will be added as 1byte) */
                gRFAL.fifo.bytesTotal = rfalCalcNumBytes(gRFAL.TxRx.ctx.txBufLen);
                
                /* Set the number of full bytes and bits to be transmitted */
                st25r3911SetNumTxBits( gRFAL.TxRx.ctx.txBufLen, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
                
                /* Load FIFO with total length or FIFO's maximum */
                gRFAL.fifo.bytesWritten = MIN( gRFAL.fifo.bytesTotal, ST25R3911_FIFO_DEPTH );
                mST25 -> writeFifo( gRFAL.TxRx.ctx.txBuf, gRFAL.fifo.bytesWritten, mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            }
        
            /*Check if Observation Mode is enabled and set it on ST25R391x */
            rfalCheckEnableObsModeTx(); 
            
            /*******************************************************************************/
            /* Trigger/Start transmission                                                  */
            if( gRFAL.TxRx.ctx.flags & RFAL_TXRX_FLAGS_CRC_TX_MANUAL )
            {
            	mST25 -> executeCommand( ST25R3911_CMD_TRANSMIT_WITHOUT_CRC, mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            }
            else
            {
            	mST25 -> executeCommand( ST25R3911_CMD_TRANSMIT_WITH_CRC, mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            }
             
            /* Check if a WL level is expected or TXE should come */
            gRFAL.TxRx.state = (( gRFAL.fifo.bytesWritten < gRFAL.fifo.bytesTotal ) ? RFAL_TXRX_STATE_TX_WAIT_WL : RFAL_TXRX_STATE_TX_WAIT_TXE);
            break;

        /*******************************************************************************/
        case RFAL_TXRX_STATE_TX_WAIT_WL:
            
            irqs = st25r3911GetInterrupt( (ST25R3911_IRQ_MASK_FWL | ST25R3911_IRQ_MASK_TXE), mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            if( irqs == ST25R3911_IRQ_MASK_NONE )
            {
               break;  /* No interrupt to process */
            }
            
            if( (irqs & ST25R3911_IRQ_MASK_FWL) && !(irqs & ST25R3911_IRQ_MASK_TXE) )
            {
                gRFAL.TxRx.state  = RFAL_TXRX_STATE_TX_RELOAD_FIFO;
            }
            else
            {
                gRFAL.TxRx.status = ERR_IO;
                gRFAL.TxRx.state  = RFAL_TXRX_STATE_TX_FAIL;
                break;
            }
            
            /* fall through */
            
        /*******************************************************************************/
        case RFAL_TXRX_STATE_TX_RELOAD_FIFO:
            
        #if RFAL_FEATURE_NFCV
            /*******************************************************************************/
            /* In NFC-V streaming mode, the FIFO needs to be loaded with the coded bits    */
            if( (RFAL_MODE_POLL_NFCV == gRFAL.mode) || (RFAL_MODE_POLL_PICOPASS == gRFAL.mode) )
            {
                uint16_t maxLen;
                                
                /* Load FIFO with the remaining length or maximum available (which fit on the coding buffer) */
                maxLen = MIN( (gRFAL.fifo.bytesTotal - gRFAL.fifo.bytesWritten), gRFAL.fifo.expWL);
                maxLen = MIN( maxLen, sizeof(gRFAL.nfcvData.codingBuffer) );
                tmp    = 0;

                /* Calculate the bytes needed to be Written into FIFO (a incomplete byte will be added as 1byte) */
                ret = iso15693VCDCode(gRFAL.TxRx.ctx.txBuf, rfalConvBitsToBytes(gRFAL.TxRx.ctx.txBufLen), ((gRFAL.nfcvData.origCtx.flags & RFAL_TXRX_FLAGS_CRC_TX_MANUAL)?false:true), ((gRFAL.nfcvData.origCtx.flags & RFAL_TXRX_FLAGS_NFCV_FLAG_MANUAL)?false:true), (RFAL_MODE_POLL_PICOPASS == gRFAL.mode),
                          &gRFAL.fifo.bytesTotal, &gRFAL.nfcvData.nfcvOffset, gRFAL.nfcvData.codingBuffer, maxLen, &tmp);

                if( (ret != ERR_NONE) && (ret != ERR_AGAIN) )
                {
                    gRFAL.TxRx.status = ret;
                    gRFAL.TxRx.state  = RFAL_TXRX_STATE_TX_FAIL;
                    break;
                }

                /* Load FIFO with coded bytes */
                mST25 -> writeFifo( gRFAL.nfcvData.codingBuffer, tmp, mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            }
            /*******************************************************************************/
            else
        #endif /* RFAL_FEATURE_NFCV */
            {
                /* Load FIFO with the remaining length or maximum available */
                tmp = MIN( (gRFAL.fifo.bytesTotal - gRFAL.fifo.bytesWritten), gRFAL.fifo.expWL);       /* tmp holds the number of bytes written on this iteration */
                mST25 -> writeFifo( gRFAL.TxRx.ctx.txBuf + gRFAL.fifo.bytesWritten, tmp, mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            }
            
            /* Update total written bytes to FIFO */
            gRFAL.fifo.bytesWritten += tmp;
            
            /* Check if a WL level is expected or TXE should come */
            gRFAL.TxRx.state = (( gRFAL.fifo.bytesWritten < gRFAL.fifo.bytesTotal ) ? RFAL_TXRX_STATE_TX_WAIT_WL : RFAL_TXRX_STATE_TX_WAIT_TXE);
            break;
            
            
        /*******************************************************************************/
        case RFAL_TXRX_STATE_TX_WAIT_TXE:

            irqs = st25r3911GetInterrupt( (ST25R3911_IRQ_MASK_FWL | ST25R3911_IRQ_MASK_TXE), mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            if( irqs == ST25R3911_IRQ_MASK_NONE )
            {
               break;  /* No interrupt to process */
            }
                        
            
            if( (irqs & ST25R3911_IRQ_MASK_TXE) )
            {
                /* In Active comm start SW timer to measure FWT */
                if( rfalIsModeActiveComm( gRFAL.mode) && (gRFAL.TxRx.ctx.fwt != RFAL_FWT_NONE) && (gRFAL.TxRx.ctx.fwt != 0) ) 
                {
                    rfalTimerStart( gRFAL.tmr.FWT, rfalConv1fcToMs( gRFAL.TxRx.ctx.fwt ) );
                }
                
                gRFAL.TxRx.state = RFAL_TXRX_STATE_TX_DONE;
            }
            else if( (irqs & ST25R3911_IRQ_MASK_FWL) )
            {
                /*******************************************************************************/
                /* REMARK: Silicon workaround ST25R3911 Errata #TBD                            */
                /* ST25R3911 may send a WL even when all bytes have been written to FIFO       */
                /*******************************************************************************/
                break;  /* Ignore ST25R3911 FIFO WL if total TxLen is already on the FIFO */
            }
            else
            {
               gRFAL.TxRx.status = ERR_IO;
               gRFAL.TxRx.state  = RFAL_TXRX_STATE_TX_FAIL;
               break;
            } // @suppress("No break at end of case")
            
            /* fall through */
           
        
        /*******************************************************************************/
        case RFAL_TXRX_STATE_TX_DONE:
            
            /* If no rxBuf is provided do not wait/expect Rx */
            if( gRFAL.TxRx.ctx.rxBuf == NULL )
            {
                /*Check if Observation Mode was enabled and disable it on ST25R391x */
                rfalCheckDisableObsMode();
                
                /* Clean up Transceive */
                rfalCleanupTransceive( mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
                                
                gRFAL.TxRx.status = ERR_NONE;
                gRFAL.TxRx.state  =  RFAL_TXRX_STATE_IDLE;
                break;
            }
            
            rfalCheckEnableObsModeRx();
            
            /* Goto Rx */
            gRFAL.TxRx.state  =  RFAL_TXRX_STATE_RX_IDLE;
            break;
           
        /*******************************************************************************/
        case RFAL_TXRX_STATE_TX_FAIL:
            
            /* Error should be assigned by previous state */
            if( gRFAL.TxRx.status == ERR_BUSY )
            {
                gRFAL.TxRx.status = ERR_SYSTEM;
            }
            
            /*Check if Observation Mode was enabled and disable it on ST25R391x */
            rfalCheckDisableObsMode();
            
            /* Clean up Transceive */
            rfalCleanupTransceive( mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            
            gRFAL.TxRx.state = RFAL_TXRX_STATE_IDLE;
            break;
        
        /*******************************************************************************/
        default:
            gRFAL.TxRx.status = ERR_SYSTEM;
            gRFAL.TxRx.state  = RFAL_TXRX_STATE_TX_FAIL;
            break;
    }
}


/*******************************************************************************/
static void rfalTransceiveRx( SPI* mspiChannel, ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 )
{
    volatile uint32_t irqs;
    uint8_t           tmp;
    uint8_t           aux;
    
    irqs = ST25R3911_IRQ_MASK_NONE;
    
    if( gRFAL.TxRx.state != gRFAL.TxRx.lastState )
    {        
        /* rfalLogD( "RFAL: lastSt: %d curSt: %d \r\n", gRFAL.TxRx.lastState, gRFAL.TxRx.state ); */
        gRFAL.TxRx.lastState = gRFAL.TxRx.state;
    }
    
    switch( gRFAL.TxRx.state )
    {
        /*******************************************************************************/
        case RFAL_TXRX_STATE_RX_IDLE:
            
            /* Clear rx counters */
            gRFAL.fifo.bytesWritten   = 0;    /* Total bytes written on RxBuffer */
            gRFAL.fifo.bytesTotal     = 0;    /* Total bytes in FIFO will now be from Rx */
            if( gRFAL.TxRx.ctx.rxRcvdLen )  *gRFAL.TxRx.ctx.rxRcvdLen = 0;
           
            gRFAL.TxRx.state = ( rfalIsModeActiveComm( gRFAL.mode ) ? RFAL_TXRX_STATE_RX_WAIT_EON : RFAL_TXRX_STATE_RX_WAIT_RXS );
            break;
           
           
        /*******************************************************************************/
        case RFAL_TXRX_STATE_RX_WAIT_RXS:
        
            /*******************************************************************************/
            /* If in Active comm, Check if FWT SW timer has expired */
            if( rfalIsModeActiveComm( gRFAL.mode ) && (gRFAL.TxRx.ctx.fwt != RFAL_FWT_NONE) && (gRFAL.TxRx.ctx.fwt != 0) )
            {
                if( rfalTimerisExpired( gRFAL.tmr.FWT ) )  
                {
                    gRFAL.TxRx.status = ERR_TIMEOUT;
                    gRFAL.TxRx.state  = RFAL_TXRX_STATE_RX_FAIL;
                    break;
                }
            }
            
            /*******************************************************************************/
            irqs = st25r3911GetInterrupt( ( ST25R3911_IRQ_MASK_RXS | ST25R3911_IRQ_MASK_NRE | ST25R3911_IRQ_MASK_EOF | ST25R3911_IRQ_MASK_RXE), mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            if( irqs == ST25R3911_IRQ_MASK_NONE )
            {
                break;  /* No interrupt to process */
            }
            
                        
            /*******************************************************************************/
            /* REMARK: Silicon workaround ST25R3911 Errata #1.7                            */
            /* NRE interrupt may be triggered twice                                        */
            /* Ignore NRE if is detected together with no Rx Start                         */
            /*******************************************************************************/
            
            /* Only raise Timeout if NRE is detected with no Rx Start (NRT EMV mode)       */
            if( (irqs & ST25R3911_IRQ_MASK_NRE) && !(irqs & ST25R3911_IRQ_MASK_RXS) )
            {
                gRFAL.TxRx.status = ERR_TIMEOUT;
                gRFAL.TxRx.state  = RFAL_TXRX_STATE_RX_FAIL;
                break;
            }
            
            /* Only raise Link Loss if EOF is detected with no Rx Start */
            if( (irqs & ST25R3911_IRQ_MASK_EOF) && !(irqs & ST25R3911_IRQ_MASK_RXS) )
            {
                gRFAL.TxRx.status = ERR_LINK_LOSS;
                gRFAL.TxRx.state  = RFAL_TXRX_STATE_RX_FAIL;
                break;
            }
            
            if( (irqs & ST25R3911_IRQ_MASK_RXS) )
            {
                /* If we got RXS + RXE together, jump directly into RFAL_TXRX_STATE_RX_ERR_CHECK */
                if( (irqs & ST25R3911_IRQ_MASK_RXE) )
                {
                    gRFAL.TxRx.rxse  = true;
                    gRFAL.TxRx.state = RFAL_TXRX_STATE_RX_ERR_CHECK;
                    break;
                }
                else
                {
                    /*******************************************************************************/
                    /* REMARK: Silicon workaround ST25R3911 Errata #1.1                            */
                    /* Rarely on corrupted frames I_rxs gets signaled but I_rxe is not signaled    */
                    /* Use a SW timer to handle an eventual missing RXE                            */
                    rfalTimerStart( gRFAL.tmr.RXE, RFAL_NORXE_TOUT );
                    /*******************************************************************************/
                    
                    gRFAL.TxRx.state  = RFAL_TXRX_STATE_RX_WAIT_RXE;
                }
            }
            else if( (irqs & ST25R3911_IRQ_MASK_RXE) )
            {
                /*******************************************************************************/
                /* REMARK: Silicon workaround ST25R3911 Errata #1.9                            */
                /* ST25R3911 may indicate RXE without RXS previously, this happens upon some   */
                /* noise or incomplete byte frames with less than 4 bits                       */
                /*******************************************************************************/
                
                gRFAL.TxRx.status = ERR_IO;
                gRFAL.TxRx.state  = RFAL_TXRX_STATE_RX_FAIL;
                
                rfalErrorHandling(mST25, mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
                break;
            }
            else
            {
               gRFAL.TxRx.status = ERR_IO;
               gRFAL.TxRx.state  = RFAL_TXRX_STATE_RX_FAIL;
               break;
            }
            
            /* fall through */
            
            
        /*******************************************************************************/    
        case RFAL_TXRX_STATE_RX_WAIT_RXE:
            
            irqs = st25r3911GetInterrupt( (ST25R3911_IRQ_MASK_RXE | ST25R3911_IRQ_MASK_FWL | ST25R3911_IRQ_MASK_EOF), mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            if( irqs == ST25R3911_IRQ_MASK_NONE )
            {
                /*******************************************************************************/
                /* REMARK: Silicon workaround ST25R3911B Errata #1.1                           */
                /* ST25R3911 may indicate RXS without RXE afterwards, this happens rarely on   */
                /* corrupted frames.                                                           */
                /* SW timer is used to timeout upon a missing RXE                              */
                if( rfalTimerisExpired( gRFAL.tmr.RXE ) )
                {
                    gRFAL.TxRx.status = ERR_FRAMING;
                    gRFAL.TxRx.state  = RFAL_TXRX_STATE_RX_FAIL;
                }
                /*******************************************************************************/
                    
                break;  /* No interrupt to process */
            }
            
            if( (irqs & ST25R3911_IRQ_MASK_FWL) && !(irqs & ST25R3911_IRQ_MASK_RXE) )
            {
                gRFAL.TxRx.state = RFAL_TXRX_STATE_RX_READ_FIFO;
                break;
            }
            
            gRFAL.TxRx.state = RFAL_TXRX_STATE_RX_ERR_CHECK;
            /* fall through */
            
            
        /*******************************************************************************/
        case RFAL_TXRX_STATE_RX_ERR_CHECK:
        
            /* Retrieve and check for any error irqs */
            irqs |= st25r3911GetInterrupt( (ST25R3911_IRQ_MASK_CRC | ST25R3911_IRQ_MASK_PAR | ST25R3911_IRQ_MASK_ERR1 | ST25R3911_IRQ_MASK_ERR2 | ST25R3911_IRQ_MASK_COL), mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
        
            if( (irqs & ST25R3911_IRQ_MASK_ERR1) )
            {
                gRFAL.TxRx.status = ERR_FRAMING;
                gRFAL.TxRx.state  = RFAL_TXRX_STATE_RX_READ_DATA;
                
                /* Check if there's a specific error handling for this */
                rfalErrorHandling(mST25, mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
                break;
            }
            /* Discard Soft Framing errors if not in EMVCo error handling */
            else if( (irqs & ST25R3911_IRQ_MASK_ERR2) && (gRFAL.conf.eHandling == RFAL_ERRORHANDLING_EMVCO) )
            {
                gRFAL.TxRx.status = ERR_FRAMING;
                gRFAL.TxRx.state  = RFAL_TXRX_STATE_RX_READ_DATA;
                
                /* Check if there's a specific error handling for this */
                rfalErrorHandling(mST25, mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
                break;
            }
            else if( (irqs & ST25R3911_IRQ_MASK_PAR) )
            {
                gRFAL.TxRx.status = ERR_PAR;
                gRFAL.TxRx.state  = RFAL_TXRX_STATE_RX_READ_DATA;
                
                /* Check if there's a specific error handling for this */
                rfalErrorHandling(mST25, mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
                break;
            }
            else if( (irqs & ST25R3911_IRQ_MASK_CRC) )
            {
                gRFAL.TxRx.status = ERR_CRC;
                gRFAL.TxRx.state  = RFAL_TXRX_STATE_RX_READ_DATA;
                
                /* Check if there's a specific error handling for this */
                rfalErrorHandling(mST25, mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
                break;
            }
            else if( (irqs & ST25R3911_IRQ_MASK_COL) )
            {
                gRFAL.TxRx.status = ERR_RF_COLLISION;
                gRFAL.TxRx.state  = RFAL_TXRX_STATE_RX_READ_DATA;
                
                /* Check if there's a specific error handling for this */
                rfalErrorHandling(mST25, mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
                break;
            }
            else if( (irqs & ST25R3911_IRQ_MASK_EOF) && !(irqs & ST25R3911_IRQ_MASK_RXE) )
            {
                 gRFAL.TxRx.status = ERR_LINK_LOSS;
                 gRFAL.TxRx.state  = RFAL_TXRX_STATE_RX_FAIL;
                 break;
            }
            else if( (irqs & ST25R3911_IRQ_MASK_RXE) || gRFAL.TxRx.rxse )
            {
                /* Reception ended without any error indication,                  *
                 * check FIFO status for malformed or incomplete frames           */
                
                /* Check if the reception ends with an incomplete byte (residual bits) */
                if( rfalFIFOStatusIsIncompleteByte(mST25, mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 )  )
                {
                   gRFAL.TxRx.status = ERR_INCOMPLETE_BYTE;
                }
                /* Check if the reception ends with missing parity bit */
                else if( rfalFIFOStatusIsMissingPar(mST25, mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 )  )
                {
                   gRFAL.TxRx.status = ERR_FRAMING;
                }
                
                gRFAL.TxRx.state = RFAL_TXRX_STATE_RX_READ_DATA;
            }
            else
            {
                gRFAL.TxRx.status = ERR_IO;
                gRFAL.TxRx.state  = RFAL_TXRX_STATE_RX_FAIL;
                break;
            }
                        
            /* fall through */
            
            
        /*******************************************************************************/    
        case RFAL_TXRX_STATE_RX_READ_DATA:
                        
            tmp = rfalFIFOStatusGetNumBytes(mST25, mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
                        
            /*******************************************************************************/
            /* Check if CRC should not be placed in rxBuf                                  */
            if( !(gRFAL.TxRx.ctx.flags & RFAL_TXRX_FLAGS_CRC_RX_KEEP) )
            {
                /* Check if CRC is being placed into the FIFO and if received frame was bigger than CRC */
                if( ( st25r3911CheckReg(ST25R3911_REG_AUX, ST25R3911_REG_AUX_crc_2_fifo, ST25R3911_REG_AUX_crc_2_fifo, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 )  )
                		&& ( gRFAL.fifo.bytesTotal + tmp) )
                {
                    /* By default CRC will not be placed into the rxBuffer */
                    if( ( tmp > RFAL_CRC_LEN) )  
                    {
                        tmp -= RFAL_CRC_LEN;
                    }
                    /* If the CRC was already placed into rxBuffer (due to WL interrupt where CRC was already in FIFO Read)
                     * cannot remove it from rxBuf. Can only remove it from rxBufLen not indicate the presence of CRC    */ 
                    else if(gRFAL.fifo.bytesTotal > RFAL_CRC_LEN)                       
                    {                        
                        gRFAL.fifo.bytesTotal -= RFAL_CRC_LEN;
                    }
                }
            }
            
            gRFAL.fifo.bytesTotal += tmp;                    /* add to total bytes counter */
            
            /*******************************************************************************/
            /* Check if remaining bytes fit on the rxBuf available                         */
            if( gRFAL.fifo.bytesTotal > rfalConvBitsToBytes(gRFAL.TxRx.ctx.rxBufLen) )
            {
                tmp = ( rfalConvBitsToBytes(gRFAL.TxRx.ctx.rxBufLen) - gRFAL.fifo.bytesWritten);
                
                gRFAL.TxRx.status = ERR_NOMEM;
                gRFAL.TxRx.state  = RFAL_TXRX_STATE_RX_FAIL;
            }

            /*******************************************************************************/
            /* Retrieve remaining bytes from FIFO to rxBuf, and assign total length rcvd   */
            mST25 -> readFifo( (uint8_t*)(gRFAL.TxRx.ctx.rxBuf + gRFAL.fifo.bytesWritten), tmp, mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            if( gRFAL.TxRx.ctx.rxRcvdLen )
            {
                (*gRFAL.TxRx.ctx.rxRcvdLen) = rfalConvBytesToBits( gRFAL.fifo.bytesTotal );
                if( rfalFIFOStatusIsIncompleteByte(mST25, mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 )  )
                {
                    (*gRFAL.TxRx.ctx.rxRcvdLen) -= (RFAL_BITS_IN_BYTE - rfalFIFOGetNumIncompleteBits(mST25, mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) );
                }
            }
            
        #if RFAL_FEATURE_NFCV
            /*******************************************************************************/
            /* Decode sub bit stream into payload bits for NFCV, if no error found so far  */
            if( ((RFAL_MODE_POLL_NFCV == gRFAL.mode) || (RFAL_MODE_POLL_PICOPASS == gRFAL.mode)) && (gRFAL.TxRx.status == ERR_BUSY) )
            {
                ReturnCode ret;
                uint16_t offset = 0;

                ret = iso15693VICCDecode(gRFAL.TxRx.ctx.rxBuf, gRFAL.fifo.bytesTotal,
                        gRFAL.nfcvData.origCtx.rxBuf, rfalConvBitsToBytes(gRFAL.nfcvData.origCtx.rxBufLen), &offset, gRFAL.nfcvData.origCtx.rxRcvdLen, gRFAL.nfcvData.ignoreBits, (RFAL_MODE_POLL_PICOPASS == gRFAL.mode) );

                if( ((ERR_NONE == ret) || (ERR_CRC == ret))
                     && !(RFAL_TXRX_FLAGS_CRC_RX_KEEP & gRFAL.nfcvData.origCtx.flags)
                     &&  ((*gRFAL.nfcvData.origCtx.rxRcvdLen % RFAL_BITS_IN_BYTE) == 0)
                     &&  (*gRFAL.nfcvData.origCtx.rxRcvdLen >= rfalConvBytesToBits(RFAL_CRC_LEN) )
                   )
                {
                   *gRFAL.nfcvData.origCtx.rxRcvdLen -= rfalConvBytesToBits(RFAL_CRC_LEN); /* Remove CRC */
                }
                
                /* Restore original ctx */
                gRFAL.TxRx.ctx    = gRFAL.nfcvData.origCtx;
                gRFAL.TxRx.status = ret;

                if(gRFAL.TxRx.status)
                {
                    gRFAL.TxRx.state = RFAL_TXRX_STATE_RX_FAIL;
                    break;
                }
            }
        #endif /* RFAL_FEATURE_NFCV */
            
            /*******************************************************************************/
            /* If an error as been marked/detected don't fall into to RX_DONE  */
            if( gRFAL.TxRx.status != ERR_BUSY )
            {
                gRFAL.TxRx.state = RFAL_TXRX_STATE_RX_FAIL;
                break;
            }
            
            if( rfalIsModeActiveComm( gRFAL.mode ) )
            {
                gRFAL.TxRx.state = RFAL_TXRX_STATE_RX_WAIT_EOF;
                break;
            }
            
            gRFAL.TxRx.state = RFAL_TXRX_STATE_RX_DONE;
            /* fall through */
                            
            
        /*******************************************************************************/
        case RFAL_TXRX_STATE_RX_DONE:
            
            /*Check if Observation Mode was enabled and disable it on ST25R391x */
            rfalCheckDisableObsMode();
            
            /* Clean up Transceive */
            rfalCleanupTransceive( mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;

            
            gRFAL.TxRx.status = ERR_NONE;
            gRFAL.TxRx.state  = RFAL_TXRX_STATE_IDLE;
            break;
            
            
        /*******************************************************************************/
        case RFAL_TXRX_STATE_RX_READ_FIFO:
        
            /*******************************************************************************/
            /* REMARK: Silicon workaround ST25R3911B Errata #1.1                           */
            /* ST25R3911 may indicate RXS without RXE afterwards, this happens rarely on   */
            /* corrupted frames.                                                           */
            /* Re-Start SW timer to handle an eventual missing RXE                         */
            rfalTimerStart( gRFAL.tmr.RXE, RFAL_NORXE_TOUT );
            /*******************************************************************************/        
                    
        
            tmp = rfalFIFOStatusGetNumBytes(mST25, mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            gRFAL.fifo.bytesTotal += tmp;
            
            /*******************************************************************************/
            /* Calculate the amount of bytes that still fits in rxBuf                      */
            aux = (( gRFAL.fifo.bytesTotal > rfalConvBitsToBytes(gRFAL.TxRx.ctx.rxBufLen) ) ? (rfalConvBitsToBytes(gRFAL.TxRx.ctx.rxBufLen) - gRFAL.fifo.bytesWritten) : tmp);
            
            /*******************************************************************************/
            /* Retrieve incoming bytes from FIFO to rxBuf, and store already read amount   */
            mST25 -> readFifo( (uint8_t*)(gRFAL.TxRx.ctx.rxBuf + gRFAL.fifo.bytesWritten), aux, mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            gRFAL.fifo.bytesWritten += aux;
            
            /*******************************************************************************/
            /* If the bytes already read were not the full FIFO WL, dump the remaining     *
             * FIFO so that ST25R391x can continue with reception                             */
            if( aux < tmp )
            {
                mST25 -> readFifo( NULL, (tmp - aux), mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            }
            
            rfalFIFOStatusClear();
            gRFAL.TxRx.state  = RFAL_TXRX_STATE_RX_WAIT_RXE;
            break;
            
            
        /*******************************************************************************/
        case RFAL_TXRX_STATE_RX_FAIL:
            
            /*Check if Observation Mode was enabled and disable it on ST25R391x */
            rfalCheckDisableObsMode();
            
            /* Clean up Transceive */
            rfalCleanupTransceive( mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            
            /* Error should be assigned by previous state */
            if( gRFAL.TxRx.status == ERR_BUSY )
            {                
                gRFAL.TxRx.status = ERR_SYSTEM;
            }
             
            /*rfalLogD( "RFAL: curSt: %d  Error: %d \r\n", gRFAL.TxRx.state, gRFAL.TxRx.status );*/
            gRFAL.TxRx.state = RFAL_TXRX_STATE_IDLE;
            break;
        
        
        /*******************************************************************************/    
        case RFAL_TXRX_STATE_RX_WAIT_EON:
            
            irqs = st25r3911GetInterrupt( (ST25R3911_IRQ_MASK_EON | ST25R3911_IRQ_MASK_NRE), mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            if( irqs == ST25R3911_IRQ_MASK_NONE )
            {
                break;  /* No interrupt to process */
            }
            
            if( (irqs & ST25R3911_IRQ_MASK_EON) )
            {
                gRFAL.TxRx.state = RFAL_TXRX_STATE_RX_WAIT_RXS;
            }
            
            if( (irqs & ST25R3911_IRQ_MASK_NRE) )
            {
                /* ST25R3911 uses the NRT to measure other device's Field On max time: Tadt + (n x Trfw)  */
                gRFAL.TxRx.status = ERR_LINK_LOSS;
                gRFAL.TxRx.state  = RFAL_TXRX_STATE_RX_FAIL;
            }
            break;

        
        /*******************************************************************************/    
        case RFAL_TXRX_STATE_RX_WAIT_EOF:
           
            irqs = st25r3911GetInterrupt( (ST25R3911_IRQ_MASK_CAT | ST25R3911_IRQ_MASK_CAC), mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            if( irqs == ST25R3911_IRQ_MASK_NONE )
            {
               break;  /* No interrupt to process */
            }
            
            if( (irqs & ST25R3911_IRQ_MASK_CAT) )
            {
               gRFAL.TxRx.state = RFAL_TXRX_STATE_RX_DONE;
            }
            else if( (irqs & ST25R3911_IRQ_MASK_CAC) )
            {
               gRFAL.TxRx.status = ERR_RF_COLLISION;
               gRFAL.TxRx.state  = RFAL_TXRX_STATE_RX_FAIL;
            }
            else
            {
               gRFAL.TxRx.status = ERR_IO;
               gRFAL.TxRx.state  = RFAL_TXRX_STATE_RX_FAIL;
            }
            break;
            
            
        /*******************************************************************************/
        default:
            gRFAL.TxRx.status = ERR_SYSTEM;
            gRFAL.TxRx.state  = RFAL_TXRX_STATE_RX_FAIL;
            break;           
    }    
}

/*******************************************************************************/
static void rfalFIFOStatusUpdate( ST25R3911* mST25, SPI * mspiChannel, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 )
{
    if(gRFAL.fifo.status[RFAL_FIFO_STATUS_REG2] == RFAL_FIFO_STATUS_INVALID)
    {
    	   mST25 -> readMultipleRegisters( ST25R3911_REG_FIFO_RX_STATUS1, gRFAL.fifo.status, ST25R3911_FIFO_STATUS_LEN, mspiChannel,mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    }
}


/*******************************************************************************/
static void rfalFIFOStatusClear( void )
{
    gRFAL.fifo.status[RFAL_FIFO_STATUS_REG2] = RFAL_FIFO_STATUS_INVALID;
}


/*******************************************************************************/
static uint8_t rfalFIFOStatusGetNumBytes( ST25R3911* mST25, SPI * mspiChannel, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 )
{
    rfalFIFOStatusUpdate(mST25, mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    
    return gRFAL.fifo.status[RFAL_FIFO_STATUS_REG1]; 
   
}


/*******************************************************************************/
static bool rfalFIFOStatusIsIncompleteByte( ST25R3911* mST25, SPI * mspiChannel, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 )
{
    rfalFIFOStatusUpdate(mST25, mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    return ((gRFAL.fifo.status[RFAL_FIFO_STATUS_REG2] & (ST25R3911_REG_FIFO_RX_STATUS2_mask_fifo_lb | ST25R3911_REG_FIFO_RX_STATUS2_fifo_ncp)) != 0);
}


/*******************************************************************************/
static bool rfalFIFOStatusIsMissingPar( ST25R3911* mST25, SPI * mspiChannel, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 )
{
    rfalFIFOStatusUpdate(mST25, mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    return ((gRFAL.fifo.status[RFAL_FIFO_STATUS_REG2] & ST25R3911_REG_FIFO_RX_STATUS2_np_lb) != 0);
}


/*******************************************************************************/
static uint8_t rfalFIFOGetNumIncompleteBits( ST25R3911* mST25, SPI * mspiChannel, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 )
{
    rfalFIFOStatusUpdate(mST25, mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    return ((gRFAL.fifo.status[RFAL_FIFO_STATUS_REG2] & ST25R3911_REG_FIFO_RX_STATUS2_mask_fifo_lb) >> ST25R3911_REG_FIFO_RX_STATUS2_shift_fifo_lb);
}


#if RFAL_FEATURE_NFCA

/*******************************************************************************/
ReturnCode rfalISO14443ATransceiveShortFrame( rfal14443AShortFrameCmd txCmd, uint8_t* rxBuf, uint8_t rxBufLen, uint16_t* rxRcvdLen, uint32_t fwt, SPI* mspiChannel, ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 )
{
    ReturnCode ret;
    uint8_t    directCmd;

    /* Check if RFAL is properly initialized */
    if( (gRFAL.state < RFAL_STATE_MODE_SET) || (( gRFAL.mode != RFAL_MODE_POLL_NFCA ) && ( gRFAL.mode != RFAL_MODE_POLL_NFCA_T1T )) ||
    		!( st25r3911CheckReg(ST25R3911_REG_OP_CONTROL, ST25R3911_REG_OP_CONTROL_tx_en, ST25R3911_REG_OP_CONTROL_tx_en, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 )  ) )
    {
        return ERR_WRONG_STATE;
    }
    
    /* Check for valid parameters */
    if( (rxBuf == NULL) || (rxRcvdLen == NULL) || (fwt == RFAL_FWT_NONE) )
    {
        return ERR_PARAM;
    }
    
    /*******************************************************************************/
    /* Select the Direct Command to be performed                                   */
    switch (txCmd)
    {
        case RFAL_14443A_SHORTFRAME_CMD_WUPA:
            directCmd = ST25R3911_CMD_TRANSMIT_WUPA;
            break;
            
        case RFAL_14443A_SHORTFRAME_CMD_REQA:
            directCmd = ST25R3911_CMD_TRANSMIT_REQA;
            break;
            
        default:
            return ERR_PARAM;
    }
    
    
    /*******************************************************************************/
    /* Enable anti collision to recognise collision in first byte of SENS_REQ */
    st25r3911SetRegisterBits( ST25R3911_REG_ISO14443A_NFC, ST25R3911_REG_ISO14443A_NFC_antcl, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    
    /* Disable CRC while receiving since ATQA has no CRC included */
    st25r3911SetRegisterBits( ST25R3911_REG_AUX, ST25R3911_REG_AUX_no_crc_rx, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    
    
    /*******************************************************************************/
    /* Wait for GT and FDT */
    while( !rfalIsGTExpired() );
    while( st25r3911CheckReg(ST25R3911_REG_REGULATOR_RESULT, ST25R3911_REG_REGULATOR_RESULT_gpt_on, ST25R3911_REG_REGULATOR_RESULT_gpt_on, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 )   );
    
    gRFAL.tmr.GT = RFAL_TIMING_NONE;

    
    /*******************************************************************************/
    /* Prepare for Transceive, Receive only (bypass Tx states) */
    gRFAL.TxRx.ctx.flags     = ( RFAL_TXRX_FLAGS_CRC_TX_MANUAL | RFAL_TXRX_FLAGS_CRC_RX_KEEP );
    gRFAL.TxRx.ctx.rxBuf     = rxBuf;
    gRFAL.TxRx.ctx.rxBufLen  = rxBufLen;
    gRFAL.TxRx.ctx.rxRcvdLen = rxRcvdLen;
    
    /*******************************************************************************/
    /* Load NRT with FWT */
    st25r3911SetNoResponseTime_64fcs( rfalConv1fcTo64fc( MIN( (fwt + RFAL_FWT_ADJUSTMENT + RFAL_FWT_A_ADJUSTMENT), RFAL_ST25R3911_NRT_MAX_1FC ) ),
    		mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    
    if( gRFAL.timings.FDTListen != RFAL_TIMING_NONE )
    {
        /* Set Minimum FDT(Listen) in which PICC is not allowed to send a response */
    	mST25 -> writeRegister( ST25R3911_REG_MASK_RX_TIMER, rfalConv1fcTo64fc( ((RFAL_FDT_LISTEN_MRT_ADJUSTMENT + RFAL_FDT_LISTEN_A_ADJUSTMENT) > gRFAL.timings.FDTListen) ? RFAL_ST25R3911_MRT_MIN_1FC : (gRFAL.timings.FDTListen - (RFAL_FDT_LISTEN_MRT_ADJUSTMENT + RFAL_FDT_LISTEN_A_ADJUSTMENT)) ),
    			mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    }
    
    /* In Passive communications General Purpose Timer is used to measure FDT Poll */
    if( gRFAL.timings.FDTPoll != RFAL_TIMING_NONE )
    {
        /* Configure GPT to start at RX end */
        st25r3911StartGPTimer_8fcs( rfalConv1fcTo8fc( MIN( gRFAL.timings.FDTPoll, (gRFAL.timings.FDTPoll - RFAL_FDT_POLL_ADJUSTMENT) ) ), ST25R3911_REG_GPT_CONTROL_gptc_erx,
        		mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    }
    
    /*******************************************************************************/
    rfalPrepareTransceive( mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    
    /* Also enable bit collision interrupt */
    st25r3911GetInterrupt( ST25R3911_IRQ_MASK_COL, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    st25r3911EnableInterrupts( ST25R3911_IRQ_MASK_COL, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    
    /*Check if Observation Mode is enabled and set it on ST25R391x */
    rfalCheckEnableObsModeTx();
    
    /*******************************************************************************/
    /* Chip bug: Clear nbtx bits before sending WUPA/REQA - otherwise ST25R3911 will report parity error */
    mST25 -> writeRegister( ST25R3911_REG_NUM_TX_BYTES2, 0, mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;

    /* Send either WUPA or REQA. All affected tags will backscatter ATQA and change to READY state */
    mST25 -> executeCommand( directCmd, mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    
    /* Wait for TXE */
    if( !st25r3911WaitForInterruptsTimed( ST25R3911_IRQ_MASK_TXE, MAX( rfalConv1fcToMs( fwt ), RFAL_ST25R3911_SW_TMR_MIN_1MS ), mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 )  )
    {
        ret = ERR_IO;
    }
    else
    {
        /*Check if Observation Mode is enabled and set it on ST25R391x */
        rfalCheckEnableObsModeRx();
        
        /* Jump into a transceive Rx state for reception (bypass Tx states) */
        gRFAL.state       = RFAL_STATE_TXRX;
        gRFAL.TxRx.state  = RFAL_TXRX_STATE_RX_IDLE;
        gRFAL.TxRx.status = ERR_BUSY;
        
        /* Execute Transceive Rx blocking */
        ret = rfalTransceiveBlockingRx( mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    }
    
    
    /* Disable Collision interrupt */
    st25r3911DisableInterrupts( (ST25R3911_IRQ_MASK_COL), mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    
    /* Disable anti collision again */
    st25r3911ClrRegisterBits( ST25R3911_REG_ISO14443A_NFC, ST25R3911_REG_ISO14443A_NFC_antcl, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    
    /* ReEnable CRC on Rx */
    st25r3911ClrRegisterBits(ST25R3911_REG_AUX, ST25R3911_REG_AUX_no_crc_rx, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 );
    
    return ret;
}

/*******************************************************************************/
ReturnCode rfalISO14443ATransceiveAnticollisionFrame( uint8_t *buf, uint8_t *bytesToSend, uint8_t *bitsToSend,
		uint16_t *rxLength, uint32_t fwt, SPI* mspiChannel, ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 )
{
    ReturnCode            ret;
    rfalTransceiveContext ctx;
    uint8_t               collByte;
    uint8_t               collData;
    
    /* Check if RFAL is properly initialized */
    if( (gRFAL.state < RFAL_STATE_MODE_SET) || ( gRFAL.mode != RFAL_MODE_POLL_NFCA ) )
    {
        return ERR_WRONG_STATE;
    }
    
    /* Check for valid parameters */
    if( (buf == NULL) || (bytesToSend == NULL) || (bitsToSend == NULL) || (rxLength == NULL) )
    {
        return ERR_PARAM;
    }
    
    /*******************************************************************************/
    /* Enable anti collision to recognise collision in first byte of SENS_REQ */
    st25r3911SetRegisterBits( ST25R3911_REG_ISO14443A_NFC, ST25R3911_REG_ISO14443A_NFC_antcl, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    
    /* Disable CRC while receiving */
    st25r3911SetRegisterBits( ST25R3911_REG_AUX, ST25R3911_REG_AUX_no_crc_rx, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    
    
    
    /*******************************************************************************/
    /* Prepare for Transceive                                                      */
    ctx.flags     = ( RFAL_TXRX_FLAGS_CRC_TX_MANUAL | RFAL_TXRX_FLAGS_CRC_RX_KEEP | RFAL_TXRX_FLAGS_AGC_OFF );  /* Disable Automatic Gain Control (AGC) for better detection of collision */
    ctx.txBuf     = buf;
    ctx.txBufLen  = (rfalConvBytesToBits( *bytesToSend ) + *bitsToSend );
    ctx.rxBuf     = (buf + (*bytesToSend));
    ctx.rxBufLen  = rfalConvBytesToBits( RFAL_ISO14443A_SDD_RES_LEN );
    ctx.rxRcvdLen = rxLength;
    ctx.fwt       = fwt;
    
    rfalStartTransceive( &ctx, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    
    /* Additionally enable bit collision interrupt */
    st25r3911GetInterrupt( ST25R3911_IRQ_MASK_COL, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    st25r3911EnableInterrupts( ST25R3911_IRQ_MASK_COL, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    
    /*******************************************************************************/
    collByte = 0;
    
    /* save the collision byte */
    if ((*bitsToSend) > 0)
    {
        buf[(*bytesToSend)] <<= (RFAL_BITS_IN_BYTE - (*bitsToSend));
        buf[(*bytesToSend)] >>= (RFAL_BITS_IN_BYTE - (*bitsToSend));
        collByte = buf[(*bytesToSend)];
    }
    
    
    /*******************************************************************************/
    /* Run Transceive blocking */
    ret = rfalTransceiveRunBlockingTx( mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    if( ret == ERR_NONE)
    {
       ret = rfalTransceiveBlockingRx( mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 );
    
       /*******************************************************************************/
       if ((*bitsToSend) > 0)
       {
           buf[(*bytesToSend)] >>= (*bitsToSend);
           buf[(*bytesToSend)] <<= (*bitsToSend);
           buf[(*bytesToSend)] |= collByte;
       }
       
       if( (ERR_RF_COLLISION == ret) )
       {                      
           /* read out collision register */
    	   mST25 -> readRegister( ST25R3911_REG_COLLISION_STATUS, &collData, mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;

           (*bytesToSend) = ((collData >> ST25R3911_REG_COLLISION_STATUS_shift_c_byte) & 0x0F); /* 4-bits Byte information */
           (*bitsToSend)  = ((collData >> ST25R3911_REG_COLLISION_STATUS_shift_c_bit)  & 0x07); /* 3-bits bit information */

       }
    }
    
   
    /*******************************************************************************/
    /* Disable Collision interrupt */
    st25r3911DisableInterrupts( (ST25R3911_IRQ_MASK_COL), mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    
    /* Disable anti collision again */
    st25r3911ClrRegisterBits( ST25R3911_REG_ISO14443A_NFC, ST25R3911_REG_ISO14443A_NFC_antcl, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    
    /* ReEnable CRC on Rx */
    st25r3911ClrRegisterBits( ST25R3911_REG_AUX, ST25R3911_REG_AUX_no_crc_rx, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    
    return ret;
}

#endif /* RFAL_FEATURE_NFCA */

#if RFAL_FEATURE_NFCV

/*******************************************************************************/
ReturnCode rfalISO15693TransceiveAnticollisionFrame( uint8_t *txBuf, uint8_t txBufLen, uint8_t *rxBuf, uint8_t rxBufLen, uint16_t *actLen, SPI* mspiChannel, ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 )
{
    ReturnCode            ret;
    rfalTransceiveContext ctx;
    
    /* Check if RFAL is properly initialized */
    if( (gRFAL.state < RFAL_STATE_MODE_SET) || ( gRFAL.mode != RFAL_MODE_POLL_NFCV ) )
    {
        return ERR_WRONG_STATE;
    }
    
    /* Ignoring collisions before the UID (RES_FLAG + DSFID) */
    gRFAL.nfcvData.ignoreBits = RFAL_ISO15693_IGNORE_BITS;
    
    /*******************************************************************************/
    /* Prepare for Transceive  */
    ctx.flags     = ( ((txBufLen==0)?RFAL_TXRX_FLAGS_CRC_TX_MANUAL:RFAL_TXRX_FLAGS_CRC_TX_AUTO) | RFAL_TXRX_FLAGS_CRC_RX_KEEP | RFAL_TXRX_FLAGS_AGC_OFF | ((txBufLen==0)?RFAL_TXRX_FLAGS_NFCV_FLAG_MANUAL:RFAL_TXRX_FLAGS_NFCV_FLAG_AUTO) ); /* Disable Automatic Gain Control (AGC) for better detection of collision */
    ctx.txBuf     = txBuf;
    ctx.txBufLen  = rfalConvBytesToBits(txBufLen);
    ctx.rxBuf     = rxBuf;
    ctx.rxBufLen  = rfalConvBytesToBits(rxBufLen);
    ctx.rxRcvdLen = actLen;
    ctx.fwt       = rfalConv64fcTo1fc(ISO15693_NO_RESPONSE_TIME);
    
    rfalStartTransceive( &ctx, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    
    /*******************************************************************************/
    /* Run Transceive blocking */
    ret = rfalTransceiveRunBlockingTx( mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    if( ret == ERR_NONE)
    {
        ret = rfalTransceiveBlockingRx( mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    }
        
    gRFAL.nfcvData.ignoreBits = 0;
    return ret;
}

/*******************************************************************************/
ReturnCode rfalISO15693TransceiveAnticollisionEOF( uint8_t *rxBuf, uint8_t rxBufLen, uint16_t *actLen, SPI* mspiChannel,
		ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 )
{
    uint8_t dummy;

    return rfalISO15693TransceiveAnticollisionFrame( &dummy, 0, rxBuf, rxBufLen, actLen, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
}

/*******************************************************************************/
ReturnCode rfalISO15693TransceiveEOF( uint8_t *rxBuf, uint8_t rxBufLen, uint16_t *actLen,
		SPI* mspiChannel, ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 )
{
    ReturnCode ret;
    uint8_t    dummy;
    
    /* Check if RFAL is properly initialized */
    if( ( gRFAL.state < RFAL_STATE_MODE_SET ) || ( gRFAL.mode != RFAL_MODE_POLL_NFCV ) )
    {
        return ERR_WRONG_STATE;
    }
    
    /*******************************************************************************/
    /* Run Transceive blocking */
    ret = rfalTransceiveBlockingTxRx( &dummy,
                                      0,
                                      rxBuf,
                                      rxBufLen,
                                      actLen,
                                      ( RFAL_TXRX_FLAGS_CRC_TX_MANUAL | RFAL_TXRX_FLAGS_CRC_RX_KEEP | RFAL_TXRX_FLAGS_AGC_ON ),
                                      rfalConv64fcTo1fc(ISO15693_NO_RESPONSE_TIME), mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    return ret;
}

#endif  /* RFAL_FEATURE_NFCV */

#if RFAL_FEATURE_NFCF

/*******************************************************************************/
ReturnCode rfalFeliCaPoll( rfalFeliCaPollSlots slots, uint16_t sysCode, uint8_t reqCode, rfalFeliCaPollRes* pollResList, uint8_t pollResListSize, uint8_t *devicesDetected,
		uint8_t *collisionsDetected, SPI* mspiChannel, ST25R3911* mST25,
		DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 )
{
    ReturnCode        ret;
    uint8_t           frame[RFAL_FELICA_POLL_REQ_LEN - RFAL_FELICA_LEN_LEN];  /* LEN is added by ST25R3911 automatically */
    uint16_t          actLen;
    uint8_t           frameIdx;
    uint8_t           devDetected;
    uint8_t           colDetected;
    rfalEHandling     curHandling;
    
    int index = (int)slots;

    /* Check if RFAL is properly initialized */
    if( (gRFAL.state < RFAL_STATE_MODE_SET) || ( gRFAL.mode != RFAL_MODE_POLL_NFCF ) )
    {
        return ERR_WRONG_STATE;
    }
    
    frameIdx    = 0;
    colDetected = 0;
    devDetected = 0;
    
    /*******************************************************************************/
    /* Compute SENSF_REQ frame */
    frame[frameIdx++] =  FELICA_CMD_POLLING;       /* CMD: SENF_REQ                       */
    frame[frameIdx++] = (uint8_t)(sysCode >> 8);   /* System Code (SC)                    */
    frame[frameIdx++] = (uint8_t)(sysCode & 0xFF); /* System Code (SC)                    */
    frame[frameIdx++] = reqCode;                   /* Communication Parameter Request (RC)*/
    frame[frameIdx++] = (uint8_t)slots;            /* TimeSlot (TSN)                      */
    
    
    /*******************************************************************************/
    /* NRT should not stop on reception - Use EMVCo mode to run NRT in nrt_emv     *
     * ERRORHANDLING_EMVCO has no special handling for NFC-F mode                  */
    curHandling = gRFAL.conf.eHandling;
    rfalSetErrorHandling( RFAL_ERRORHANDLING_EMVCO );
    
    /*******************************************************************************/
    /* Run transceive blocking, 
     * Calculate Total Response Time in(64/fc): 
     *                       512 PICC process time + (n * 256 Time Slot duration)  */
    ret = rfalTransceiveBlockingTx( frame, 
                                    frameIdx, 
                                    (uint8_t*)gRFAL.nfcfData.pollResponses, 
                                    RFAL_FELICA_POLL_RES_LEN, 
                                    &actLen,
                                    (RFAL_TXRX_FLAGS_DEFAULT),
                                    rfalConv64fcTo1fc( RFAL_FELICA_POLL_DELAY_TIME + (RFAL_FELICA_POLL_SLOT_TIME * (slots + 1)) ),
									mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    

    /*******************************************************************************/
    /* If Tx OK, Wait for all responses, store them as soon as they appear         */
    if( ret == ERR_NONE )
    {
        do 
        {
            ret = rfalTransceiveBlockingRx( mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            if( ret == ERR_TIMEOUT )
            {
                /* Upon timeout the full Poll Delay + (Slot time)*(slots) has expired */
                break;
            }
            else
            {
                /* Reception done, reEnabled Rx for following Slot */
            	mST25 -> executeCommand( ST25R3911_CMD_UNMASK_RECEIVE_DATA, mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            	mST25 -> executeCommand( ST25R3911_CMD_CLEAR_SQUELCH, mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
                
                /* If the reception was OK, new device found */
                if( ret == ERR_NONE )
                {
                   devDetected++;
                   
                   /* Overwrite the Transceive context for the next reception */
                   gRFAL.TxRx.ctx.rxBuf = (uint8_t*)gRFAL.nfcfData.pollResponses[devDetected];
                }
                /* If the reception was not OK, mark as collision */
                else
                {
                    colDetected++;
                }
                
                /* Check whether NRT has expired meanwhile  */
                if( st25r3911CheckReg( ST25R3911_REG_REGULATOR_RESULT, ST25R3911_REG_REGULATOR_RESULT_nrt_on, 0x00, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 )  )
                {
                    break;
                }
            }
            
            /* Jump again into transceive Rx state for the following reception */
            gRFAL.TxRx.status = ERR_BUSY;
            gRFAL.state       = RFAL_STATE_TXRX;
            gRFAL.TxRx.state  = RFAL_TXRX_STATE_RX_IDLE;
            
        }while(index--);
    }
    
    /*******************************************************************************/
    /* Restore NRT to normal mode - back to previous error handling */
    rfalSetErrorHandling( curHandling );
    
    /*******************************************************************************/
    /* Assign output parameters if requested                                       */
    
    if( (pollResList != NULL) && (pollResListSize > 0) && (devDetected > 0) )
    {
        ST_MEMCPY( pollResList, gRFAL.nfcfData.pollResponses, (RFAL_FELICA_POLL_RES_LEN * MIN(pollResListSize, devDetected) ) );
    }
    
    if( devicesDetected != NULL )
    {
        *devicesDetected = devDetected;
    }
    
    if( collisionsDetected != NULL )
    {
        *collisionsDetected = colDetected;
    }
    uint16_t ERR_NONEUI = ERR_NONE;
    return (( colDetected || devDetected ) ? ERR_NONEUI : ret);
}

#endif /* RFAL_FEATURE_NFCF */


/*****************************************************************************
 *  Listen Mode                                                              *
 *****************************************************************************/



/*******************************************************************************/
bool rfalIsExtFieldOn( SPI* mspiChannel, ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 )
{
    return st25r3911CheckReg(ST25R3911_REG_AUX_DISPLAY, ST25R3911_REG_AUX_DISPLAY_efd_o, ST25R3911_REG_AUX_DISPLAY_efd_o, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
}


/*******************************************************************************/
ReturnCode rfalListenStart( uint32_t lmMask, rfalLmConfPA *confA, rfalLmConfPB *confB,
		rfalLmConfPF *confF, uint8_t *rxBuf, uint16_t rxBufLen, uint16_t *rxLen,
		SPI* mspiChannel, ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 )
{
    NO_WARNING(confA);
    NO_WARNING(confB);
    NO_WARNING(confF);
    
    
    gRFAL.Lm.state  = RFAL_LM_STATE_NOT_INIT;
    
    
    /*******************************************************************************/
    if( (lmMask & RFAL_LM_MASK_NFCA) || (lmMask & RFAL_LM_MASK_NFCB) || (lmMask & RFAL_LM_MASK_NFCF) )
    {
        return ERR_NOTSUPP;
    }
    
    

    /*******************************************************************************/
    if( (lmMask & RFAL_LM_MASK_ACTIVE_P2P) )
    {
        gRFAL.state       = RFAL_STATE_LM;
       
        gRFAL.Lm.rxBuf    = rxBuf;
        gRFAL.Lm.rxBufLen = rxBufLen;
        gRFAL.Lm.rxLen    = rxLen;
        *gRFAL.Lm.rxLen   = 0;
        gRFAL.Lm.dataFlag = false;
        
        /* On Bit Rate Detection Mode ST25R391x will filter incoming frames during MRT time starting on External Field On event, use 512/fc steps */
        mST25 -> writeRegister( ST25R3911_REG_MASK_RX_TIMER, rfalConv1fcTo512fc( RFAL_LM_GT ), mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
        
        /* Restore default settings on NFCIP1 mode, Receiving parity + CRC bits and manual Tx Parity*/
        st25r3911ClrRegisterBits( ST25R3911_REG_ISO14443A_NFC,
        		(ST25R3911_REG_ISO14443A_NFC_no_tx_par | ST25R3911_REG_ISO14443A_NFC_no_rx_par | ST25R3911_REG_ISO14443A_NFC_nfc_f0),
				mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
        
        /* Enable External Field Detector */
        st25r3911SetRegisterBits( ST25R3911_REG_AUX, ST25R3911_REG_AUX_en_fd, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
      
        /* Enable Receiver */
        st25r3911ChangeRegisterBits( ST25R3911_REG_OP_CONTROL, ST25R3911_REG_OP_CONTROL_rx_en, ST25R3911_REG_OP_CONTROL_rx_en,
        		mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
        
        /* Set Analog configurations for generic Listen mode */
        /* Not on SetState(POWER OFF) as otherwise would be applied on every Field Event */
        rfalSetAnalogConfig( (RFAL_ANALOG_CONFIG_LISTEN | RFAL_ANALOG_CONFIG_TECH_AP2P | RFAL_ANALOG_CONFIG_BITRATE_COMMON | RFAL_ANALOG_CONFIG_TX), mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
        rfalSetAnalogConfig( (RFAL_ANALOG_CONFIG_LISTEN | RFAL_ANALOG_CONFIG_TECH_AP2P | RFAL_ANALOG_CONFIG_BITRATE_COMMON | RFAL_ANALOG_CONFIG_RX), mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
        
        /* Initialize as POWER_OFF and set proper mode in RF Chip */
        rfalListenSetState( RFAL_LM_STATE_POWER_OFF, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    }
    else
    {
        return ERR_REQUEST;   /* Listen Start called but no mode was enabled */
    }
    
    return ERR_NONE;
}



/*******************************************************************************/
static ReturnCode rfalRunListenModeWorker( SPI* mspiChannel, ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 )
{
    volatile uint32_t irqs;
    uint8_t           tmp;
    
    if( gRFAL.state != RFAL_STATE_LM )
    {
        return ERR_WRONG_STATE;
    }
    
    switch( gRFAL.Lm.state )
    {
        /*******************************************************************************/
        case RFAL_LM_STATE_POWER_OFF:
            
            irqs = st25r3911GetInterrupt( (  ST25R3911_IRQ_MASK_EON ), mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            if( irqs == ST25R3911_IRQ_MASK_NONE )
            {
              break;  /* No interrupt to process */
            }
            
            if( (irqs & ST25R3911_IRQ_MASK_EON) )
            {
                rfalListenSetState( RFAL_LM_STATE_IDLE, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            }
            else
            {
                break;
            }
            /* fall through */
            
              
        /*******************************************************************************/
        case RFAL_LM_STATE_IDLE:
            
            irqs = st25r3911GetInterrupt( ( ST25R3911_IRQ_MASK_NFCT | ST25R3911_IRQ_MASK_RXE | ST25R3911_IRQ_MASK_EOF ), mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            if( irqs == ST25R3911_IRQ_MASK_NONE )
            {
                break;  /* No interrupt to process */
            }
            
            if( (irqs & ST25R3911_IRQ_MASK_NFCT) )
            {
                /* Retrieve bitrate detected */
            	mST25 -> readRegister( ST25R3911_REG_NFCIP1_BIT_RATE, (uint8_t*)&gRFAL.Lm.brDetected,  mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            //	rfalBitRate test = RFAL_BR_1695;
            	gRFAL.Lm.brDetected = (rfalBitRate)((int)gRFAL.Lm.brDetected >> ST25R3911_REG_NFCIP1_BIT_RATE_nfc_rate_shift);
				//(rfalBitRate) ST25R3911_REG_NFCIP1_BIT_RATE_nfc_rate_shift;
            }
            else if( (irqs & ST25R3911_IRQ_MASK_RXE) && (gRFAL.Lm.brDetected != RFAL_BR_KEEP) )
            {
                irqs = st25r3911GetInterrupt( ( ST25R3911_IRQ_MASK_RXE | ST25R3911_IRQ_MASK_EOF | ST25R3911_IRQ_MASK_CRC | ST25R3911_IRQ_MASK_PAR | ST25R3911_IRQ_MASK_ERR2 | ST25R3911_IRQ_MASK_ERR1 ), mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
                
                if( (irqs & ST25R3911_IRQ_MASK_CRC) || (irqs & ST25R3911_IRQ_MASK_PAR) || (irqs & ST25R3911_IRQ_MASK_ERR1) )
                {
                    /* nfc_ar may have triggered RF Collision Avoidance, disable it before executing Clear (Stop All activities) */
                    st25r3911ClrRegisterBits( ST25R3911_REG_MODE, ST25R3911_REG_MODE_nfc_ar, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
                    mST25 -> executeCommand( ST25R3911_CMD_CLEAR_FIFO,  mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
                    mST25 -> executeCommand( ST25R3911_CMD_UNMASK_RECEIVE_DATA,  mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
                    st25r3911SetRegisterBits( ST25R3911_REG_MODE, ST25R3911_REG_MODE_nfc_ar, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
                    st25r3911ClrRegisterBits(ST25R3911_REG_OP_CONTROL, ST25R3911_REG_OP_CONTROL_tx_en, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
                    break; /* A bad reception occurred, remain in same state */
                }
                
                /* Retrieve received data */
                mST25 -> readRegister(ST25R3911_REG_FIFO_RX_STATUS1, &tmp,  mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
                *gRFAL.Lm.rxLen = tmp;
                
                mST25 -> readFifo( gRFAL.Lm.rxBuf, MIN( *gRFAL.Lm.rxLen, rfalConvBitsToBytes(gRFAL.Lm.rxBufLen) ), mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
                
                /* Check if the data we got has at least the CRC and remove it, otherwise leave at 0 */
                *gRFAL.Lm.rxLen  -= ((*gRFAL.Lm.rxLen > RFAL_CRC_LEN) ? RFAL_CRC_LEN : *gRFAL.Lm.rxLen);
                *gRFAL.Lm.rxLen   = rfalConvBytesToBits( *gRFAL.Lm.rxLen );
                gRFAL.Lm.dataFlag = true;
                
                /*Check if Observation Mode was enabled and disable it on ST25R391x */
                rfalCheckDisableObsMode();
            }
            else if( (irqs & ST25R3911_IRQ_MASK_EOF) && (!gRFAL.Lm.dataFlag) )
            {
                rfalListenSetState( RFAL_LM_STATE_POWER_OFF, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            }
            break;
            
            /*******************************************************************************/
            case RFAL_LM_STATE_READY_F:
            case RFAL_LM_STATE_READY_A:
            case RFAL_LM_STATE_ACTIVE_A:
            case RFAL_LM_STATE_ACTIVE_Ax:
            case RFAL_LM_STATE_SLEEP_A:
            case RFAL_LM_STATE_SLEEP_B:
            case RFAL_LM_STATE_SLEEP_AF:
            case RFAL_LM_STATE_READY_Ax:
            case RFAL_LM_STATE_CARDEMU_4A:
            case RFAL_LM_STATE_CARDEMU_4B:
            case RFAL_LM_STATE_CARDEMU_3:
                return ERR_INTERNAL;
                
            case RFAL_LM_STATE_TARGET_F:
            case RFAL_LM_STATE_TARGET_A:
                break;
                
            /*******************************************************************************/
            default:
                return ERR_WRONG_STATE;
    }
    return ERR_NONE;
}


/*******************************************************************************/
ReturnCode rfalListenStop( SPI* mspiChannel, ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 )
{
    gRFAL.Lm.state  = RFAL_LM_STATE_NOT_INIT;
  
    /*Check if Observation Mode was enabled and disable it on ST25R391x */
    rfalCheckDisableObsMode();
  
    /* Disable Receiver and Transmitter */
    rfalFieldOff( mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    
    /* As there's no Off mode, set default value: ISO14443A with automatic RF Collision Avoidance Off */
    mST25 -> writeRegister( ST25R3911_REG_MODE, (ST25R3911_REG_MODE_om_iso14443a | ST25R3911_REG_MODE_nfc_ar_off),  mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
        
    return ERR_NONE;
}


/*******************************************************************************/
ReturnCode rfalListenSleepStart( rfalLmState sleepSt, uint8_t *rxBuf, uint16_t rxBufLen, uint16_t *rxLen )
{
    NO_WARNING(sleepSt);
    NO_WARNING(rxBuf);
    NO_WARNING(rxBufLen);
    NO_WARNING(rxLen);
    
    return ERR_NOTSUPP;
}


/*******************************************************************************/
rfalLmState rfalListenGetState( bool *dataFlag, rfalBitRate *lastBR )
{
    /* Allow state retrieval even if gRFAL.state != RFAL_STATE_LM so  *
     * that this Lm state can be used by caller after activation      */

    if( lastBR != NULL )
    {
        *lastBR = gRFAL.Lm.brDetected;
    }
    
    if( dataFlag != NULL )
    {
        *dataFlag = gRFAL.Lm.dataFlag;
    }
    
    return gRFAL.Lm.state;
}


/*******************************************************************************/
ReturnCode rfalListenSetState( rfalLmState newSt, SPI* mspiChannel, ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 )
{
    ReturnCode ret;
    uint8_t    tmp;
        
    /*rfalLogD( "RFAL: curState: %02X newState: %02X \r\n", gRFAL.Lm.state, newSt );*/
    
    /* SetState clears the Data flag */
    gRFAL.Lm.dataFlag = false;
    ret               = ERR_NONE;
    
    /*******************************************************************************/
    switch( newSt )
    {
        /*******************************************************************************/
        case RFAL_LM_STATE_POWER_OFF:
            
            /*******************************************************************************/
            /* Disable nfc_ar as RF Collision Avoidance timer may have already started */
            st25r3911ClrRegisterBits( ST25R3911_REG_MODE, ST25R3911_REG_MODE_nfc_ar, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            
             mST25 -> executeCommand( ST25R3911_CMD_CLEAR_FIFO, mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
                
            /* Ensure that our field is Off, as automatic response RF Collision Avoidance may have been triggered */
            st25r3911ClrRegisterBits(ST25R3911_REG_OP_CONTROL, ST25R3911_REG_OP_CONTROL_tx_en, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            
            /*******************************************************************************/
            /* Ensure that the NFCIP1 mode is disabled */
            st25r3911ClrRegisterBits( ST25R3911_REG_ISO14443A_NFC, ST25R3911_REG_ISO14443A_NFC_nfc_f0, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            
            
            /*******************************************************************************/
            /* Clear and enable required IRQs */
            st25r3911DisableInterrupts( ST25R3911_IRQ_MASK_ALL, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 );
            
            
            st25r3911GetInterrupt( (ST25R3911_IRQ_MASK_NFCT | ST25R3911_IRQ_MASK_RXS | ST25R3911_IRQ_MASK_CRC | ST25R3911_IRQ_MASK_ERR1 |
                                    ST25R3911_IRQ_MASK_ERR2 | ST25R3911_IRQ_MASK_PAR | ST25R3911_IRQ_MASK_EON | ST25R3911_IRQ_MASK_EOF  | ST25R3911_IRQ_MASK_RXE ), mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            
            
            /*******************************************************************************/
            /* REMARK: Silicon workaround ST25R3911 Errata TDB                             */
            /* RXS and NFCT are triggered very close (specially in higher bitrates).       *
             * If the interrupt status register is being read when NFCT is trigerred, the  *
             * IRQ line might go low and NFCT is not signalled on the status register.     *
             * For initial bitrate detection, mask RXS, only wait for NFCT and RXE.        */
            /*******************************************************************************/
            
            st25r3911EnableInterrupts( (ST25R3911_IRQ_MASK_NFCT | ST25R3911_IRQ_MASK_CRC | ST25R3911_IRQ_MASK_ERR1 |
                                        ST25R3911_IRQ_MASK_ERR2 | ST25R3911_IRQ_MASK_PAR | ST25R3911_IRQ_MASK_EON | ST25R3911_IRQ_MASK_EOF  | ST25R3911_IRQ_MASK_RXE ),
            		mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            
            /*******************************************************************************/
            /* Clear the bitRate previously detected */
            gRFAL.Lm.brDetected = RFAL_BR_KEEP;
            
            
            /*******************************************************************************/
            /* Apply the BitRate detection mode mode */
                   mST25 -> writeRegister( ST25R3911_REG_MODE,
                		   (ST25R3911_REG_MODE_targ_targ | ST25R3911_REG_MODE_om_bit_rate_detection | ST25R3911_REG_MODE_nfc_ar_on),
						   mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            
            
            /*******************************************************************************/
            /* REMARK: Silicon workaround ST25R3911 Errata #1.3                            */
            /* Even though bitrate is going to be detected the bitrate must be set to      *
             * 106kbps to get correct 106kbps parity                                       */
                   mST25 -> writeRegister( ST25R3911_REG_BIT_RATE, (ST25R3911_REG_BIT_RATE_txrate_106 | ST25R3911_REG_BIT_RATE_rxrate_106),
                		   mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            /*******************************************************************************/
            
            
            /*******************************************************************************/
            /* Check if external Field is already On */
            if( rfalIsExtFieldOn( mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 )  )
            {
                return rfalListenSetState( RFAL_LM_STATE_IDLE, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;                         /* Set IDLE state */
            }
            break;
        
        /*******************************************************************************/
        case RFAL_LM_STATE_IDLE:
        
            /*******************************************************************************/
            /* In Active P2P the Initiator may:  Turn its field On;  LM goes into IDLE state;
             *      Initiator sends an unexpected frame raising a Protocol error; Initiator 
             *      turns its field Off and ST25R3911 performs the automatic RF Collision 
             *      Avoidance keeping our field On; upon a Protocol error upper layer sets 
             *      again the state to IDLE to clear dataFlag and wait for next data.
             *      
             * Ensure that when upper layer calls SetState(IDLE), it restores initial 
             * configuration and that check whether an external Field is still present     */
             
            /* nfc_ar may have triggered RF Collision Avoidance, disable it before executing Clear (Stop All activities) */
            st25r3911ClrRegisterBits( ST25R3911_REG_MODE, ST25R3911_REG_MODE_nfc_ar, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            mST25 -> executeCommand( ST25R3911_CMD_CLEAR_FIFO,  mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            st25r3911SetRegisterBits( ST25R3911_REG_MODE, ST25R3911_REG_MODE_nfc_ar, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            
            /* Ensure that our field is Off, as automatic response RF Collision Avoidance may have been triggered */
            st25r3911ClrRegisterBits(ST25R3911_REG_OP_CONTROL, ST25R3911_REG_OP_CONTROL_tx_en , mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;

            
            /* Load 2nd/3rd stage gain setting from registers into the receiver */
            mST25 -> executeCommand( ST25R3911_CMD_CLEAR_SQUELCH,  mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            
            /*******************************************************************************/
            /* REMARK: Silicon workaround ST25R3911 Errata #1.4                            */
            /* Enable; disable; enable mixer to make sure the digital decoder is in        *
             * high state. This also switches the demodulator to mixer mode.               */
            mST25 -> readRegister( ST25R3911_REG_RX_CONF1, &tmp,  mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            mST25 -> writeRegister( ST25R3911_REG_RX_CONF1, (tmp | ST25R3911_REG_RX_CONF1_amd_sel),  mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            mST25 -> writeRegister( ST25R3911_REG_RX_CONF1, (tmp & ~ST25R3911_REG_RX_CONF1_amd_sel),  mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            mST25 -> writeRegister( ST25R3911_REG_RX_CONF1, (tmp | ST25R3911_REG_RX_CONF1_amd_sel),  mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            /*******************************************************************************/
            
            /* ReEnable the receiver */
            mST25 -> executeCommand( ST25R3911_CMD_UNMASK_RECEIVE_DATA,  mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            
            
            /* If external Field is no longer detected go back to POWER_OFF */
            if( !st25r3911CheckReg(ST25R3911_REG_AUX_DISPLAY, ST25R3911_REG_AUX_DISPLAY_efd_o, ST25R3911_REG_AUX_DISPLAY_efd_o, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 )  )
            {
                return rfalListenSetState( RFAL_LM_STATE_POWER_OFF, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;                         /* Set POWER_OFF state */
            }

            /*******************************************************************************/
            /*Check if Observation Mode is enabled and set it on ST25R391x */
            rfalCheckEnableObsModeRx();
            break;
            
        /*******************************************************************************/
        case RFAL_LM_STATE_TARGET_A:
        case RFAL_LM_STATE_TARGET_F:
            /* States not handled by the LM, just keep state context */
            break;
            
        /*******************************************************************************/
        case RFAL_LM_STATE_READY_F:
        case RFAL_LM_STATE_CARDEMU_3:
        case RFAL_LM_STATE_READY_Ax:
        case RFAL_LM_STATE_READY_A:
        case RFAL_LM_STATE_ACTIVE_Ax:
        case RFAL_LM_STATE_ACTIVE_A:
        case RFAL_LM_STATE_SLEEP_A:
        case RFAL_LM_STATE_SLEEP_B:
        case RFAL_LM_STATE_SLEEP_AF:
        case RFAL_LM_STATE_CARDEMU_4A:
        case RFAL_LM_STATE_CARDEMU_4B:
            return ERR_NOTSUPP;
            
        /*******************************************************************************/
        default:
            return ERR_WRONG_STATE;
    }
    
    gRFAL.Lm.state = newSt;
    
    return ret;
}


/*******************************************************************************
 *  Wake-Up Mode                                                               *
 *******************************************************************************/

/*******************************************************************************/
ReturnCode rfalWakeUpModeStart( void *config, ST25R3911* mST25, SPI* mspiChannel, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 )
{
    uint8_t                aux;
    uint8_t                reg;
    uint32_t               irqs;
    
    /* The Wake-Up procedure is explained in detail in Application Note: AN4985 */
    
    if( config == NULL)
    {
        gRFAL.wum.cfg.period  = ST25R3911_WUM_PERIDOD_500MS;
        gRFAL.wum.cfg.irqTout = false;
      
        gRFAL.wum.cfg.indPha.enabled   = true;
        gRFAL.wum.cfg.indPha.delta     = 3;
        gRFAL.wum.cfg.indPha.reference = ST25R3911_WUM_REFRENCE_AUTO;
        gRFAL.wum.cfg.indPha.autoAvg   = true;


        gRFAL.wum.cfg.cap.enabled      = false;
        gRFAL.wum.cfg.cap.delta     = 3;
        gRFAL.wum.cfg.cap.reference = ST25R3911_WUM_REFRENCE_AUTO;
        gRFAL.wum.cfg.cap.autoAvg   = true;



        gRFAL.wum.cfg.indAmp.enabled   = false;
        gRFAL.wum.cfg.indAmp.delta     = 3;
        gRFAL.wum.cfg.indAmp.reference = ST25R3911_WUM_REFRENCE_AUTO;
        gRFAL.wum.cfg.indAmp.autoAvg   = true;
    }
    else
    {
        gRFAL.wum.cfg = *((st25r3911WakeUpConfig*)config);
    }
    
    
    if( gRFAL.wum.cfg.cap.enabled && (gRFAL.wum.cfg.indAmp.enabled || gRFAL.wum.cfg.indPha.enabled) )
    {
      return ERR_PARAM;
    }
    
    irqs = ST25R3911_IRQ_MASK_NONE;
    
    

    ///// conf wake up timer and control register IRQ if diff larger that delta am -> 00000100

   //mST25->writeRegister(ST25R3911_REG_WUP_TIMER_CONTROL, ST25R3911_REG_WUP_TIMER_CONTROL_wam, mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;




    /*******************************************************************************/
    /* Prepare Wake-Up Timer Control Register */
    reg  = ((gRFAL.wum.cfg.period & 0x0F) << ST25R3911_REG_WUP_TIMER_CONTROL_shift_wut);
    reg |= ((gRFAL.wum.cfg.period < ST25R3911_WUM_PERIDOD_100MS) ? ST25R3911_REG_WUP_TIMER_CONTROL_wur : 0x00);
    
    if(gRFAL.wum.cfg.irqTout)
    {
        reg  |= ST25R3911_REG_WUP_TIMER_CONTROL_wto;
        irqs |= ST25R3911_IRQ_MASK_WT;
    }
    
    /*******************************************************************************/
    /* Check if Inductive Amplitude is to be performed */
    if( gRFAL.wum.cfg.indAmp.enabled )
    {
        aux  = ((gRFAL.wum.cfg.indAmp.delta) << ST25R3911_REG_AMPLITUDE_MEASURE_CONF_shift_am_d);
        aux |= (gRFAL.wum.cfg.indAmp.aaInclMeas ? ST25R3911_REG_AMPLITUDE_MEASURE_CONF_am_aam : 0x00);
        aux |= ((gRFAL.wum.cfg.indAmp.aaWeight << ST25R3911_REG_AMPLITUDE_MEASURE_CONF_shift_am_aew) & ST25R3911_REG_AMPLITUDE_MEASURE_CONF_mask_am_aew);
        aux |= (gRFAL.wum.cfg.indAmp.autoAvg ? ST25R3911_REG_AMPLITUDE_MEASURE_CONF_am_ae : 0x00);
        
        mST25->writeRegister(ST25R3911_REG_AMPLITUDE_MEASURE_CONF, aux, mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
        
        /* Only need to set the reference if not using Auto Average */
        if( !gRFAL.wum.cfg.indAmp.autoAvg )
        {
            if( gRFAL.wum.cfg.indAmp.reference == ST25R3911_WUM_REFRENCE_AUTO )
            {
            		 st25r3911MeasureRF( &aux, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
                     mST25->writeRegister(ST25R3911_REG_AMPLITUDE_MEASURE_REF, aux, mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            }
            else
            {
            	    mST25->writeRegister(ST25R3911_REG_AMPLITUDE_MEASURE_REF, gRFAL.wum.cfg.indAmp.reference, mspiChannel, gpio_cs,
            	    		IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            }
        }
        
        reg  |= ST25R3911_REG_WUP_TIMER_CONTROL_wam;
        irqs |= ST25R3911_IRQ_MASK_WAM;
    }
    
    /*******************************************************************************/
    /* Check if Inductive Phase is to be performed */
    if( gRFAL.wum.cfg.indPha.enabled )
    {
        aux  = ((gRFAL.wum.cfg.indPha.delta) << ST25R3911_REG_PHASE_MEASURE_CONF_shift_pm_d);
        aux |= (gRFAL.wum.cfg.indPha.aaInclMeas ? ST25R3911_REG_PHASE_MEASURE_CONF_pm_aam : 0x00);
        aux |= ((gRFAL.wum.cfg.indPha.aaWeight << ST25R3911_REG_PHASE_MEASURE_CONF_shift_pm_aew) & ST25R3911_REG_PHASE_MEASURE_CONF_mask_pm_aew);
        aux |= (gRFAL.wum.cfg.indPha.autoAvg ? ST25R3911_REG_PHASE_MEASURE_CONF_pm_ae : 0x00);
        
            mST25->writeRegister(ST25R3911_REG_PHASE_MEASURE_CONF, aux, mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
        
        /* Only need to set the reference if not using Auto Average */
        if( !gRFAL.wum.cfg.indPha.autoAvg )
        {
            if( gRFAL.wum.cfg.indPha.reference == ST25R3911_WUM_REFRENCE_AUTO )
            {
                st25r3911MeasureAntennaResonance( &aux, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
                       mST25->writeRegister(ST25R3911_REG_PHASE_MEASURE_REF, aux, mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            }
            else
            {

                mST25->writeRegister(ST25R3911_REG_PHASE_MEASURE_REF,  gRFAL.wum.cfg.indPha.reference, mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;

            }
        }
        
        reg  |= ST25R3911_REG_WUP_TIMER_CONTROL_wph;
        irqs |= ST25R3911_IRQ_MASK_WPH;
    }
    
    /*******************************************************************************/
    /* Check if Capacitive is to be performed */
    if( gRFAL.wum.cfg.cap.enabled )
    {
        return ERR_NOT_IMPLEMENTED;
    }    
    
    /* Disable External Field Detector */
    st25r3911ClrRegisterBits( ST25R3911_REG_AUX, ST25R3911_REG_AUX_en_fd, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    
    /* Disable and clear all interrupts except Wake-Up IRQs */
    st25r3911DisableInterrupts( ST25R3911_IRQ_MASK_ALL, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    st25r3911GetInterrupt( irqs, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    st25r3911EnableInterrupts( irqs, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    
    /* Enable Low Power Wake-Up Mode */
    mST25->writeRegister(ST25R3911_REG_WUP_TIMER_CONTROL, reg, mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    mST25->writeRegister(ST25R3911_REG_OP_CONTROL, ST25R3911_REG_OP_CONTROL_wu, mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;


    
    gRFAL.wum.state = RFAL_WUM_STATE_ENABLED;
    gRFAL.state     = RFAL_STATE_WUM;  
      
    return ERR_NONE;
}


/*******************************************************************************/
bool rfalWakeUpModeHasWoke( ST25R3911* mST25, SPI* mspiChannel, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 )
{   

    return (gRFAL.wum.state == RFAL_WUM_STATE_ENABLED_WOKE);
}


/*******************************************************************************/
static void rfalRunWakeUpModeWorker( SPI* mspiChannel, ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 )
{
    uint32_t irqs;
    
    if( gRFAL.state != RFAL_STATE_WUM )
    {
        return;
    }
    
    switch( gRFAL.wum.state )
    {
        case RFAL_WUM_STATE_ENABLED:
        case RFAL_WUM_STATE_ENABLED_WOKE:
            
            irqs = st25r3911GetInterrupt( ( ST25R3911_IRQ_MASK_WT | ST25R3911_IRQ_MASK_WAM | ST25R3911_IRQ_MASK_WPH | ST25R3911_IRQ_MASK_WCAP ), mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
            if( irqs == ST25R3911_IRQ_MASK_NONE )
            {
               break;  /* No interrupt to process */
            }
            
            /*******************************************************************************/
            /* Check and mark which measurement(s) cause interrupt */
            if(irqs & ST25R3911_IRQ_MASK_WAM)
            {
                gRFAL.wum.state = RFAL_WUM_STATE_ENABLED_WOKE;
            }
            
            if(irqs & ST25R3911_IRQ_MASK_WPH)
            {
                gRFAL.wum.state = RFAL_WUM_STATE_ENABLED_WOKE;
            }
            
            if(irqs & ST25R3911_IRQ_MASK_WCAP)
            {
                gRFAL.wum.state = RFAL_WUM_STATE_ENABLED_WOKE;
            }
            break;
            
        default:
            break;
    }
}


/*******************************************************************************/
ReturnCode rfalWakeUpModeStop( SPI* mspiChannel, ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 )
{
    if( gRFAL.wum.state == RFAL_WUM_STATE_NOT_INIT )
    {
        return ERR_WRONG_STATE;
    }
    
    gRFAL.wum.state = RFAL_WUM_STATE_NOT_INIT;
    
    /* Re-Enable External Field Detector */
    st25r3911SetRegisterBits( ST25R3911_REG_AUX, ST25R3911_REG_AUX_en_fd, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    
    /* Disable Wake-Up Mode */
    st25r3911ClrRegisterBits( ST25R3911_REG_OP_CONTROL, ST25R3911_REG_OP_CONTROL_wu, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    st25r3911DisableInterrupts( (ST25R3911_IRQ_MASK_WT | ST25R3911_IRQ_MASK_WAM | ST25R3911_IRQ_MASK_WPH | ST25R3911_IRQ_MASK_WCAP),
    		mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    
    /* Re-Enable the Oscillator */
    st25r3911OscOn( mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
      
    return ERR_NONE;
}


/*******************************************************************************
 *  RF Chip                                                                    *
 *******************************************************************************/

/*******************************************************************************/
ReturnCode rfalChipWriteReg( uint16_t reg, uint8_t* values, uint8_t len, SPI * mspiChannel, ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 )
{
    if( !st25r3911IsRegValid( (uint8_t)reg) )
    {
        return ERR_PARAM;
    }

    mST25 -> writeMultipleRegisters( (uint8_t)reg, values, len, mspiChannel,mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    return ERR_NONE;
}

/*******************************************************************************/
ReturnCode rfalChipReadReg( uint16_t reg, uint8_t* values, uint8_t len, SPI * mspiChannel, ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 )
{
    if( !st25r3911IsRegValid( (uint8_t)reg) )
    {
        return ERR_PARAM;
    }
    
    mST25 -> readMultipleRegisters( (uint8_t)reg, values, len, mspiChannel,mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    return ERR_NONE;
}

/*******************************************************************************/
ReturnCode rfalChipExecCmd( uint16_t cmd, SPI * mspiChannel, ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 )
{
    if( !st25r3911IsCmdValid( (uint8_t)cmd) )
    {
        return ERR_PARAM;
    }
    
      mST25 -> executeCommand( (uint8_t) cmd, mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    return ERR_NONE;
}

/*******************************************************************************/
ReturnCode rfalChipWriteTestReg( uint16_t reg, uint8_t value, SPI * mspiChannel, ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 )
{
    st25r3911WriteTestRegister( (uint8_t)reg, value, mST25, mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    return ERR_NONE;
}

/*******************************************************************************/
ReturnCode rfalChipReadTestReg( uint16_t reg, uint8_t* value, SPI * mspiChannel, ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 )
{
    st25r3911ReadTestRegister( (uint8_t)reg, value, mST25, mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    return ERR_NONE;
}

/*******************************************************************************/
ReturnCode rfalChipChangeRegBits( uint16_t reg, uint8_t valueMask, uint8_t value, SPI* mspiChannel,
		ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 )
{
    st25r3911ChangeRegisterBits( (uint8_t)reg, valueMask, value, mspiChannel, mST25, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    return ERR_NONE;
}

/*******************************************************************************/
ReturnCode rfalChipChangeTestRegBits( uint16_t reg, uint8_t valueMask, uint8_t value, SPI * mspiChannel, ST25R3911* mST25,
		DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 )
{
    st25r3911ChangeTestRegisterBits( (uint8_t)reg, valueMask, value, mST25, mspiChannel, gpio_cs, IRQ, fieldLED_01, fieldLED_02, fieldLED_03, fieldLED_04, fieldLED_05, fieldLED_06 ) ;
    return ERR_NONE;
}



void rfalSetWumState( void )
{
    gRFAL.wum.state = RFAL_WUM_STATE_ENABLED_WOKE;
}


/*******************************************************************************/
/* extern uint8_t invalid_size_of_stream_configs[(sizeof(struct st25r3911StreamConfig) == sizeof(struct iso15693StreamConfig))?1:(-1)]; */
