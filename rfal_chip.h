
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

/*! \file rfal_chip.h
 *
 *  \author Gustavo Patricio
 *
 *  \brief RF Chip specific Layer
 *
 *  \warning This layer, which provides direct access to RF chip, should
 *           only be used for debug purposes and/or advanced features
 *
 *
 * @addtogroup RFAL
 * @{
 *
 * @addtogroup RFAL-HAL
 * @brief RFAL Hardware Abstraction Layer
 * @{
 *
 * @addtogroup Chip
 * @brief RFAL RF Chip Module
 * @{
 *
 */


#ifndef RFAL_CHIP_H
#define RFAL_CHIP_H

/*
******************************************************************************
* INCLUDES
******************************************************************************
*/
#include "platform1.h"
#include "st_errno.h"
#include "rfal_rf.h"


/*****************************************************************************
 *  RF Chip                                                                  *
 *****************************************************************************/

/*!
 *****************************************************************************
 * \brief Writes a register on the RF Chip
 *
 * Checks if the given register is valid and if so, writes the value(s)
 * on the RF Chip register
 *
 * \param[in] reg: register address to be written, or the first if len > 1
 * \param[in] values: pointer with content to be written on the register(s)
 * \param[in] len: number of consecutive registers to be written
 *
 *
 * \return ERR_PARAM Invalid register or bad request
 * \return ERR_NONE  Write done with no error
 *****************************************************************************
 */
ReturnCode rfalChipWriteReg( uint16_t reg, uint8_t* values, uint8_t len, SPI * mspiChannel, ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 );

/*!
 *****************************************************************************
 * \brief Reads a register on the RF Chip
 *
 * Checks if the given register is valid and if so, reads the value(s)
 * of the RF Chip register(s)
 *
 * \param[in]  reg: register address to be read, or the first if len > 1
 * \param[out] values: pointer where the register(s) read content will be placed
 * \param[in]  len: number of consecutive registers to be read
 *
 * \return ERR_PARAM Invalid register or bad request
 * \return ERR_NONE  Read done with no error
 *****************************************************************************
 */
ReturnCode rfalChipReadReg( uint16_t reg, uint8_t* values, uint8_t len, SPI * mspiChannel, ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 );

/*!
 *****************************************************************************
 * \brief Change a register on the RF Chip
 *
 * Change the value of the register bits on the RF Chip Test set in the valueMask.
 *
 * \param[in] reg: register address to be modified
 * \param[in] valueMask: mask value of the register bits to be changed
 * \param[in] value: register value to be set
 *
 * \return ERR_PARAM Invalid register or bad request
 * \return ERR_OK    Change done with no error
 *****************************************************************************
 */
ReturnCode rfalChipChangeRegBits( uint16_t reg, uint8_t valueMask, uint8_t value, SPI* mspiChannel, ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 );

/*!
 *****************************************************************************
 * \brief Writes a Test register on the RF Chip
 *
 * Writes the value on the RF Chip Test register
 *
 * \param[in] reg: register address to be written
 * \param[in] value: value to be written on the register
 *
 *
 * \return ERR_PARAM Invalid register or bad request
 * \return ERR_NONE  Write done with no error
 *****************************************************************************
 */
ReturnCode rfalChipWriteTestReg( uint16_t reg, uint8_t value, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 );

/*!
 *****************************************************************************
 * \brief Reads a Test register on the RF Chip
 *
 * Reads the value of the RF Chip Test register
 *
 * \param[in]  reg: register address to be read
 * \param[out] value: pointer where the register content will be placed
 *
 * \return ERR_PARAM Invalid register or bad request
 * \return ERR_NONE  Read done with no error
 *****************************************************************************
 */
ReturnCode rfalChipReadTestReg( uint16_t reg, uint8_t* value );

/*!
 *****************************************************************************
 * \brief Change a Test register on the RF Chip
 *
 * Change the value of the register bits on the RF Chip Test set in the valueMask.
 *
 * \param[in] reg: test register address to be modified
 * \param[in] valueMask: mask value of the register bits to be changed
 * \param[in] value: register value to be set
 *
 * \return ERR_PARAM Invalid register or bad request
 * \return ERR_OK    Change done with no error
 *****************************************************************************
 */
ReturnCode rfalChipChangeTestRegBits( uint16_t reg, uint8_t valueMask, uint8_t value, SPI* mspiChannel, ST25R3911* mST25,
		DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 );

/*!
 *****************************************************************************
 * \brief Execute command on the RF Chip
 *
 * Checks if the given command is valid and if so, executes it on
 * the RF Chip
 *
 * \param[in] cmd: direct command to be executed
 *
 * \return ERR_PARAM Invalid command or bad request
 * \return ERR_NONE  Direct command executed with no error
 *****************************************************************************
 */
ReturnCode rfalChipExecCmd( uint16_t cmd, SPI * mspiChannel, ST25R3911* mST25, DigitalOut* gpio_cs, InterruptIn* IRQ, DigitalOut* fieldLED_01, DigitalOut* fieldLED_02, DigitalOut* fieldLED_03, DigitalOut* fieldLED_04, DigitalOut* fieldLED_05, DigitalOut* fieldLED_06 );


#endif /* RFAL_CHIP_H */

/**
  * @}
  *
  * @}
  *
  * @}
  */
