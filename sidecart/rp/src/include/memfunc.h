/**
 * @file memfunc.h
 * @brief Header file with the macros and functions of the memory functions.
 * Author: Diego Parrilla Santamaría
 * Date: August 2024-2025, February 2026
 * Copyright: 2024-2026 - GOODDATA LABS SL
 * Description: Header file with the macros and functions of the memory
 * functions.
 */

#ifndef MEMFUNC_H
#define MEMFUNC_H

#include "constants.h"
#include "debug.h"
#include "hardware/dma.h"
#include "hardware/structs/xip_ctrl.h"

#define COPY_FIRMWARE_TO_RAM(emulROM, emulROM_length)  \
  do {                                                 \
    COPY_FIRMWARE_TO_RAM_DMA(emulROM, emulROM_length); \
  } while (0)

#define ERASE_FIRMWARE_IN_RAM()                                \
  do {                                                         \
    memset((void *)&__rom_in_ram_start__, 0,                   \
           ROM_SIZE_LONGWORDS * ROM_BANKS * sizeof(uint32_t)); \
    DPRINTF("RAM for the firmware zeroed.\n");                 \
  } while (0)

#define COPY_FIRMWARE_TO_RAM_MEMCPY(emulROM, emulROM_length) \
  do {                                                       \
    memcpy(&__rom_in_ram_start__, emulROM, emulROM_length);  \
    DPRINTF("Emulation firmware copied to RAM.\n");          \
  } while (0)

#define COPY_FIRMWARE_TO_RAM_DMA(emulROM, emulROM_length)                     \
  do {                                                                        \
    while (!(xip_ctrl_hw->stat & XIP_STAT_FIFO_EMPTY))                        \
      (void)xip_ctrl_hw->stream_fifo;                                         \
    int dma_chan = dma_claim_unused_channel(false);                            \
    if (dma_chan < 0) {                                                       \
      DPRINTF("No DMA channel available for firmware copy. Using memcpy.\n"); \
      size_t __copy_words = (size_t)(emulROM_length);                         \
      uint16_t *__dst = (uint16_t *)&__rom_in_ram_start__;                    \
      const uint16_t *__src = (const uint16_t *)(emulROM);                    \
      for (size_t __i = 0; __i < __copy_words; ++__i) {                       \
        __dst[__i] = __src[__i];                                              \
      }                                                                        \
      break;                                                                   \
    }                                                                         \
    xip_ctrl_hw->stream_addr = (uint32_t)&(emulROM)[0];                       \
    xip_ctrl_hw->stream_ctr = (emulROM_length) / 2;                           \
    dma_channel_config cfg = dma_channel_get_default_config(dma_chan);        \
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_32);                 \
    channel_config_set_read_increment(&cfg, false);                           \
    channel_config_set_write_increment(&cfg, true);                           \
    channel_config_set_dreq(&cfg, DREQ_XIP_STREAM);                           \
    dma_channel_configure(dma_chan, &cfg,                                     \
                          (void *)&__rom_in_ram_start__, /* Write addr */     \
                          (const void *)XIP_AUX_BASE,    /* Read addr */      \
                          (emulROM_length) / 2,          /* Transfer count */ \
                          true /* Start immediately! */                       \
    );                                                                        \
    dma_channel_wait_for_finish_blocking(dma_chan);                           \
    dma_channel_unclaim((uint)dma_chan);                                      \
  } while (0)

#define CHANGE_ENDIANESS_BLOCK16(dest_ptr_word, size_in_bytes) \
  do {                                                         \
    uint16_t *word_ptr = (uint16_t *)(dest_ptr_word);          \
    for (uint16_t j = 0; j < (size_in_bytes) / 2; ++j) {       \
      word_ptr[j] = (word_ptr[j] << 8) | (word_ptr[j] >> 8);   \
    }                                                          \
  } while (0)

#define COPY_AND_CHANGE_ENDIANESS_BLOCK16(src_ptr_word, dest_ptr_word,    \
                                          size_in_bytes)                  \
  do {                                                                    \
    uint16_t *src_word_ptr = (uint16_t *)(src_ptr_word);                  \
    uint16_t *dest_word_ptr = (uint16_t *)(dest_ptr_word);                \
    for (uint16_t j = 0; j < (size_in_bytes) / 2; ++j) {                  \
      dest_word_ptr[j] = (src_word_ptr[j] << 8) | (src_word_ptr[j] >> 8); \
    }                                                                     \
  } while (0)

#define SWAP_WORD(data) \
  ((((uint16_t)data << 8) & 0xFF00) | (((uint16_t)data >> 8) & 0xFF))

#define SWAP_LONGWORD(data) \
  ((((uint32_t)data << 16) & 0xFFFF0000) | (((uint32_t)data >> 16) & 0xFFFF))

#define WRITE_AND_SWAP_LONGWORD(address, offset, data) \
  *((volatile uint32_t *)((address) + (offset))) =     \
      ((((uint32_t)(data) << 16) & 0xFFFF0000) |       \
       (((uint32_t)(data) >> 16) & 0xFFFF))

#define WRITE_LONGWORD_RAW(address, offset, data) \
  *((volatile uint32_t *)((address) + (offset))) = data

#define WRITE_WORD(address, offset, data) \
  *((volatile uint16_t *)((address) + (offset))) = data

#define MEMSET16BIT(memory_address, offset, size, value)              \
  do {                                                                \
    for (int i = 0; i < size; i++) {                                  \
      *((volatile uint16_t *)((memory_address) + (offset) + i * 2)) = \
          (uint16_t)(value);                                          \
    }                                                                 \
  } while (0)

#define READ_WORD(address, offset) \
  (*((volatile uint16_t *)((address) + (offset))))

#define READ_LONGWORD(address, offset) \
  (*((volatile uint32_t *)((address) + (offset))))

static inline uint32_t memfunc_readAndSwapLongword(
    const volatile uint32_t *address) {
  uint32_t raw = *address;
  return (((raw << 16) & 0xFFFF0000u) | ((raw >> 16) & 0x0000FFFFu));
}

#define READ_AND_SWAP_LONGWORD(address, offset) \
  memfunc_readAndSwapLongword((const volatile uint32_t *)((address) + (offset)))

#define COPY_AND_SWAP_16BIT_DMA(dest, source, num_bytes)           \
  do {                                                             \
    size_t _rounded_bytes = ((num_bytes) + 1) & ~1;                \
    size_t _num_words = _rounded_bytes / 2;                        \
    int _dma_channel = dma_claim_unused_channel(true);             \
    dma_channel_config _dma_cfg =                                  \
        dma_channel_get_default_config(_dma_channel);              \
    channel_config_set_transfer_data_size(&_dma_cfg, DMA_SIZE_16); \
    channel_config_set_read_increment(&_dma_cfg, true);            \
    channel_config_set_write_increment(&_dma_cfg, true);           \
    channel_config_set_bswap(&_dma_cfg, true);                     \
    dma_channel_configure(                                         \
        _dma_channel, /* Channel */                                \
        &_dma_cfg,    /* Channel config */                         \
        (dest),       /* Destination address */                    \
        (source),     /* Source address */                         \
        (_num_words), /* Number of 16-bit words to transfer */     \
        false         /* Don't start yet */                        \
    );                                                             \
    dma_channel_start(_dma_channel);                               \
    dma_channel_wait_for_finish_blocking(_dma_channel);            \
    dma_channel_unclaim(_dma_channel);                             \
  } while (0)

/**
 * @brief Macro to set a shared variable.
 *
 * This macro sets a shared variable at the specified index to the given value.
 *
 * @param p_shared_variable_index The index of the shared variable.
 * @param p_shared_variable_value The value to set for the shared variable.
 * @param memory_shared_address The base address of the shared memory.
 * @param memory_shared_variables_offset The offset of the shared variables
 * within the shared memory.
 */
#define SET_SHARED_VAR(p_shared_variable_index, p_shared_variable_value,      \
                       memory_shared_address, memory_shared_variables_offset) \
  do {                                                                        \
    DPRINTF("Setting shared variable %d to %x\n", p_shared_variable_index,    \
            p_shared_variable_value);                                         \
    *((volatile uint16_t *)(memory_shared_address +                           \
                            memory_shared_variables_offset +                  \
                            (p_shared_variable_index * 4) + 2)) =             \
        p_shared_variable_value & 0xFFFF;                                     \
    *((volatile uint16_t *)(memory_shared_address +                           \
                            memory_shared_variables_offset +                  \
                            (p_shared_variable_index * 4))) =                 \
        p_shared_variable_value >> 16;                                        \
  } while (0)

/**
 * @brief Macro to get a shared variable.
 *
 * This macro retrieves the 32-bit value of a shared variable at the specified
 * index from the shared memory and assigns it to the provided destination
 * variable.
 *
 * @param p_shared_variable_index The index of the shared variable.
 * @param p_shared_variable_result The variable to store the retrieved value.
 * @param memory_shared_address The base address of the shared memory.
 * @param memory_shared_variables_offset The offset of the shared variables
 * within the shared memory.
 */
#define GET_SHARED_VAR(p_shared_variable_index, p_shared_variable_result,     \
                       memory_shared_address, memory_shared_variables_offset) \
  do {                                                                        \
    uint16_t high = *((volatile uint16_t *)(memory_shared_address +           \
                                            memory_shared_variables_offset +  \
                                            (p_shared_variable_index * 4)));  \
    uint16_t low =                                                            \
        *((volatile uint16_t *)(memory_shared_address +                       \
                                memory_shared_variables_offset +              \
                                (p_shared_variable_index * 4) + 2));          \
    *p_shared_variable_result = ((uint32_t)high << 16) | low;                 \
    DPRINTF("Getting shared variable %d = %x\n", p_shared_variable_index,     \
            p_shared_variable_result);                                        \
  } while (0)

/**
 * @brief Macro to set a private shared variable.
 *
 * This macro sets a private shared variable at the specified index to the given
 * value.
 *
 * @param p_shared_variable_index The index of the private shared variable.
 * @param p_shared_variable_value The value to set for the private shared
 * variable.
 * @param memory_shared_address The base address of the shared memory.
 * @param memory_shared_variables_offset The offset of the shared variables
 * within the shared memory.
 */
#define SET_SHARED_PRIVATE_VAR(p_shared_variable_index,                        \
                               p_shared_variable_value, memory_shared_address, \
                               memory_shared_variables_offset)                 \
  do {                                                                         \
    DPRINTF("Setting private shared variable %d to %d\n",                      \
            p_shared_variable_index, p_shared_variable_value);                 \
    DPRINTF("Memory address: %x\n",                                            \
            (memory_shared_address + memory_shared_variables_offset +          \
             (p_shared_variable_index * 4)));                                  \
    *((volatile uint16_t *)(memory_shared_address +                            \
                            memory_shared_variables_offset +                   \
                            (p_shared_variable_index * 4) + 2)) =              \
        p_shared_variable_value & 0xFFFF;                                      \
    *((volatile uint16_t *)(memory_shared_address +                            \
                            memory_shared_variables_offset +                   \
                            (p_shared_variable_index * 4))) =                  \
        p_shared_variable_value >> 16;                                         \
  } while (0)

/**
 * @brief Macro to set a bit of a private shared variable.
 *
 * This macro sets a specific bit of a private shared variable at the specified
 * index.
 *
 * @param p_shared_variable_index The index of the private shared variable.
 * @param bit_position The position of the bit to set.
 * @param memory_shared_address The base address of the shared memory.
 * @param memory_shared_variables_offset The offset of the shared variables
 * within the shared memory.
 */
#define SET_SHARED_PRIVATE_VAR_BIT(p_shared_variable_index, bit_position,    \
                                   memory_shared_address,                    \
                                   memory_shared_variables_offset)           \
  do {                                                                       \
    DPRINTF("Setting bit %d of private shared variable %d\n", bit_position,  \
            p_shared_variable_index);                                        \
    uint32_t addr = memory_shared_address + memory_shared_variables_offset + \
                    (p_shared_variable_index * 4);                           \
    uint32_t value = *((volatile uint16_t *)(addr + 2)) |                    \
                     (*((volatile uint16_t *)(addr)) << 16);                 \
    value |= (1 << bit_position);                                            \
    DPRINTF("Memory address: %x, New Value: %x\n", addr, value);             \
    *((volatile uint16_t *)(addr + 2)) = value & 0xFFFF;                     \
    *((volatile uint16_t *)(addr)) = value >> 16;                            \
  } while (0)

/**
 * @brief Macro to clear a bit of a private shared variable.
 *
 * This macro clears a specific bit of a private shared variable at the
 * specified index.
 *
 * @param p_shared_variable_index The index of the private shared variable.
 * @param bit_position The position of the bit to clear.
 * @param memory_shared_address The base address of the shared memory.
 * @param memory_shared_variables_offset The offset of the shared variables
 * within the shared memory.
 */
#define CLEAR_SHARED_PRIVATE_VAR_BIT(p_shared_variable_index, bit_position,  \
                                     memory_shared_address,                  \
                                     memory_shared_variables_offset)         \
  do {                                                                       \
    DPRINTF("Clearing bit %d of private shared variable %d\n", bit_position, \
            p_shared_variable_index);                                        \
    uint32_t addr = memory_shared_address + memory_shared_variables_offset + \
                    (p_shared_variable_index * 4);                           \
    uint32_t value = *((volatile uint16_t *)(addr + 2)) |                    \
                     (*((volatile uint16_t *)(addr)) << 16);                 \
    value &= ~(1 << bit_position);                                           \
    DPRINTF("Memory address: %x, New Value: %x\n", addr, value);             \
    *((volatile uint16_t *)(addr + 2)) = value & 0xFFFF;                     \
    *((volatile uint16_t *)(addr)) = value >> 16;                            \
  } while (0)

/**
 * @brief Macro to check if a flag is set.
 *
 * This macro checks if the specified flag is set in the 'flags' variable.
 *
 * @param flag The flag to check.
 * @return 1 if the flag is set, 0 otherwise.
 */
#define IS_FLAG_SET(flag) (flags & flag)

/**
 * @brief Set a flag in the flags variable.
 *
 * This macro sets a flag in the `flags` variable by performing a bitwise OR
 * operation.
 *
 * @param flag The flag to be set.
 */
#define SET_FLAG(flag) (flags |= flag)

/**
 * @brief Clear a specific flag in the given flags variable.
 *
 * This macro clears the specified flag in the given flags variable by
 * performing a bitwise AND operation with the complement of the flag. The
 * result is then stored back in the flags variable.
 *
 * @param flag The flag to be cleared.
 */
#define CLEAR_FLAG(flag) (flags &= ~flag)

#endif  // MEMFUNC_H
