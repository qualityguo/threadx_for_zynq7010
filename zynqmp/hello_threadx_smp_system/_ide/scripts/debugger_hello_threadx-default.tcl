# Usage with Vitis IDE:
# In Vitis IDE create a Single Application Debug launch configuration,
# change the debug type to 'Attach to running target' and provide this 
# tcl script in 'Execute Script' option.
# Path of this script: D:\RTOS_Study\threadx\vitis\zynqmp\hello_threadx_smp_system\_ide\scripts\debugger_hello_threadx-default.tcl
# 
# 
# Usage with xsct:
# To debug using xsct, launch xsct and run below command
# source D:\RTOS_Study\threadx\vitis\zynqmp\hello_threadx_smp_system\_ide\scripts\debugger_hello_threadx-default.tcl
# 
connect -url tcp:127.0.0.1:3121
source D:/Xilinx2021/Vitis/2021.1/scripts/vitis/util/zynqmp_utils.tcl
targets -set -nocase -filter {name =~"APU*"}
rst -system
after 3000
targets -set -filter {jtag_cable_name =~ "MiLianKe MLK.JTAG1U0 2517871F41F" && level==0 && jtag_device_ctx=="jsn-MLK.JTAG1U0-2517871F41F-14730093-0"}
fpga -file D:/RTOS_Study/threadx/vitis/zynqmp/hello_threadx_smp/_ide/bitstream/system_wrapper.bit
targets -set -nocase -filter {name =~"APU*"}
loadhw -hw D:/RTOS_Study/threadx/vitis/zynqmp/zynq7eg/export/zynq7eg/hw/system_wrapper.xsa -mem-ranges [list {0x80000000 0xbfffffff} {0x400000000 0x5ffffffff} {0x1000000000 0x7fffffffff}] -regs
configparams force-mem-access 1
targets -set -nocase -filter {name =~"APU*"}
set mode [expr [mrd -value 0xFF5E0200] & 0xf]
targets -set -nocase -filter {name =~ "*A53*#0"}
rst -processor
dow D:/RTOS_Study/threadx/vitis/zynqmp/zynq7eg/export/zynq7eg/sw/zynq7eg/boot/fsbl.elf
set bp_15_54_fsbl_bp [bpadd -addr &XFsbl_Exit]
con -block -timeout 60
bpremove $bp_15_54_fsbl_bp
targets -set -nocase -filter {name =~ "*A53*#0"}
rst -processor
dow D:/RTOS_Study/threadx/vitis/zynqmp/hello_threadx_smp/Debug/hello_threadx_smp.elf
configparams force-mem-access 0
bpadd -addr &main
