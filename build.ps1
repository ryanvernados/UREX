param([string]$ToolchainBin = '')
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
if (-not $ToolchainBin) {
    $compiler = Get-Command arm-none-eabi-gcc.exe -ErrorAction SilentlyContinue
    if ($compiler) { $ToolchainBin = Split-Path $compiler.Source }
    else {
        $compiler = Get-ChildItem C:/ST -Filter arm-none-eabi-gcc.exe -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($compiler) { $ToolchainBin = $compiler.DirectoryName }
    }
}
if (-not $ToolchainBin) { throw 'Pass -ToolchainBin with the ARM GCC bin directory.' }
$gcc = Join-Path $ToolchainBin 'arm-none-eabi-gcc.exe'
New-Item -ItemType Directory -Path build -Force | Out-Null
# Windows PowerShell can turn redirected compiler warnings into errors under
# Stop. Native-tool failures below are checked by exit code instead.
$ErrorActionPreference = 'Continue'
$flags = @('-mcpu=cortex-m4','-mthumb','-mfpu=fpv4-sp-d16','-mfloat-abi=hard','-DSTM32F407xx','-DUSE_HAL_DRIVER','-ICore/Inc','-IDrivers/STM32F4xx_HAL_Driver/Inc','-IDrivers/CMSIS/Device/ST/STM32F4xx/Include','-IDrivers/CMSIS/Include','-ffunction-sections','-fdata-sections','-Og','-g3','-Wall','-Wextra')
$sources = @(Get-ChildItem Core/Src -Filter *.c | ForEach-Object FullName)
$hal = @('stm32f4xx_hal','stm32f4xx_hal_can','stm32f4xx_hal_cortex','stm32f4xx_hal_dma','stm32f4xx_hal_flash','stm32f4xx_hal_flash_ex','stm32f4xx_hal_gpio','stm32f4xx_hal_pwr','stm32f4xx_hal_pwr_ex','stm32f4xx_hal_rcc','stm32f4xx_hal_rcc_ex')
$sources += $hal | ForEach-Object { "Drivers/STM32F4xx_HAL_Driver/Src/$_.c" }
$sources += 'Core/Startup/startup_stm32f407ighx.s'
$objects = @()
foreach ($source in $sources) {
    $object = 'build/' + [IO.Path]::GetFileNameWithoutExtension($source) + '.o'
    & $gcc @flags -c $source -o $object
    if ($LASTEXITCODE -ne 0) { throw "Compile failed: $source" }
    $objects += $object
}
& $gcc @flags @objects '-TSTM32F407IGHX_FLASH.ld' '--specs=nano.specs' '--specs=nosys.specs' '-Wl,--gc-sections,-Map=build/motor_test.map' -o build/motor_test.elf
if ($LASTEXITCODE -ne 0) { throw 'Link failed' }
& (Join-Path $ToolchainBin 'arm-none-eabi-objcopy.exe') -O binary build/motor_test.elf build/motor_test.bin
if ($LASTEXITCODE -ne 0) { throw 'Binary conversion failed' }
& (Join-Path $ToolchainBin 'arm-none-eabi-size.exe') build/motor_test.elf
if ($LASTEXITCODE -ne 0) { throw 'Size check failed' }
