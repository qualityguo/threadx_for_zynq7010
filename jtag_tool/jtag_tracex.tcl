#============================================================
# 自动化JTAG-TraceX工具
# 自动连接调试器 -> 选核 -> 抓取数据 -> 导出分析
# 用法: xsct jtag_tracex.tcl <zynq|zynqmp>
#============================================================

# 设置核过滤规则(ZYNQ7000-CortexA9)
set platform [lindex $argv 0]
if {$platform eq "zynq"} {
	set TARGET_FILTER {name =~ "*A9*#0"}
} elseif {$platform eq "zynqmp"} {
	set TARGET_FILTER {name =~ "*A53*#0"}
} else {
	puts "Usage: xsct jtag_tracex.tcl <zynq|zynqmp>"
	exit 1
}

#-----------------用户配置-----------------
# 如果需要远程调试, 请设置IP地址和端口
set HOST "127.0.0.1"
set PORT "3121"

# 连接到hw_server
puts "正在连接到硬件服务器 $HOST:$PORT ..."
if {[catch {connect -url TCP:$HOST:$PORT} result]} {
    puts "错误:连接hw_server失败 - $result"
    puts "请检查连接"
    exit 1
}
puts "连接成功 ..."

# 列出所有可用目标
puts "\n可用目标列表:"
targets

# 自动选择目标核
puts "\n正在选择目标: $TARGET_FILTER"
if {[catch {targets -set -filter $TARGET_FILTER} result]} {
    puts "错误:连接目标核失败 - $result"
    puts "请检查连接"
    exit 1
}
puts "目标核已被选中 ..."

# 获得g_trace_buffer的信息
set print_result [print -dict -defs g_trace_buffer]
# 取出地址和大小的信息
set props [lindex $print_result 1]
# 取出addr
set addr [dict get $props addr]
# 取出size
set size_bytes [dict get $props size]
# 计算字大小
set word_cnt [expr {$size_bytes / 4}]

# 获得当前脚本的目录
set script_dir [file dirname [info script]]
# 获得当前时间
set timestamp [clock format [clock seconds] -format "%Y%m%d_%H%M%S"]
# 设置输出文件名
set output_file [file join $script_dir "trace_data_${platform}_${timestamp}.trx"]

puts "正在保存数据到: $output_file"

# 保存二进制文件
mrd -bin -file $output_file $addr $word_cnt

puts "数据保存完毕, 大小:$size_bytes 字节"

# vwait forever
# 脚本将在此阻塞直到JTAG终端关闭
puts "JTAG-TraceX关闭..."