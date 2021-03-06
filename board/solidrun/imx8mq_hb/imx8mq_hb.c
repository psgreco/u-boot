// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2018 NXP
 */

#include <common.h>
#include <malloc.h>
#include <errno.h>
#include <asm/io.h>
#include <miiphy.h>
#include <netdev.h>
#include <asm/mach-imx/iomux-v3.h>
#include <asm-generic/gpio.h>
#include <fsl_esdhc.h>
#include <mmc.h>
#include <asm/arch/imx8mq_pins.h>
#include <asm/arch/sys_proto.h>
#include <asm/mach-imx/gpio.h>
#include <asm/mach-imx/mxc_i2c.h>
#include <asm/arch/clock.h>
#include <spl.h>
#include <power/pmic.h>
#include <power/pfuze100_pmic.h>
#include <usb.h>
#include <dwc3-uboot.h>
#include <linux/usb/dwc3.h>
#include <dm.h>
#include "../../freescale/common/pfuze.h"
#include "../../freescale/common/tcpc.h"

DECLARE_GLOBAL_DATA_PTR;

#define QSPI_PAD_CTRL   (PAD_CTL_DSE2 | PAD_CTL_HYS)

#define UART_PAD_CTRL	(PAD_CTL_DSE6 | PAD_CTL_FSEL1)

#define WDOG_PAD_CTRL	(PAD_CTL_DSE6 | PAD_CTL_HYS | PAD_CTL_PUE)

static iomux_v3_cfg_t const wdog_pads[] = {
	IMX8MQ_PAD_GPIO1_IO02__WDOG1_WDOG_B | MUX_PAD_CTRL(WDOG_PAD_CTRL),
};

static iomux_v3_cfg_t const uart_pads[] = {
	IMX8MQ_PAD_UART1_RXD__UART1_RX | MUX_PAD_CTRL(UART_PAD_CTRL),
	IMX8MQ_PAD_UART1_TXD__UART1_TX | MUX_PAD_CTRL(UART_PAD_CTRL),
};

#ifdef CONFIG_FSL_QSPI
static iomux_v3_cfg_t const qspi_pads[] = {
	IMX8MQ_PAD_NAND_ALE__QSPI_A_SCLK | MUX_PAD_CTRL(QSPI_PAD_CTRL),
	IMX8MQ_PAD_NAND_CE0_B__QSPI_A_SS0_B | MUX_PAD_CTRL(QSPI_PAD_CTRL),

	IMX8MQ_PAD_NAND_DATA00__QSPI_A_DATA0 | MUX_PAD_CTRL(QSPI_PAD_CTRL),
	IMX8MQ_PAD_NAND_DATA01__QSPI_A_DATA1 | MUX_PAD_CTRL(QSPI_PAD_CTRL),
	IMX8MQ_PAD_NAND_DATA02__QSPI_A_DATA2 | MUX_PAD_CTRL(QSPI_PAD_CTRL),
	IMX8MQ_PAD_NAND_DATA03__QSPI_A_DATA3 | MUX_PAD_CTRL(QSPI_PAD_CTRL),
};
#endif

#ifdef CONFIG_FSL_QSPI
int board_qspi_init(void)
{
	imx_iomux_v3_setup_multiple_pads(qspi_pads, ARRAY_SIZE(qspi_pads));

	set_clk_qspi();

	return 0;
}
#endif

int board_early_init_f(void)
{
	struct wdog_regs *wdog = (struct wdog_regs *)WDOG1_BASE_ADDR;

	imx_iomux_v3_setup_multiple_pads(wdog_pads, ARRAY_SIZE(wdog_pads));
	set_wdog_reset(wdog);

	imx_iomux_v3_setup_multiple_pads(uart_pads, ARRAY_SIZE(uart_pads));

	return 0;
}

#define MEM_STRIDE 0x4000000
static u64 get_ram_size_stride_test(u64 *base, u64 maxsize)
{
	volatile u64 *addr;
	u64	  save[64];
	u64	  cnt;
	u64	  size;
	int	  i = 0;

	/* First save the data */
	for (cnt = 0; cnt < maxsize; cnt += MEM_STRIDE) {
		addr = (volatile u64 *)((u64)base + cnt);       /* pointer arith! */
		sync ();
		save[i++] = *addr;
		sync ();
	}

	/* First write a signature */
	* (volatile u64 *)base = 0x12345678;
	for (size = MEM_STRIDE; size < maxsize; size += MEM_STRIDE) {
		* (volatile u64 *)((u64)base + size) = size;
		sync ();
		if (* (volatile u64 *)((u64)base) == size) {    /* We reached the overlapping address */
			break;
		}
	}

	/* Restore the data */
	for (cnt = (maxsize - MEM_STRIDE); i > 0; cnt -= MEM_STRIDE) {
		addr = (volatile u64 *)((u64)base + cnt);       /* pointer arith! */
		sync ();
		*addr = save[i--];
		sync ();
	}

	return (size);
}

int dram_init(void)
{
	u64 ram_size = get_ram_size_stride_test((u64 *) CONFIG_SYS_SDRAM_BASE,
						(u64)PHYS_SDRAM_SIZE);
	/* rom_pointer[1] contains the size of TEE occupies */
	if (rom_pointer[1])
		gd->ram_size = ram_size - rom_pointer[1];
	else
		gd->ram_size = ram_size;

	return 0;
}

#ifdef CONFIG_FEC_MXC
static int setup_fec(void)
{
	struct iomuxc_gpr_base_regs *gpr =
		(struct iomuxc_gpr_base_regs *)IOMUXC_GPR_BASE_ADDR;

	/* Use 125M anatop REF_CLK1 for ENET1, not from external */
	clrsetbits_le32(&gpr->gpr[1], BIT(13) | BIT(17), 0);
	return set_clk_enet(ENET_125MHZ);
}

int board_phy_config(struct phy_device *phydev)
{
	/* enable rgmii rxc skew and phy mode select to RGMII copper */
	phy_write(phydev, MDIO_DEVAD_NONE, 0x1d, 0x1f);
	phy_write(phydev, MDIO_DEVAD_NONE, 0x1e, 0x8);

	phy_write(phydev, MDIO_DEVAD_NONE, 0x1d, 0x05);
	phy_write(phydev, MDIO_DEVAD_NONE, 0x1e, 0x100);

	if (phydev->drv->config)
		phydev->drv->config(phydev);
	return 0;
}
#endif

#ifdef CONFIG_USB_TCPC
struct tcpc_port port;
struct tcpc_port_config port_config = {
        .i2c_bus = 0,
        .addr = 0x50,
        .port_type = TYPEC_PORT_UFP,
        .max_snk_mv = 20000,
        .max_snk_ma = 3000,
        .max_snk_mw = 15000,
        .op_snk_mv = 9000,
};

#define USB_TYPEC_SEL IMX_GPIO_NR(3, 15)

static iomux_v3_cfg_t ss_mux_gpio[] = {
        IMX8MQ_PAD_NAND_RE_B__GPIO3_IO15 | MUX_PAD_CTRL(NO_PAD_CTRL),
};

void ss_mux_select(enum typec_cc_polarity pol)
{
        if (pol == TYPEC_POLARITY_CC1)
                gpio_direction_output(USB_TYPEC_SEL, 1);
        else
                gpio_direction_output(USB_TYPEC_SEL, 0);
}

static int setup_typec(void)
{
        int ret;

        imx_iomux_v3_setup_multiple_pads(ss_mux_gpio, ARRAY_SIZE(ss_mux_gpio));
        gpio_request(USB_TYPEC_SEL, "typec_sel");

        ret = tcpc_init(&port, port_config, &ss_mux_select);
        if (ret) {
                printf("%s: tcpc init failed, err=%d\n",
                       __func__, ret);
        }

        return ret;
}
#endif


#if defined(CONFIG_USB)
int board_usb_of_init(struct udevice *dev)
{
        int ret, index, init = 0;
	enum usb_dr_mode dr_mode = USB_DR_MODE_HOST;
	struct udevice *dwc3 = NULL;

	index = imx8m_get_usb_index(dev);

	ret = device_find_first_child(dev, &dwc3);
        if (ret)
                return ret;

	if (dwc3) {
		dr_mode = usb_get_dr_mode(dev_of_offset(dwc3));
	}

	if (dr_mode > USB_DR_MODE_HOST)
		init = USB_INIT_DEVICE;
	else
		init = USB_INIT_HOST;

	printf("board_usb_of_init index: %d, init = %d\n", index, init);

	init_usb_clk(index);
        imx8m_usb_power(index, true);

        if (index == 0 && init == USB_INIT_DEVICE) {
#ifdef CONFIG_USB_TCPC
                ret = tcpc_setup_ufp_mode(&port);
#endif
        } else if (index == 0 && init == USB_INIT_HOST) {
#ifdef CONFIG_USB_TCPC
                ret = tcpc_setup_dfp_mode(&port);
#endif
                return ret;
        }

        return 0;
}

void board_usb_of_cleanup(struct udevice *dev)
{
        int ret, index, init = 0;
	enum usb_dr_mode dr_mode = USB_DR_MODE_HOST;
	struct udevice *dwc3 = NULL;

	index = imx8m_get_usb_index(dev);

	ret = device_find_first_child(dev, &dwc3);
        if (ret)
                return;

	if (dwc3) {
		dr_mode = usb_get_dr_mode(dev_of_offset(dwc3));
	}

	if (dr_mode > USB_DR_MODE_HOST)
		init = USB_INIT_DEVICE;
	else
		init = USB_INIT_HOST;

	printf("board_usb_of_init index: %d, init = %d\n", index, init);

        if (index == 0 && init == USB_INIT_HOST) {
#ifdef CONFIG_USB_TCPC
                ret = tcpc_disable_src_vbus(&port);
#endif
        }

        imx8m_usb_power(index, false);

        return;
}
#endif

int board_init(void)
{
#ifdef CONFIG_FEC_MXC
	setup_fec();
#endif

#ifdef CONFIG_USB_TCPC
        setup_typec();
#endif

	return 0;
}

int board_mmc_get_env_dev(int devno)
{
	return devno;
}

#ifdef CONFIG_OF_BOARD_SETUP
int ft_board_setup(void *blob, bd_t *bd)
{
	return 0;
}
#endif

int board_late_init(void)
{
#ifdef CONFIG_ENV_VARS_UBOOT_RUNTIME_CONFIG
	env_set("board_name", "HummingBoard Pulse");
	env_set("board_rev", "iMX8MQ");
#endif

	return 0;
}

void board_preboot_os(void)
{
        init_usb_clk(0);
        init_usb_clk(1);
}
