编译kernel后自动生成以下代码，注意是代码，不是文件，文件kernel本身是
有的。
arch/arm/kernel/vmlinux.lds
  __initcall_start = .;
   *(.initcallearly.init) __early_initcall_end = .; *(.initcall0.init) *(.initcall0s.init) *(.initcall1.init) *(.initcall1s.init) *(.initcall2.init) *(.initcall2s.init) *(.initcall3.init) *(.initcall3s.init) *(.initcall4.init) *(.initcall4s.init) *(.initcall5.init) *(.initcall5s.init) *(.initcallrootfs.init) *(.initcall6.init) *(.initcall6s.init) *(.initcall7.init) *(.initcall7s.init)

  __initcall_end = .;
  __con_initcall_start = .;
   *(.con_initcall.init)
  __con_initcall_end = .;

对应的函数在以下文件中定义
#define __define_initcall(level,fn,id) \
	static initcall_t __initcall_##fn##id __used \
	__attribute__((__section__(".initcall" level ".init"))) = fn

这里可以看出console_initcall（fn）中fn放入.con_initcall.init section中
#define console_initcall(fn) \
	static initcall_t __initcall_##fn \
	__used __section(.con_initcall.init) = fn

串口驱动的入口函数
./arch/arm/configs/tq2440_defconfig:756:CONFIG_SERIAL_SAMSUNG_CONSOLE=y

1.入口函数console_initcall

drivers/serial/samsung.h
#ifdef CONFIG_SERIAL_SAMSUNG_CONSOLE

#define s3c24xx_console_init(__drv, __inf)			\
static int __init s3c_serial_console_init(void)			\
{								\
	return s3c24xx_serial_initconsole(__drv, __inf);	\
}								\
								\
console_initcall(s3c_serial_console_init)

#else
#define s3c24xx_console_init(drv, inf) extern void no_console(void)
#endif

可以看出，只有定义了CONFIG_SERIAL_SAMSUNG_CONSOLE，才能有串口的初始化

drivers/serial/s3c2440.c
s3c24xx_console_init(&s3c2440_serial_drv, &s3c2440_uart_inf);

drivers/serial/samsung.c
int s3c24xx_serial_initconsole(struct platform_driver *drv,
			       struct s3c24xx_uart_info *info)

{
	1.将s3c24xx_uart_devs赋给platform_device
	struct platform_device *dev = s3c24xx_uart_devs[0];

	dbg("s3c24xx_serial_initconsole\n");

	/* select driver based on the cpu */

	if (dev == NULL) {
		printk(KERN_ERR "s3c24xx: no devices for console init\n");
		return 0;
	}

	2.检查dev->name和driver的name是否移植
	if (strcmp(dev->name, drv->driver.name) != 0)
		return 0;

	s3c24xx_serial_console.data = &s3c24xx_uart_drv;
	s3c24xx_serial_init_ports(info);

	register_console(&s3c24xx_serial_console);
	return 0;
}
