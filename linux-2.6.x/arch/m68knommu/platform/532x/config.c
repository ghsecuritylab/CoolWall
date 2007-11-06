/***************************************************************************/

/*
 *	linux/arch/m68knommu/platform/532x/config.c
 *
 *	Copyright (C) 1999-2002, Greg Ungerer (gerg@snapgear.com)
 *	Copyright (C) 2000, Lineo (www.lineo.com)
 *	Yaroslav Vinogradov yaroslav.vinogradov@freescale.com
 *	Copyright Freescale Semiconductor, Inc 2006
 *	Copyright (c) 2006, emlix, Sebastian Hess <sh@emlix.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/***************************************************************************/

#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <asm/machdep.h>
#include <asm/io.h>
#include <asm/coldfire.h>
#include <asm/mcfsim.h>
#include <asm/mcfuart.h>
#include <asm/mcfdma.h>
#include <asm/mcfwdebug.h>

/***************************************************************************/

void coldfire_reset(void);

extern unsigned int mcf_timervector;
extern unsigned int mcf_profilevector;
extern unsigned int mcf_timerlevel;

/***************************************************************************/

int sys_clk_khz = 0;
int sys_clk_mhz = 0;

void wtm_init(void);
void scm_init(void);
void gpio_init(void);
void fbcs_init(void);
void sdramc_init(void);
int  clock_pll (int fsys, int flags);
int  clock_limp (int);
int  clock_exit_limp (void);
int  get_sys_clock (void);

/***************************************************************************/

static struct mcf_platform_uart m532x_uart_platform[] = {
	{
		.mapbase	= MCF_MBAR + MCFUART_BASE1,
		.irq		= MCFINT_VECBASE + MCFINT_UART0,
	},
	{
		.mapbase 	= MCF_MBAR + MCFUART_BASE2,
		.irq		= MCFINT_VECBASE + MCFINT_UART1,
	},
	{
		.mapbase 	= MCF_MBAR + MCFUART_BASE3,
		.irq		= MCFINT_VECBASE + MCFINT_UART2,
	},
	{ },
};

static struct platform_device m532x_uart = {
	.name			= "mcfuart",
	.id			= 0,
	.dev.platform_data	= m532x_uart_platform,
};

static struct platform_device *m532x_devices[] __initdata = {
	&m532x_uart,
};

/***************************************************************************/

static void __init m532x_uart_init_line(int line, int irq)
{
	if (line == 0) {
		MCF_INTC0_ICR26 = 0x3;
		MCF_INTC0_CIMR = 26;
		/* GPIO initialization */
		MCF_GPIO_PAR_UART |= 0x000F;
	} else if (line == 1) {
		MCF_INTC0_ICR27 = 0x3;
		MCF_INTC0_CIMR = 27;
		/* GPIO initialization */
		MCF_GPIO_PAR_UART |= 0x0FF0;
	} else if (line == 2) {
		MCF_INTC0_ICR28 = 0x3;
		MCF_INTC0_CIMR = 28;
	}
}

static void __init m532x_uarts_init(void)
{
	const int nrlines = ARRAY_SIZE(m532x_uart_platform);
	int line;

	for (line = 0; (line < nrlines); line++)
		m532x_uart_init_line(line, m532x_uart_platform[line].irq);
}

/***************************************************************************/

void mcf_settimericr(unsigned int timer, unsigned int level)
{
	volatile unsigned char *icrp;
	unsigned int icr;
	unsigned char irq;

	if (timer <= 2) {
		switch (timer) {
		case 2:  irq = 33; icr = MCFSIM_ICR_TIMER2; break;
		default: irq = 32; icr = MCFSIM_ICR_TIMER1; break;
		}
		
		icrp = (volatile unsigned char *) (MCF_MBAR + icr);
		*icrp = level;
		mcf_enable_irq0(irq);
	}
}

/***************************************************************************/

int mcf_timerirqpending(int timer)
{
	unsigned int imr = 0;

	switch (timer) {
	case 1:  imr = 0x1; break;
	case 2:  imr = 0x2; break;
	default: break;
	}
	return (mcf_getiprh() & imr);
}

/***************************************************************************/

void __init config_BSP(char *commandp, int size)
{
	sys_clk_khz = get_sys_clock();
	sys_clk_mhz = sys_clk_khz/1000;

	mcf_setimr(MCFSIM_IMR_MASKALL);

#if !defined(CONFIG_BOOTPARAM)
	/* Copy command line from FLASH to local buffer... */
	memcpy(commandp, (char *) 0x4000, 4);
	if(strncmp(commandp, "kcl ", 4) == 0){
		memcpy(commandp, (char *) 0x4004, size);
		commandp[size-1] = 0;
	} else {
		memset(commandp, 0, size);
	}
#endif

	mcf_timervector = 64+32;
	mcf_profilevector = 64+33;
	mach_reset = coldfire_reset;

#ifdef MCF_BDM_DISABLE
	/*
	 * Disable the BDM clocking.  This also turns off most of the rest of
	 * the BDM device.  This is good for EMC reasons. This option is not
	 * incompatible with the memory protection option.
	 */
	wdebug(MCFDEBUG_CSR, MCFDEBUG_CSR_PSTCLK);
#endif
}

/***************************************************************************/

static int __init init_BSP(void)
{
	m532x_uarts_init();
	platform_add_devices(m532x_devices, ARRAY_SIZE(m532x_devices));
	return 0;
}

arch_initcall(init_BSP);

/***************************************************************************/
/* Board initialization */

/********************************************************************/
/* 
 * PLL min/max specifications
 */
#define MAX_FVCO	500000	/* KHz */
#define MAX_FSYS	80000 	/* KHz */
#define MIN_FSYS	58333 	/* KHz */
#define FREF		16000   /* KHz */


#define MAX_MFD		135     /* Multiplier */
#define MIN_MFD		88      /* Multiplier */
#define BUSDIV		6       /* Divider */

/*
 * Low Power Divider specifications
 */
#define MIN_LPD		(1 << 0)    /* Divider (not encoded) */
#define MAX_LPD		(1 << 15)   /* Divider (not encoded) */
#define DEFAULT_LPD	(1 << 1)	/* Divider (not encoded) */

#define SYS_CLK_KHZ	80000
#define SYSTEM_PERIOD	12.5
/*
 *  SDRAM Timing Parameters
 */  
#define SDRAM_BL	8	/* # of beats in a burst */
#define SDRAM_TWR	2	/* in clocks */
#define SDRAM_CASL	2.5	/* CASL in clocks */
#define SDRAM_TRCD	2	/* in clocks */
#define SDRAM_TRP	2	/* in clocks */
#define SDRAM_TRFC	7	/* in clocks */
#define SDRAM_TREFI	7800	/* in ns */

#define EXT_SRAM_ADDRESS	(0xC0000000)
#define FLASH_ADDRESS		(0x00000000)
#define SDRAM_ADDRESS		(0x40000000)

#define NAND_FLASH_ADDRESS	(0xD0000000)


asmlinkage void __init sysinit(void)
{
	wtm_init();
	scm_init();
	gpio_init();
	fbcs_init();
	sdramc_init();
}

void wtm_init(void)
{
	/* Disable watchdog timer */
	MCF_WTM_WCR = 0;
}

#define MCF_SCM_BCR_GBW		(0x00000100)
#define MCF_SCM_BCR_GBR		(0x00000200)

void scm_init(void)
{
	/* All masters are trusted */
	MCF_SCM_MPR = 0x77777777;
    
	/* Allow supervisor/user, read/write, and trusted/untrusted
	   access to all slaves */
	MCF_SCM_PACRA = 0;
	MCF_SCM_PACRB = 0;
	MCF_SCM_PACRC = 0;
	MCF_SCM_PACRD = 0;
	MCF_SCM_PACRE = 0;
	MCF_SCM_PACRF = 0;

	/* Enable bursts */
	MCF_SCM_BCR = (MCF_SCM_BCR_GBR | MCF_SCM_BCR_GBW);
}


void fbcs_init(void)
{
#if defined(CONFIG_COBRA5329)
	/* The COBRA5329 by senTec needs this settings */

	/*
	 * We need to give the LCD enough bandwidth
	 */

        MCF_XBS_PRS1 = MCF_XBS_PRIO_LCD(MCF_PRIO_LVL_1)
                     | MCF_XBS_PRIO_CORE(MCF_PRIO_LVL_2)
                     | MCF_XBS_PRIO_FEC(MCF_PRIO_LVL_3)
                     | MCF_XBS_PRIO_USBHOST(MCF_PRIO_LVL_4)
                     | MCF_XBS_PRIO_EDMA(MCF_PRIO_LVL_5)
                     | MCF_XBS_PRIO_USBOTG(MCF_PRIO_LVL_6)
                     | MCF_XBS_PRIO_FACTTEST(MCF_PRIO_LVL_7);

        /* Boot Flash connected to FBCS0 */
        MCF_FBCS0_CSAR = FLASH_ADDRESS;
        MCF_FBCS0_CSCR = (MCF_FBCS_CSCR_PS_16
                        | MCF_FBCS_CSCR_BEM
                        | MCF_FBCS_CSCR_AA
                        | MCF_FBCS_CSCR_WS(8));

        MCF_FBCS0_CSMR = (MCF_FBCS_CSMR_BAM_1G
                        | MCF_FBCS_CSMR_V);

        /* Fix bug #10 in the errata */
        MCF_FBCS1_CSAR = 0xC0000000;
        MCF_FBCS1_CSCR = (MCF_FBCS_CSCR_PS_16
                        | MCF_FBCS_CSCR_BEM
                        | MCF_FBCS_CSCR_AA
                        | MCF_FBCS_CSCR_WS(8));

        MCF_FBCS1_CSMR = (0x30000000
                        | MCF_FBCS_CSMR_V
                        | MCF_FBCS_CSMR_WP );
#else
	MCF_GPIO_PAR_CS = 0x0000003E;

	/* Latch chip select */
	MCF_FBCS1_CSAR = 0x10080000;

	MCF_FBCS1_CSCR = 0x002A3580 | (MCF_FBCS1_CSCR&0x200);
	MCF_FBCS1_CSMR = (MCF_FBCS_CSMR_BAM_2M | MCF_FBCS_CSMR_V);

	/* Initialize latch to drive signals to inactive states */
	*((u16 *)(0x10080000)) = 0xD3FF;

//	/* External SRAM */
//	MCF_FBCS1_CSAR = EXT_SRAM_ADDRESS;
//	MCF_FBCS1_CSCR = (MCF_FBCS_CSCR_PS_16
//			| MCF_FBCS_CSCR_AA
//			| MCF_FBCS_CSCR_SBM
//			| MCF_FBCS_CSCR_WS(1));
//	MCF_FBCS1_CSMR = (MCF_FBCS_CSMR_BAM_512K
//			| MCF_FBCS_CSMR_V);

	/* Boot Flash connected to FBCS0 */
	MCF_FBCS0_CSAR = FLASH_ADDRESS;
	MCF_FBCS0_CSCR = (MCF_FBCS_CSCR_PS_16
			| MCF_FBCS_CSCR_BEM
			| MCF_FBCS_CSCR_AA
			| MCF_FBCS_CSCR_SBM
			| MCF_FBCS_CSCR_WS(7));
	MCF_FBCS0_CSMR = (MCF_FBCS_CSMR_BAM_32M
			| MCF_FBCS_CSMR_V);
#endif
}

void sdramc_init(void)
{
	/*
	 * Check to see if the SDRAM has already been initialized
	 * by a run control tool
	 */
	if (!(MCF_SDRAMC_SDCR & MCF_SDRAMC_SDCR_REF)) {
		/* SDRAM chip select initialization */
		
		/* Initialize SDRAM chip select */
		MCF_SDRAMC_SDCS0 = (0
			| MCF_SDRAMC_SDCS_BA(SDRAM_ADDRESS)
			| MCF_SDRAMC_SDCS_CSSZ(MCF_SDRAMC_SDCS_CSSZ_32MBYTE));

	/*
	 * Basic configuration and initialization
	 */
	MCF_SDRAMC_SDCFG1 = (0
		| MCF_SDRAMC_SDCFG1_SRD2RW((int)((SDRAM_CASL + 2) + 0.5 ))
		| MCF_SDRAMC_SDCFG1_SWT2RD(SDRAM_TWR + 1)
		| MCF_SDRAMC_SDCFG1_RDLAT((int)((SDRAM_CASL*2) + 2))
		| MCF_SDRAMC_SDCFG1_ACT2RW((int)((SDRAM_TRCD ) + 0.5))
		| MCF_SDRAMC_SDCFG1_PRE2ACT((int)((SDRAM_TRP ) + 0.5))
		| MCF_SDRAMC_SDCFG1_REF2ACT((int)(((SDRAM_TRFC) ) + 0.5))
		| MCF_SDRAMC_SDCFG1_WTLAT(3));
	MCF_SDRAMC_SDCFG2 = (0
		| MCF_SDRAMC_SDCFG2_BRD2PRE(SDRAM_BL/2 + 1)
		| MCF_SDRAMC_SDCFG2_BWT2RW(SDRAM_BL/2 + SDRAM_TWR)
		| MCF_SDRAMC_SDCFG2_BRD2WT((int)((SDRAM_CASL+SDRAM_BL/2-1.0)+0.5))
		| MCF_SDRAMC_SDCFG2_BL(SDRAM_BL-1));

            
	/*
	 * Precharge and enable write to SDMR
	 */
        MCF_SDRAMC_SDCR = (0
		| MCF_SDRAMC_SDCR_MODE_EN
		| MCF_SDRAMC_SDCR_CKE
		| MCF_SDRAMC_SDCR_DDR
		| MCF_SDRAMC_SDCR_MUX(1)
		| MCF_SDRAMC_SDCR_RCNT((int)(((SDRAM_TREFI/(SYSTEM_PERIOD*64)) - 1) + 0.5))
		| MCF_SDRAMC_SDCR_PS_16
		| MCF_SDRAMC_SDCR_IPALL);            

	/*
	 * Write extended mode register
	 */
	MCF_SDRAMC_SDMR = (0
		| MCF_SDRAMC_SDMR_BNKAD_LEMR
		| MCF_SDRAMC_SDMR_AD(0x0)
		| MCF_SDRAMC_SDMR_CMD);

	/*
	 * Write mode register and reset DLL
	 */
	MCF_SDRAMC_SDMR = (0
		| MCF_SDRAMC_SDMR_BNKAD_LMR
		| MCF_SDRAMC_SDMR_AD(0x163)
		| MCF_SDRAMC_SDMR_CMD);

	/*
	 * Execute a PALL command
	 */
	MCF_SDRAMC_SDCR |= MCF_SDRAMC_SDCR_IPALL;

	/*
	 * Perform two REF cycles
	 */
	MCF_SDRAMC_SDCR |= MCF_SDRAMC_SDCR_IREF;
	MCF_SDRAMC_SDCR |= MCF_SDRAMC_SDCR_IREF;

	/*
	 * Write mode register and clear reset DLL
	 */
	MCF_SDRAMC_SDMR = (0
		| MCF_SDRAMC_SDMR_BNKAD_LMR
		| MCF_SDRAMC_SDMR_AD(0x063)
		| MCF_SDRAMC_SDMR_CMD);
				
	/*
	 * Enable auto refresh and lock SDMR
	 */
	MCF_SDRAMC_SDCR &= ~MCF_SDRAMC_SDCR_MODE_EN;
	MCF_SDRAMC_SDCR |= (0
		| MCF_SDRAMC_SDCR_REF
		| MCF_SDRAMC_SDCR_DQS_OE(0xC));
	}
}

void gpio_init(void)
{
	/* Enable UART0 pins */
	MCF_GPIO_PAR_UART = ( 0
		| MCF_GPIO_PAR_UART_PAR_URXD0
		| MCF_GPIO_PAR_UART_PAR_UTXD0);

	/* Initialize TIN3 as a GPIO output to enable the write
	   half of the latch */
	MCF_GPIO_PAR_TIMER = 0x00;
	MCF_GPIO_PDDR_TIMER = 0x08;
	MCF_GPIO_PCLRR_TIMER = 0x0;

}

int clock_pll(int fsys, int flags)
{
	int fref, temp, fout, mfd;
	u32 i;

	fref = FREF;
        
	if (fsys == 0) {
		/* Return current PLL output */
		mfd = MCF_PLL_PFDR;

		return (fref * mfd / (BUSDIV * 4));
	}

	/* Check bounds of requested system clock */
	if (fsys > MAX_FSYS)
		fsys = MAX_FSYS;
	if (fsys < MIN_FSYS)
		fsys = MIN_FSYS;

	/* Multiplying by 100 when calculating the temp value,
	   and then dividing by 100 to calculate the mfd allows
	   for exact values without needing to include floating
	   point libraries. */
	temp = 100 * fsys / fref;
	mfd = 4 * BUSDIV * temp / 100;
    	    	    	
	/* Determine the output frequency for selected values */
	fout = (fref * mfd / (BUSDIV * 4));

	/*
	 * Check to see if the SDRAM has already been initialized.
	 * If it has then the SDRAM needs to be put into self refresh
	 * mode before reprogramming the PLL.
	 */
	if (MCF_SDRAMC_SDCR & MCF_SDRAMC_SDCR_REF)
		/* Put SDRAM into self refresh mode */
		MCF_SDRAMC_SDCR &= ~MCF_SDRAMC_SDCR_CKE;

	/*
	 * Initialize the PLL to generate the new system clock frequency.
	 * The device must be put into LIMP mode to reprogram the PLL.
	 */

	/* Enter LIMP mode */
	clock_limp(DEFAULT_LPD);
     					
	/* Reprogram PLL for desired fsys */
	MCF_PLL_PODR = (0
		| MCF_PLL_PODR_CPUDIV(BUSDIV/3)
		| MCF_PLL_PODR_BUSDIV(BUSDIV));
						
	MCF_PLL_PFDR = mfd;
		
	/* Exit LIMP mode */
	clock_exit_limp();
	
	/*
	 * Return the SDRAM to normal operation if it is in use.
	 */
	if (MCF_SDRAMC_SDCR & MCF_SDRAMC_SDCR_REF)
		/* Exit self refresh mode */
		MCF_SDRAMC_SDCR |= MCF_SDRAMC_SDCR_CKE;

	/* Errata - workaround for SDRAM opeartion after exiting LIMP mode */
	MCF_SDRAMC_LIMP_FIX = MCF_SDRAMC_REFRESH;

	/* wait for DQS logic to relock */
	for (i = 0; i < 0x200; i++)
		;

	return fout;
}

int clock_limp(int div)
{
	u32 temp;

	/* Check bounds of divider */
	if (div < MIN_LPD)
		div = MIN_LPD;
	if (div > MAX_LPD)
		div = MAX_LPD;
    
	/* Save of the current value of the SSIDIV so we don't
	   overwrite the value*/
	temp = (MCF_CCM_CDR & MCF_CCM_CDR_SSIDIV(0xF));
      
	/* Apply the divider to the system clock */
	MCF_CCM_CDR = ( 0
		| MCF_CCM_CDR_LPDIV(div)
		| MCF_CCM_CDR_SSIDIV(temp));
    
	MCF_CCM_MISCCR |= MCF_CCM_MISCCR_LIMP;
    
	return (FREF/(3*(1 << div)));
}

int clock_exit_limp(void)
{
	int fout;
	
	/* Exit LIMP mode */
	MCF_CCM_MISCCR = (MCF_CCM_MISCCR & ~ MCF_CCM_MISCCR_LIMP);

	/* Wait for PLL to lock */
	while (!(MCF_CCM_MISCCR & MCF_CCM_MISCCR_PLL_LOCK))
		;
	
	fout = get_sys_clock();

	return fout;
}

int get_sys_clock(void)
{
	int divider;
	
	/* Test to see if device is in LIMP mode */
	if (MCF_CCM_MISCCR & MCF_CCM_MISCCR_LIMP) {
		divider = MCF_CCM_CDR & MCF_CCM_CDR_LPDIV(0xF);
		return (FREF/(2 << divider));
	}
	else
		return ((FREF * MCF_PLL_PFDR) / (BUSDIV * 4));
}