px4_add_module(
	MODULE modules__vimana
	MAIN vimana
	STACK_MAIN 2048
	SRCS
		vimana_main.cpp
	DEPENDS
		px4_work_queue
)
