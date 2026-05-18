# 
# Usage: To re-create this platform project launch xsct with below options.
# xsct D:\RTOS_Study\threadx\vitis\zynq7010\platform.tcl
# 
# OR launch xsct and run below command.
# source D:\RTOS_Study\threadx\vitis\zynq7010\platform.tcl
# 
# To create the platform in a different location, modify the -out option of "platform create" command.
# -out option specifies the output directory of the platform project.

platform create -name {zynq7010}\
-hw {D:\RTOS_Study\threadx\vivado\zynq7010.xsa}\
-out {D:/RTOS_Study/threadx/vitis}

platform write
domain create -name {standalone_ps7_cortexa9_0} -display-name {standalone_ps7_cortexa9_0} -os {standalone} -proc {ps7_cortexa9_0} -runtime {cpp} -arch {32-bit} -support-app {hello_world}
platform generate -domains 
platform active {zynq7010}
domain active {zynq_fsbl}
domain active {standalone_ps7_cortexa9_0}
platform generate -quick
platform generate
platform generate -domains standalone_ps7_cortexa9_0,zynq_fsbl 
platform clean
platform generate
platform clean
platform generate
platform active {zynq7010}
bsp reload
platform generate -domains 
platform generate -domains 
platform generate
platform clean
platform generate
platform active {zynq7010}
bsp reload
bsp reload
platform generate -domains 
