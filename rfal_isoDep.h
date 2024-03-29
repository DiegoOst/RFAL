
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

/*! \file rfal_isoDep.h
 *
 *  \author Gustavo Patricio
 *
 *  \brief Implementation of ISO-DEP protocol
 *
 *  This implementation was based on the following specs:
 *    - ISO/IEC 14443-4  2nd Edition 2008-07-15
 *    - NFC Forum Digital Protocol  1.1 2014-01-14
 *
 *
 * @addtogroup RFAL
 * @{
 *
 * @addtogroup RFAL-AL
 * @brief RFAL Abstraction Layer
 * @{
 *
 * @addtogroup ISO-DEP
 * @brief RFAL ISO-DEP Module
 * @{
 *
 */

#ifndef RFAL_ISODEP_H_
#define RFAL_ISODEP_H_
/*
 ******************************************************************************
 * INCLUDES
 ******************************************************************************
 */
#include "platform1.h"
#include "rfal_nfcb.h"

/*
 ******************************************************************************
 * DEFINES
 ******************************************************************************
 */

#define RFAL_ISODEP_PROLOGUE_SIZE               (3)     /*!< Length of Prologue Field for I-Block Format                       */

#define RFAL_ISODEP_PCB_LEN                     (1)     /*!< PCB length                                                        */
#define RFAL_ISODEP_DID_LEN                     (1)     /*!< DID length                                                        */
#define RFAL_ISODEP_NAD_LEN                     (1)     /*!< NAD length                                                        */
#define RFAL_ISODEP_NO_DID                      (0x00)  /*!< DID value indicating the ISO-DEP layer not to use DID             */
#define RFAL_ISODEP_NO_NAD                      (0xFF)  /*!< NAD value indicating the ISO-DEP layer not to use NAD             */

#define RFAL_ISODEP_FSDI_DEFAULT                (8)     /*!< Default Frame Size Integer supported by NFCC as Initiator         */
#define RFAL_ISODEP_FWI_MASK                    (0xF0)  /*!< Mask bits of FWI                                                  */
#define RFAL_ISODEP_FWI_SHIFT                   (4)     /*!< Shift val of FWI                                                  */
#define RFAL_ISODEP_FWI_DEFAULT                 (4)     /*!< Default value for FWI Digital 1.0 11.6.2.17                       */
#define RFAL_ISODEP_ADV_FEATURE                 (0x0F)  /*!< Indicate 256 Bytes FSD and Advanc Proto Feature support:NAD & DID */

#define RFAL_ISODEP_DID_MAX                     (14)    /*!< Maximum DID value                                                 */

#define RFAL_ISODEP_BRI_MASK                    (0x07)  /*!< Mask bits for Poll to Listen Send bitrate                         */
#define RFAL_ISODEP_BSI_MASK                    (0x70)  /*!< Mask bits for Listen to Poll Send bitrate                         */
#define RFAL_ISODEP_SAME_BITRATE_MASK           (0x80)  /*!< Mask bit indicate only same bit rate D for both direction support */
#define RFAL_ISODEP_BITRATE_RFU_MASK            (0x08)  /*!< Mask bit for RFU                                                  */

/*! Maximum Frame Waiting Time = ((256 * 16/fc) * 2^FWImax) = ((256*16/fc)*2^14) = (67108864)/fc = 2^26 (1/fc)                 */
#define RFAL_ISODEP_MAX_FWT                     (1<<26)



#define RFAL_ISODEP_FSX_KEEP                    (0xFF)               /*!< Flag to keep FSX from activation                     */
#define RFAL_ISODEP_DEFAULT_FSCI                RFAL_ISODEP_FSXI_256 /*!< FSCI default value to be used  in Listen Mode        */
#define RFAL_ISODEP_DEFAULT_FSC                 RFAL_ISODEP_FSX_256  /*!< FSC default value (aligned RFAL_ISODEP_DEFAULT_FSCI) */
#define RFAL_ISODEP_DEFAULT_SFGI                (0)                  /*!< SFGI Default value to be used  in Listen Mode        */

#define RFAL_ISODEP_APDU_MAX_LEN                RFAL_ISODEP_FSX_1024  /*!< Max APDU length                                     */

#define RFAL_ISODEP_ATTRIB_RES_MBLI_NO_INFO     (0x00)  /*!< MBLI indicating no information on its internal input buffer size  */
#define RFAL_ISODEP_ATTRIB_REQ_PARAM1_DEFAULT   (0x00)  /*!< Default values of Param 1 of ATTRIB_REQ Digital 1.0  12.6.1.3-5   */
#define RFAL_ISODEP_ATTRIB_HLINFO_LEN           (32)    /*!< Maximum Size of Higher Layer Information                          */
#define RFAL_ISODEP_ATS_HB_MAX_LEN              (15)    /*!< Maximum length of Historical Bytes  Digital 1.1  13.6.2.23        */
#define RFAL_ISODEP_ATTRIB_REQ_MIN_LEN          (9)     /*!< Minimum Length of ATTRIB_REQ command                              */
#define RFAL_ISODEP_ATTRIB_RES_MIN_LEN          (1)     /*!< Minimum Length of ATTRIB_RES response                             */

#define RFAL_ISODEP_ATS_TA_DPL_212              (0x01)  /*!< ATS TA DSI 212 kbps support bit mask                              */
#define RFAL_ISODEP_ATS_TA_DPL_424              (0x02)  /*!< ATS TA DSI 424 kbps support bit mask                              */
#define RFAL_ISODEP_ATS_TA_DPL_848              (0x04)  /*!< ATS TA DSI 848 kbps support bit mask                              */
#define RFAL_ISODEP_ATS_TA_DLP_212              (0x10)  /*!< ATS TA DSI 212 kbps support bit mask                              */
#define RFAL_ISODEP_ATS_TA_DLP_424              (0x20)  /*!< ATS TA DRI 424 kbps support bit mask                              */
#define RFAL_ISODEP_ATS_TA_DLP_848              (0x40)  /*!< ATS TA DRI 848 kbps support bit mask                              */
#define RFAL_ISODEP_ATS_TA_SAME_D               (0x80)  /*!< ATS TA same bit both directions bit mask                          */
#define RFAL_ISODEP_ATS_TB_FWI_MASK             (0xF0)  /*!< Mask bits for FWI (Frame Waiting Integer) in TB byte              */
#define RFAL_ISODEP_ATS_TB_SFGI_MASK            (0x0F)  /*!< Mask bits for SFGI (Start-Up Frame Guard Integer) in TB byte      */

#define RFAL_ISODEP_ATS_T0_TA_PRESENCE_MASK     (0x10)  /*!< Mask bit for TA presence                                          */
#define RFAL_ISODEP_ATS_T0_TB_PRESENCE_MASK     (0x20)  /*!< Mask bit for TB presence                                          */
#define RFAL_ISODEP_ATS_T0_TC_PRESENCE_MASK     (0x40)  /*!< Mask bit for TC presence                                          */
#define RFAL_ISODEP_ATS_T0_FSCI_MASK            (0x0F)  /*!< Mask bit for FSCI presence                                        */
#define RFAL_ISODEP_ATS_T0_OFFSET               (0x01)  /*!< Offset of T0 in ATS Response                                      */


#define RFAL_ISODEP_MAX_I_RETRYS                (2)     /*!< Number of retries for a I-Block  Digital 1.1   15.2.5.4                            */
#define RFAL_ISODEP_MAX_R_RETRYS                (3)     /*!< Number of retries for a R-Block  Digital 1.1 A8 - nRETRY ACK/NAK:  [2,5]           */
#define RFAL_ISODEP_MAX_S_RETRYS                (3)     /*!< Number of retries for a S-Block  Digital 1.1 A8 - nRETRY DESELECT: [0,5] WTX[2,5]  */
#define RFAL_ISODEP_RATS_RETRIES                (1)     /*!< RATS retries upon fail           Digital 1.1  A.6 - [0,1]                          */


/*! Frame Size for Proximity Card Integer definitions                                                               */
typedef enum
{
      RFAL_ISODEP_FSXI_16   =  0,  /*!< Frame Size for Proximity Card Integer with 16 bytes                         */
      RFAL_ISODEP_FSXI_24   =  1,  /*!< Frame Size for Proximity Card Integer with 24 bytes                         */
      RFAL_ISODEP_FSXI_32   =  2,  /*!< Frame Size for Proximity Card Integer with 32 bytes                         */
      RFAL_ISODEP_FSXI_40   =  3,  /*!< Frame Size for Proximity Card Integer with 40 bytes                         */
      RFAL_ISODEP_FSXI_48   =  4,  /*!< Frame Size for Proximity Card Integer with 48 bytes                         */
      RFAL_ISODEP_FSXI_64   =  5,  /*!< Frame Size for Proximity Card Integer with 64 bytes                         */
      RFAL_ISODEP_FSXI_96   =  6,  /*!< Frame Size for Proximity Card Integer with 96 bytes                         */
      RFAL_ISODEP_FSXI_128  =  7,  /*!< Frame Size for Proximity Card Integer with 128 bytes                        */
      RFAL_ISODEP_FSXI_256  =  8,  /*!< Frame Size for Proximity Card Integer with 256 bytes                        */
      RFAL_ISODEP_FSXI_512  =  9,  /*!< Frame Size for Proximity Card Integer with 512 bytes   ISO14443-3 Amd2 2012 */
      RFAL_ISODEP_FSXI_1024 = 10,  /*!< Frame Size for Proximity Card Integer with 1024 bytes  ISO14443-3 Amd2 2012 */
      RFAL_ISODEP_FSXI_2048 = 11,  /*!< Frame Size for Proximity Card Integer with 2048 bytes  ISO14443-3 Amd2 2012 */
      RFAL_ISODEP_FSXI_4096 = 12   /*!< Frame Size for Proximity Card Integer with 4096 bytes  ISO14443-3 Amd2 2012 */
} rfalIsoDepFSxI;

/*! Frame Size for Proximity Card  definitions                                                             */
typedef enum
{
    RFAL_ISODEP_FSX_16   = 16,    /*!< Frame Size for Proximity Card with 16 bytes                         */
    RFAL_ISODEP_FSX_24   = 24,    /*!< Frame Size for Proximity Card with 16 bytes                         */
    RFAL_ISODEP_FSX_32   = 32,    /*!< Frame Size for Proximity Card with 32 bytes                         */
    RFAL_ISODEP_FSX_40   = 40,    /*!< Frame Size for Proximity Card with 40 bytes                         */
    RFAL_ISODEP_FSX_48   = 48,    /*!< Frame Size for Proximity Card with 48 bytes                         */
    RFAL_ISODEP_FSX_64   = 64,    /*!< Frame Size for Proximity Card with 64 bytes                         */
    RFAL_ISODEP_FSX_96   = 96,    /*!< Frame Size for Proximity Card with 96 bytes                         */
    RFAL_ISODEP_FSX_128  = 128,   /*!< Frame Size for Proximity Card with 128 bytes                        */
    RFAL_ISODEP_FSX_256  = 256,   /*!< Frame Size for Proximity Card with 256 bytes                        */
    RFAL_ISODEP_FSX_512  = 512,   /*!< Frame Size for Proximity Card with 512 bytes   ISO14443-3 Amd2 2012 */
    RFAL_ISODEP_FSX_1024 = 1024,  /*!< Frame Size for Proximity Card with 1024 bytes  ISO14443-3 Amd2 2012 */
    RFAL_ISODEP_FSX_2048 = 2048,  /*!< Frame Size for Proximity Card with 2048 bytes  ISO14443-3 Amd2 2012 */
    RFAL_ISODEP_FSX_4096 = 4096,  /*!< Frame Size for Proximity Card with 4096 bytes  ISO14443-3 Amd2 2012 */
} rfalIsoDepFSx;

/*
 ******************************************************************************
 * GLOBAL MACROS
 ******************************************************************************
 */

/*
 ******************************************************************************
 * GLOBAL DATA TYPES
 ******************************************************************************
 */

/*! RATS format  Digital 1.1 13.6.1                                                               */
typedef struct
{
    uint8_t      CMD;                               /*!< RATS command byte: 0xE0                  */
    uint8_t      PARAM;                             /*!< Param indicating FSDI and DID            */
} rfalIsoDepRats;


/*! ATS response format  Digital 1.1 13.6.2                                                       */
typedef struct
{
  uint8_t        TL;                                /*!< Length Byte, including TL byte itself    */
  uint8_t        T0;                                /*!< Format Byte T0 indicating if TA, TB, TC  */
  uint8_t        TA;                                /*!< Interface Byte TA(1)                     */
  uint8_t        TB;                                /*!< Interface Byte TB(1)                     */
  uint8_t        TC;                                /*!< Interface Byte TC(1)                     */
  uint8_t        HB[RFAL_ISODEP_ATS_HB_MAX_LEN];    /*!< Historical Bytes                         */
} rfalIsoDepAts;


/*! PPS Request format (Protocol and Parameter Selection) ISO14443-4  5.3                         */
typedef struct
{
    uint8_t      PPSS;                              /*!< Start Byte: [ 1101b | CID[4b] ]          */
    uint8_t      PPS0;                              /*!< Parameter 0:[ 000b | PPS1[1n] | 0001b ]  */
    uint8_t      PPS1;                              /*!< Parameter 1:[ 0000b | DSI[2b] | DRI[2b] ]*/
} rfalIsoDepPpsReq;


/*! PPS Response format (Protocol and Parameter Selection) ISO14443-4  5.4                        */
typedef struct
{
    uint8_t      PPSS;                              /*!< Start Byte:  [ 1101b | CID[4b] ]  */
} rfalIsoDepPpsRes;


/*! ATTRIB Command Format  Digital 1.1  15.6.1 */
typedef struct
{
    uint8_t         cmd;                                   /*!< ATTRIB_REQ command byte           */
    uint8_t         nfcid0[RFAL_NFCB_NFCID0_LEN];          /*!< NFCID0 of the card to be selected */
    struct{
            uint8_t PARAM1;                                /*!< PARAM1 of ATTRIB command          */
            uint8_t PARAM2;                                /*!< PARAM2 of ATTRIB command          */
            uint8_t PARAM3;                                /*!< PARAM3 of ATTRIB command          */
            uint8_t PARAM4;                                /*!< PARAM4 of ATTRIB command          */
    }Param;                                                /*!< Parameter of ATTRIB command       */
    uint8_t         HLInfo[RFAL_ISODEP_ATTRIB_HLINFO_LEN]; /*!< Higher Layer Information          */
} rfalIsoDepAttribCmd;


/*! ATTRIB Response Format  Digital 1.1  15.6.2 */
typedef struct
{
    uint8_t         mbliDid;                               /*!< Contains MBLI and DID             */
    uint8_t         HLInfo[RFAL_ISODEP_ATTRIB_HLINFO_LEN]; /*!< Higher Layer Information          */
} rfalIsoDepAttribRes;


/*! Activation info as Poller and Listener for NFC-A and NFC-B                                    */
typedef union {

    /*! NFC-A information                                                                         */
    union {
        struct {
            rfalIsoDepAts        ATS;               /*!< ATS response            (Poller mode)    */
            uint8_t              ATSLen;            /*!< ATS response length     (Poller mode)    */
            }Listener;
        struct {
            rfalIsoDepRats      RATS;               /*!< RATS request          (Listener mode)    */
        }Poller;
    }A;

    /*! NFC-B information                                                                         */
    union {
        struct{
            rfalIsoDepAttribRes  ATTRIB_RES;        /*!< ATTRIB_RES              (Poller mode)    */
            uint8_t              ATTRIB_RESLen;     /*!< ATTRIB_RES length       (Poller mode)    */
        }Listener;
        struct{
            rfalIsoDepAttribCmd  ATTRIB;            /*!< ATTRIB request        (Listener mode)    */
            uint8_t              ATTRIBLen;         /*!< ATTRIB request length (Listener mode)    */
        }Poller;
    }B;
}rfalIsoDepActivation;


/*! ISO-DEP device Info */
typedef struct {
    uint8_t            FWI;             /*!< Frame Waiting Integer                                */
    uint32_t           FWT;             /*!< Frame Waiting Time (1/fc)                            */
    uint32_t           dFWT;            /*!< Delta Frame Waiting Time (1/fc)                      */
    uint32_t           SFGI;            /*!< Start-up Frame Guard time Integer                    */
    uint32_t           SFGT;            /*!< Start-up Frame Guard Time (ms)                       */
    uint8_t            FSxI;            /*!< Frame Size Device/Card Integer (FSDI or FSCI)        */
    uint16_t           FSx;             /*!< Frame Size Device/Card (FSD or FSC)                  */
    uint32_t           MBL;             /*!< Maximum Buffer Length (optional for NFC-B)           */
    rfalBitRate        DSI;             /*!< Bit Rate coding from Listener (PICC) to Poller (PCD) */
    rfalBitRate        DRI;             /*!< Bit Rate coding from Poller (PCD) to Listener (PICC) */
    uint8_t            DID;             /*!< Device ID                                            */
    uint8_t            NAD;             /*!< Node ADdress                                         */
    bool               supDID;          /*!< DID supported flag                                   */
    bool               supNAD;          /*!< NAD supported flag                                   */
    bool               supAdFt;         /*!< Advanced Features supported flag                     */
} rfalIsoDepInfo;


/*! ISO-DEP Device structure */
typedef struct {
    rfalIsoDepActivation    activation; /*!< Activation Info                                      */
    rfalIsoDepInfo          info;       /*!< ISO-DEP (ISO14443-4) device Info                     */
} rfalIsoDepDevice;


/*! ATTRIB Response parameters */
typedef struct
{
    uint8_t  mbli;                                  /*!< MBLI                                     */
    uint8_t  HLInfo[RFAL_ISODEP_ATTRIB_HLINFO_LEN]; /*!< Hi Layer Information                     */
    uint8_t  HLInfoLen;                             /*!< Hi Layer Information Length              */
} rfalIsoDepAttribResParam;


/*! ATS Response parameter */
typedef struct
{
    uint8_t     fsci;                               /*!< Frame Size of Proximity Card Integer     */
    uint8_t     fwi;                                /*!< Frame Waiting Time Integer               */
    uint8_t     sfgi;                               /*!< Start-Up Frame Guard Time Integer        */
    bool        didSupport;                         /*!< DID Supported                            */
    uint8_t     ta;                                 /*!< Max supported bitrate both direction     */
    uint8_t     *hb;                                /*!< Historical Bytes data                    */
    uint8_t     hbLen;                              /*!< Historical Bytes Length                  */
} rfalIsoDepAtsParam;


/*! Structure of I-Block Buffer format from caller */
typedef struct
{
    uint8_t  prologue[RFAL_ISODEP_PROLOGUE_SIZE];   /*!< Prologue/SoD buffer                      */
    uint8_t  inf[RFAL_ISODEP_DEFAULT_FSC];          /*!< INF/Payload buffer                       */
} rfalIsoDepBufFormat;


/*! Structure of APDU Buffer format from caller */
typedef struct
{
    uint8_t  prologue[RFAL_ISODEP_PROLOGUE_SIZE];   /*!< Prologue/SoD buffer                      */
    uint8_t  apdu[RFAL_ISODEP_APDU_MAX_LEN];        /*!< APDU/Payload buffer                      */
} rfalIsoDepApduBufFormat;


/*! Listen Activation Parameters Structure */
typedef struct
{
    rfalIsoDepBufFormat  *rxBuf;                    /*!< Receive Buffer struct reference          */
    uint16_t             *rxLen;                    /*!< Received INF data length in Bytes        */
    bool                 *isRxChaining;             /*!< Received data is not complete            */
    rfalIsoDepDevice     *isoDepDev;                /*!< ISO-DEP device info                      */
} rfalIsoDepListenActvParam;


/*! Structure of parameters used on ISO DEP Transceive */
typedef struct
{
    rfalIsoDepBufFormat  *txBuf;                    /*!< Transmit Buffer struct reference         */
    uint16_t             txBufLen;                  /*!< Transmit Buffer INF field length in Bytes*/
    bool                 isTxChaining;              /*!< Transmit data is not complete            */
    rfalIsoDepBufFormat  *rxBuf;                    /*!< Receive Buffer struct reference in Bytes */
    uint16_t             *rxLen;                    /*!< Received INF data length in Bytes         */
    bool                 *isRxChaining;             /*!< Received data is not complete            */
    uint32_t             FWT;                       /*!< FWT to be used (ignored in Listen Mode)  */
    uint32_t             dFWT;                      /*!< Delta FWT to be used                     */
    uint16_t             ourFSx;                    /*!< Our device Frame Size (FSD or FSC)       */
    uint16_t             FSx;                       /*!< Other device Frame Size (FSD or FSC)     */
    uint8_t              DID;                       /*!< Device ID (RFAL_ISODEP_NO_DID if no DID) */
} rfalIsoDepTxRxParam;


/*! Structure of parameters used on ISO DEP APDU Transceive */
typedef struct
{
    rfalIsoDepApduBufFormat  *txBuf;                /*!< Transmit Buffer struct reference         */
    uint16_t                 txBufLen;              /*!< Transmit Buffer INF field length in Bytes*/
    rfalIsoDepApduBufFormat  *rxBuf;                /*!< Receive Buffer struct reference in Bytes */
    uint16_t                 *rxLen;                /*!< Received INF data length in Bytes        */
    rfalIsoDepBufFormat      *tmpBuf;               /*!< Temp buffer for Rx I-Blocks (internal)   */
    uint32_t                 FWT;                   /*!< FWT to be used (ignored in Listen Mode)  */
    uint32_t                 dFWT;                  /*!< Delta FWT to be used                     */
    uint16_t                 FSx;                   /*!< Other device Frame Size (FSD or FSC)     */
    uint16_t                 ourFSx;                /*!< Our device Frame Size (FSD or FSC)       */
    uint8_t                  DID;                   /*!< Device ID (RFAL_ISODEP_NO_DID if no DID) */
} rfalIsoDepApduTxRxParam;

/*
 ******************************************************************************
 * GLOBAL FUNCTION PROTOTYPES
 ******************************************************************************
 */


/*!
 ******************************************************************************
 * \brief Initialize the ISO-DEP protocol
 *
 * Initialize the ISO-DEP protocol layer with default config
 ******************************************************************************
 */
void rfalIsoDepInitialize( void );


/*!
 ******************************************************************************
 * \brief Initialize the ISO-DEP protocol
 *
 * Initialize the ISO-DEP protocol layer with additional parameters allowing
 * to customise the protocol layer for specific behaviours
 *
 *  \param[in] compMode       : compliance mode to be performed
 *  \param[in] maxRetriesR    :  Number of retries for a R-Block
 *  \param[in] maxRetriesS    :  Number of retries for a S-Block
 *  \param[in] maxRetriesI    :  Number of retries for a I-Block
 *  \param[in] maxRetriesRATS :  Number of retries for RATS
 *
 ******************************************************************************
 */
void rfalIsoDepInitializeWithParams( rfalComplianceMode compMode, uint8_t maxRetriesR, uint8_t maxRetriesS, uint8_t maxRetriesI, uint8_t maxRetriesRATS );


/*!
 *****************************************************************************
 *  \brief  FSxI to FSx
 *
 *  Convert Frame Size for proximity coupling Device Integer (FSxI) to
 *  Frame Size for proximity coupling Device (FSx)
 *
 *  FSD - maximum frame size for NFC Forum Device in Poll Mode
 *  FSC - maximum frame size for NFC Forum Device in Listen Mode
 *
 *  FSxI = FSDI or FSCI
 *  FSx  = FSD or FSC
 *
 *  The FSD/FSC value includes the header and CRC
 *
 *  \param[in] fsxi :  Frame Size for proximity coupling Device Integer
 *
 *  \return fsx : Frame Size for proximity coupling Device (FSD or FSC)
 *
 *****************************************************************************
 */
uint16_t rfalIsoDepFSxI2FSx( uint8_t fsxi );


/*!
 *****************************************************************************
 *  \brief  FWI to FWT
 *
 *  Convert Frame Waiting time Integer (FWI) to Frame Waiting Time (FWT) in
 *  1/fc units
 *
 *  \param[in] fwi : Frame Waiting time Integer
 *
 *  \return fwt : Frame Waiting Time in 1/fc units
 *
 *****************************************************************************
 */
uint32_t rfalIsoDepFWI2FWT( uint8_t fwi );


/*!
 *****************************************************************************
 *  \brief  Check if the buffer data contains a valid RATS command
 *
 *  Check if it is a  well formed RATS command with 2 bytes
 *  This function does not check the validity of FSDI and DID
 *
 *  \param[in] buf    : reference to buffer containing the data to be checked
 *  \param[in] bufLen : length of data in the buffer in bytes
 *
 *  \return true if the data indicates a RATS command; false otherwise
 *****************************************************************************
 */
bool rfalIsoDepIsRats( uint8_t *buf, uint8_t bufLen );


/*!
 *****************************************************************************
 *  \brief  Check if the buffer data contains a valid ATTRIB command
 *
 *  Check if it is a well formed ATTRIB command, but does not check the
 *  validity of the information inside
 *
 *  \param[in] buf    : reference to buffer containing the data to be checked
 *  \param[in] bufLen : length of data in the buffer in bytes
 *
 *  \return true if the data indicates a ATTRIB command; false otherwise
 *****************************************************************************
 */
bool rfalIsoDepIsAttrib( uint8_t *buf, uint8_t bufLen );


/*!
 *****************************************************************************
 * \brief Start Listen Activation Handling
 *
 * Start Listen Activation Handling and setup to receive first I-block which may
 * contain complete or partial APDU after activation is completed
 *
 *  Pass in RATS for T4AT, or ATTRIB for T4BT, to handle ATS or ATTRIB Response respectively
 *  The Activation Handling handles ATS and ATTRIB Response; and additionally PPS Response
 *  if a PPS is received for T4AT.
 *  The method uses the current RFAL state machine to determine if it is expecting RATS or ATTRIB
 *
 *  Activation is completed if PPS Response is sent or if first PDU is received in T4T-A
 *  Activation is completed if ATTRIB Response is sent in T4T-B
 *
 *  \ref rfalIsoDepListenGetActivationStatus provide status if activation is completed.
 *  \ref rfalIsoDepStartTransceive shall be called right after activation is completed
 *
 *  \param[in] atsParam       : reference to ATS parameters
 *  \param[in] attribResParam : reference to ATTRIB_RES parameters
 *  \param[in] buf            : reference to buffer containing RATS or ATTRIB
 *  \param[in] bufLen         : length in bytes of the given bufffer
 *  \param[in] rxParam        : reference to incoming reception information will be placed
 *
 *
 *  \warning Once the Activation has been completed the method
 *  rfalIsoDepGetTransceiveStatus() must be called.
 *  If activation has completed due to reception of a data block (not PPS) the
 *  buffer owned by the caller and passed on rxParam must still contain this data.
 *  The first data will be processed (I-Block or S-DSL) by rfalIsoDepGetTransceiveStatus()
 *  inform the caller and then for the next transaction use rfalIsoDepStartTransceive()
 *
 *  \return ERR_NONE    : RATS/ATTRIB is valid and activation has started
 *  \return ERR_PARAM   : Invalid parameters
 *  \return ERR_PROTO   : Invalid request
 *  \return ERR_NOTSUPP : Feature not supported
 *****************************************************************************
 */
ReturnCode rfalIsoDepListenStartActivation( rfalIsoDepAtsParam *atsParam, rfalIsoDepAttribResParam *attribResParam, uint8_t *buf, uint16_t bufLen, rfalIsoDepListenActvParam rxParam, SPI* mspiChannel, ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 );


/*!
 *****************************************************************************
 *  \brief Get the current Activation Status
 *
 *  \return ERR_NONE if Activation is already completed
 *  \return ERR_BUSY if Activation is ongoing
 *  \return ERR_LINK_LOSS if Remote Field is turned off
 *****************************************************************************
 */
ReturnCode rfalIsoDepListenGetActivationStatus( SPI* mspiChannel, ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 );


/*!
 *****************************************************************************
 *  \brief Get the ISO-DEP Communication Information
 *
 *  Gets the maximum INF length in bytes based on current Frame Size
 *  for proximity coupling Device (FSD or FSC) excluding the header and CRC
 *
 *  \return maximum INF length in bytes
 *****************************************************************************
 */
uint16_t rfalIsoDepGetMaxInfLen( void );


/*!
 *****************************************************************************
 *  \brief ISO-DEP Start Transceive
 *
 *  This method triggers a ISO-DEP Transceive containing a complete or
 *  partial APDU
 *  It transmits the given message and handles all protocol retransmitions,
 *  error handling and control messages
 *
 *  The txBuf  contains a complete or partial APDU (INF) to be transmitted
 *  The Prologue field will be manipulated by the Transceive
 *
 *  If the buffer contains a partial APDU and is not the last block,
 *  then isTxChaining must be set to true
 *
 *  \param[in] param: reference parameters to be used for the Transceive
 *
 *  \return ERR_PARAM       : Bad request
 *  \return ERR_WRONG_STATE : The module is not in a proper state
 *  \return ERR_NONE        : The Transceive request has been started
 *****************************************************************************
 */
ReturnCode rfalIsoDepStartTransceive( rfalIsoDepTxRxParam param );


/*!
 *****************************************************************************
 *  \brief Get the Transceive status
 *
 *  Returns the status of the ISO-DEP Transceive
 *
 *  \warning  When the other device is performing chaining once a chained
 *            block is received the error ERR_AGAIN is sent. At this point
 *            caller must handle the received data immediately.
 *            When ERR_AGAIN is returned an ACK has already been sent to
 *            the other device and the next block might be incoming.
 *            If rfalWorker() is called frequently it will place the next
 *            block on the given buffer
 *
 *
 *  \return ERR_NONE      : Transceive has been completed successfully
 *  \return ERR_BUSY      : Transceive is ongoing
 *  \return ERR_PROTO     : Protocol error occurred
 *  \return ERR_TIMEOUT   : Timeout error occurred
 *  \return ERR_SLEEP_REQ : Deselect has been received and responded
 *  \return ERR_NOMEM     : The received INF does not fit into the
 *                            receive buffer
 *  \return ERR_LINK_LOSS : Communication is lost because Reader/Writer
 *                            has turned off its field
 *  \return ERR_AGAIN     : received one chaining block, continue to call
 *                            this method to retrieve the remaining blocks
 *****************************************************************************
 */
ReturnCode rfalIsoDepGetTransceiveStatus( SPI* mspiChannel, ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 );


/*!
 *****************************************************************************
 *  \brief ISO-DEP Start APDU Transceive
 *
 *  This method triggers a ISO-DEP Transceive containing a complete APDU
 *  It transmits the given message and handles all protocol retransmitions,
 *  error handling and control messages
 *
 *  The txBuf  contains a complete APDU to be transmitted
 *  The Prologue field will be manipulated by the Transceive
 *
 *  \warning the txBuf will be modified during the transmission
 *  \warning the maximum RF frame which can be received is limited by param.tmpBuf
 *
 *  \param[in] param: reference parameters to be used for the Transceive
 *
 *  \return ERR_PARAM       : Bad request
 *  \return ERR_WRONG_STATE : The module is not in a proper state
 *  \return ERR_NONE        : The Transceive request has been started
 *****************************************************************************
 */
ReturnCode rfalIsoDepStartApduTransceive( rfalIsoDepApduTxRxParam param );


/*!
 *****************************************************************************
 *  \brief Get the APDU Transceive status
 *
 *  \return ERR_NONE      : if Transceive has been completed successfully
 *  \return ERR_BUSY      : if Transceive is ongoing
 *  \return ERR_PROTO     : if a protocol error occurred
 *  \return ERR_TIMEOUT   : if a timeout error occurred
 *  \return ERR_SLEEP_REQ : if Deselect is received and responded
 *  \return ERR_NOMEM     : if the received INF does not fit into the
 *                            receive buffer
 *  \return ERR_LINK_LOSS : if communication is lost because Reader/Writer
 *                            has turned off its field
 *****************************************************************************
 */
ReturnCode rfalIsoDepGetApduTransceiveStatus( SPI*  mspiChannel, ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 );

/*!
 *****************************************************************************
 *  \brief  ISO-DEP Send RATS
 *
 *  This sends a RATS to make a NFC-A Listen Device to enter
 *  ISO-DEP layer (ISO14443-4) and checks if the received ATS is valid
 *
 *  \param[in]  FSDI   : Frame Size Device Integer to be used
 *  \param[in]  DID    : Device ID to be used or RFAL_ISODEP_NO_DID for not use DID
 *  \param[out] ats    : pointer to place the ATS Response
 *  \param[out] atsLen : pointer to place the ATS length
 *
 *  \return ERR_WRONG_STATE  : RFAL not initialized or incorrect mode
 *  \return ERR_PARAM        : Invalid parameters
 *  \return ERR_IO           : Generic internal error
 *  \return ERR_TIMEOUT      : Timeout error
 *  \return ERR_PAR          : Parity error detected
 *  \return ERR_CRC          : CRC error detected
 *  \return ERR_FRAMING      : Framing error detected
 *  \return ERR_PROTO        : Protocol error detected
 *  \return ERR_NONE         : No error, ATS received
 *****************************************************************************
 */
ReturnCode rfalIsoDepRATS( rfalIsoDepFSxI FSDI, uint8_t DID, rfalIsoDepAts *ats , uint8_t *atsLen, SPI* mspiChannel, ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 );


/*!
 *****************************************************************************
 *  \brief  ISO-DEP Send PPS
 *
 *  This sends a PPS to make a NFC-A Listen Device change the communications
 *  bit rate from 106kbps to one of the supported bit rates
 *  Additionally checks if the received PPS response is valid
 *
 *  \param[in]  DID    : Device ID
 *  \param[in]  DSI    : DSI code the divisor from Listener (PICC) to Poller (PCD)
 *  \param[in]  DRI    : DRI code the divisor from Poller (PCD) to Listener (PICC)
 *  \param[out] ppsRes : pointer to place the PPS Response
 *
 *  \return ERR_WRONG_STATE  : RFAL not initialized or incorrect mode
 *  \return ERR_PARAM        : Invalid parameters
 *  \return ERR_IO           : Generic internal error
 *  \return ERR_TIMEOUT      : Timeout error
 *  \return ERR_PAR          : Parity error detected
 *  \return ERR_CRC          : CRC error detected
 *  \return ERR_FRAMING      : Framing error detected
 *  \return ERR_PROTO        : Protocol error detected
 *  \return ERR_NONE         : No error, PPS Response received
 *****************************************************************************
 */
ReturnCode rfalIsoDepPPS( uint8_t DID, rfalBitRate DSI, rfalBitRate DRI, rfalIsoDepPpsRes *ppsRes, SPI* mspiChannel, ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 );


/*!
 *****************************************************************************
 *  \brief  ISO-DEP Send ATTRIB
 *
 *  This sends a ATTRIB to make a NFC-B Listen Device to enter
 *  ISO-DEP layer (ISO14443-4) and checks if the received ATTRIB Response is valid
 *
 *  \param[in]  nfcid0    : NFCID0 to be used for the ATTRIB
 *  \param[in]  PARAM1    : ATTRIB PARAM1 byte (communication parameters)
 *  \param[in]  DSI       : DSI code the divisor from Listener (PICC) to Poller (PCD)
 *  \param[in]  DRI       : DRI code the divisor from Poller (PCD) to Listener (PICC)
 *  \param[in]  FSDI      : PCD's Frame Size to be announced on the ATTRIB
 *  \param[in]  PARAM3    : ATTRIB PARAM1 byte (protocol type)
 *  \param[in]  DID       : Device ID to be used or RFAL_ISODEP_NO_DID for not use DID
 *  \param[in]  HLInfo    : pointer to Higher layer INF (NULL if none)
 *  \param[in]  HLInfoLen : Length HLInfo
 *  \param[in]  fwt       : Frame Waiting Time to be used (from SENSB_RES)
 *  \param[out] attribRes    : pointer to place the ATTRIB Response
 *  \param[out] attribResLen : pointer to place the ATTRIB Response length
 *
 *  \return ERR_WRONG_STATE  : RFAL not initialized or incorrect mode
 *  \return ERR_PARAM        : Invalid parameters
 *  \return ERR_IO           : Generic internal error
 *  \return ERR_TIMEOUT      : Timeout error
 *  \return ERR_CRC          : CRC error detected
 *  \return ERR_FRAMING      : Framing error detected
 *  \return ERR_PROTO        : Protocol error detected
 *  \return ERR_NONE         : No error, ATTRIB Response received
 *****************************************************************************
 */
ReturnCode rfalIsoDepATTRIB( uint8_t* nfcid0, uint8_t PARAM1, rfalBitRate DSI, rfalBitRate DRI, rfalIsoDepFSxI FSDI, uint8_t PARAM3, uint8_t DID, uint8_t* HLInfo, uint8_t HLInfoLen, uint32_t fwt, rfalIsoDepAttribRes *attribRes, uint8_t *attribResLen, SPI* mspiChannel, ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 );


/*!
 *****************************************************************************
 *  \brief  Deselects PICC
 *
 *  This function sends a deselect command to PICC and waits for it`s
 *  responce in a blocking way
 *
 *  \return ERR_NONE   : Deselect successfully sent and acknowledged by PICC
 *  \return ERR_TIMEOUT: No response rcvd from PICC
 *
 *****************************************************************************
 */
ReturnCode rfalIsoDepDeselect( SPI* mspiChannel, ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 );

/*!
 *****************************************************************************
 *  \brief  ISO-DEP Poller Handle NFC-A Activation
 *
 *  This performs a NFC-A Activation into ISO-DEP layer (ISO14443-4) with the given
 *  parameters. It sends RATS and if the higher bit rates are supported by
 *  both devices it additionally sends PPS
 *  Once Activated all details of the device are provided on isoDepDev
 *
 *  \param[in]  FSDI      : Frame Size Device Integer to be used
 *  \param[in]  DID       : Device ID to be used or RFAL_ISODEP_NO_DID for not use DID
 *  \param[in]  maxBR     : Max bit rate supported by the Poller
 *  \param[out] isoDepDev : ISO-DEP information of the activated Listen device
 *
 *  \return ERR_WRONG_STATE  : RFAL not initialized or incorrect mode
 *  \return ERR_PARAM        : Invalid parameters
 *  \return ERR_IO           : Generic internal error
 *  \return ERR_TIMEOUT      : Timeout error
 *  \return ERR_PAR          : Parity error detected
 *  \return ERR_CRC          : CRC error detected
 *  \return ERR_FRAMING      : Framing error detected
 *  \return ERR_PROTO        : Protocol error detected
 *  \return ERR_NONE         : No error, activation successful
 *****************************************************************************
 */
ReturnCode rfalIsoDepPollAHandleActivation( rfalIsoDepFSxI FSDI, uint8_t DID, rfalBitRate maxBR, rfalIsoDepDevice *isoDepDev, SPI* mspiChannel, ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 );


/*!
 *****************************************************************************
 *  \brief  ISO-DEP Poller Handle NFC-B Activation
 *
 *  This performs a NFC-B Activation into ISO-DEP layer (ISO14443-4) with the given
 *  parameters. It sends ATTRIB and calculates supported higher bit rates of both
 *  devices and performs activation.
 *  Once Activated all details of the device are provided on isoDepDev
 *
 *  \param[in]  FSDI         : Frame Size Device Integer to be used
 *  \param[in]  DID          : Device ID to be used or RFAL_ISODEP_NO_DID for not use DID
 *  \param[in]  maxBR        : Max bit rate supported by the Poller
 *  \param[in]  PARAM1       : ATTRIB PARAM1 byte (communication parameters)
 *  \param[in]  nfcbDev      : pointer to the NFC-B Device containing the SENSB_RES
 *  \param[in]  HLInfo       : pointer to Higher layer INF (NULL if none)
 *  \param[in]  HLInfoLen    : Length HLInfo
 *  \param[out] isoDepDev    : ISO-DEP information of the activated Listen device
 *
 *  \return ERR_WRONG_STATE  : RFAL not initialized or incorrect mode
 *  \return ERR_PARAM        : Invalid parameters
 *  \return ERR_IO           : Generic internal error
 *  \return ERR_TIMEOUT      : Timeout error
 *  \return ERR_PAR          : Parity error detected
 *  \return ERR_CRC          : CRC error detected
 *  \return ERR_FRAMING      : Framing error detected
 *  \return ERR_PROTO        : Protocol error detected
 *  \return ERR_NONE         : No error, activation successful
 *****************************************************************************
 */
ReturnCode rfalIsoDepPollBHandleActivation( rfalIsoDepFSxI FSDI, uint8_t DID, rfalBitRate maxBR, uint8_t PARAM1, rfalNfcbListenDevice *nfcbDev, uint8_t* HLInfo, uint8_t HLInfoLen, rfalIsoDepDevice *isoDepDev, SPI* mspiChannel, ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 );


#endif /* RFAL_ISODEP_H_ */

/**
  * @}
  *
  * @}
  *
  * @}
  */
