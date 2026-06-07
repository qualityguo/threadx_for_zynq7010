# 
# Usage: To re-create this platform project launch xsct with below options.
# xsct D:\RTOS_Study\threadx\vitis\zynqmp\zynq7eg\platform.tcl
# 
# OR launch xsct and run below command.
# source D:\RTOS_Study\threadx\vitis\zynqmp\zynq7eg\platform.tcl
# 
# To create the platform in a different location, modify the -out option of "platform create" command.
# -out option specifies the output directory of the platform project.

platform create -name {zynq7eg}\
-hw {D:\RTOS_Study\threadx\vivado\system_wrapper.xsa}\
-proc {psu_cortexa53_0} -os {standalone} -arch {64-bit} -no-boot-bsp -fsbl-target {psu_cortexa53_0} -out {D:/RTOS_Study/threadx/vitis/zynqmp}

platform write
platform generate -domains 
platform active {zynq7eg}
platform generate
platform clean
platform generate
