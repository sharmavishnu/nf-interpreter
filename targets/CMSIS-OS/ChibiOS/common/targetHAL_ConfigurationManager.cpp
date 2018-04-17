//
// Copyright (c) 2018 The nanoFramework project contributors
// See LICENSE file in the project root for full license information.
//

#include <nanoHAL.h>
#include <nanoHAL_v2.h>
#include <nanoWeak.h>
#include <Target_BlockStorage_STM32FlashDriver.h>

// initialization of configuration manager
// provided as weak so it can be replaced at target level, if required because of the target implementing the storage with a mechanism other then saving to flash
__nfweak void ConfigurationManager_Initialize()
{
    // enumerate the blocks
    ConfigurationManager_EnumerateConfigurationBlocks();
};

// Enumerates the configuration blocks from the configuration flash sector 
// it's implemented with 'weak' attribute so it can be replaced at target level if a different persistance mechanism is used
__nfweak void ConfigurationManager_EnumerateConfigurationBlocks()
{
    // find network configuration blocks
    HAL_CONFIGURATION_NETWORK* networkConfigs = ConfigurationManager_FindNetworkConfigurationBlocks((uint32_t)&__nanoConfig_start__, (uint32_t)&__nanoConfig_end__);

    // find wireless 80211 network configuration blocks
    HAL_CONFIGURATION_NETWORK_WIRELESS80211* networkWirelessConfigs = ConfigurationManager_FindNetworkWireless80211ConfigurationBlocks((uint32_t)&__nanoConfig_start__, (uint32_t)&__nanoConfig_end__);

    // alloc memory for g_TargetConfiguration
    // because this is a struct of structs that use flexible members the memory has to be allocated from the heap
    // the malloc size for each struct is computed separately 
    uint32_t sizeOfNetworkInterfaceConfigs = offsetof(HAL_CONFIGURATION_NETWORK, Configs) + networkConfigs->Count * sizeof(networkConfigs->Configs[0]);
    uint32_t sizeOfNetworkWireless80211InterfaceConfigs = offsetof(HAL_CONFIGURATION_NETWORK_WIRELESS80211, Configs) + networkWirelessConfigs->Count * sizeof(networkWirelessConfigs->Configs[0]);

    g_TargetConfiguration.NetworkInterfaceConfigs = (HAL_CONFIGURATION_NETWORK*)platform_malloc(sizeOfNetworkInterfaceConfigs);
    g_TargetConfiguration.NetworkWireless80211InterfaceConfigs = (HAL_CONFIGURATION_NETWORK_WIRELESS80211*)platform_malloc(sizeOfNetworkWireless80211InterfaceConfigs);

    // copy structs to g_TargetConfiguration
    memcpy((HAL_CONFIGURATION_NETWORK*)g_TargetConfiguration.NetworkInterfaceConfigs, networkConfigs, sizeOfNetworkInterfaceConfigs);
    memcpy((HAL_CONFIGURATION_NETWORK_WIRELESS80211*)g_TargetConfiguration.NetworkWireless80211InterfaceConfigs, networkWirelessConfigs, sizeOfNetworkWireless80211InterfaceConfigs);

    // // now free the memory of the original structs
    platform_free(networkConfigs);
    platform_free(networkWirelessConfigs);
}

// Gets the network configuration block from the configuration flash sector 
// it's implemented with 'weak' attribute so it can be replaced at target level if a different persistance mechanism is used
__nfweak bool ConfigurationManager_GetConfigurationBlock(void* configurationBlock, DeviceConfigurationOption configuration, uint32_t configurationIndex)
{
    int sizeOfBlock = 0;
    uint8_t* blockAddress;

    // validate if the requested block exists
    // Count has to be non zero
    // requested Index has to exist (array index starts at zero, so need to add one)
    if(configuration == DeviceConfigurationOption_Network)
    {
        if(g_TargetConfiguration.NetworkInterfaceConfigs->Count == 0 ||
            (configurationIndex + 1) > g_TargetConfiguration.NetworkInterfaceConfigs->Count)
        {
            return FALSE;
        }

        // set block size
        sizeOfBlock = sizeof(HAL_Configuration_NetworkInterface);

        // get block address
        blockAddress = (uint8_t*)g_TargetConfiguration.NetworkInterfaceConfigs->Configs[configurationIndex];
    }
    else if(configuration == DeviceConfigurationOption_Wireless80211Network)
    {
        if(g_TargetConfiguration.NetworkWireless80211InterfaceConfigs->Count == 0 ||
            (configurationIndex + 1) > g_TargetConfiguration.NetworkWireless80211InterfaceConfigs->Count)
        {
            return FALSE;
        }

        // set block size
        sizeOfBlock = sizeof(HAL_Configuration_Wireless80211NetworkInterface);

        // get block address
        blockAddress = (uint8_t*)g_TargetConfiguration.NetworkWireless80211InterfaceConfigs->Configs[configurationIndex];
    }

    // copy the config block content to the pointer in the argument
    memcpy(configurationBlock, blockAddress, sizeOfBlock);

    return TRUE;
}

// Stores the configuration block to the configuration flash sector 
// it's implemented with 'weak' attribute so it can be replaced at target level if a different persistance mechanism is used
__nfweak bool ConfigurationManager_StoreConfigurationBlock(void* configurationBlock, DeviceConfigurationOption configuration, uint32_t configurationIndex, uint32_t blockSize)
{
    ByteAddress storageAddress;
    bool requiresEnumeration = FALSE;
    bool success = FALSE;

    if(configuration == DeviceConfigurationOption_Network)
    {
        if(g_TargetConfiguration.NetworkInterfaceConfigs->Count == 0 ||
            (configurationIndex + 1) > g_TargetConfiguration.NetworkInterfaceConfigs->Count)
        {
            // this is a block that wasn't here before, so we need to enumerate the blocks again after storing it
            requiresEnumeration = TRUE;
        }

        // set storage address from block address
        storageAddress = (ByteAddress)g_TargetConfiguration.NetworkInterfaceConfigs->Configs[configurationIndex];

        // set block size, in case it's not already set
        blockSize = sizeof(HAL_Configuration_NetworkInterface);
    }
    else if(configuration == DeviceConfigurationOption_Wireless80211Network)
    {
        if(g_TargetConfiguration.NetworkWireless80211InterfaceConfigs->Count == 0 ||
            (configurationIndex + 1) > g_TargetConfiguration.NetworkWireless80211InterfaceConfigs->Count)
        {
            // this is a block that wasn't here before, so we need to enumerate the blocks again after storing it
            requiresEnumeration = TRUE;
        }

        // set storage address from block address
        storageAddress = (ByteAddress)g_TargetConfiguration.NetworkWireless80211InterfaceConfigs->Configs[configurationIndex];

        // set block size, in case it's not already set
        blockSize = sizeof(HAL_Configuration_Wireless80211NetworkInterface);
    }
    else if(configuration == DeviceConfigurationOption_All)
    {
        // particular situation where we are receiving the full configuration block

        // set storage address as the start of the flash configuration sector
        storageAddress = (ByteAddress)&__nanoConfig_start__;

        // always enumerate the blocks again after storing it
        requiresEnumeration = TRUE;

        // for save all the block size has to be provided, check it 
        if(blockSize == 0)
        {
            return FALSE;
        }
    }

    // copy the config block content to the config block storage
    success = STM32FlashDriver_Write(NULL, storageAddress, blockSize, (unsigned char*)configurationBlock, true);

    if(success == TRUE && requiresEnumeration)
    {
        // free the current allocation(s)
        platform_free(g_TargetConfiguration.NetworkInterfaceConfigs);
        platform_free(g_TargetConfiguration.NetworkWireless80211InterfaceConfigs);

        // perform enumeration of configuration blocks
        ConfigurationManager_EnumerateConfigurationBlocks();
    }

    return success;
}

// Updates a configuration block in the configuration flash sector
// The flash sector has to be erased before writing the updated block
// it's implemented with 'weak' attribute so it can be replaced at target level if a different persistance mechanism is used
__nfweak bool ConfigurationManager_UpdateConfigurationBlock(void* configurationBlock, DeviceConfigurationOption configuration, uint32_t configurationIndex)
{
    ByteAddress storageAddress;
    uint32_t blockOffset;
    uint8_t* blockAddressInCopy;
    uint32_t blockSize;
    bool success = FALSE;

    // config sector size
    int sizeOfConfigSector = (uint32_t)&__nanoConfig_end__ - (uint32_t)&__nanoConfig_start__;

    // allocate memory from CRT heap
    uint8_t* configSectorCopy = (uint8_t*)platform_malloc(sizeOfConfigSector);

    if(configSectorCopy != NULL)
    {
        // copy config sector from flash to RAM
        memcpy(configSectorCopy, &__nanoConfig_start__, sizeOfConfigSector);

        // find out the address for the config block to update in the configSectorCopy
        // because we are copying back the config block to flash and just replacing the config block content
        // the addresses in g_TargetConfiguration will remain the same
        // plus we can calculate the offset of the config block from g_TargetConfiguration
        if(configuration == DeviceConfigurationOption_Network)
        {
            // get storage address from block address
            storageAddress = (ByteAddress)g_TargetConfiguration.NetworkInterfaceConfigs->Configs[configurationIndex];

            // set block size, in case it's not already set
            blockSize = sizeof(HAL_Configuration_NetworkInterface);
        }
        else if(configuration == DeviceConfigurationOption_Wireless80211Network)
        {
            // storage address from block address
            storageAddress = (ByteAddress)g_TargetConfiguration.NetworkWireless80211InterfaceConfigs->Configs[configurationIndex];

            // set block size, in case it's not already set
            blockSize = sizeof(HAL_Configuration_Wireless80211NetworkInterface);
        }
        else
        {
            // this not a valid configuration option to update, quit
            // free memory first
            platform_free(configSectorCopy);

            return FALSE;
        }
    
        // erase config sector
        if(STM32FlashDriver_EraseBlock(NULL, (uint32_t)&__nanoConfig_start__) == TRUE)
        {
            // flash block is erased

            // subtract the start address of config sector to get the offset
            blockOffset = storageAddress - (uint32_t)&__nanoConfig_start__;

            // set pointer to block to udpate
            blockAddressInCopy = configSectorCopy + blockOffset;
            
            // replace config block with new content by replacing memory
            memcpy(blockAddressInCopy, configSectorCopy, blockSize);

            // copy the config block copy back to the config block storage
            success = STM32FlashDriver_Write(NULL, (uint32_t)&__nanoConfig_start__, sizeOfConfigSector, (unsigned char*)configSectorCopy, true);
        }

        // free memory
        platform_free(configSectorCopy);
    }

    return success;
}