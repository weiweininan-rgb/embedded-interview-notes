# 外部模块构建：Kbuild 进入第一分支，直接 make 进入第二分支。
ifneq ($(KERNELRELEASE),)
obj-m += sparam_sim.o
obj-m += rf_switch.o
ccflags-y += -I$(src)/../include
else
KDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

.PHONY: all clean
all:
	# 委托目标内核已配置好的 Kbuild 树完成编译。
	$(MAKE) -C $(KDIR) M=$(PWD) modules
clean:
	# 仅删除本模块产生的构建产物。
	$(MAKE) -C $(KDIR) M=$(PWD) clean
endif
