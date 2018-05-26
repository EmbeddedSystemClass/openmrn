/** \copyright
 * Copyright (c) 2016, Stuart W Baker
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are  permitted provided that the following conditions are met:
 * 
 *  - Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 *  - Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * \file CC32xxSPI.hxx
 * This file implements a SPI device driver layer specific to CC32xx.
 *
 * @author Stuart W. Baker
 * @date 30 May 2016
 */

#ifndef _FREERTOS_DRIVERS_TI_CC32XXSPI_HXX_
#define _FREERTOS_DRIVERS_TI_CC32XXSPI_HXX_

#ifndef gcc
#define gcc
#endif

#include <cstdint>

#include "SPI.hxx"

#include "driverlib/udma.h"

/** Specialization of Serial SPI driver for CC32xx devices.
 */
class CC32xxSPI : public SPI
{
public:
    /** Constructor.
     * @param name name of this device instance in the file system
     * @param base base address of this device
     * @param interrupt interrupt number of this device
     * @param cs_assert function pointer to a method that asserts chip select
     * @param cs_deassert function pointer to a method that deasserts chip
     *                    select
     * @param bus_lock the user must provide a shared mutex if the device
     *                 instance represents more than one chip select on the
     *                 same bus interface.
     * @param dma_threshold the threshold in bytes to use a DMA transaction,
     *                      0 = DMA always disabled
     * @param dma_channel_index_tx UDMA_CH7_GSPI_TX or UDMA_CH31_GSPI_TX
     * @param dma_channel_index_rx UDMA_CH6_GSPI_RX or UDMA_CH30_GSPI_RX
     */
    CC32xxSPI(const char *name, unsigned long base, uint32_t interrupt,
            ChipSelectMethod cs_assert, ChipSelectMethod cs_deassert,
            OSMutex *bus_lock = nullptr, size_t dma_threshold = 0,
            uint32_t dma_channel_index_tx = UDMA_CH7_GSPI_TX,
            uint32_t dma_channel_index_rx = UDMA_CH6_GSPI_RX);

    /** Destructor.
     */
    ~CC32xxSPI()
    {
    }

    /** @todo (Stuart Baker) this should be made private */
    /** handle an interrupt.
     */
    void interrupt_handler();

private:
    /** Maximum number of bytes transferred in a single DMA transaction */
    static constexpr size_t MAX_DMA_TRANSFER_AMOUNT = 1024;

    /**
     * This lookup table is used to configure the DMA channels for the appropriate
     * (8bit, 16bit or 32bit) transfer sizes.
     * Table for an SPI DMA RX channel.
     */
    static constexpr uint32_t dmaRxConfig_ =
        UDMA_SIZE_8  | UDMA_SRC_INC_NONE | UDMA_DST_INC_8  | UDMA_ARB_1;

    /**
     * This lookup table is used to configure the DMA channels for the appropriate
     * (8bit, 16bit or 32bit) transfer sizes.
     * Table for an SPI DMA TX channel
     */
    static constexpr uint32_t dmaTxConfig_ =
        UDMA_SIZE_8  | UDMA_SRC_INC_8  | UDMA_DST_INC_NONE | UDMA_ARB_1;

    /**
     * This lookup table is used to configure the DMA channels for the appropriate
     * (8bit, 16bit or 32bit) transfer sizes when either txBuf or rxBuf are NULL.
     */
    static constexpr uint32_t dmaNullConfig_ =
        UDMA_SIZE_8  | UDMA_SRC_INC_NONE | UDMA_DST_INC_NONE | UDMA_ARB_1;

    void enable() override {} /**< function to enable device */
    void disable() override {} /**< function to disable device */

    /** Method to transmit/receive the data.
     * @param msg message(s) to transact.
     * @return bytes transfered upon success, -errno upon failure
     */
    int transfer(struct spi_ioc_transfer *msg) override;

    /** Update the configuration of the bus.
     * @return >= 0 upon success, -errno upon failure
     */
    int update_configuration() override;

    unsigned long base; /**< base address of this device */
    unsigned long clock; /**< clock rate supplied to the module */
    unsigned long interrupt; /**< interrupt of this device */
    size_t dmaThreshold_; /**< threshold in bytes to start using DMA */
    uint32_t dmaChannelIndexTx_; /**< TX DMA channel index */
    uint32_t dmaChannelIndexRx_; /**< RX DMA channel index */
    size_t dmaTransferSize_; /**< current DMA transfer size in bytes */
    struct spi_ioc_transfer *msg_; /**< message for current transaction */
    bool stop_; /**< current transaction ends in a stop if true */
    int count_; /**< current count index within transaction */

    /** Semaphore to wakeup task level from ISR */
    OSSem sem_;

    /** Default constructor.
     */
    CC32xxSPI();

    DISALLOW_COPY_AND_ASSIGN(CC32xxSPI);
};

#endif /* _FREERTOS_DRIVERS_TI_CC32XXSPI_HXX_ */
