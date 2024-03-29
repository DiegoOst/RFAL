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

/*! \file rfal_AnalogConfig.h
 *
 *  \author bkam
 *
 *  \brief RF Chip Analog Configuration Settings
 *
 *
 * @addtogroup RFAL
 * @{
 *
 * @addtogroup RFAL-HAL
 * @brief RFAL Hardware Abstraction Layer
 * @{
 *
 * @addtogroup AnalogConfig
 * @brief RFAL Analog Config Module
 * @{
 *
 */

#ifndef RFAL_ANALOG_CONFIG_H
#define RFAL_ANALOG_CONFIG_H

/*
 ******************************************************************************
 * INCLUDES
 ******************************************************************************
 */
#include "platform1.h"
#include "st_errno.h"
#include "ST25R3911.h"

/*
 ******************************************************************************
 * DEFINES
 ******************************************************************************
 */

#define RFAL_ANALOG_CONFIG_LUT_SIZE                 (87)    /*!< Maximum number of Configuration IDs in the Loop Up Table      */
#define RFAL_ANALOG_CONFIG_LUT_NOT_FOUND            (0xFF)  /*!< Index value indicating no Configuration IDs found             */

#define RFAL_ANALOG_CONFIG_TBL_SIZE                 (1024)  /*!< Maximum number of Register-Mask-Value in the Setting List     */


#define RFAL_ANALOG_CONFIG_POLL_LISTEN_MODE_MASK    (0x8000) /*!< Mask bit of Poll Mode in Analog Configuration ID             */
#define RFAL_ANALOG_CONFIG_TECH_MASK                (0x7F00) /*!< Mask bits for Technology in Analog Configuration ID          */
#define RFAL_ANALOG_CONFIG_BITRATE_MASK             (0x00F0) /*!< Mask bits for Bit rate in Analog Configuration ID            */
#define RFAL_ANALOG_CONFIG_DIRECTION_MASK           (0x0003) /*!< Mask bits for Direction in Analog Configuration ID           */

#define RFAL_ANALOG_CONFIG_POLL_LISTEN_MODE_SHIFT   (15)     /*!< Shift value of Poll Mode in Analog Configuration ID          */
#define RFAL_ANALOG_CONFIG_TECH_SHIFT               (8)      /*!< Shift value for Technology in Analog Configuration ID        */
#define RFAL_ANALOG_CONFIG_BITRATE_SHIFT            (4)      /*!< Shift value for Technology in Analog Configuration ID        */
#define RFAL_ANALOG_CONFIG_DIRECTION_SHIFT          (0)      /*!< Shift value for Direction in Analog Configuration ID         */

#define RFAL_ANALOG_CONFIG_POLL                     (0x0000) /*!< Poll Mode bit setting in Analog Configuration ID             */
#define RFAL_ANALOG_CONFIG_LISTEN                   (0x8000) /*!< Listen Mode bit setting in Analog Configuration ID           */

#define RFAL_ANALOG_CONFIG_TECH_CHIP                (0x0000) /*!< Chip-Specific bit setting in Analog Configuration ID         */
#define RFAL_ANALOG_CONFIG_TECH_NFCA                (0x0100) /*!< NFC-A Technology bits setting in Analog Configuration ID     */
#define RFAL_ANALOG_CONFIG_TECH_NFCB                (0x0200) /*!< NFC-B Technology bits setting in Analog Configuration ID     */
#define RFAL_ANALOG_CONFIG_TECH_NFCF                (0x0400) /*!< NFC-F Technology bits setting in Analog Configuration ID     */
#define RFAL_ANALOG_CONFIG_TECH_AP2P                (0x0800) /*!< AP2P Technology bits setting in Analog Configuration ID      */
#define RFAL_ANALOG_CONFIG_TECH_NFCV                (0x1000) /*!< NFC-V Technology bits setting in Analog Configuration ID     */
#define RFAL_ANALOG_CONFIG_TECH_RFU                 (0x2000) /*!< RFU for Technology bits */

#define RFAL_ANALOG_CONFIG_BITRATE_COMMON           (0x0000) /*!< Common settings for all bit rates in Analog Configuration ID */
#define RFAL_ANALOG_CONFIG_BITRATE_106              (0x0010) /*!< 106kbits/s settings in Analog Configuration ID               */
#define RFAL_ANALOG_CONFIG_BITRATE_212              (0x0020) /*!< 212kbits/s settings in Analog Configuration ID               */
#define RFAL_ANALOG_CONFIG_BITRATE_424              (0x0030) /*!< 424kbits/s settings in Analog Configuration ID               */
#define RFAL_ANALOG_CONFIG_BITRATE_848              (0x0040) /*!< 848kbits/s settings in Analog Configuration ID               */
#define RFAL_ANALOG_CONFIG_BITRATE_1695             (0x0050) /*!< 1695kbits/s settings in Analog Configuration ID              */
#define RFAL_ANALOG_CONFIG_BITRATE_3390             (0x0060) /*!< 3390kbits/s settings in Analog Configuration ID              */
#define RFAL_ANALOG_CONFIG_BITRATE_6780             (0x0070) /*!< 6780kbits/s settings in Analog Configuration ID              */
#define RFAL_ANALOG_CONFIG_BITRATE_1OF4             (0x00C0) /*!< 1 out of 4 for NFC-V setting in Analog Configuration ID      */
#define RFAL_ANALOG_CONFIG_BITRATE_1OF256           (0x00D0) /*!< 1 out of 256 for NFC-V setting in Analog Configuration ID    */

#define RFAL_ANALOG_CONFIG_NO_DIRECTION             (0x0000) /*!< No direction setting in Analog Conf ID (Chip Specific only)  */
#define RFAL_ANALOG_CONFIG_TX                       (0x0001) /*!< Transmission bit setting in Analog Configuration ID          */
#define RFAL_ANALOG_CONFIG_RX                       (0x0002) /*!< Reception bit setting in Analog Configuration ID             */

#define RFAL_ANALOG_CONFIG_UPDATE_LAST              (0x00)   /*!< Value indicating Last configuration set during update        */
#define RFAL_ANALOG_CONFIG_UPDATE_MORE              (0x01)   /*!< Value indicating More configuration set coming during update */

/*
 ******************************************************************************
 * GLOBAL MACROS
 ******************************************************************************
 */

#define RFAL_ANALOG_CONFIG_ID_GET_POLL_LISTEN(id)   (RFAL_ANALOG_CONFIG_POLL_LISTEN_MODE_MASK & id) /*!< Check if id indicates Listen mode   */

#define RFAL_ANALOG_CONFIG_ID_GET_TECH(id)          (RFAL_ANALOG_CONFIG_TECH_MASK & id)      /*!< Get the technology of Configuration ID     */
#define RFAL_ANALOG_CONFIG_ID_IS_CHIP(id)           (RFAL_ANALOG_CONFIG_TECH_MASK & id)      /*!< Check if ID indicates Chip-specific        */
#define RFAL_ANALOG_CONFIG_ID_IS_NFCA(id)           (RFAL_ANALOG_CONFIG_TECH_NFCA & id)      /*!< Check if ID indicates NFC-A                */
#define RFAL_ANALOG_CONFIG_ID_IS_NFCB(id)           (RFAL_ANALOG_CONFIG_TECH_NFCB & id)      /*!< Check if ID indicates NFC-B                */
#define RFAL_ANALOG_CONFIG_ID_IS_NFCF(id)           (RFAL_ANALOG_CONFIG_TECH_NFCF & id)      /*!< Check if ID indicates NFC-F                */
#define RFAL_ANALOG_CONFIG_ID_IS_AP2P(id)           (RFAL_ANALOG_CONFIG_TECH_AP2P & id)      /*!< Check if ID indicates AP2P                 */
#define RFAL_ANALOG_CONFIG_ID_IS_NFCV(id)           (RFAL_ANALOG_CONFIG_TECH_NFCV & id)      /*!< Check if ID indicates NFC-V                */

#define RFAL_ANALOG_CONFIG_ID_GET_BITRATE(id)       (RFAL_ANALOG_CONFIG_BITRATE_MASK & id)   /*!< Get Bitrate of Configuration ID            */
#define RFAL_ANALOG_CONFIG_ID_IS_COMMON(id)         (RFAL_ANALOG_CONFIG_BITRATE_MASK & id)   /*!< Check if ID indicates common bitrate       */
#define RFAL_ANALOG_CONFIG_ID_IS_106(id)            (RFAL_ANALOG_CONFIG_BITRATE_106 & id)    /*!< Check if ID indicates 106kbits/s           */
#define RFAL_ANALOG_CONFIG_ID_IS_212(id)            (RFAL_ANALOG_CONFIG_BITRATE_212 & id)    /*!< Check if ID indicates 212kbits/s           */
#define RFAL_ANALOG_CONFIG_ID_IS_424(id)            (RFAL_ANALOG_CONFIG_BITRATE_424 & id)    /*!< Check if ID indicates 424kbits/s           */
#define RFAL_ANALOG_CONFIG_ID_IS_848(id)            (RFAL_ANALOG_CONFIG_BITRATE_848 & id)    /*!< Check if ID indicates 848kbits/s           */
#define RFAL_ANALOG_CONFIG_ID_IS_1695(id)           (RFAL_ANALOG_CONFIG_BITRATE_1695 & id)   /*!< Check if ID indicates 1695kbits/s          */
#define RFAL_ANALOG_CONFIG_ID_IS_3390(id)           (RFAL_ANALOG_CONFIG_BITRATE_3390 & id)   /*!< Check if ID indicates 3390kbits/s          */
#define RFAL_ANALOG_CONFIG_ID_IS_6780(id)           (RFAL_ANALOG_CONFIG_BITRATE_6780 & id)   /*!< Check if ID indicates 6780kbits/s          */
#define RFAL_ANALOG_CONFIG_ID_IS_1OF4(id)           (RFAL_ANALOG_CONFIG_BITRATE_1OF4 & id)   /*!< Check if ID indicates 1 out of 4 bitrate   */
#define RFAL_ANALOG_CONFIG_ID_IS_1OF256(id)         (RFAL_ANALOG_CONFIG_BITRATE_1OF256 & id) /*!< Check if ID indicates 1 out of 256 bitrate */

#define RFAL_ANALOG_CONFIG_ID_GET_DIRECTION(id)     (RFAL_ANALOG_CONFIG_DIRECTION_MASK & id) /*!< Get Direction of Configuration ID          */
#define RFAL_ANALOG_CONFIG_ID_IS_TX(id)             (RFAL_ANALOG_CONFIG_TX & id)             /*!< Check if id indicates TX                   */
#define RFAL_ANALOG_CONFIG_ID_IS_RX(id)             (RFAL_ANALOG_CONFIG_RX & id)             /*!< Check if id indicates RX                   */

#define RFAL_ANALOG_CONFIG_CONFIG_NUM(x)            (sizeof(x)/sizeof(x[0]))                 /*!< Get Analog Config number                   */

/*! Set Analog Config ID value by: Mode, Technology, Bitrate and Direction      */
#define RFAL_ANALOG_CONFIG_ID_SET(mode, tech, br, direction)    \
    (  RFAL_ANALOG_CONFIG_ID_GET_POLL_LISTEN(mode) \
     | RFAL_ANALOG_CONFIG_ID_GET_TECH(tech) \
     | RFAL_ANALOG_CONFIG_ID_GET_BITRATE(br) \
     | RFAL_ANALOG_CONFIG_ID_GET_DIRECTION(direction) \
    )

/*
 ******************************************************************************
 * GLOBAL DATA TYPES
 ******************************************************************************
 */

typedef uint8_t  rfalAnalogConfigMode;       /*!< Polling or Listening Mode of Configuration                    */
typedef uint8_t  rfalAnalogConfigTech;       /*!< Technology of Configuration                                   */
typedef uint8_t  rfalAnalogConfigBitrate;    /*!< Bitrate of Configuration                                      */
typedef uint8_t  rfalAnalogConfigDirection;  /*!< Transmit/Receive direction of Configuration                   */

typedef uint8_t  rfalAnalogConfigRegAddr[2]; /*!< Register Address to ST Chip                                  */
typedef uint8_t  rfalAnalogConfigRegMask;    /*!< Register Mask Value                                           */
typedef uint8_t  rfalAnalogConfigRegVal;     /*!< Register Value                                                */

typedef uint16_t rfalAnalogConfigId;         /*!< Analog Configuration ID                                       */
typedef uint16_t rfalAnalogConfigOffset;     /*!< Analog Configuration offset address in the table              */
typedef uint8_t  rfalAnalogConfigNum;        /*!< Number of Analog settings for the respective Configuration ID */


/*! Struct that contain the Register-Mask-Value set. Make sure that the whole structure size is even and unaligned! */
typedef struct {
    rfalAnalogConfigRegAddr addr;  /*!< Register Address    */
    rfalAnalogConfigRegMask mask;  /*!< Register Mask Value */
    rfalAnalogConfigRegVal  val;   /*!< Register Value      */
} rfalAnalogConfigRegAddrMaskVal;


/*! Struct that represents the Analog Configs */
typedef struct {
    uint8_t                        id[sizeof(rfalAnalogConfigId)]; /*!< Configuration ID                   */
    rfalAnalogConfigNum            num;                            /*!< Number of Config Sets to follow    */
    rfalAnalogConfigRegAddrMaskVal regSet[];                       /*!< Register-Mask-Value sets           */
} rfalAnalogConfig;


/*
******************************************************************************
* GLOBAL FUNCTION PROTOTYPES
******************************************************************************
*/

/*!
 *****************************************************************************
 * \brief Initialize the Analog Configuration
 *
 * Reset the Analog Configuration LUT pointer to reference to default settings.
 *
 *****************************************************************************
 */
void rfalAnalogConfigInitialize( void );


/*!
 *****************************************************************************
 * \brief Indicate if the current Analog Configuration Table is complete and ready to be used.
 *
 * \return true if current Analog Configuration Table is complete and ready to be used.
 * \return false if current Analog Configuration Table is incomplete
 *
 *****************************************************************************
 */
bool rfalAnalogConfigIsReady( void );

/*!
 *****************************************************************************
 * \brief  Write the whole Analog Configuration table in raw format
 *
 * Writes the Analog Configuration and Look Up Table with the given raw table
 *
 * NOTE: Function does not check the validity of the given Table contents
 *
 * \param[in]  configTbl:     location of config Table to be loaded
 * \param[in]  configTblSize: size of the config Table to be loaded
 *
 * \return ERR_NONE    : if setting is updated
 * \return ERR_PARAM   : if configTbl is invalid
 * \return ERR_NOMEM   : if the given Table is bigger exceeds the max size
 * \return ERR_REQUEST : if the update Configuration Id is disabled
 *
 *****************************************************************************
 */
ReturnCode rfalAnalogConfigListWriteRaw( const uint8_t *configTbl, uint16_t configTblSize );

/*!
 *****************************************************************************
 * \brief  Write the Analog Configuration table with new analog settings.
 *
 * Writes the Analog Configuration and Look Up Table with the new list of register-mask-value
 * and Configuration ID respectively.
 *
 * NOTE: Function does not check for the validity of the Register Address.
 *
 * \param[in]  more: 0x00 indicates it is last Configuration ID settings;
 *                   0x01 indicates more Configuration ID setting(s) are coming.
 * \param[in]  *config: reference to the configuration list of current Configuraiton ID.
 *
 * \return ERR_PARAM   : if Configuration ID or parameter is invalid
 * \return ERR_NOMEM   : if LUT is full
 * \return ERR_REQUEST : if the update Configuration Id is disabled
 * \return ERR_NONE    : if setting is updated
 *
 *****************************************************************************
 */
ReturnCode rfalAnalogConfigListWrite( uint8_t more, rfalAnalogConfig *config );

/*!
 *****************************************************************************
 * \brief  Read the whole Analog Configuration table in raw format
 *
 * Reads the whole Analog Configuration Table in raw format
 *
 * \param[out]   tblBuf: location to the buffer to place the Config Table
 * \param[in]    tblBufLen: length of the buffer to place the Config Table
 * \param[out]   configTblSize: Config Table size
 *
 * \return ERR_PARAM : if configTbl or configTblSize is invalid
 * \return ERR_NOMEM : if configTblSize is not enough for the whole table
 * \return ERR_NONE  : if read is successful
 *
 *****************************************************************************
 */
ReturnCode rfalAnalogConfigListReadRaw( uint8_t *tblBuf, uint16_t tblBufLen, uint16_t *configTblSize );

/*!
 *****************************************************************************
 * \brief  Read the Analog Configuration table.
 *
 * Read the Analog Configuration Table
 *
 * \param[in]     configOffset: offset to the next Configuration ID in the List Table to be read.
 * \param[out]    more: 0x00 indicates it is last Configuration ID settings;
 *                      0x01 indicates more Configuration ID setting(s) are coming.
 * \param[out]    config: configuration id, number of configuration sets and register-mask-value sets
 * \param[in]     numConfig: the remaining configuration settings space available;
 *
 * \return ERR_NOMEM : if number of Configuration for respective Configuration ID is greater the the remaining configuration setting space available
 * \return ERR_NONE  : if read is successful
 *
 *****************************************************************************
 */
ReturnCode rfalAnalogConfigListRead( rfalAnalogConfigOffset *configOffset, uint8_t *more, rfalAnalogConfig *config, rfalAnalogConfigNum numConfig );

/*!
 *****************************************************************************
 * \brief  Set the Analog settings of indicated Configuration ID.
 *
 * Update the chip with indicated analog settings of indicated Configuration ID.
 *
 * \param[in]  configId: configuration ID
 *
 * \return ERR_PARAM if Configuration ID is invalid
 * \return ERR_INTERNAL if error updating setting to chip
 * \return ERR_NONE if new settings is applied to chip
 *
 *****************************************************************************
 */
ReturnCode rfalSetAnalogConfig( rfalAnalogConfigId configId, SPI* mspiChannel, ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 );


#endif /* RFAL_ANALOG_CONFIG_H */

/**
  * @}
  *
  * @}
  *
  * @}
  */
