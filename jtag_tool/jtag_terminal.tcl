#============================================================
# 自动化JTAG终端脚本
# 自动连接调试器 -> 选核 -> 启动终端
# 用法: xsct jtag_terminal.tcl <zynq|zynqmp>
#============================================================

# 设置核过滤规则(ZYNQ7000-CortexA9)
set platform [lindex $argv 0]
if {$platform eq "zynq"} {
	set TARGET_FILTER {name =~ "*A9*#0"}
} elseif {$platform eq "zynqmp"} {
	set TARGET_FILTER {name =~ "*A53*#0"}
} else {
	puts "Usage: xsct jtag_terminal.tcl <zynq|zynqmp>"
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

# 记录当前tclsh85.exe的进程列表
set before_pids {}
catch {
	set before_list [split [exec tasklist /fi "imagename eq tclsh85t.exe" /fo csv /nh] "\n"]
	foreach line $before_list {
		if {[regexp {tclsh85t.exe","(\d+)"} $line -> pid]} {
			lappend before_pids $pid
		}
	}
}

# 启动JTAG终端
puts "\n启动JTAG终端 ..."
puts "提示: 保证应用程序正确加载"
puts "      Ctrl+C可结束终端\n"
after 1000
jtagterminal -start

# 等待进程启动
after 1000

# 获得新的tclsh85.exe的进程列表
set new_pid {}
catch {
	set after_list [split [exec tasklist /fi "imagename eq tclsh85t.exe" /fo csv /nh] "\n"]
	foreach line $after_list {
		if {[regexp {tclsh85t.exe","(\d+)"} $line -> pid]} {
			if {[lsearch -exact $before_pids $pid] == -1} {
				set new_pid $pid
				break
			}
		}
	}
}

# 检测PID是否存在
if {$new_pid ne ""} {
	puts "等待JATG终端关闭 (PID $new_pid) ..."
	while {1} {
		after 1000
		set result [catch {exec tasklist /fi "PID eq $new_pid" /nh} output]
		if {$result != 0 || [string match "*No tasks*" $output]} {
			break
		}
	}
	puts "JATG terminal closed ..."
} else {
	puts "Warning: could not detect JTAG terminal process"
}


# vwait forever
# 脚本将在此阻塞直到JTAG终端关闭
puts "JTAG终端关闭..."